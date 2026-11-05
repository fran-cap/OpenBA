// android/app/src/main/java/org/openba/game/GameFilesJob.java (docs/android/README.md, issue #96)
//
// The one background job of the game-files screen (GameFilesScreen.java): copying the player's
// packages from a folder or a .zip they picked, and exporting / importing the saves as a .zip.
// One job at a time, held in a static so it outlives the activity that started it (the screen
// turning off, the activity made again): a new screen attaches to the running job and is told its
// progress and its end. The work runs on its own thread; the listener is called on the main thread.
//
// The copy of the packages. This is the player's own Banished install, on the player's own device,
// copied at the player's explicit action (they pick the folder or the archive in the system's
// picker) into this app's own data folder: the same thing `tools/android.sh push-data` does for a
// developer and the USB copy the README describes. Nothing of the game is in the apk or the tree
// (AGENTS.md section 1, docs/extensions/DESIGN.md section 3 rule 7); the app only reads what the
// player hands it. The packages of the one folder that holds data0.pkg are taken: its *.pkg (retail) and
// *.pkm (the player's mods, which the game lists from WinData), never a subfolder (parked mods: _mods_off
// and the like), the rule push-data follows.
//
// Every copy is staged: the files are written into a folder beside WinData and moved into it only
// when every one of them arrived, so a cancel, a failure or a wrong folder leaves WinData as it was.
// A process killed during the final moves (or a package that cannot be moved back after a failure)
// leaves the replaced packages in WinData.replaced: the next copy moves them back first.
// Archives: java.util.zip only (no new dependency); .7z and .rar would need one and are not read.
package org.openba.game;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

final class GameFilesJob {
    private static final String TAG = "OpenBA";
    // How deep below the picked folder the one holding data0.pkg is looked for (the picked folder
    // is 0: WinData itself; 1: the Banished folder; 2..3: a folder holding the Banished folder).
    private static final int MAX_FOLDER_DEPTH = 3;
    private static final int BUFFER_SIZE = 1 << 20;
    private static final long PROGRESS_INTERVAL_MS = 100;
    // Kept free on the data folder's volume by every job that writes there (the game's own saves and
    // settings live on it too).
    private static final long FREE_SPACE_MARGIN = 64L << 20;

    enum Kind { COPY_FOLDER, COPY_ARCHIVE, EXPORT_SAVES, IMPORT_SAVES }

    interface Listener {
        void onProgress(GameFilesJob job);
        void onFinished(GameFilesJob job);
    }

    // The job's state, read by the screen on the main thread.
    final Kind kind;
    volatile String currentName = "";
    volatile long bytesDone;
    volatile long bytesTotal;     // the bar's whole: the bytes to write, or the archive's size; 0 unknown
    volatile long barDone;        // the bar's part done: bytes written, or the archive's bytes read
    volatile boolean finished;
    volatile boolean succeeded;
    volatile boolean cancelled;
    volatile String message = "";
    // IMPORT_SAVES: the staged saves and how many have the name of a save already there.
    final List<String> importedNames = new ArrayList<>();
    int importConflicts;
    long startedAt;
    long finishedAt;

    private static GameFilesJob sCurrent;
    private static Listener sListener;
    // The first-run screen while the game's start waits for a package copy (OpenBAActivity.onCreate).
    // The copy may run from "Game files" (a task of its own), whose screen can hold the listener and
    // take the result (clearFinished): the waiter is told that a package copy ended all the same.
    private static Listener sCopyWaiter;
    private static final Handler sMain = new Handler(Looper.getMainLooper());

    private final Context mApp;
    private final Uri mUri;
    private volatile boolean mCancelRequested;
    private long mLastPost;
    // The bytes the job may write into the data folder (startWriteBudget); unbounded for the export,
    // which writes to the picked document.
    private long mFreeAtStart;
    private long mWriteBudget = Long.MAX_VALUE;

    private GameFilesJob(Context app, Kind kind, Uri uri) {
        mApp = app;
        this.kind = kind;
        mUri = uri;
    }

    // The running job, or the last one until the screen has taken its result (clearFinished).
    static GameFilesJob current() {
        return sCurrent;
    }

    static boolean isRunning() {
        return sCurrent != null && !sCurrent.finished;
    }

    // True while a package copy runs: WinData is about to change, so the game does not start yet.
    static boolean isCopyingPackages() {
        return isRunning() && isPackageCopy(sCurrent.kind);
    }

    static boolean isPackageCopy(Kind kind) {
        return kind == Kind.COPY_FOLDER || kind == Kind.COPY_ARCHIVE;
    }

    static void setListener(Listener listener) {
        sListener = listener;
    }

    static void clearListener(Listener listener) {
        if (sListener == listener) {
            sListener = null;
        }
    }

    static void setCopyWaiter(Listener waiter) {
        sCopyWaiter = waiter;
    }

    static void clearCopyWaiter(Listener waiter) {
        if (sCopyWaiter == waiter) {
            sCopyWaiter = null;
        }
    }

    static void clearFinished() {
        if (sCurrent != null && sCurrent.finished) {
            sCurrent = null;
        }
    }

    static GameFilesJob start(Context context, Kind kind, Uri uri) {
        if (isRunning()) {
            return null;
        }
        final GameFilesJob job = new GameFilesJob(context.getApplicationContext(), kind, uri);
        sCurrent = job;
        job.startedAt = SystemClock.elapsedRealtime();
        final Thread thread = new Thread(job::run, "OpenBA-GameFiles");
        thread.start();
        return job;
    }

    void cancel() {
        mCancelRequested = true;
    }

    // ------------------------------------------------------------------------------------------
    // The thread
    // ------------------------------------------------------------------------------------------

    private void run() {
        try {
            if (kind != Kind.EXPORT_SAVES) {
                startWriteBudget();
            }
            switch (kind) {
            case COPY_FOLDER:
                copyFromFolder();
                break;
            case COPY_ARCHIVE:
                copyFromArchive();
                break;
            case EXPORT_SAVES:
                exportSaves();
                break;
            case IMPORT_SAVES:
                importSaves();
                break;
            }
        } catch (CancelledException e) {
            cancelled = true;
            message = "Cancelled. Nothing was changed.";
        } catch (IOException | RuntimeException e) {
            Log.w(TAG, "game files: " + kind + " failed", e);
            succeeded = false;
            message = "Failed: " + e.getMessage();
        }
        finishedAt = SystemClock.elapsedRealtime();
        Log.i(TAG, "game files: " + kind + (succeeded ? " done" : cancelled ? " cancelled" : " failed")
                       + ", " + bytesDone + " bytes in " + (finishedAt - startedAt) + " ms: " + message);
        sMain.post(() -> {
            finished = true;
            final Listener listener = sListener;
            if (listener != null) {
                listener.onFinished(this);
            }
            final Listener waiter = sCopyWaiter;
            if (waiter != null && waiter != listener && isPackageCopy(kind)) {
                waiter.onFinished(this);
            }
        });
    }

    private static final class CancelledException extends IOException {
        CancelledException() {
            super("cancelled");
        }
    }

    private void checkCancel() throws CancelledException {
        if (mCancelRequested) {
            throw new CancelledException();
        }
    }

    private void progress(boolean force) {
        final long now = SystemClock.elapsedRealtime();
        if (!force && now - mLastPost < PROGRESS_INTERVAL_MS) {
            return;
        }
        mLastPost = now;
        sMain.post(() -> {
            if (sListener != null && !finished) {
                sListener.onProgress(this);
            }
        });
    }

    // Copies in to out in 1 MB blocks, counting into bytesDone, checking the cancel at each block.
    private void pump(InputStream in, OutputStream out) throws IOException {
        final byte[] buffer = new byte[BUFFER_SIZE];
        int n;
        while ((n = in.read(buffer)) > 0) {
            checkCancel();
            checkWriteBudget(n);
            out.write(buffer, 0, n);
            bytesDone += n;
            barDone = bytesDone;
            progress(false);
        }
    }

    private File stagingDir(String name) throws IOException {
        final File data = GameFiles.dataDir(mApp);
        if (data == null) {
            throw new IOException("the app's data folder is not available");
        }
        final File dir = new File(data, name);
        GameFiles.deleteTree(dir);
        if (!dir.mkdirs()) {
            throw new IOException("cannot make " + dir);
        }
        return dir;
    }

    // The free space is read once, at the start. A size the archive or the provider reports bounds
    // nothing (a zip entry inflates to any size, a provider's SIZE is its own word), so every write
    // into the data folder is counted against it.
    private void startWriteBudget() {
        final File data = GameFiles.dataDir(mApp);
        mFreeAtStart = data != null ? data.getUsableSpace() : 0;
        mWriteBudget = mFreeAtStart - FREE_SPACE_MARGIN;
    }

    private void checkWriteBudget(long more) throws IOException {
        if (bytesDone + more > mWriteBudget) {
            throw new IOException("not enough free space: more than " + GameFiles.megabytes(bytesDone + more)
                                  + " to write, " + GameFiles.megabytes(mFreeAtStart) + " free");
        }
    }

    private void checkFreeSpace(long needed) throws IOException {
        final File data = GameFiles.dataDir(mApp);
        final long free = data != null ? data.getUsableSpace() : 0;
        if (needed > 0 && free < needed + FREE_SPACE_MARGIN) {
            throw new IOException("not enough free space: " + GameFiles.megabytes(needed) + " needed, "
                                  + GameFiles.megabytes(free) + " free");
        }
    }

    // Moves every file of `staged` into WinData (a same-name package is replaced). All or nothing: the
    // packages it replaces are first moved aside into WinData.replaced, then the new ones in; on a
    // failure the new ones are removed and the old ones moved back, so WinData is as it was. Renames
    // inside one file system: quick, and nothing is half-written in WinData.
    private void installStaged(File staged) throws IOException {
        // The running game opens WinData's packages (the Mods page reads every .pkm again). The screen
        // greys its buttons while the game runs, but the game can start during a long copy (from its
        // icon: GameFilesActivity is a task of its own), so the lock is checked again at the move.
        if (OpenBAActivity.gameRunning()) {
            throw new IOException("the game is running: quit it, then copy the game files again");
        }
        final File winData = GameFiles.winDataDir(mApp);
        if (winData == null) {
            throw new IOException("the app's data folder is not available");
        }
        final File[] files = staged.listFiles();
        if (files == null) {
            throw new IOException("cannot list " + staged);
        }
        restoreReplaced(winData);
        final File aside = stagingDir("WinData.replaced");
        final List<File> movedAside = new ArrayList<>();
        final List<File> movedIn = new ArrayList<>();
        try {
            for (File f : files) {
                final File target = new File(winData, f.getName());
                if (!target.exists()) {
                    continue;
                }
                final File old = new File(aside, f.getName());
                // A folder of a package's name is refused, as before: only packages are replaced.
                if (target.isDirectory() || !target.renameTo(old)) {
                    throw new IOException("cannot replace " + target.getName());
                }
                movedAside.add(old);
            }
            for (File f : files) {
                final File target = new File(winData, f.getName());
                if (!f.renameTo(target)) {
                    throw new IOException("cannot move " + f.getName() + " into WinData");
                }
                movedIn.add(target);
            }
        } catch (IOException e) {
            for (File f : movedIn) {
                f.delete();
            }
            boolean restored = true;
            for (File old : movedAside) {
                if (!old.renameTo(new File(winData, old.getName()))) {
                    Log.w(TAG, "game files: cannot move " + old.getName() + " back into WinData");
                    restored = false;
                }
            }
            // A package that could not go back stays in WinData.replaced: the next copy moves it back
            // first (restoreReplaced).
            if (restored) {
                GameFiles.deleteTree(aside);
            }
            throw e;
        }
        GameFiles.deleteTree(aside);
    }

    // Moves what an earlier copy left in WinData.replaced (a package it could not move back, or the
    // process killed between installStaged's two loops) back into WinData where WinData has no file
    // of its name, before stagingDir empties that folder: an old package is never deleted unseen.
    private void restoreReplaced(File winData) throws IOException {
        final File data = GameFiles.dataDir(mApp);
        final File[] left = data != null ? new File(data, "WinData.replaced").listFiles() : null;
        if (left == null) {
            return;
        }
        for (File old : left) {
            final File target = new File(winData, old.getName());
            if (target.exists()) {
                continue;
            }
            if (!old.renameTo(target)) {
                throw new IOException("cannot move " + old.getName() + " back into WinData");
            }
            Log.i(TAG, "game files: " + old.getName() + " moved back into WinData from WinData.replaced");
        }
    }

    // ------------------------------------------------------------------------------------------
    // The packages from a folder (ACTION_OPEN_DOCUMENT_TREE)
    // ------------------------------------------------------------------------------------------

    private static final class Doc {
        final String id;
        final String name;
        final boolean folder;
        final long size;

        Doc(String id, String name, boolean folder, long size) {
            this.id = id;
            this.name = name;
            this.folder = folder;
            this.size = size;
        }
    }

    private List<Doc> children(String parentId) {
        final List<Doc> out = new ArrayList<>();
        final Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(mUri, parentId);
        final String[] columns = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE,
        };
        try (Cursor c = mApp.getContentResolver().query(uri, columns, null, null, null)) {
            if (c == null) {
                return out;
            }
            while (c.moveToNext()) {
                final String mime = c.getString(2);
                final boolean folder = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
                final long size = c.isNull(3) ? 0 : c.getLong(3);
                out.add(new Doc(c.getString(0), c.getString(1), folder, size));
            }
        }
        return out;
    }

    private static boolean holdsBasePackage(List<Doc> docs) {
        for (Doc d : docs) {
            if (!d.folder && GameFiles.isBasePackageName(d.name)) {
                return true;
            }
        }
        return false;
    }

    private void copyFromFolder() throws IOException {
        currentName = "looking for data0.pkg";
        progress(true);
        // Breadth first: the shallowest folder holding data0.pkg is the game's WinData.
        final ArrayDeque<String> ids = new ArrayDeque<>();
        final ArrayDeque<Integer> depths = new ArrayDeque<>();
        ids.add(DocumentsContract.getTreeDocumentId(mUri));
        depths.add(0);
        List<Doc> found = null;
        while (!ids.isEmpty() && found == null) {
            checkCancel();
            final String id = ids.poll();
            final int depth = depths.poll();
            final List<Doc> docs = children(id);
            if (holdsBasePackage(docs)) {
                found = docs;
                break;
            }
            if (depth < MAX_FOLDER_DEPTH) {
                for (Doc d : docs) {
                    if (d.folder) {
                        ids.add(d.id);
                        depths.add(depth + 1);
                    }
                }
            }
        }
        if (found == null) {
            message = "No data0.pkg in that folder. Choose Banished's WinData folder: it holds data0.pkg, "
                      + "data1.pkg, patch0.pkg and patch1.pkg.";
            return;
        }
        final List<Doc> packages = new ArrayList<>();
        for (Doc d : found) {
            // The provider's display name becomes the staged file's name: one that is not a plain
            // file name ("../WinData/x.pkg", "a/b.pkg") would be written outside the staging folder.
            if (!d.folder && GameFiles.isPackageName(d.name) && GameFiles.isPlainFileName(d.name)) {
                packages.add(d);
                bytesTotal += d.size;
            }
        }
        checkFreeSpace(bytesTotal);
        final File staged = stagingDir("WinData.incoming");
        try {
            final ContentResolver resolver = mApp.getContentResolver();
            for (Doc d : packages) {
                currentName = d.name;
                progress(true);
                final Uri doc = DocumentsContract.buildDocumentUriUsingTree(mUri, d.id);
                try (InputStream in = resolver.openInputStream(doc);
                     OutputStream out = new FileOutputStream(new File(staged, d.name))) {
                    if (in == null) {
                        throw new IOException("cannot open " + d.name);
                    }
                    pump(in, out);
                }
            }
            installStaged(staged);
        } finally {
            GameFiles.deleteTree(staged);
        }
        succeeded = true;
        message = "Copied " + count(packages.size(), "package") + ", " + GameFiles.megabytes(bytesDone) + ".";
    }

    // ------------------------------------------------------------------------------------------
    // The packages from a .zip (ACTION_OPEN_DOCUMENT)
    // ------------------------------------------------------------------------------------------

    // Counts the archive's bytes read, for the bar: a zip read as a stream does not say how much it
    // holds until its end, the archive's own size does.
    private static final class CountingInputStream extends FilterInputStream {
        long count;

        CountingInputStream(InputStream in) {
            super(in);
        }

        @Override
        public int read() throws IOException {
            final int b = super.read();
            if (b >= 0) {
                count++;
            }
            return b;
        }

        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            final int n = super.read(b, off, len);
            if (n > 0) {
                count += n;
            }
            return n;
        }
    }

    private long documentSize(Uri uri) {
        try (Cursor c = mApp.getContentResolver().query(uri, new String[] {OpenableColumns.SIZE}, null, null,
                                                        null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) {
                return c.getLong(0);
            }
        } catch (RuntimeException e) {
            Log.w(TAG, "game files: no size for " + uri + ": " + e);
        }
        return 0;
    }

    private static int depthOf(String folder) {
        if (folder.isEmpty()) {
            return 0;
        }
        int depth = 1;
        for (int i = 0; i < folder.length(); i++) {
            if (folder.charAt(i) == '/') {
                depth++;
            }
        }
        return depth;
    }

    private void copyFromArchive() throws IOException {
        // One pass over the stream (a picked document need not be seekable). Every *.pkg / *.pkm is staged by
        // the folder it is in; the shallowest folder holding data0.pkg wins at the end and the rest
        // are dropped. A folder deeper than one already known to hold data0.pkg is skipped.
        final long archiveSize = documentSize(mUri);
        checkFreeSpace(archiveSize);
        bytesTotal = archiveSize;
        final File staged = stagingDir("WinData.incoming");
        final Map<String, File> folders = new HashMap<>();
        final Map<String, Integer> counts = new HashMap<>();
        String best = null;
        long counted = 0;
        try {
            final InputStream raw = mApp.getContentResolver().openInputStream(mUri);
            if (raw == null) {
                throw new IOException("cannot open the archive");
            }
            final CountingInputStream counting = new CountingInputStream(raw);
            try (ZipInputStream zip = new ZipInputStream(counting)) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    checkCancel();
                    if (entry.isDirectory()) {
                        continue;
                    }
                    String path = entry.getName().replace('\\', '/');
                    while (path.startsWith("/") || path.startsWith("./")) {
                        path = path.substring(path.startsWith("/") ? 1 : 2);
                    }
                    final int slash = path.lastIndexOf('/');
                    final String folder = slash >= 0 ? path.substring(0, slash) : "";
                    final String name = path.substring(slash + 1);
                    if (!GameFiles.isPackageName(name) || name.startsWith(".")) {
                        continue;
                    }
                    if (best != null && depthOf(folder) > depthOf(best)) {
                        continue;
                    }
                    File dir = folders.get(folder);
                    if (dir == null) {
                        // A numbered folder per archive folder: the archive's names never reach a path.
                        dir = new File(staged, Integer.toString(folders.size()));
                        if (!dir.mkdirs()) {
                            throw new IOException("cannot make " + dir);
                        }
                        folders.put(folder, dir);
                    }
                    currentName = name;
                    progress(true);
                    try (OutputStream out = new FileOutputStream(new File(dir, name))) {
                        final long before = bytesDone;
                        final byte[] buffer = new byte[BUFFER_SIZE];
                        int n;
                        while ((n = zip.read(buffer)) > 0) {
                            checkCancel();
                            checkWriteBudget(n);
                            out.write(buffer, 0, n);
                            bytesDone += n;
                            // The bar follows the archive's bytes read; the text says MB written.
                            counted = counting.count;
                            barDone = counted;
                            progress(false);
                        }
                        if (bytesDone == before && entry.getSize() > 0) {
                            throw new IOException(name + " is empty in the archive");
                        }
                    }
                    counts.put(folder, counts.getOrDefault(folder, 0) + 1);
                    if (GameFiles.isBasePackageName(name) && (best == null || depthOf(folder) < depthOf(best))) {
                        best = folder;
                    }
                }
            }
            if (best == null) {
                message = "No data0.pkg in that archive. Choose a .zip holding Banished's WinData folder, "
                          + "or its .pkg files.";
                return;
            }
            installStaged(folders.get(best));
        } finally {
            GameFiles.deleteTree(staged);
        }
        succeeded = true;
        message = "Copied " + count(counts.get(best), "package") + ", " + GameFiles.megabytes(bytesDone) + ", from "
                  + (best.isEmpty() ? "the archive's top" : best + "/") + ".";
        Log.i(TAG, "game files: archive read " + counted + " of " + archiveSize + " bytes");
    }

    // ------------------------------------------------------------------------------------------
    // The saves
    // ------------------------------------------------------------------------------------------

    static File[] saveFiles(Context context) {
        final File dir = GameFiles.savesDir(context);
        final File[] files = dir != null ? dir.listFiles(File::isFile) : null;
        return files != null ? files : new File[0];
    }

    private void exportSaves() throws IOException {
        final File[] files = saveFiles(mApp);
        for (File f : files) {
            bytesTotal += f.length();
        }
        try (OutputStream raw = mApp.getContentResolver().openOutputStream(mUri, "wt")) {
            if (raw == null) {
                throw new IOException("cannot write the archive");
            }
            try (ZipOutputStream zip = new ZipOutputStream(raw)) {
                for (File f : files) {
                    currentName = f.getName();
                    progress(true);
                    final ZipEntry entry = new ZipEntry("Save/" + f.getName());
                    entry.setTime(f.lastModified());
                    zip.putNextEntry(entry);
                    try (InputStream in = new FileInputStream(f)) {
                        pump(in, zip);
                    }
                    zip.closeEntry();
                }
            }
        } catch (CancelledException e) {
            // A half-written archive is not a backup: remove the document the picker made.
            try {
                DocumentsContract.deleteDocument(mApp.getContentResolver(), mUri);
            } catch (Exception ignored) {
                // The provider may not allow it; the player then has a partial file to delete.
            }
            throw e;
        }
        succeeded = true;
        message = "Exported " + count(files.length, "save") + ", " + GameFiles.megabytes(bytesDone) + ".";
    }

    // Stages every *.sav of the archive (at any depth, by its file name) in Save.incoming beside
    // Save/; the screen then asks about the names already there and calls finishImport.
    private void importSaves() throws IOException {
        final File saves = GameFiles.savesDir(mApp);
        if (saves == null) {
            throw new IOException("the app's data folder is not available");
        }
        final File staged = new File(saves.getParentFile(), "Save.incoming");
        GameFiles.deleteTree(staged);
        if (!staged.mkdirs()) {
            throw new IOException("cannot make " + staged);
        }
        boolean keep = false;
        try {
            final InputStream raw = mApp.getContentResolver().openInputStream(mUri);
            if (raw == null) {
                throw new IOException("cannot open the archive");
            }
            try (ZipInputStream zip = new ZipInputStream(raw)) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    checkCancel();
                    if (entry.isDirectory()) {
                        continue;
                    }
                    final String path = entry.getName().replace('\\', '/');
                    final String name = path.substring(path.lastIndexOf('/') + 1);
                    if (name.startsWith(".") || !name.toLowerCase(Locale.ROOT).endsWith(".sav")) {
                        continue;
                    }
                    currentName = name;
                    progress(true);
                    final File target = new File(staged, name);
                    try (OutputStream out = new FileOutputStream(target)) {
                        pump(zip, out);
                    }
                    // The save's own time, kept by Export saves: the Load dialog dates the save by it.
                    if (entry.getTime() > 0) {
                        target.setLastModified(entry.getTime());
                    }
                    if (!importedNames.contains(name)) {
                        importedNames.add(name);
                        if (new File(saves, name).exists()) {
                            importConflicts++;
                        }
                    }
                }
            }
            if (importedNames.isEmpty()) {
                message = "No .sav files in that archive. Choose a .zip made by Export saves, or one "
                          + "holding Banished .sav files.";
                return;
            }
            keep = true;
        } finally {
            if (!keep) {
                GameFiles.deleteTree(staged);
            }
        }
        succeeded = true;
        message = "";
    }

    // Main thread, after importSaves succeeded: moves the staged saves into Save/. With replace false
    // a save whose name is already there is left alone and the archive's copy dropped.
    static String finishImport(Context context, boolean replace) {
        final File saves = GameFiles.savesDir(context);
        final File staged = new File(saves.getParentFile(), "Save.incoming");
        saves.mkdirs();
        int moved = 0;
        int kept = 0;
        final File[] files = staged.listFiles();
        if (files != null) {
            for (File f : files) {
                final File target = new File(saves, f.getName());
                if (target.exists()) {
                    if (!replace) {
                        kept++;
                        continue;
                    }
                    target.delete();
                }
                if (f.renameTo(target)) {
                    moved++;
                }
            }
        }
        GameFiles.deleteTree(staged);
        String text = "Imported " + count(moved, "save") + ".";
        if (kept > 0) {
            text += " Kept " + kept + " of yours with the same name.";
        }
        return text;
    }

    static String count(int n, String noun) {
        return n + " " + noun + (n == 1 ? "" : "s");
    }
}
