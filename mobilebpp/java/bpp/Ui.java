package bpp;

import android.webkit.JavascriptInterface;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Proxy;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Tiny bridge: Android UI work is posted through Runnable; game commands are queued natively. */
public final class Ui implements Runnable {
    private static boolean mini, pressed, dragging, moved;
    private static float density=1, startX, startY, originX, originY, windowX, windowY;
    private static int screenHeight;
    private static Object followBadge;
    public static native void onUi();
    public static native void onPost();
    public static native void onCommand(String action, String value);

    @Override public void run() { onUi(); onPost(); }

    @JavascriptInterface
    public void command(String action, String value) { onCommand(action, value); }

    public static void miniGeometry(boolean enabled, float scale, int x, int y, int screenH) {
        mini=enabled; density=scale; windowX=x; windowY=y; screenHeight=screenH;
        if (!enabled) { pressed=false; dragging=false; }
    }

    // Compose the original copper banner and favorite glyph once. The banner's
    // right-facing point becomes the hanging tip; no text covers the card art.
    public static void followMarker(Object target, Object activity) {
        try {
            Class<?> context=Class.forName("android.content.Context");
            Class<?> bitmap=Class.forName("android.graphics.Bitmap");
            Class<?> paint=Class.forName("android.graphics.Paint");
            Class<?> rect=Class.forName("android.graphics.Rect");
            Class<?> rectF=Class.forName("android.graphics.RectF");
            if(followBadge==null){
                Object assets=context.getMethod("getAssets").invoke(activity);
                Class<?> manager=Class.forName("android.content.res.AssetManager");
                Class<?> factory=Class.forName("android.graphics.BitmapFactory");
                Object banner,heart;
                try(java.io.InputStream stream=(java.io.InputStream)manager.getMethod("open",String.class).invoke(assets,"bpp/theme/follow-banner.webp")){
                    banner=factory.getMethod("decodeStream",java.io.InputStream.class).invoke(null,stream);
                }
                try(java.io.InputStream stream=(java.io.InputStream)manager.getMethod("open",String.class).invoke(assets,"bpp/theme/star-icon.webp")){
                    heart=factory.getMethod("decodeStream",java.io.InputStream.class).invoke(null,stream);
                }
                Class<?> matrix=Class.forName("android.graphics.Matrix");
                Object rotation=matrix.getConstructor().newInstance();
                matrix.getMethod("postRotate",float.class).invoke(rotation,90f);
                int w=(Integer)bitmap.getMethod("getWidth").invoke(banner),h=(Integer)bitmap.getMethod("getHeight").invoke(banner);
                Object hanging=bitmap.getMethod("createBitmap",bitmap,int.class,int.class,int.class,int.class,matrix,boolean.class)
                    .invoke(null,banner,0,0,w,h,rotation,true);
                Class<?> config=Class.forName("android.graphics.Bitmap$Config");
                Object badge=bitmap.getMethod("createBitmap",int.class,int.class,config)
                    .invoke(null,h,w,config.getField("ARGB_8888").get(null));
                Class<?> canvas=Class.forName("android.graphics.Canvas");
                Object surface=canvas.getConstructor(bitmap).newInstance(badge);
                Object brush=paint.getConstructor(int.class).newInstance(3);
                canvas.getMethod("drawBitmap",bitmap,float.class,float.class,paint).invoke(surface,hanging,0f,0f,brush);
                Object glyph=rectF.getConstructor(float.class,float.class,float.class,float.class)
                    .newInstance(h*.33f,w*.30f,h*.67f,w*.30f+h*.34f);
                canvas.getMethod("drawBitmap",bitmap,rect,rectF,paint).invoke(surface,heart,null,glyph,brush);
                followBadge=badge;
                bitmap.getMethod("recycle").invoke(banner);
                bitmap.getMethod("recycle").invoke(heart);
                bitmap.getMethod("recycle").invoke(hanging);
            }
            Class<?> resources=Class.forName("android.content.res.Resources");
            Object drawable=Class.forName("android.graphics.drawable.BitmapDrawable")
                .getConstructor(resources,bitmap).newInstance(context.getMethod("getResources").invoke(activity),followBadge);
            Class.forName("android.view.View").getMethod("setBackground",Class.forName("android.graphics.drawable.Drawable"))
                .invoke(target,drawable);
        }catch(Exception e){onCommand("status","follow_badge_unavailable");}
    }

    // Keep the frame on the same native view as the opaque panel base. Chromium
    // can retain a border-image at the old viewport size after this WebView resizes.
    public static void panelFrame(Object target, Object activity) {
        try {
            Class<?> context = Class.forName("android.content.Context");
            Object assets = context.getMethod("getAssets").invoke(activity);
            Object bitmap;
            try (java.io.InputStream stream = (java.io.InputStream) Class.forName("android.content.res.AssetManager")
                    .getMethod("open", String.class).invoke(assets, "bpp/theme/panel-frame.webp")) {
                bitmap = Class.forName("android.graphics.BitmapFactory")
                    .getMethod("decodeStream", java.io.InputStream.class).invoke(null, stream);
            }
            Class<?> bitmapClass = Class.forName("android.graphics.Bitmap");
            // The original sprite is 256 x 128, with 64 / 48 px corner slices.
            // At this density the fixed corners occupy about 11 x 8 dp.
            bitmapClass.getMethod("setDensity", int.class).invoke(bitmap, 960);
            ByteBuffer chunk = ByteBuffer.allocate(84).order(ByteOrder.nativeOrder());
            chunk.put((byte) 1).put((byte) 2).put((byte) 2).put((byte) 9);
            for (int i = 0; i < 7; i++) chunk.putInt(0);
            chunk.putInt(64).putInt(192).putInt(48).putInt(80);
            // Leave the center transparent so the base keeps its theme color.
            for (int i = 0; i < 9; i++) chunk.putInt(i == 4 ? 0 : 1);
            Class<?> resources = Class.forName("android.content.res.Resources");
            Class<?> rect = Class.forName("android.graphics.Rect");
            Object frame = Class.forName("android.graphics.drawable.NinePatchDrawable")
                .getConstructor(resources, bitmapClass, byte[].class, rect, String.class)
                .newInstance(context.getMethod("getResources").invoke(activity), bitmap, chunk.array(),
                    rect.getConstructor().newInstance(), "bazaarBot panel frame");
            Class.forName("android.view.View").getMethod("setForeground", Class.forName("android.graphics.drawable.Drawable"))
                .invoke(target, frame);
        } catch (Exception e) { onCommand("status", "native_panel_frame_unavailable"); }
    }

    public static void installTouch(Object webView) {
        try {
            Class<?> view=Class.forName("android.view.View");
            Class<?> listener=Class.forName("android.view.View$OnTouchListener");
            Class<?> motion=Class.forName("android.view.MotionEvent");
            Object callback=Proxy.newProxyInstance(listener.getClassLoader(),new Class<?>[]{listener},(proxy,method,args)->{
                if (!method.getName().equals("onTouch")) return null;
                if (!mini) return false;
                Object event=args[1];
                int action=(Integer)motion.getMethod("getActionMasked").invoke(event);
                float x=(Float)motion.getMethod("getRawX").invoke(event);
                float y=(Float)motion.getMethod("getRawY").invoke(event);
                if (action==0) {
                    pressed=true; dragging=false; moved=false;
                    startX=x;startY=y;originX=windowX;originY=windowY;
                    view.getMethod("postDelayed",Runnable.class,long.class).invoke(webView,(Runnable)()->{
                        if (pressed&&mini&&!moved) { dragging=true;onCommand("float_drag","1"); }
                    },350L);
                } else if (action==2&&pressed) {
                    if (Math.hypot(x-startX,y-startY)>10*density) moved=true;
                    if (dragging)onCommand("float_move",((originX+x-startX)/density)+","+((originY+y-startY)/density));
                } else if (action==1&&pressed) {
                    pressed=false;
                    if (dragging) {
                        float left=(originX+x-startX)/density,top=(originY+y-startY)/density;
                        if (moved&&left<38&&Math.abs(y-screenHeight*.5f)<52*density)onCommand("dock","");
                        onCommand("float_commit",Math.max(0,left)+","+Math.max(0,top));
                        onCommand("float_drag","0");dragging=false;
                    } else if (!moved) onCommand("float_tap","");
                } else if (action==3) {
                    pressed=false;dragging=false;onCommand("float_drag","0");
                }
                return true;
            });
            view.getMethod("setOnTouchListener",listener).invoke(webView,callback);
        } catch (Exception e) { onCommand("status","native_touch_unavailable"); }
    }

    // Use Android's public PixelCopy API. Reflection keeps this tiny bridge
    // buildable without shipping framework classes or an Android SDK jar.
    private static Object surface(Object view) throws Exception {
        if (Class.forName("android.view.SurfaceView").isInstance(view)) return view;
        Class<?> group = Class.forName("android.view.ViewGroup");
        if (!group.isInstance(view)) return null;
        int count = (Integer) group.getMethod("getChildCount").invoke(view);
        for (int i = 0; i < count; i++) {
            Object found = surface(group.getMethod("getChildAt", int.class).invoke(view, i));
            if (found != null) return found;
        }
        return null;
    }

    public static void capture(Object activity, String path) {
        try {
            Object window = activity.getClass().getMethod("getWindow").invoke(activity);
            Object root = Class.forName("android.view.Window").getMethod("getDecorView").invoke(window);
            Object source = surface(root);
            if (source == null) { onCommand("capture_done", "error:no_surface"); return; }
            Class<?> view = Class.forName("android.view.View");
            int w = (Integer) view.getMethod("getWidth").invoke(source);
            int h = (Integer) view.getMethod("getHeight").invoke(source);
            if (w < 1 || h < 1) { onCommand("capture_done", "error:empty_surface"); return; }
            Class<?> bitmap = Class.forName("android.graphics.Bitmap");
            Class<?> config = Class.forName("android.graphics.Bitmap$Config");
            Object image = bitmap.getMethod("createBitmap", int.class, int.class, config)
                .invoke(null, w, h, config.getField("ARGB_8888").get(null));
            Class<?> listener = Class.forName("android.view.PixelCopy$OnPixelCopyFinishedListener");
            Object callback = Proxy.newProxyInstance(listener.getClassLoader(), new Class<?>[]{listener}, (proxy, method, args) -> {
                if (!method.getName().equals("onPixelCopyFinished")) return null;
                int result = (Integer) args[0];
                if (result != 0) {
                    bitmap.getMethod("recycle").invoke(image);
                    onCommand("capture_done", "error:pixelcopy_" + result);
                    return null;
                }
                new Thread(() -> {
                    try (FileOutputStream out = new FileOutputStream(path)) {
                        Class<?> format = Class.forName("android.graphics.Bitmap$CompressFormat");
                        boolean ok = (Boolean) bitmap.getMethod("compress", format, int.class, java.io.OutputStream.class)
                            .invoke(image, format.getField("PNG").get(null), 100, out);
                        onCommand("capture_done", ok ? new File(path).getName() : "error:encode");
                    } catch (Exception e) { onCommand("capture_done", "error:write"); }
                    finally { try { bitmap.getMethod("recycle").invoke(image); } catch (Exception ignored) {} }
                }, "bazaarBot-capture").start();
                return null;
            });
            Class<?> looper = Class.forName("android.os.Looper");
            Class<?> handler = Class.forName("android.os.Handler");
            Object main = handler.getConstructor(looper).newInstance(looper.getMethod("getMainLooper").invoke(null));
            Class.forName("android.view.PixelCopy").getMethod("request", Class.forName("android.view.SurfaceView"), bitmap, listener, handler)
                .invoke(null, source, image, callback, main);
        } catch (Exception e) { onCommand("capture_done", "error:unavailable"); }
    }
}
