// WebView overlay inside the game's own window.
//
// Unity's script thread is NOT the Android UI thread, so views must be created on
// "main". JNI cannot subclass Runnable, so we ship a small dex with
// `bpp.Ui implements Runnable`, load it through InMemoryDexClassLoader, bind its
// native method, and hand the instance to Activity.runOnUiThread().

#include "bpp_ui.h"

#include <dlfcn.h>
#include <jni.h>

#include <mutex>
#include <string>
#include <atomic>
#include <vector>
#include <algorithm>

#include "bpp_common.h"
#include "bpp_dex.h"

namespace {

JavaVM* g_vm = nullptr;
jobject g_webview = nullptr;
jobject g_overlay_window = nullptr;
int g_viewport_w=0,g_viewport_h=0;
float g_window_css_w=26,g_window_css_h=64;
jobject g_panel_background = nullptr;
jobject g_activity = nullptr;
std::string g_html_path;
jobject g_runnable = nullptr;
jclass g_ui_class = nullptr;
std::string g_pending_js;
std::mutex g_js_mu;
std::vector<WebCommand> g_commands;
std::atomic<bool> g_created{false}, g_open{false}, g_page_ready{false}, g_layout_dirty{true};
std::atomic<bool> g_bubble{false};
std::atomic<bool> g_hide_keyboard{false};
std::atomic<bool> g_mini{false}, g_dragging{false}, g_markers_dirty{false};
std::atomic<float> g_mini_w{300}, g_mini_h{72}, g_mini_x{-1}, g_mini_y{-1};
jobject g_dock_view=nullptr;
std::vector<jobject> g_marker_views;
std::vector<WebMarker> g_markers;
std::string g_match_summary;
jobject g_match_view=nullptr;
std::string g_capture_path;

struct LocalFrame {
    JNIEnv* env;
    explicit LocalFrame(JNIEnv* e) : env(e) { env->PushLocalFrame(64); }
    ~LocalFrame() { env->PopLocalFrame(nullptr); }
};

void PostUi();

jobject Label(JNIEnv* env, const char* text) {
    jclass klass=env->FindClass("android/widget/TextView");
    jobject view=env->NewObject(klass,env->GetMethodID(klass,"<init>","(Landroid/content/Context;)V"),g_activity);
    env->CallVoidMethod(view,env->GetMethodID(klass,"setText","(Ljava/lang/CharSequence;)V"),env->NewStringUTF(text));
    env->CallVoidMethod(view,env->GetMethodID(klass,"setTextColor","(I)V"),(jint)0xff241e10);
    env->CallVoidMethod(view,env->GetMethodID(klass,"setTextSize","(F)V"),11.f);
    env->CallVoidMethod(view,env->GetMethodID(klass,"setGravity","(I)V"),17);
    jclass vk=env->FindClass("android/view/View");
    env->CallVoidMethod(view,env->GetMethodID(vk,"setBackgroundColor","(I)V"),(jint)0xddedc66e);
    env->CallVoidMethod(view,env->GetMethodID(vk,"setClickable","(Z)V"),JNI_FALSE);
    env->CallVoidMethod(view,env->GetMethodID(vk,"setFocusable","(Z)V"),JNI_FALSE);
    env->CallVoidMethod(view,env->GetMethodID(vk,"setImportantForAccessibility","(I)V"),2);
    jclass lp=env->FindClass("android/widget/FrameLayout$LayoutParams");
    jobject params=env->NewObject(lp,env->GetMethodID(lp,"<init>","(III)V"),1,1,51);
    jclass activity=env->FindClass("android/app/Activity");
    env->CallVoidMethod(g_activity,env->GetMethodID(activity,"addContentView","(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"),view,params);
    return env->NewGlobalRef(view);
}

jobject DockTarget(JNIEnv* env) {
    jclass text=env->FindClass("android/widget/TextView");
    jobject target=env->NewObject(text,env->GetMethodID(text,"<init>","(Landroid/content/Context;)V"),g_activity);
    env->CallVoidMethod(target,env->GetMethodID(text,"setText","(Ljava/lang/CharSequence;)V"),env->NewStringUTF("‹\n拖回\n侧栏"));
    env->CallVoidMethod(target,env->GetMethodID(text,"setTextSize","(F)V"),12.f);
    env->CallVoidMethod(target,env->GetMethodID(text,"setTextColor","(I)V"),(jint)0xffefd39a);
    env->CallVoidMethod(target,env->GetMethodID(text,"setGravity","(I)V"),17);
    jclass view=env->FindClass("android/view/View");
    env->CallVoidMethod(target,env->GetMethodID(view,"setBackgroundColor","(I)V"),(jint)0xf229180f);
    jmethodID frame=env->GetStaticMethodID(g_ui_class,"panelFrame","(Ljava/lang/Object;Ljava/lang/Object;)V");
    if(frame)env->CallStaticVoidMethod(g_ui_class,frame,target,g_activity);
    env->CallVoidMethod(target,env->GetMethodID(view,"setClickable","(Z)V"),JNI_FALSE);
    env->CallVoidMethod(target,env->GetMethodID(view,"setFocusable","(Z)V"),JNI_FALSE);
    env->CallVoidMethod(target,env->GetMethodID(view,"setImportantForAccessibility","(I)V"),2);
    jclass lp=env->FindClass("android/widget/FrameLayout$LayoutParams");
    jobject params=env->NewObject(lp,env->GetMethodID(lp,"<init>","(III)V"),1,1,51);
    jclass activity=env->FindClass("android/app/Activity");
    env->CallVoidMethod(g_activity,env->GetMethodID(activity,"addContentView","(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"),target,params);
    return env->NewGlobalRef(target);
}

void PlaceLabel(JNIEnv* env,jobject label,int x,int y,int w,int h,bool visible) {
    jclass view=env->FindClass("android/view/View");
    env->CallVoidMethod(label,env->GetMethodID(view,"setVisibility","(I)V"),visible?0:8);
    if(!visible)return;
    jclass lp=env->FindClass("android/widget/FrameLayout$LayoutParams");
    jobject p=env->NewObject(lp,env->GetMethodID(lp,"<init>","(III)V"),w,h,51);
    env->SetIntField(p,env->GetFieldID(lp,"leftMargin","I"),x);
    env->SetIntField(p,env->GetFieldID(lp,"topMargin","I"),y);
    env->CallVoidMethod(label,env->GetMethodID(view,"setLayoutParams","(Landroid/view/ViewGroup$LayoutParams;)V"),p);
}

void HideKeyboardOnUi(JNIEnv* env) {
    if (!g_webview || !g_activity) return;
    jclass view = env->FindClass("android/view/View");
    jclass context = env->FindClass("android/content/Context");
    jclass input = env->FindClass("android/view/inputmethod/InputMethodManager");
    if (!view || !context || !input) { env->ExceptionClear(); return; }
    jmethodID token_method = env->GetMethodID(view, "getWindowToken", "()Landroid/os/IBinder;");
    jmethodID clear = env->GetMethodID(view, "clearFocus", "()V");
    jmethodID service = env->GetMethodID(context, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jmethodID hide = env->GetMethodID(input, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
    if (env->ExceptionCheck() || !token_method || !clear || !service || !hide) { env->ExceptionClear(); return; }
    jobject manager = env->CallObjectMethod(g_activity, service, env->NewStringUTF("input_method"));
    jobject token = env->CallObjectMethod(g_webview, token_method);
    if (manager && token) env->CallBooleanMethod(manager, hide, token, 0);
    env->CallVoidMethod(g_webview, clear);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
}

void ApplyLayout(JNIEnv* env,bool apply_window=true) {
    if (!g_webview || !g_activity) return;
    jclass ac = env->GetObjectClass(g_activity);
    jmethodID get_res = env->GetMethodID(ac, "getResources", "()Landroid/content/res/Resources;");
    jobject res = env->CallObjectMethod(g_activity, get_res);
    jclass rc = env->FindClass("android/content/res/Resources");
    jobject dm = env->CallObjectMethod(res, env->GetMethodID(rc, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;"));
    jclass dc = env->FindClass("android/util/DisplayMetrics");
    int w = env->GetIntField(dm, env->GetFieldID(dc, "widthPixels", "I"));
    int h = env->GetIntField(dm, env->GetFieldID(dc, "heightPixels", "I"));
    float density = env->GetFloatField(dm, env->GetFieldID(dc, "density", "F"));
    bool open = g_open.load(), bubble = g_bubble.load();
    jclass view = env->FindClass("android/view/View");
    if(g_panel_background){
        env->CallVoidMethod(g_panel_background,env->GetMethodID(view,"setVisibility","(I)V"),open?0:8);
        if(open){
            jclass bg_lp=env->FindClass("android/widget/FrameLayout$LayoutParams");
            jobject params=env->NewObject(bg_lp,env->GetMethodID(bg_lp,"<init>","(III)V"),(jint)(w*.56f),h,51);
            env->CallVoidMethod(g_panel_background,env->GetMethodID(view,"setLayoutParams","(Landroid/view/ViewGroup$LayoutParams;)V"),params);
        }
    }
    // Expanded WebView also owns the transparent dismiss area. Its first tap
    // closes the panel and cannot accidentally activate a game choice beneath.
    bool mini=g_mini.load()&&!open;
    int width = open ? w : mini ? std::min(w,(int)(g_mini_w.load()*density)) : (int)((bubble ? 58 : 26) * density);
    int height = open ? h : mini ? std::min(h,(int)(g_mini_h.load()*density)) : (int)((bubble ? 58 : 64) * density);
    jclass lp_class = env->FindClass("android/widget/FrameLayout$LayoutParams");
    jobject lp = env->NewObject(lp_class, env->GetMethodID(lp_class, "<init>", "(III)V"),
                               width, height, open || mini ? 51 /*TOP|LEFT*/ : (bubble ? 85 /*BOTTOM|RIGHT*/ : 19 /*CENTER_VERTICAL|LEFT*/));
    int mini_x=0,mini_y=0;
    if(mini){
        float x=g_mini_x.load(),y=g_mini_y.load();
        if(x<0)x=(w/density-g_mini_w.load())*.5f;
        if(y<0)y=h/density*.15f;
        mini_x=std::clamp((int)(x*density),0,w-width);mini_y=std::clamp((int)(y*density),0,h-height);
        env->SetIntField(lp,env->GetFieldID(lp_class,"leftMargin","I"),mini_x);
        env->SetIntField(lp,env->GetFieldID(lp_class,"topMargin","I"),mini_y);
    } else if (!open && bubble) {
        env->SetIntField(lp, env->GetFieldID(lp_class, "rightMargin", "I"), (int)(12 * density));
        env->SetIntField(lp, env->GetFieldID(lp_class, "bottomMargin", "I"), (int)(24 * density));
    }
    if(apply_window){
        // Keep Chromium's viewport stable. Only resize/position the clipping
        // window, so painting and touch coordinates use the same screen scale.
        env->CallVoidMethod(g_overlay_window,env->GetMethodID(view,"setLayoutParams","(Landroid/view/ViewGroup$LayoutParams;)V"),lp);
        if(w!=g_viewport_w||h!=g_viewport_h){
            jobject full=env->NewObject(lp_class,env->GetMethodID(lp_class,"<init>","(III)V"),w,h,51);
            env->CallVoidMethod(g_webview,env->GetMethodID(view,"setLayoutParams","(Landroid/view/ViewGroup$LayoutParams;)V"),full);
            g_viewport_w=w;g_viewport_h=h;
        }
        g_window_css_w=width/density;g_window_css_h=height/density;
    }
    if(apply_window&&g_ui_class){
        jmethodID geometry=env->GetStaticMethodID(g_ui_class,"miniGeometry","(ZFIII)V");
        if(geometry)env->CallStaticVoidMethod(g_ui_class,geometry,mini?JNI_TRUE:JNI_FALSE,density,mini_x,mini_y,h);
    }
    if(g_dragging.load()&&!g_dock_view)g_dock_view=DockTarget(env);
    if(g_dock_view)PlaceLabel(env,g_dock_view,0,(h-(int)(104*density))/2,(int)(44*density),(int)(104*density),mini&&g_dragging.load());
    std::vector<WebMarker> markers;std::string summary;
    {std::lock_guard<std::mutex> lock(g_js_mu);markers=g_markers;summary=g_match_summary;}
    bool show_summary=!open&&markers.empty()&&!summary.empty();
    if(show_summary&&!g_match_view){
        g_match_view=Label(env,"");
        jclass text=env->FindClass("android/widget/TextView");
        env->CallVoidMethod(g_match_view,env->GetMethodID(view,"setBackgroundColor","(I)V"),(jint)0xef29180f);
        env->CallVoidMethod(g_match_view,env->GetMethodID(text,"setTextColor","(I)V"),(jint)0xffefd39a);
        jmethodID frame=env->GetStaticMethodID(g_ui_class,"panelFrame","(Ljava/lang/Object;Ljava/lang/Object;)V");
        if(frame)env->CallStaticVoidMethod(g_ui_class,frame,g_match_view,g_activity);
    }
    if(g_match_view){
        if(show_summary){
            jclass text=env->FindClass("android/widget/TextView");
            env->CallVoidMethod(g_match_view,env->GetMethodID(text,"setText","(Ljava/lang/CharSequence;)V"),env->NewStringUTF(summary.c_str()));
        }
        PlaceLabel(env,g_match_view,(int)(w*.30f),(int)(6*density),(int)(w*.40f),(int)(32*density),show_summary);
    }
    size_t count=open?0:std::min(markers.size(),size_t(32));
    while(g_marker_views.size()<count){
        jobject marker=Label(env,"");
        jmethodID badge=env->GetStaticMethodID(g_ui_class,"followMarker","(Ljava/lang/Object;Ljava/lang/Object;)V");
        if(badge)env->CallStaticVoidMethod(g_ui_class,badge,marker,g_activity);
        g_marker_views.push_back(marker);
    }
    for(size_t i=0;i<g_marker_views.size();++i){
        bool show=i<count;
        // Fit the hanging badge to the actual projected card, including skills.
        float badge_width=show?std::min(markers[i].width*w*.40f,markers[i].height*h*.25f*198.f/256.f):0;
        int label_w=(int)std::clamp(badge_width,10*density,20*density);
        int label_h=(int)(label_w*256.f/198.f);
        int x=show?std::clamp((int)(markers[i].x*w)-label_w/2,0,w-label_w):0;
        int y=show?std::clamp((int)(markers[i].y*h)-(int)(2*density),0,h-label_h):0;
        PlaceLabel(env,g_marker_views[i],x,y,label_w,label_h,show);
    }
}

void JNICALL OnCommand(JNIEnv* env, jclass, jstring action, jstring value) {
    if (!action || !value) return;
    const char* a = env->GetStringUTFChars(action, nullptr);
    const char* v = env->GetStringUTFChars(value, nullptr);
    if (a && v) {
        if (!strcmp(a, "ready")) { g_page_ready.store(true); g_layout_dirty.store(true); }
        else {
            std::lock_guard<std::mutex> lk(g_js_mu);
            size_t limit = !strcmp(a,"runs") || !strcmp(a,"watch") ? 4096 : 128;
            if (g_commands.size() < 64 && strlen(a) < 32 && strlen(v) < limit)
                g_commands.push_back({a, v});
        }
    }
    if (a) env->ReleaseStringUTFChars(action, a);
    if (v) env->ReleaseStringUTFChars(value, v);
    PostUi();
}

bool EnsureVm() {
    if (g_vm) return true;
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
    if (!get_vms) return false;
    JavaVM* vms[1] = {nullptr};
    jsize n = 0;
    if (get_vms(vms, 1, &n) != JNI_OK || n == 0) return false;
    g_vm = vms[0];
    return true;
}

JNIEnv* Env() {
    if (!EnsureVm()) return nullptr;
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    return env;
}

void LogAndClear(JNIEnv* env, const char* where) {
    if (!env->ExceptionCheck()) return;
    jthrowable t = env->ExceptionOccurred();
    env->ExceptionClear();
    jclass cls_t = env->FindClass("java/lang/Throwable");
    jmethodID m = cls_t ? env->GetMethodID(cls_t, "toString", "()Ljava/lang/String;") : nullptr;
    jstring js = m ? (jstring)env->CallObjectMethod(t, m) : nullptr;
    const char* c = js ? env->GetStringUTFChars(js, nullptr) : nullptr;
    LOGW("%s threw: %s", where, c ? c : "?");
    if (c) env->ReleaseStringUTFChars(js, c);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

void JNICALL OnPost(JNIEnv* env, jclass);

// Runs on the Android main thread.
void JNICALL OnUi(JNIEnv* env, jclass) {
    if (g_webview || !g_activity) return;
    LocalFrame frame(env);
    jclass cls_web = env->FindClass("android/webkit/WebView");
    // Opt-in developer inspection for layout/paint mismatches on real devices.
    // Normal installs never expose a WebView debugging socket.
    if (bpp::FileExists(bpp::MediaDir() + "/bpp_ui_debug")) {
        jmethodID debug=env->GetStaticMethodID(cls_web,"setWebContentsDebuggingEnabled","(Z)V");
        if(debug)env->CallStaticVoidMethod(cls_web,debug,JNI_TRUE);
    }
    jmethodID ctor = cls_web ? env->GetMethodID(cls_web, "<init>", "(Landroid/content/Context;)V")
                             : nullptr;
    jobject wv = ctor ? env->NewObject(cls_web, ctor, g_activity) : nullptr;
    if (!wv) {
        LogAndClear(env, "webui: WebView ctor");
        return;
    }

    jclass cls_settings = env->FindClass("android/webkit/WebSettings");
    jmethodID get_settings =
        env->GetMethodID(cls_web, "getSettings", "()Landroid/webkit/WebSettings;");
    jobject settings = get_settings ? env->CallObjectMethod(wv, get_settings) : nullptr;
    if (settings && cls_settings) {
        jmethodID set_js = env->GetMethodID(cls_settings, "setJavaScriptEnabled", "(Z)V");
        if (set_js) env->CallVoidMethod(settings, set_js, JNI_TRUE);
        jmethodID set_fa = env->GetMethodID(cls_settings, "setAllowFileAccess", "(Z)V");
        if (set_fa) env->CallVoidMethod(settings, set_fa, JNI_TRUE);
        // The panel is a fixed layout: no pinch zoom, no wide viewport rescaling.
        jmethodID zoom = env->GetMethodID(cls_settings, "setBuiltInZoomControls", "(Z)V");
        if (zoom) env->CallVoidMethod(settings, zoom, JNI_FALSE);
        jmethodID dzc = env->GetMethodID(cls_settings, "setDisplayZoomControls", "(Z)V");
        if (dzc) env->CallVoidMethod(settings, dzc, JNI_FALSE);
        jmethodID sup = env->GetMethodID(cls_settings, "setSupportZoom", "(Z)V");
        if (sup) env->CallVoidMethod(settings, sup, JNI_FALSE);
        jmethodID wide = env->GetMethodID(cls_settings, "setUseWideViewPort", "(Z)V");
        if (wide) env->CallVoidMethod(settings, wide, JNI_FALSE);
        jmethodID ov = env->GetMethodID(cls_settings, "setLoadWithOverviewMode", "(Z)V");
        if (ov) env->CallVoidMethod(settings, ov, JNI_FALSE);
        jmethodID storage=env->GetMethodID(cls_settings,"setDomStorageEnabled","(Z)V");
        if(storage)env->CallVoidMethod(settings,storage,JNI_TRUE);
    }

    jmethodID set_bg = env->GetMethodID(cls_web, "setBackgroundColor", "(I)V");
    if (set_bg) env->CallVoidMethod(wv, set_bg, 0);

    jclass cls_lp = env->FindClass("android/widget/FrameLayout$LayoutParams");
    jmethodID lp_ctor = cls_lp ? env->GetMethodID(cls_lp, "<init>", "(II)V") : nullptr;
    jint w = 1080, h = 1920;
    {
        jclass cls_act_m = env->FindClass("android/app/Activity");
        jmethodID get_res = cls_act_m ? env->GetMethodID(cls_act_m, "getResources",
                                                          "()Landroid/content/res/Resources;")
                                      : nullptr;
        jclass cls_res = env->FindClass("android/content/res/Resources");
        jmethodID get_dm = cls_res ? env->GetMethodID(cls_res, "getDisplayMetrics",
                                                      "()Landroid/util/DisplayMetrics;")
                                   : nullptr;
        jclass cls_dm = env->FindClass("android/util/DisplayMetrics");
        jfieldID fw = cls_dm ? env->GetFieldID(cls_dm, "widthPixels", "I") : nullptr;
        jfieldID fh = cls_dm ? env->GetFieldID(cls_dm, "heightPixels", "I") : nullptr;
        if (get_res && get_dm && fw && fh) {
            jobject res = env->CallObjectMethod(g_activity, get_res);
            jobject dm = res ? env->CallObjectMethod(res, get_dm) : nullptr;
            if (dm) {
                w = env->GetIntField(dm, fw);
                h = env->GetIntField(dm, fh);
            }
        }
        LogAndClear(env, "webui: metrics");
    }
    const jint panel_w = (jint)(w * 0.56f);
    jobject lp = lp_ctor ? env->NewObject(cls_lp, lp_ctor, panel_w, h) : nullptr;

    jclass cls_act = env->FindClass("android/app/Activity");
    jmethodID add =
        cls_act ? env->GetMethodID(cls_act, "addContentView",
                                   "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V")
                : nullptr;
    if (!add || !lp) {
        LogAndClear(env, "webui: addContentView lookup");
        return;
    }
    // Transparent WebView compositing on some Android GPU paths clips CSS
    // backgrounds after resizing. Keep an opaque, non-interactive native base
    // behind the expanded panel; it is hidden for the launcher and mini window.
    jclass base_view=env->FindClass("android/view/View");
    jobject base=env->NewObject(base_view,env->GetMethodID(base_view,"<init>","(Landroid/content/Context;)V"),g_activity);
    env->CallVoidMethod(base,env->GetMethodID(base_view,"setBackgroundColor","(I)V"),(jint)0xff29180f);
    env->CallVoidMethod(base,env->GetMethodID(base_view,"setClickable","(Z)V"),JNI_FALSE);
    env->CallVoidMethod(base,env->GetMethodID(base_view,"setFocusable","(Z)V"),JNI_FALSE);
    env->CallVoidMethod(base,env->GetMethodID(base_view,"setImportantForAccessibility","(I)V"),2);
    jmethodID panel_frame=env->GetStaticMethodID(g_ui_class,"panelFrame","(Ljava/lang/Object;Ljava/lang/Object;)V");
    if(panel_frame)env->CallStaticVoidMethod(g_ui_class,panel_frame,base,g_activity);
    LogAndClear(env,"webui: native panel frame");
    env->CallVoidMethod(g_activity,add,base,lp);
    g_panel_background=env->NewGlobalRef(base);
    jclass frame_class=env->FindClass("android/widget/FrameLayout");
    jobject window=env->NewObject(frame_class,env->GetMethodID(frame_class,"<init>","(Landroid/content/Context;)V"),g_activity);
    jclass group=env->FindClass("android/view/ViewGroup");
    env->CallVoidMethod(window,env->GetMethodID(group,"setClipChildren","(Z)V"),JNI_TRUE);
    env->CallVoidMethod(window,env->GetMethodID(group,"setClipToPadding","(Z)V"),JNI_TRUE);
    jobject full=env->NewObject(cls_lp,lp_ctor,w,h);
    env->CallVoidMethod(window,env->GetMethodID(group,"addView","(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"),wv,full);
    env->CallVoidMethod(g_activity,add,window,full);
    g_overlay_window=env->NewGlobalRef(window);
    if (env->ExceptionCheck()) {
        LogAndClear(env, "webui: addContentView");
        return;
    }

    jmethodID bridge = env->GetMethodID(cls_web, "addJavascriptInterface", "(Ljava/lang/Object;Ljava/lang/String;)V");
    if (bridge) env->CallVoidMethod(wv, bridge, g_runnable, env->NewStringUTF("Bpp"));
    jmethodID load = env->GetMethodID(cls_web, "loadUrl", "(Ljava/lang/String;)V");
    if (load && !g_html_path.empty()) {
        std::string url = "file://" + g_html_path;
        jstring jurl = env->NewStringUTF(url.c_str());
        env->CallVoidMethod(wv, load, jurl);
        LogAndClear(env, "webui: loadUrl");
    }

    g_webview = env->NewGlobalRef(wv);
    if(g_ui_class){
        jmethodID touch=env->GetStaticMethodID(g_ui_class,"installTouch","(Ljava/lang/Object;)V");
        if(touch)env->CallStaticVoidMethod(g_ui_class,touch,wv);
    }
    g_created.store(true);
    ApplyLayout(env);
    LOGI("webui: created on main thread (%p)", g_webview);
}

// Runs on the Android main thread right after OnUi.
void JNICALL OnPost(JNIEnv* env, jclass) {
    LocalFrame frame(env);
    std::string js;
    if (g_hide_keyboard.exchange(false)) HideKeyboardOnUi(env);
    bool layout = g_layout_dirty.exchange(false);
    bool markers_dirty=g_markers_dirty.exchange(false);
    if ((layout || markers_dirty) && g_webview) ApplyLayout(env,layout);
    std::string capture;
    {std::lock_guard<std::mutex> lock(g_js_mu);capture.swap(g_capture_path);}
    if(!capture.empty()&&g_ui_class){
        jmethodID method=env->GetStaticMethodID(g_ui_class,"capture","(Ljava/lang/Object;Ljava/lang/String;)V");
        if(method)env->CallStaticVoidMethod(g_ui_class,method,g_activity,env->NewStringUTF(capture.c_str()));
        LogAndClear(env,"webui: screenshot");
    }
    if (!g_page_ready.load()) return;
    {
        std::lock_guard<std::mutex> lk(g_js_mu);
        js.swap(g_pending_js);
    }
    if (layout) js += std::string("window.setPanelState({nativePanel:true,windowWidth:")+std::to_string(g_window_css_w)+",windowHeight:"+std::to_string(g_window_css_h)+",open:" + (g_open.load() ? "true" : "false") +
        ",mini:"+(g_mini.load()?"true":"false")+",mode:'" + (g_bubble.load() ? "bubble" : "side") + "'});";
    if (js.empty() || !g_webview) return;
    jclass cls_web = env->FindClass("android/webkit/WebView");
    if(layout){
        jmethodID scale=env->GetMethodID(cls_web,"getScale","()F");
        if(scale)LOGI("webui: paint scale=%.3f viewport=%dx%d window=%.1fx%.1f css",env->CallFloatMethod(g_webview,scale),g_viewport_w,g_viewport_h,g_window_css_w,g_window_css_h);
    }
    jmethodID eval = cls_web ? env->GetMethodID(cls_web, "evaluateJavascript",
                                                "(Ljava/lang/String;Landroid/webkit/ValueCallback;)V")
                             : nullptr;
    if (!eval) return;
    jstring code = env->NewStringUTF(js.c_str());
    env->CallVoidMethod(g_webview,eval,code,nullptr);
    LogAndClear(env, "webui: evaluateJavascript");
}

bool MakeRunnable(JNIEnv* env, jobject activity, jobject* out) {
    jobject buf = env->NewDirectByteBuffer(const_cast<unsigned char*>(kBppDex), kBppDexLen);
    if (!buf) {
        LogAndClear(env, "webui: direct buffer");
        return false;
    }
    jclass cls_obj = env->FindClass("java/lang/Object");
    jclass cls_cls = env->FindClass("java/lang/Class");
    jmethodID get_class = env->GetMethodID(cls_obj, "getClass", "()Ljava/lang/Class;");
    jmethodID get_loader =
        env->GetMethodID(cls_cls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject cls = env->CallObjectMethod(activity, get_class);
    jobject loader = cls ? env->CallObjectMethod(cls, get_loader) : nullptr;

    jclass cls_dcl = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (!cls_dcl) {
        LogAndClear(env, "webui: InMemoryDexClassLoader missing (API < 26?)");
        return false;
    }
    jmethodID ctor = env->GetMethodID(cls_dcl, "<init>",
                                      "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    jobject dcl = ctor ? env->NewObject(cls_dcl, ctor, buf, loader) : nullptr;
    if (!dcl) {
        LogAndClear(env, "webui: dex loader ctor");
        return false;
    }
    jmethodID load_class =
        env->GetMethodID(cls_dcl, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring name = env->NewStringUTF("bpp.Ui");
    jclass cls_ui = load_class ? (jclass)env->CallObjectMethod(dcl, load_class, name) : nullptr;
    if (!cls_ui) {
        LogAndClear(env, "webui: loadClass bpp.Ui");
        return false;
    }
    JNINativeMethod m[3] = {{"onUi", "()V", (void*)OnUi}, {"onPost", "()V", (void*)OnPost},
                          {"onCommand", "(Ljava/lang/String;Ljava/lang/String;)V", (void*)OnCommand}};
    if (env->RegisterNatives(cls_ui, m, 3) != JNI_OK) {
        LogAndClear(env, "webui: RegisterNatives");
        return false;
    }
    g_ui_class=(jclass)env->NewGlobalRef(cls_ui);
    jmethodID ctor_ui = env->GetMethodID(cls_ui, "<init>", "()V");
    jobject runnable = ctor_ui ? env->NewObject(cls_ui, ctor_ui) : nullptr;
    if (!runnable) {
        LogAndClear(env, "webui: instantiate bpp.Ui");
        return false;
    }
    g_runnable = env->NewGlobalRef(runnable);
    *out = runnable;
    return true;
}

}  // namespace

bool ShowWebPanel(const std::string& html_path) {
    if (g_webview) return true;
    g_html_path = html_path;
    JNIEnv* env = Env();
    if (!env) return false;
    LocalFrame frame(env);

    jclass cls_unity = env->FindClass("com/unity3d/player/UnityPlayer");
    jfieldID fid = cls_unity ? env->GetStaticFieldID(cls_unity, "currentActivity",
                                                     "Landroid/app/Activity;")
                             : nullptr;
    jobject activity = fid ? env->GetStaticObjectField(cls_unity, fid) : nullptr;
    if (!activity) {
        LogAndClear(env, "webui: currentActivity");
        return false;
    }
    g_activity = env->NewGlobalRef(activity);

    jobject runnable = nullptr;
    if (!MakeRunnable(env, activity, &runnable)) return false;

    jclass cls_act = env->FindClass("android/app/Activity");
    jmethodID run = env->GetMethodID(cls_act, "runOnUiThread", "(Ljava/lang/Runnable;)V");
    if (!run) {
        LogAndClear(env, "webui: runOnUiThread lookup");
        return false;
    }
    env->CallVoidMethod(activity, run, runnable);
    LogAndClear(env, "webui: runOnUiThread");
    LOGI("webui: posted to main thread");
    return true;
}

bool WebPanelCreated() { return g_created.load(); }
bool WebPanelReady() { return g_created.load() && g_page_ready.load(); }
bool WebPanelVisible() { return g_created.load() && g_open.load(); }

void SetWebPanelVisible(bool visible) {
    g_mini.store(false);g_dragging.store(false);
    g_open.store(visible);
    if (!visible) g_hide_keyboard.store(true);
    g_layout_dirty.store(true);
    PostUi();
}

bool WebMiniVisible(){return g_mini.load()&&!g_open.load();}
void SetWebMini(bool visible,float width,float height,float x,float y){
    g_mini.store(visible);g_open.store(false);g_dragging.store(false);
    g_mini_w.store(std::clamp(width,100.f,650.f));g_mini_h.store(std::clamp(height,60.f,240.f));
    g_mini_x.store(std::clamp(x,-1.f,10000.f));g_mini_y.store(std::clamp(y,-1.f,10000.f));g_hide_keyboard.store(true);g_layout_dirty.store(true);PostUi();
}
void MoveWebMini(float x,float y){if(!g_mini.load())return;g_mini_x.store(std::clamp(x,0.f,10000.f));g_mini_y.store(std::clamp(y,0.f,10000.f));g_layout_dirty.store(true);PostUi();}
void SetWebMiniDragging(bool dragging){g_dragging.store(dragging);g_layout_dirty.store(true);PostUi();}
void UpdateWebMarkers(const std::vector<WebMarker>& markers,const std::string& summary){
    {std::lock_guard<std::mutex> lock(g_js_mu);g_markers=markers;g_match_summary=summary;}
    g_markers_dirty.store(true);PostUi();
}
void CaptureGameScreen(const std::string& path){
    {std::lock_guard<std::mutex> lock(g_js_mu);g_capture_path=path;}
    PostUi();
}

void HideWebKeyboard() {
    g_hide_keyboard.store(true);
    PostUi();
}

void SetWebPanelMode(const std::string& mode) {
    g_bubble.store(mode == "bubble");
    g_layout_dirty.store(true);
    PostUi();
}

namespace {
void PostUi() {
    if (!g_webview || !g_activity || !g_runnable) return;
    JNIEnv* env = Env();
    if (!env) return;
    LocalFrame frame(env);
    jclass cls_act = env->FindClass("android/app/Activity");
    jmethodID run = cls_act ? env->GetMethodID(cls_act, "runOnUiThread", "(Ljava/lang/Runnable;)V")
                            : nullptr;
    if (run) {
        env->CallVoidMethod(g_activity, run, g_runnable);
        LogAndClear(env, "webui: post js");
    }
}
}  // namespace

void PushJs(const std::string& js) {
    if (!g_created.load() || !g_page_ready.load()) return;
    {
        std::lock_guard<std::mutex> lk(g_js_mu);
        g_pending_js += js;
    }
    PostUi();
}

std::vector<WebCommand> DrainWebCommands() {
    std::vector<WebCommand> result;
    std::lock_guard<std::mutex> lk(g_js_mu);
    result.swap(g_commands);
    return result;
}
