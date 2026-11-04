// android/app/src/main/java/org/openba/game/GameFiles.java (docs/android/README.md, issue #96)
//
// Where the app keeps the player's game files and saves, and whether the packages are there. The
// folders are the ones the native side uses (src/platform/android/app_android.cpp, EnterRunDirectory):
// the data folder is getExternalFilesDir(null) = SDL_GetAndroidExternalStoragePath(), the packages
// are in its WinData/, the saves in Documents/Banished/Save/.
//
// "Present" is src/app/game_data_check.h's rule: WinData holds data0.pkg (any case, as the loader's
// FindFiles matches it). The native check stays the authority; this one only decides whether the
// activity shows the first-run screen before the game starts.
package org.openba.game;

import android.content.Context;

import java.io.File;
import java.util.Locale;

final class GameFiles {
    static final String BASE_PACKAGE = "data0.pkg";
    static final String PACKAGE_SUFFIX = ".pkg";
    static final String MOD_SUFFIX = ".pkm";   // a mod package beside data0.pkg (issue #96: the reporter's mods)

    private GameFiles() {
    }

    static File dataDir(Context context) {
        return context.getExternalFilesDir(null);
    }

    // WinData/, made by the app: a folder adb or MTP makes may belong to another user, and the app
    // can neither list nor open anything inside it (docs/android/README.md). Null without a data folder.
    static File winDataDir(Context context) {
        final File data = dataDir(context);
        if (data == null) {
            return null;
        }
        final File dir = new File(data, "WinData");
        dir.mkdirs();
        return dir;
    }

    static File savesDir(Context context) {
        final File data = dataDir(context);
        if (data == null) {
            return null;
        }
        return new File(data, "Documents/Banished/Save");
    }

    // A retail package (.pkg) or a mod (.pkm): both live in WinData, the game lists *.pkm there for its Mods page.
    static boolean isPackageName(String name) {
        if (name == null) {
            return false;
        }
        final String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(PACKAGE_SUFFIX) || lower.endsWith(MOD_SUFFIX);
    }

    // A name that is one file in its folder: no separator, no NUL, not "." or "..". A folder copy names
    // its files by the DocumentsProvider's display name, which is free text ("../WinData/x.pkg").
    static boolean isPlainFileName(String name) {
        return name != null && !name.isEmpty() && !name.equals(".") && !name.equals("..")
            && name.indexOf('/') < 0 && name.indexOf('\\') < 0 && name.indexOf('\0') < 0;
    }

    static boolean isBasePackageName(String name) {
        return name != null && name.equalsIgnoreCase(BASE_PACKAGE);
    }

    static boolean hasGameData(Context context) {
        final File dir = winDataDir(context);
        if (dir == null) {
            return false;
        }
        final File[] files = dir.listFiles();
        if (files == null) {
            return false;
        }
        for (File f : files) {
            if (f.isFile() && isBasePackageName(f.getName()) && f.length() > 0) {
                return true;
            }
        }
        return false;
    }

    // The packages in WinData: count and bytes, for the screen's status line.
    static long[] packageSummary(Context context) {
        final long[] summary = new long[2];
        final File dir = winDataDir(context);
        final File[] files = dir != null ? dir.listFiles() : null;
        if (files == null) {
            return summary;
        }
        for (File f : files) {
            if (f.isFile() && isPackageName(f.getName())) {
                summary[0]++;
                summary[1] += f.length();
            }
        }
        return summary;
    }

    static String megabytes(long bytes) {
        return String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024.0));
    }

    static void deleteTree(File f) {
        if (f == null || !f.exists()) {
            return;
        }
        final File[] children = f.listFiles();
        if (children != null) {
            for (File c : children) {
                deleteTree(c);
            }
        }
        f.delete();
    }
}
