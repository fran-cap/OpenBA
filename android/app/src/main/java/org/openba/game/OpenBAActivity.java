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
//    environment variable before the native library is loaded, when the intent also carries the
//    harness token (OPENBA_HARNESS_TOKEN, the text of the one-use file harness_token in the data
//    folder, which adb can write and another app cannot). tools/android.sh run does both:
//        tools/android.sh run BANISHED_AUTOSTART=1
//    The activity is exported (the launcher starts it), so without the token any app could load
//    and change the player's saves through the flags. Variables left from an earlier launch in the
//    same process are cleared first, so a launch from the icon (no extras) runs with none. Nothing
//    else sets the environment.
//
//  * The surface is OpenBASurface: SDL's, with the captured mouse's buttons and motion passed on
//    (a camera-rotate drag's middle-button release was lost; OpenBASurface.java has the defect).
//
//  * The typeface. The apk carries FrancophilSans as assets (tools/release/fonts, the files the
//    desktop packages put beside the exe; THIRD_PARTY_NOTICES.md). ext.sharp_ui's rasteriser reads
//    the TTF from the app's data folder (src/platform/android/font_raster_android.cpp, the folder
//    SDL_GetAndroidExternalStoragePath names = getExternalFilesDir(null)), so each file absent there
//    is copied in before the native side starts, and so is an empty one (a copy cut short). Any
//    other file present is left alone (the player's own copy wins). Here rather than in the native boot: assets are a Java API, the native side
//    would need the activity's AssetManager through JNI for the same three copies.
//
//  * physicalDisplay, called by the native side through JNI (src/platform/android/
//    display_size_android.cpp): the display's size in pixels and dots per inch, which the first
//    run's settings seed turns into inches to tell a tablet from a phone (issue #96); and
//    openGameFilesScreen, the same way (src/platform/android/app_android.cpp): the main and pause
//    menus' "Game files" button starts GameFilesActivity.
//
//  * The first-run screen (issue #96). When WinData holds no data0.pkg, the activity shows the
//    game-files screen (GameFilesScreen.java) in place of SDL's layout before the game starts: the
//    player picks the folder or the .zip holding their packages, the app copies them into WinData,
//    and the screen hands back. SDL starts the game's main only once its surface is ready
//    (SDLActivity.handleNativeState), and its surface is in SDL's layout, so while the screen is the
//    content view nothing native runs; putting SDL's layout back makes the surface and starts the
//    game in the same process, no restart. A harness launch (any BANISHED_* extra) never shows the
//    screen: the native check then reports NO_GAME_DATA and exits with code 6, as before
//    (src/app/game_data_check.h). The launcher icon's long-press shortcut "Game files"
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
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.Collections;
import java.util.Map;
import java.util.TreeMap;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class OpenBAActivity extends SDLActivity {
    private static final String TAG = "OpenBA";
    private static final String FLAG_PREFIX = "BANISHED_";
    // The harness launch's credential (tools/android.sh run): an extra, and the file in the data folder
    // holding the same text.
    private static final String HARNESS_TOKEN_EXTRA = "OPENBA_HARNESS_TOKEN";
    private static final String HARNESS_TOKEN_FILE = "harness_token";
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
        // A copy started from "Game files" (its own task) may still run when the icon is tapped: the
        // game waits on the same screen, which starts it when the copy ends, and never opens a
        // WinData that is being replaced.
        final boolean copying = GameFilesJob.isCopyingPackages();
        if (!harness && (copying || !GameFiles.hasGameData(this))) {
            Log.i(TAG, copying ? "game files: a copy is running, the game starts when it ends"
                               : "game files: no data0.pkg in WinData, the first-run screen is shown");
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

    // Called by the native side through JNI (src/platform/android/app_android.cpp,
    // Platform::App::OpenGameFilesScreen, "()Z") from the game's thread when the player presses the
    // menu's "Game files" button: GameFilesActivity is started on the UI thread.
    public boolean openGameFilesScreen() {
        runOnUiThread(() -> {
            final Intent intent = new Intent(this, GameFilesActivity.class);
            startActivity(intent);
        });
        return true;
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
                                          .setShortLabel("Game files")
                                          .setLongLabel("Game files")
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
            // An empty file is a copy cut short (the process killed between the open and the first
            // write: the .part rename makes that rare, an older build or a crash can still leave one);
            // it is copied again. Any other file present is the player's and is kept.
            if (target.isFile() && target.length() > 0) {
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

    // True when the intent carried a BANISHED_* extra (a harness launch) and the harness token.
    private boolean applyLaunchEnvironment(Intent intent) {
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
        // The activity is exported (it is the launcher's): any app can start it with any extras. The
        // flags load and change saves (BANISHED_AUTOLOAD, BANISHED_SAVE_TEST, ...), so they are taken
        // only from the harness. A Bundle is unparcelled at its first read, and an extra of a class
        // the app does not have throws there: such an intent is a launch without flags.
        final Map<String, String> flags = new TreeMap<>();
        final String token;
        try {
            for (String key : extras.keySet()) {
                final Object value = key.startsWith(FLAG_PREFIX) ? extras.get(key) : null;
                if (value != null) {
                    flags.put(key, String.valueOf(value));
                }
            }
            token = extras.getString(HARNESS_TOKEN_EXTRA);
        } catch (RuntimeException e) {
            Log.w(TAG, "launch extras ignored: " + e);
            return false;
        }
        if (flags.isEmpty()) {
            return false;
        }
        if (!harnessTokenMatches(token)) {
            Log.w(TAG, "launch extras ignored: no harness token (tools/android.sh run passes it)");
            return false;
        }
        for (Map.Entry<String, String> flag : flags.entrySet()) {
            try {
                Os.setenv(flag.getKey(), flag.getValue(), true);
                Log.i(TAG, "launch environment " + flag.getKey() + "=" + flag.getValue());
            } catch (ErrnoException e) {
                Log.w(TAG, "setenv " + flag.getKey() + " failed: " + e);
            }
        }
        return true;
    }

    // tools/android.sh run writes a fresh random token into the data folder, where adb can write and
    // another app cannot, and passes the same token as an extra. The file is used once.
    private boolean harnessTokenMatches(String token) {
        final File dir = getExternalFilesDir(null);
        if (token == null || token.isEmpty() || dir == null) {
            return false;
        }
        final File file = new File(dir, HARNESS_TOKEN_FILE);
        final byte[] expected;
        try {
            expected = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8)
                           .trim()
                           .getBytes(StandardCharsets.UTF_8);
        } catch (IOException e) {
            return false;
        }
        if (!file.delete()) {
            Log.w(TAG, "harness token: " + file + " not removed");
        }
        return expected.length > 0 && MessageDigest.isEqual(expected, token.getBytes(StandardCharsets.UTF_8));
    }
}
