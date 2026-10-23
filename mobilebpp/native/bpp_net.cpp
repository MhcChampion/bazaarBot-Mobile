// Minimal HTTPS GET through the JVM that Unity already hosts.
// C++ has no TLS stack here, and spawning curl is fragile on modern Android, so
// we borrow java.net.URL from the running ART. Runs on a background thread.

#include "bpp_net.h"

#include <dlfcn.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include "bpp_ui.h"

#include "bpp_common.h"

namespace {

constexpr const char* kOrigin = "https://bazaarqiubot.com";

JavaVM* g_vm = nullptr;

bool EnsureVm() {
    if (g_vm) return true;
    // Resolve lazily: linking against it directly makes bionic reject libbpp.so.
    typedef jint (*get_vms_fn)(JavaVM**, jsize, jsize*);
    static get_vms_fn get_vms = nullptr;
    if (!get_vms) {
        get_vms = (get_vms_fn)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
        if (!get_vms) {
            void* h = dlopen("libnativehelper.so", RTLD_NOW);
            if (!h) h = dlopen("libart.so", RTLD_NOW);
            if (h) get_vms = (get_vms_fn)dlsym(h, "JNI_GetCreatedJavaVMs");
        }
    }
    if (!get_vms) {
        LOGW("net: JNI_GetCreatedJavaVMs unavailable");
        return false;
    }
    JavaVM* vms[1] = {nullptr};
    jsize count = 0;
    if (get_vms(vms, 1, &count) != JNI_OK || count == 0) return false;
    g_vm = vms[0];
    return g_vm != nullptr;
}

void ClearException(JNIEnv* env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

class JniScope {
public:
    JNIEnv* env = nullptr;
    JniScope() {
        if (!EnsureVm()) return;
        if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) { env = nullptr; return; }
            attached_ = true;
        }
        if (env->PushLocalFrame(64) < 0) { ClearException(env); env = nullptr; }
    }
    ~JniScope() {
        if (env) env->PopLocalFrame(nullptr);
        if (attached_) g_vm->DetachCurrentThread();
    }
private:
    bool attached_ = false;
};

std::string HttpGet(const char* url) {
    JniScope scope;
    JNIEnv* env = scope.env;
    if (!env) {
        LOGW("net: no JavaVM");
        return {};
    }

    std::string out;
    jclass cls_url = env->FindClass("java/net/URL");
    if (!cls_url) { ClearException(env); return {}; }
    jmethodID ctor = env->GetMethodID(cls_url, "<init>", "(Ljava/lang/String;)V");
    jmethodID open = env->GetMethodID(cls_url, "openConnection", "()Ljava/net/URLConnection;");
    if (!ctor || !open) { ClearException(env); return {}; }

    jstring jurl = env->NewStringUTF(url);
    jobject u = env->NewObject(cls_url, ctor, jurl);
    jobject conn = u ? env->CallObjectMethod(u, open) : nullptr;
    if (env->ExceptionCheck() || !conn) {
        ClearException(env);
        return {};
    }

    jclass cls_conn = env->FindClass("java/net/URLConnection");
    jmethodID set_ct = env->GetMethodID(cls_conn, "setConnectTimeout", "(I)V");
    jmethodID set_rt = env->GetMethodID(cls_conn, "setReadTimeout", "(I)V");
    jmethodID get_is = env->GetMethodID(cls_conn, "getInputStream", "()Ljava/io/InputStream;");
    if (set_ct) env->CallVoidMethod(conn, set_ct, 8000);
    if (set_rt) env->CallVoidMethod(conn, set_rt, 12000);
    jobject is = get_is ? env->CallObjectMethod(conn, get_is) : nullptr;
    if (env->ExceptionCheck() || !is) {
        ClearException(env);
        LOGW("net: getInputStream failed");
        return {};
    }

    jclass cls_is = env->FindClass("java/io/InputStream");
    jmethodID read = env->GetMethodID(cls_is, "read", "([B)I");
    jbyteArray buf = env->NewByteArray(4096);
    bool complete = false;
    while (read && buf) {
        jint n = env->CallIntMethod(is, read, buf);
        if (env->ExceptionCheck()) {
            ClearException(env);
            break;
        }
        if (n == -1) { complete = true; break; }
        if (n == 0) continue;
        jbyte* p = env->GetByteArrayElements(buf, nullptr);
        if (p) {
            out.append(reinterpret_cast<char*>(p), static_cast<size_t>(n));
            env->ReleaseByteArrayElements(buf, p, JNI_ABORT);
        }
        if (out.size() > 8 * 1024 * 1024) break;
    }
    jmethodID close = env->GetMethodID(cls_is, "close", "()V");
    if (close) env->CallVoidMethod(is, close);
    ClearException(env);
    return complete ? out : std::string();
}

std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
        else out += c;
    }
    return out + '"';
}

std::string Encode(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out += c;
        else { char buf[4]; snprintf(buf, sizeof(buf), "%%%02X", c); out += buf; }
    }
    return out;
}

struct Json {
    JNIEnv* e;
    jclass object, array;
    jmethodID ctor, string, integer, get_array, length, at, to_string, get_object;
    explicit Json(JNIEnv* env) : e(env) {
        object=e->FindClass("org/json/JSONObject"); array=e->FindClass("org/json/JSONArray");
        ctor=e->GetMethodID(object,"<init>","(Ljava/lang/String;)V");
        string=e->GetMethodID(object,"getString","(Ljava/lang/String;)Ljava/lang/String;");
        integer=e->GetMethodID(object,"getInt","(Ljava/lang/String;)I");
        get_array=e->GetMethodID(object,"getJSONArray","(Ljava/lang/String;)Lorg/json/JSONArray;");
        get_object=e->GetMethodID(object,"getJSONObject","(Ljava/lang/String;)Lorg/json/JSONObject;");
        length=e->GetMethodID(array,"length","()I");
        at=e->GetMethodID(array,"getString","(I)Ljava/lang/String;");
        to_string=e->GetMethodID(object,"toString","()Ljava/lang/String;");
    }
    bool ok() { if (!e->ExceptionCheck()) return true; e->ExceptionClear(); return false; }
    std::string utf(jstring value) {
        if (!ok() || !value) return {};
        const char* p=e->GetStringUTFChars(value,nullptr);
        if (!p) return {};
        std::string out(p); e->ReleaseStringUTFChars(value,p); return out;
    }
    jobject parse(const std::string& text) {
        jobject out=e->NewObject(object,ctor,e->NewStringUTF(text.c_str()));
        return ok()?out:nullptr;
    }
    std::string str(jobject root,const char* key) {
        if (!root) return {};
        return utf((jstring)e->CallObjectMethod(root,string,e->NewStringUTF(key)));
    }
    int number(jobject root,const char* key) {
        if (!root) return -1;
        int out=e->CallIntMethod(root,integer,e->NewStringUTF(key)); return ok()?out:-1;
    }
    std::vector<std::string> strings(jobject root,const char* key) {
        std::vector<std::string> out;
        jobject arr=e->CallObjectMethod(root,get_array,e->NewStringUTF(key));
        if (!ok() || !arr) return out;
        int n=e->CallIntMethod(arr,length);
        if (n>80) return out;
        for(int i=0;i<n;++i) out.push_back(utf((jstring)e->CallObjectMethod(arr,at,i)));
        return out;
    }
};

std::mutex g_mu;
std::condition_variable g_cv;
std::once_flag g_once;
std::string g_request, g_status="巴扎丘Bot：等待当前英雄";
std::string g_detail_request;
uint64_t g_revision=0;
uint64_t g_detail_revision=0;
std::map<std::string,std::string> g_art;

bool Current(uint64_t revision) { std::lock_guard<std::mutex> lock(g_mu); return revision==g_revision; }
void Status(const std::string& value) { std::lock_guard<std::mutex> lock(g_mu); g_status=value; }

std::string ArtJson() {
    std::string out="{";
    for (const auto& pair:g_art) { if(out.size()>1)out+=','; out+=Quote(pair.first)+':'+pair.second; }
    return out+'}';
}

std::string ReadCache() {
    FILE* f=fopen((bpp::MediaDir()+"/bpp_runs.json").c_str(),"rb");
    if(!f)return {};
    std::string out; char buf[4096]; size_t n;
    while((n=fread(buf,1,sizeof(buf),f))>0 && out.size()<2*1024*1024)out.append(buf,n);
    fclose(f); return out;
}

void SaveCache(const std::string& body) {
    std::string path=bpp::MediaDir()+"/bpp_runs.json";
    FILE* f=fopen((path+".tmp").c_str(),"wb");
    if(!f)return;
    bool written=fwrite(body.data(),1,body.size(),f)==body.size();
    bool closed=fclose(f)==0;
    if(written&&closed)rename((path+".tmp").c_str(),path.c_str());
}

void Reply(const std::string& body,uint64_t revision) {
    if(Current(revision))PushJs("window.setRuns(JSON.parse("+Quote(body)+"));");
}

void FetchRuns(const std::string& request,uint64_t revision) {
    JniScope scope; if(!scope.env)return;
    // Art lookups use separate frames below; keep this frame bounded even as
    // the player discovers more cards across a long session.
    Json j(scope.env); jobject req=j.parse(request);
    if(!req)return;
    std::string hero=j.str(req,"hero"), key=j.str(req,"key");
    int page=j.number(req,"page");
    auto cards=j.strings(req,"cards"), board=j.strings(req,"board");
    const std::set<std::string> heroes={"Karnok","Vanessa","Pygmalien","Dooley","Mak","Jules","Stelle","TheDragons"};
    if(!heroes.count(hero)||key.empty()||key.size()>3000||page<1||page>100000||cards.size()>10||board.size()>10)return;
    std::string names;
    for(const auto& name:cards) {
        if(name.empty()||name.size()>120)return;
        if(!names.empty())names+='+';
        names+=name;
    }
    std::string url=std::string(kOrigin)+"/api/runs?min_wins=10&days=7&hero="+
        Encode(hero=="TheDragons"?"The Dragons":hero)+"&page="+std::to_string(page);
    if(!names.empty())url+="&cards="+Encode(names);
    Status("巴扎丘Bot：查询 "+hero+" 第 "+std::to_string(page)+" 页");
    std::string body=HttpGet(url.c_str());
    jobject data=body.empty()?nullptr:j.parse(body);
    jobject runs=data?scope.env->CallObjectMethod(data,j.get_array,scope.env->NewStringUTF("runs")):nullptr;
    bool valid=j.ok()&&runs&&j.number(data,"total")>=0&&j.number(data,"page")>0&&j.number(data,"pages")>=0;
    if(!valid) {
        std::string cache=ReadCache(); jobject cached=j.parse(cache);
        std::string error="查询失败，请稍后重试";
        if(cached&&j.str(cached,"key")==key) {
            // Keep the original timestamp and explicitly mark cached data.
            auto put_bool=scope.env->GetMethodID(j.object,"put","(Ljava/lang/String;Z)Lorg/json/JSONObject;");
            scope.env->CallObjectMethod(cached,put_bool,scope.env->NewStringUTF("cached"),JNI_TRUE);
            Reply(j.utf((jstring)scope.env->CallObjectMethod(cached,j.to_string)),revision);
            Status("巴扎丘Bot：联网失败，显示相同筛选的缓存");
        } else {
            Reply("{\"key\":"+Quote(key)+",\"error\":"+Quote(error)+"}",revision);
            Status("巴扎丘Bot：查询失败");
        }
        return;
    }
    long long now=time(nullptr);
    auto envelope=[&] { return "{\"key\":"+Quote(key)+",\"data\":"+body+
        ",\"art\":"+ArtJson()+",\"cached\":false,\"fetched_at\":"+std::to_string(now)+"}"; };
    Reply(envelope(),revision);
    Status("巴扎丘Bot："+hero+" 十胜记录 "+std::to_string(j.number(data,"total"))+" 条");
    for(const auto& id:board) {
        if(!Current(revision))break;
        if(g_art.count(id)||id.size()!=36||id.find_first_not_of("0123456789abcdef-")!=std::string::npos)continue;
        std::string art_body=HttpGet((std::string(kOrigin)+"/api/card-preview/"+id).c_str());
        JniScope art_scope; if(!art_scope.env)continue;
        Json art_json(art_scope.env); jobject art=art_json.parse(art_body);
        std::string img=art_json.str(art,"img"),size=art_json.str(art,"size");
        if(img.rfind("https://s.bazaardb.gg/",0)!=0)continue;
        g_art[id]="{\"art\":"+Quote(img)+",\"s\":"+Quote(size)+"}";
    }
    if(Current(revision)) { std::string out=envelope(); SaveCache(out); Reply(out,revision); }
}

void FetchDetail(const std::string& id) {
    JniScope scope;if(!scope.env)return;
    std::string body=HttpGet((std::string(kOrigin)+"/api/runs/"+id).c_str());
    Json j(scope.env);jobject data=j.parse(body);
    jobject run=data?scope.env->CallObjectMethod(data,j.get_object,scope.env->NewStringUTF("run")):nullptr;
    bool valid=j.ok()&&run&&j.str(run,"id")==id;
    std::string reply="{\"id\":"+Quote(id)+(valid?",\"run\":"+j.utf((jstring)scope.env->CallObjectMethod(run,j.to_string)):",\"error\":\"详情暂时无法更新\"")+"}";
    PushJs("window.setRunDetail(JSON.parse("+Quote(reply)+"));");
}

void Worker() {
    using Clock=std::chrono::steady_clock;
    uint64_t processed=0,detail_processed=0;
    auto last_start=Clock::now()-std::chrono::seconds(2);
    for(;;) {
        std::unique_lock<std::mutex> lock(g_mu);
        g_cv.wait(lock,[&]{return g_revision!=processed||g_detail_revision!=detail_processed;});
        bool detail=g_detail_revision!=detail_processed;
        if(detail){
            g_cv.wait_until(lock,last_start+std::chrono::milliseconds(1100),[]{return false;});
            std::string id=g_detail_request;detail_processed=g_detail_revision;last_start=Clock::now();lock.unlock();
            FetchDetail(id);continue;
        }
        uint64_t rev=g_revision;
        if(g_cv.wait_for(lock,std::chrono::milliseconds(300),[&]{return g_revision!=rev;}))continue;
        if(g_cv.wait_until(lock,last_start+std::chrono::milliseconds(1100),[&]{return g_revision!=rev;}))continue;
        std::string req=g_request; processed=rev; last_start=Clock::now(); lock.unlock();
        FetchRuns(req,rev);
    }
}

} // namespace

void QueueQiuRuns(const std::string& request) {
    if(request.empty()||request.size()>4096)return;
    std::call_once(g_once,[]{std::thread(Worker).detach();});
    { std::lock_guard<std::mutex> lock(g_mu); g_request=request; ++g_revision; }
    g_cv.notify_one();
}

void QueueQiuDetail(const std::string& id){
    if(id.empty()||id.size()>128||id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-")!=std::string::npos)return;
    std::call_once(g_once,[]{std::thread(Worker).detach();});
    {std::lock_guard<std::mutex> lock(g_mu);g_detail_request=id;++g_detail_revision;}
    g_cv.notify_one();
}

std::string QiuNetworkStatus() { std::lock_guard<std::mutex> lock(g_mu); return g_status; }

