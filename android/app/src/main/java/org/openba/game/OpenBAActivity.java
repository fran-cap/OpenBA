// android/app/src/main/java/org/openba/game/OpenBAActivity.java (docs/android/README.md)
//
// The app's one activity: SDL's SDLActivity (compiled from the pinned SDL submodule, see
// android/app/build.gradle) with six changes.
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
//
//  * The first-run screen (issue #96). When WinData holds no data0.pkg, the activity shows the
//    game-files screen (GameFilesScreen.java) in place of SDL's layout before the game starts: the
//    player picks the folder or the .zip holding their packages, the app copies them into WinData,
//    and the screen hands back. SDL starts the game's main only once its surface is ready
//    (SDLActivity.handleNativeState), and its surface is in SDL's layout, so while the screen is the
//    content view nothing native runs; putting SDL's layout back makes the surface and starts the
//    game in the same process, no restart. A harness launch (any BANISHED_* extra) never shows the
//    screen: the native check then reports NO_GAME_DATA and exits with code 6, as before
//    (src/app/game_data_check.h). The launcher icon's long-press shortcut "Saves and game files"
//    (GameFilesActivity) is published here.
package org.openba.game;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ShortcutInfo;
import android.content.pm.ShortcutManager;
import android.graphics.drawable.Icon;
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
import java.util.Collections;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class OpenBAActivity extends SDLActivity {
    private static final String TAG = "OpenBA";
    private static final String FLAG_PREFIX = "BANISHED_";
    // The apk's font assets (android/app/build.gradle: tools/release/fonts), copied as they are.
    private static final String[] FONT_ASSETS = {
        "FrancophilSans.ttf", "FrancophilSans-Bold.ttf", "FrancophilSans_README.txt",
    };

    // The first-run screen while it is the content view (the game not started); null otherwise.
    private GameFilesScreen mFirstRun;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        final boolean harness = applyLaunchEnvironment(getIntent());
        copyFontAssets();
        // WinData is made here too, before the native side: tools/android.sh push-data starts the
        // app once so that the folder belongs to it, and the first-run screen may hold the start.
        GameFiles.winDataDir(this);
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || isFinishing()) {
            return;
        }
        publishShortcut();
        if (!harness && !GameFiles.hasGameData(this)) {
            Log.i(TAG, "game files: no data0.pkg in WinData, the first-run screen is shown");
            mFirstRun = new GameFilesScreen(this, GameFilesScreen.Mode.FIRST_RUN, this::startGameAfterFirstRun);
            setContentView(mFirstRun.view());
        }
    }

    // The packages are in WinData: SDL's layout goes back, its surface is made, and SDL starts main.
    private void startGameAfterFirstRun() {
        Log.i(TAG, "game files: installed, starting the game");
        mFirstRun.destroy();
        mFirstRun = null;
        setContentView(mLayout);
    }

    // True while the game's main runs in this process (GameFilesActivity keeps the packages then).
    static boolean gameRunning() {
        return mSDLThread != null && !mSDLMainFinished;
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mFirstRun != null) {
            mFirstRun.resume();
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (mFirstRun == null) {
            super.onBackPressed();
            return;
        }
        if (!mFirstRun.onBackPressed()) {
            finish();
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (mFirstRun != null) {
            mFirstRun.onActivityResult(requestCode, resultCode, data);
        }
    }

    @Override
    protected void onDestroy() {
        final boolean leftFirstRun = mFirstRun != null && isFinishing();
        if (mFirstRun != null) {
            mFirstRun.destroy();
            mFirstRun = null;
        }
        super.onDestroy();
        // SDL refuses a second activity in the same process (SDLActivity.onCreate exits it), so a
        // player who left the first-run screen would find the next tap on the icon doing nothing.
        // The game never started: nothing is lost by ending the process here.
        if (leftFirstRun) {
            System.exit(0);
        }
    }

    // The launcher icon's long-press entry for GameFilesActivity (API 25+; the minSdk is 29).
    private void publishShortcut() {
        try {
            final ShortcutManager manager = getSystemService(ShortcutManager.class);
            if (manager == null) {
                return;
            }
            final Intent intent = new Intent(Intent.ACTION_VIEW);
            intent.setClass(this, GameFilesActivity.class);
            final ShortcutInfo info = new ShortcutInfo.Builder(this, "game_files")
                                          .setShortLabel("Saves and files")
                                          .setLongLabel("Saves and game files")
                                          .setIcon(Icon.createWithResource(this, getApplicationInfo().icon))
                                          .setIntent(intent)
                                          .build();
            manager.setDynamicShortcuts(Collections.singletonList(info));
        } catch (RuntimeException e) {
            Log.w(TAG, "shortcut: " + e);
        }
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

    // True when the intent carried a BANISHED_* extra (a harness launch).
    private static boolean applyLaunchEnvironment(Intent intent) {
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
            return false;
        }
        boolean any = false;
        for (String key : extras.keySet()) {
            if (!key.startsWith(FLAG_PREFIX)) {
                continue;
            }
            final Object value = extras.get(key);
            if (value == null) {
                continue;
            }
            any = true;
            try {
                Os.setenv(key, String.valueOf(value), true);
                Log.i(TAG, "launch environment " + key + "=" + value);
            } catch (ErrnoException e) {
                Log.w(TAG, "setenv " + key + " failed: " + e);
            }
        }
        return any;
    }
}
