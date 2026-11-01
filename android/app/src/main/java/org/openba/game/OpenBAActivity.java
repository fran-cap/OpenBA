// android/app/src/main/java/org/openba/game/OpenBAActivity.java (docs/android/README.md)
//
// The app's one activity: SDL's SDLActivity (compiled from the pinned SDL submodule, see
// android/app/build.gradle) with five changes.
//
//  * The native entry point is the game's own `main` (src/app/main.cpp, built into libmain.so),
//    the same function Linux runs, not SDL's renamed SDL_main: main.cpp defines SDL_MAIN_HANDLED.
//
//  * The launch environment. An Android process has no environment a launcher could fill, and
//    the game reads its BANISHED_* flags once at boot from the environment (src/app/options.cpp,
//    the one getenv in the tree; src/app/options.h is the inventory). The sanctioned source on
//    Android is the launch intent: every string extra whose key starts with BANISHED_ becomes that
//    environment variable before the native library is loaded, e.g.
//        adb shell am start -n <package>/org.openba.game.OpenBAActivity -e BANISHED_AUTOSTART 1
//    Variables left from an earlier launch in the same process are cleared first, so a launch
//    from the icon (no extras) runs with none. Nothing else sets the environment.
//
//  * The surface is OpenBASurface: SDL's, with the captured mouse's buttons and motion passed on
//    (a camera-rotate drag's middle-button release was lost; OpenBASurface.java has the defect).
//
//  * The typeface. The apk carries FrancophilSans as assets (tools/release/fonts, the files the
//    desktop packages put beside the exe; THIRD_PARTY_NOTICES.md). ext.sharp_ui's rasteriser reads
//    the TTF from the app's data folder (src/platform/android/font_raster_android.cpp, the folder
//    SDL_GetAndroidExternalStoragePath names = getExternalFilesDir(null)), so each file absent there
//    is copied in before the native side starts. A file present is left alone (the player's own
//    copy wins). Here rather than in the native boot: assets are a Java API, the native side
//    would need the activity's AssetManager through JNI for the same three copies.
//
//  * physicalDisplay, called by the native side through JNI (src/platform/android/
//    display_size_android.cpp): the display's size in pixels and dots per inch, which the first
//    run's settings seed turns into inches to tell a tablet from a phone (issue #96).
package org.openba.game;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.DisplayMetrics;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class OpenBAActivity extends SDLActivity {
    private static final String TAG = "OpenBA";
    private static final String FLAG_PREFIX = "BANISHED_";
    // The apk's font assets (android/app/build.gradle: tools/release/fonts), copied as they are.
    private static final String[] FONT_ASSETS = {
        "FrancophilSans.ttf", "FrancophilSans-Bold.ttf", "FrancophilSans_README.txt",
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        applyLaunchEnvironment(getIntent());
        copyFontAssets();
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new OpenBASurface(context);
    }

    // The whole display's metrics (getRealMetrics: the panel, not the window less the system bars):
    // { widthPixels, heightPixels, xdpi, ydpi, densityDpi }. Called once at boot through JNI from
    // src/platform/android/display_size_android.cpp, which names this method and its "()[F" signature.
    @SuppressWarnings("deprecation")
    public float[] physicalDisplay() {
        final DisplayMetrics metrics = new DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        return new float[] {
            metrics.widthPixels, metrics.heightPixels, metrics.xdpi, metrics.ydpi, metrics.densityDpi,
        };
    }

    private void copyFontAssets() {
        final File dataDir = getExternalFilesDir(null);
        if (dataDir == null) {
            Log.w(TAG, "font: no data folder, the font is not copied");
            return;
        }
        for (String name : FONT_ASSETS) {
            final File target = new File(dataDir, name);
            if (target.isFile()) {
                continue;
            }
            final File part = new File(dataDir, name + ".part");
            try (InputStream in = getAssets().open(name); OutputStream out = new FileOutputStream(part)) {
                final byte[] buffer = new byte[65536];
                int n;
                while ((n = in.read(buffer)) > 0) {
                    out.write(buffer, 0, n);
                }
            } catch (IOException e) {
                Log.w(TAG, "font: copying " + name + " failed: " + e);
                part.delete();
                continue;
            }
            if (part.renameTo(target)) {
                Log.i(TAG, "font: copied " + name + " to " + dataDir);
            } else {
                Log.w(TAG, "font: renaming " + part + " failed");
                part.delete();
            }
        }
    }

    private static void applyLaunchEnvironment(Intent intent) {
        try {
            for (String entry : Os.environ()) {
                final int eq = entry.indexOf('=');
                if (eq > 0 && entry.startsWith(FLAG_PREFIX)) {
                    Os.unsetenv(entry.substring(0, eq));
                }
            }
        } catch (ErrnoException e) {
            Log.w(TAG, "clearing the launch environment failed: " + e);
        }
        final Bundle extras = intent != null ? intent.getExtras() : null;
        if (extras == null) {
            return;
        }
        for (String key : extras.keySet()) {
            if (!key.startsWith(FLAG_PREFIX)) {
                continue;
            }
            final Object value = extras.get(key);
            if (value == null) {
                continue;
            }
            try {
                Os.setenv(key, String.valueOf(value), true);
                Log.i(TAG, "launch environment " + key + "=" + value);
            } catch (ErrnoException e) {
                Log.w(TAG, "setenv " + key + " failed: " + e);
            }
        }
    }
}
