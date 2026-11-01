// android/app/src/main/java/org/openba/game/GameFilesScreen.java (docs/android/README.md, issue #96)
//
// The game-files screen: plain Android views in the game's dark palette. Two hosts:
//
//  * FIRST_RUN, in OpenBAActivity before the game starts, when WinData holds no data0.pkg. Since
//    Android 11 a file manager on the phone cannot write into Android/data, so most players could
//    not put their packages where the game reads them. The screen asks the system's picker (the
//    Storage Access Framework, no permission) for the folder holding the packages or for a .zip,
//    copies the packages into the app's WinData (GameFilesJob) and hands back to the activity, which
//    then starts the game. The overflow button holds Import saves / Export saves.
//
//  * MANAGE, in GameFilesActivity (the launcher icon's long-press shortcut "Saves and game files"),
//    any time: the saves' export and import, and the packages replaced while the game is not running.
//
// Text is short: it has to fit a phone in landscape. The screen scrolls when it does not.
package org.openba.game;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.text.format.DateFormat;
import android.util.TypedValue;
import android.view.ContextThemeWrapper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Date;

final class GameFilesScreen implements GameFilesJob.Listener {
    enum Mode { FIRST_RUN, MANAGE }

    interface Host {
        // FIRST_RUN: the packages are in WinData now; the host starts the game.
        void onGameFilesInstalled();
    }

    private static final int REQUEST_FOLDER = 0x4f01;
    private static final int REQUEST_ARCHIVE = 0x4f02;
    private static final int REQUEST_EXPORT = 0x4f03;
    private static final int REQUEST_IMPORT = 0x4f04;

    // The game's dark palette: the menus' dark wood and parchment text.
    private static final int COLOR_BACKGROUND = 0xff17120d;
    private static final int COLOR_TEXT = 0xffeadfc8;
    private static final int COLOR_DIM = 0xffb5a68a;
    private static final int COLOR_TITLE = 0xffd9a94e;
    private static final int COLOR_BUTTON = 0xff4a3826;
    private static final int COLOR_BUTTON_EDGE = 0xff8c6a3e;
    private static final int COLOR_ERROR = 0xffe58a6a;

    private final Activity mActivity;
    private final Mode mMode;
    private final Host mHost;
    private final FrameLayout mRoot;
    private final TextView mBody;
    private final Button mFolderButton;
    private final Button mArchiveButton;
    private final Button mExportButton;
    private final Button mImportButton;
    private final Button mCancelButton;
    private final ProgressBar mBar;
    private final TextView mStatus;
    private View mOverflow;

    GameFilesScreen(Activity activity, Mode mode, Host host) {
        mActivity = activity;
        mMode = mode;
        mHost = host;

        mRoot = new FrameLayout(activity);
        mRoot.setBackgroundColor(COLOR_BACKGROUND);
        final ScrollView scroll = new ScrollView(activity);
        scroll.setFillViewport(true);
        mRoot.addView(scroll, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                           ViewGroup.LayoutParams.MATCH_PARENT));
        final LinearLayout column = new LinearLayout(activity);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER);
        column.setPadding(dp(56), dp(16), dp(56), dp(16));
        scroll.addView(column, new ScrollView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                           ViewGroup.LayoutParams.MATCH_PARENT));

        final TextView title = text(mode == Mode.FIRST_RUN ? "Banished game files needed" : "Game files and saves",
                                    22, COLOR_TITLE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        column.addView(title, wrap());

        mBody = text("", 15, COLOR_TEXT);
        final LinearLayout.LayoutParams bodyParams = wrap();
        bodyParams.topMargin = dp(8);
        column.addView(mBody, bodyParams);

        final LinearLayout dataRow = row(activity);
        mFolderButton = button("Choose the folder");
        mFolderButton.setOnClickListener(v -> pickFolder());
        mArchiveButton = button("Choose an archive");
        mArchiveButton.setOnClickListener(v -> pickArchive());
        dataRow.addView(mFolderButton, weighted());
        dataRow.addView(mArchiveButton, weighted());
        column.addView(dataRow, rowParams());

        mExportButton = button("Export saves");
        mExportButton.setOnClickListener(v -> exportSaves());
        mImportButton = button("Import saves");
        mImportButton.setOnClickListener(v -> importSaves());
        if (mode == Mode.MANAGE) {
            final LinearLayout savesRow = row(activity);
            savesRow.addView(mExportButton, weighted());
            savesRow.addView(mImportButton, weighted());
            column.addView(savesRow, rowParams());
        }

        mBar = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);
        mBar.setMax(1000);
        mBar.setVisibility(View.GONE);
        final LinearLayout.LayoutParams barParams = new LinearLayout.LayoutParams(dp(480), dp(12));
        barParams.topMargin = dp(12);
        column.addView(mBar, barParams);

        mStatus = text("", 14, COLOR_DIM);
        final LinearLayout.LayoutParams statusParams = wrap();
        statusParams.topMargin = dp(8);
        column.addView(mStatus, statusParams);

        mCancelButton = button("Cancel");
        mCancelButton.setOnClickListener(v -> {
            final GameFilesJob job = GameFilesJob.current();
            if (job != null) {
                job.cancel();
            }
        });
        mCancelButton.setVisibility(View.GONE);
        final LinearLayout.LayoutParams cancelParams = new LinearLayout.LayoutParams(dp(200), dp(48));
        cancelParams.topMargin = dp(8);
        column.addView(mCancelButton, cancelParams);

        if (mode == Mode.FIRST_RUN) {
            // The overflow: the saves, for a player who reinstalled and kept an export.
            final TextView overflow = text("⋮", 26, COLOR_TEXT);
            overflow.setGravity(Gravity.CENTER);
            overflow.setContentDescription("More");
            overflow.setOnClickListener(this::showOverflow);
            final FrameLayout.LayoutParams p = new FrameLayout.LayoutParams(dp(56), dp(56), Gravity.TOP | Gravity.END);
            p.setMargins(0, dp(8), dp(16), 0);
            mRoot.addView(overflow, p);
            mOverflow = overflow;
        }

        GameFilesJob.setListener(this);
        refresh();
    }

    View view() {
        return mRoot;
    }

    // The host's onResume: take the listener back (another screen may have had it) and catch up.
    void resume() {
        GameFilesJob.setListener(this);
        final GameFilesJob job = GameFilesJob.current();
        if (job != null && job.finished) {
            onFinished(job);
        } else {
            refresh();
        }
    }

    void destroy() {
        GameFilesJob.clearListener(this);
    }

    // The back key: a running job is cancelled (and the screen stays); otherwise false.
    boolean onBackPressed() {
        final GameFilesJob job = GameFilesJob.current();
        if (job != null && !job.finished) {
            job.cancel();
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------------------------
    // Views
    // ------------------------------------------------------------------------------------------

    private int dp(float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v,
                                                    mActivity.getResources().getDisplayMetrics()));
    }

    private TextView text(String s, float sp, int color) {
        final TextView t = new TextView(mActivity);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER_HORIZONTAL);
        return t;
    }

    private Button button(String label) {
        final Button b = new Button(mActivity);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        b.setTextColor(COLOR_TEXT);
        final GradientDrawable bg = new GradientDrawable();
        bg.setColor(COLOR_BUTTON);
        bg.setStroke(dp(2), COLOR_BUTTON_EDGE);
        bg.setCornerRadius(dp(6));
        b.setBackground(bg);
        return b;
    }

    private static LinearLayout row(Activity activity) {
        final LinearLayout r = new LinearLayout(activity);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER);
        return r;
    }

    private LinearLayout.LayoutParams rowParams() {
        final LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(dp(520), ViewGroup.LayoutParams.WRAP_CONTENT);
        p.topMargin = dp(14);
        return p;
    }

    private LinearLayout.LayoutParams weighted() {
        final LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, dp(56), 1f);
        p.setMargins(dp(8), 0, dp(8), 0);
        return p;
    }

    private static LinearLayout.LayoutParams wrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private void showOverflow(View anchor) {
        final ContextThemeWrapper themed =
            new ContextThemeWrapper(mActivity, android.R.style.Theme_DeviceDefault);
        final PopupMenu menu = new PopupMenu(themed, anchor);
        menu.getMenu().add(0, 1, 0, "Import saves");
        menu.getMenu().add(0, 2, 1, "Export saves");
        menu.setOnMenuItemClickListener(item -> {
            if (item.getItemId() == 1) {
                importSaves();
            } else {
                exportSaves();
            }
            return true;
        });
        menu.show();
    }

    private static boolean gameRunning() {
        return OpenBAActivity.gameRunning();
    }

    // The idle screen: the body text, which buttons work, no bar.
    private void refresh() {
        final GameFilesJob job = GameFilesJob.current();
        final boolean busy = job != null && !job.finished;
        final boolean dataLocked = mMode == Mode.MANAGE && gameRunning();
        mFolderButton.setEnabled(!busy && !dataLocked);
        mArchiveButton.setEnabled(!busy && !dataLocked);
        mExportButton.setEnabled(!busy);
        mImportButton.setEnabled(!busy);
        if (mOverflow != null) {
            mOverflow.setEnabled(!busy);
        }
        mFolderButton.setAlpha(mFolderButton.isEnabled() ? 1f : 0.45f);
        mArchiveButton.setAlpha(mArchiveButton.isEnabled() ? 1f : 0.45f);
        mCancelButton.setVisibility(busy ? View.VISIBLE : View.GONE);
        mBar.setVisibility(busy ? View.VISIBLE : View.GONE);
        if (busy) {
            mActivity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            onProgress(job);
        } else {
            mActivity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
        if (mMode == Mode.FIRST_RUN) {
            mBody.setText("OpenBA plays the files of your own copy of Banished. Copy its WinData folder "
                          + "(data0.pkg, data1.pkg, patch0.pkg, patch1.pkg) to this device, then choose "
                          + "it here, or a .zip holding it. The .pkg files are copied into the app.");
        } else {
            final long[] summary = GameFiles.packageSummary(mActivity);
            String s = GameFiles.hasGameData(mActivity)
                           ? "Game files: " + summary[0] + " packages, " + GameFiles.megabytes(summary[1]) + "."
                           : "Game files: none yet (no data0.pkg).";
            s += " Saves: " + GameFilesJob.saveFiles(mActivity).length + ".";
            if (dataLocked) {
                s += "\nThe game is running: quit it to replace the game files.";
            }
            mBody.setText(s);
        }
    }

    private void setStatus(String s, boolean error) {
        mStatus.setText(s);
        mStatus.setTextColor(error ? COLOR_ERROR : COLOR_DIM);
    }

    // ------------------------------------------------------------------------------------------
    // The pickers (the Storage Access Framework: the system's UI, no permission)
    // ------------------------------------------------------------------------------------------

    private void launch(Intent intent, int request) {
        try {
            mActivity.startActivityForResult(intent, request);
        } catch (ActivityNotFoundException e) {
            setStatus("This device has no document picker.", true);
        }
    }

    private void pickFolder() {
        launch(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_FOLDER);
    }

    private static Intent openZip() {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(Intent.EXTRA_MIME_TYPES,
                        new String[] {"application/zip", "application/x-zip-compressed", "application/x-zip"});
        return intent;
    }

    private void pickArchive() {
        launch(openZip(), REQUEST_ARCHIVE);
    }

    private void exportSaves() {
        if (GameFilesJob.saveFiles(mActivity).length == 0) {
            setStatus("No saves to export yet.", false);
            return;
        }
        final Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        intent.putExtra(Intent.EXTRA_TITLE,
                        "OpenBA saves " + DateFormat.format("yyyy-MM-dd", new Date()) + ".zip");
        launch(intent, REQUEST_EXPORT);
    }

    private void importSaves() {
        launch(openZip(), REQUEST_IMPORT);
    }

    // The host's onActivityResult. True when the request was one of the screen's.
    boolean onActivityResult(int requestCode, int resultCode, Intent data) {
        GameFilesJob.Kind kind;
        switch (requestCode) {
        case REQUEST_FOLDER:
            kind = GameFilesJob.Kind.COPY_FOLDER;
            break;
        case REQUEST_ARCHIVE:
            kind = GameFilesJob.Kind.COPY_ARCHIVE;
            break;
        case REQUEST_EXPORT:
            kind = GameFilesJob.Kind.EXPORT_SAVES;
            break;
        case REQUEST_IMPORT:
            kind = GameFilesJob.Kind.IMPORT_SAVES;
            break;
        default:
            return false;
        }
        final Uri uri = data != null ? data.getData() : null;
        if (resultCode != Activity.RESULT_OK || uri == null) {
            // The picker was backed out of: nothing changes.
            setStatus("", false);
            return true;
        }
        if (GameFilesJob.start(mActivity, kind, uri) == null) {
            setStatus("Busy: wait for the copy that is running.", true);
            return true;
        }
        setStatus("", false);
        refresh();
        return true;
    }

    // ------------------------------------------------------------------------------------------
    // GameFilesJob.Listener (main thread)
    // ------------------------------------------------------------------------------------------

    @Override
    public void onProgress(GameFilesJob job) {
        if (job.bytesTotal > 0) {
            mBar.setIndeterminate(false);
            mBar.setProgress((int) Math.min(1000, job.barDone * 1000 / job.bytesTotal));
        } else {
            mBar.setIndeterminate(true);
        }
        String verb;
        switch (job.kind) {
        case EXPORT_SAVES:
            verb = "Exporting ";
            break;
        case IMPORT_SAVES:
            verb = "Importing ";
            break;
        default:
            verb = "Copying ";
            break;
        }
        String s = verb + job.currentName + ": " + GameFiles.megabytes(job.bytesDone);
        if (job.kind == GameFilesJob.Kind.COPY_FOLDER && job.bytesTotal > 0) {
            s += " of " + GameFiles.megabytes(job.bytesTotal);
        }
        setStatus(s, false);
    }

    @Override
    public void onFinished(GameFilesJob job) {
        GameFilesJob.clearFinished();
        refresh();
        if (!job.succeeded) {
            setStatus(job.message, !job.cancelled);
            return;
        }
        switch (job.kind) {
        case COPY_FOLDER:
        case COPY_ARCHIVE:
            if (!GameFiles.hasGameData(mActivity)) {
                setStatus(job.message + " But WinData still has no data0.pkg.", true);
                return;
            }
            if (mMode == Mode.FIRST_RUN) {
                setStatus(job.message + " Starting...", false);
                mHost.onGameFilesInstalled();
            } else {
                setStatus(job.message + " Start OpenBA from its icon.", false);
            }
            break;
        case EXPORT_SAVES:
            setStatus(job.message, false);
            break;
        case IMPORT_SAVES:
            if (job.importConflicts == 0) {
                setStatus(GameFilesJob.finishImport(mActivity, false), false);
                refresh();
                break;
            }
            final int n = job.importConflicts;
            new AlertDialog.Builder(mActivity, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setMessage(n + (n == 1 ? " save has" : " saves have") + " the name of a save already here. "
                            + "Replace yours?")
                .setPositiveButton("Replace", (d, w) -> {
                    setStatus(GameFilesJob.finishImport(mActivity, true), false);
                    refresh();
                })
                .setNegativeButton("Keep mine", (d, w) -> {
                    setStatus(GameFilesJob.finishImport(mActivity, false), false);
                    refresh();
                })
                .setCancelable(false)
                .show();
            break;
        }
    }
}
