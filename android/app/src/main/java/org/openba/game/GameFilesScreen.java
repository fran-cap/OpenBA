// android/app/src/main/java/org/openba/game/GameFilesScreen.java (docs/android/README.md, issue #96)
//
// The game-files screen: plain Android views dressed as the game's dialogs (its panel and button
// colours, square-cornered bevelled buttons, the FrancophilSans typeface). Two hosts:
//
//  * FIRST_RUN, in OpenBAActivity before the game starts, when WinData holds no data0.pkg. Since
//    Android 11 a file manager on the phone cannot write into Android/data, so most players could
//    not put their packages where the game reads them. The screen asks the system's picker (the
//    Storage Access Framework, no permission) for the folder holding the packages or for a .zip,
//    copies the packages into the app's WinData (GameFilesJob) and hands back to the activity, which
//    then starts the game. The overflow button holds Import saves / Export saves.
//
//  * MANAGE, in GameFilesActivity (the main and pause menus' "Game files" button through
//    Platform::App::OpenGameFilesScreen, and the launcher icon's long-press shortcut of that name), any
//    time: the saves' export and import, and the packages replaced while the game is not running.
//
// Text is short: it has to fit a phone in landscape. The screen scrolls when it does not.
package org.openba.game;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.text.format.DateFormat;
import android.util.Log;
import android.util.TypedValue;
import android.view.ContextThemeWrapper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

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

    // The game's dialog colours, sampled from its own Options dialog on the tablet (a screenshot of the
    // running game, 3392x2400): the panel's warm grey, the label grey and the heading white, and a
    // button's face (a top-to-bottom gradient under a one-pixel light bevel, inside a dark outline).
    private static final int COLOR_PANEL = 0xff423e3a;          // the dialog face (66, 62, 58)
    private static final int COLOR_TEXT = 0xffc0c0c0;           // labels and button captions (192)
    private static final int COLOR_HEADING = 0xfffefefe;        // the group headings ("Touch screen")
    private static final int COLOR_BUTTON_TOP = 0xff5d5651;     // the face's top, under the bevel
    private static final int COLOR_BUTTON_BOTTOM = 0xff454039;  // the face's bottom
    private static final int COLOR_BUTTON_BEVEL = 0xff6d655e;   // the one-pixel light line on top
    private static final int COLOR_OUTLINE = 0xff2e2b28;        // button outlines, the scroll track
    private static final int COLOR_PRESSED = 0xff3a3632;        // a pressed button's face
    private static final int COLOR_ERROR = 0xffd98a74;          // a wrong pick (not a game colour)

    // FrancophilSans, the game's typeface, from the apk's assets (tools/release/fonts, the same files
    // OpenBAActivity copies into the data folder for ext.sharp_ui). Null if an asset cannot be read:
    // the views then keep the system's face.
    private static Typeface sFace;
    private static Typeface sFaceBold;

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
    private Button mBackButton;   // MANAGE only

    GameFilesScreen(Activity activity, Mode mode, Host host) {
        mActivity = activity;
        mMode = mode;
        mHost = host;
        loadFaces(activity);

        mRoot = new FrameLayout(activity);
        mRoot.setBackgroundColor(COLOR_PANEL);
        // The window draws edge to edge (target SDK 35): keep the buttons off the navigation bar,
        // the status bar and a cutout. The panel colour still fills behind them.
        mRoot.setOnApplyWindowInsetsListener((v, insets) -> {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                final android.graphics.Insets bars = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            } else {
                v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                             insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
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

        final TextView title = text(mode == Mode.FIRST_RUN ? "Banished game files needed" : "Game files",
                                    24, COLOR_HEADING);
        if (sFaceBold != null) {
            title.setTypeface(sFaceBold);
        }
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

        mBar.setProgressDrawable(barDrawable());
        mBar.setIndeterminateTintList(ColorStateList.valueOf(COLOR_BUTTON_BEVEL));

        mStatus = text("", 15, COLOR_TEXT);
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
            // The system face: FrancophilSans has no vertical ellipsis.
            overflow.setTypeface(Typeface.DEFAULT_BOLD);
            overflow.setGravity(Gravity.CENTER);
            overflow.setContentDescription("More");
            overflow.setOnClickListener(this::showOverflow);
            final FrameLayout.LayoutParams p = new FrameLayout.LayoutParams(dp(56), dp(56), Gravity.TOP | Gravity.END);
            p.setMargins(0, dp(8), dp(16), 0);
            mRoot.addView(overflow, p);
            mOverflow = overflow;
        } else {
            // Back to the game without the system's back gesture: bottom-right, where the game's
            // dialogs put Cancel / Ok. Not on the first run: there is no game to go back to yet.
            mBackButton = button("Back");
            mBackButton.setOnClickListener(v -> mActivity.finish());
            final FrameLayout.LayoutParams p =
                new FrameLayout.LayoutParams(dp(160), dp(48), Gravity.BOTTOM | Gravity.END);
            p.setMargins(0, 0, dp(24), dp(16));
            mRoot.addView(mBackButton, p);
        }

        GameFilesJob.setListener(this);
        if (mode == Mode.FIRST_RUN) {
            GameFilesJob.setCopyWaiter(this);
        }
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
        GameFilesJob.clearCopyWaiter(this);
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
        if (sFace != null) {
            t.setTypeface(sFace);
        }
        return t;
    }

    private static void loadFaces(Activity activity) {
        if (sFace != null) {
            return;
        }
        try {
            sFace = Typeface.createFromAsset(activity.getAssets(), "FrancophilSans.ttf");
            sFaceBold = Typeface.createFromAsset(activity.getAssets(), "FrancophilSans-Bold.ttf");
        } catch (RuntimeException e) {
            Log.w("OpenBA", "game files: the FrancophilSans assets did not load: " + e);
            sFace = null;
            sFaceBold = null;
        }
    }

    // A square-cornered button face as the game draws one: a dark outline, a one-pixel light bevel on
    // top, then the face's gradient. `pressed` is the flat darker face.
    private Drawable buttonFace(boolean pressed) {
        final int px = Math.max(1, dp(1));
        final GradientDrawable outline = new GradientDrawable();
        outline.setShape(GradientDrawable.RECTANGLE);
        outline.setColor(COLOR_OUTLINE);
        final GradientDrawable bevel = new GradientDrawable();
        bevel.setShape(GradientDrawable.RECTANGLE);
        bevel.setColor(pressed ? COLOR_PRESSED : COLOR_BUTTON_BEVEL);
        final GradientDrawable face = new GradientDrawable(
            GradientDrawable.Orientation.TOP_BOTTOM,
            pressed ? new int[] {COLOR_PRESSED, COLOR_PRESSED} : new int[] {COLOR_BUTTON_TOP, COLOR_BUTTON_BOTTOM});
        face.setShape(GradientDrawable.RECTANGLE);
        final LayerDrawable layers = new LayerDrawable(new Drawable[] {outline, bevel, face});
        layers.setLayerInset(1, px, px, px, px);
        layers.setLayerInset(2, px, 2 * px, px, px);
        return layers;
    }

    // The bar: the game's dark track (its scroll bars') and the button bevel's grey as the fill.
    private Drawable barDrawable() {
        final GradientDrawable track = new GradientDrawable();
        track.setColor(COLOR_OUTLINE);
        final GradientDrawable fill = new GradientDrawable(
            GradientDrawable.Orientation.TOP_BOTTOM, new int[] {COLOR_BUTTON_BEVEL, COLOR_BUTTON_BOTTOM});
        final ClipDrawable clip = new ClipDrawable(fill, Gravity.START, ClipDrawable.HORIZONTAL);
        final LayerDrawable layers = new LayerDrawable(new Drawable[] {track, clip});
        layers.setId(0, android.R.id.background);
        layers.setId(1, android.R.id.progress);
        return layers;
    }

    private Button button(String label) {
        final Button b = new Button(mActivity);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        b.setTextColor(COLOR_TEXT);
        if (sFace != null) {
            b.setTypeface(sFace);
        }
        final StateListDrawable states = new StateListDrawable();
        states.addState(new int[] {android.R.attr.state_pressed}, buttonFace(true));
        states.addState(new int[] {}, buttonFace(false));
        b.setBackground(states);
        b.setStateListAnimator(null);
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
        if (mBackButton != null) {
            // Not while a copy, an export or an import runs: Cancel first (the import's question needs
            // this screen when it ends).
            mBackButton.setEnabled(!busy);
            mBackButton.setAlpha(busy ? 0.45f : 1f);
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
        mStatus.setTextColor(error ? COLOR_ERROR : COLOR_TEXT);
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
        // The game may have started while the picker was open: refresh()'s lock is checked again.
        if (mMode == Mode.MANAGE && GameFilesJob.isPackageCopy(kind) && gameRunning()) {
            setStatus("The game is running: quit it to replace the game files.", true);
            refresh();
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
            // The game's start waited here for a copy run from "Game files" (OpenBAActivity.onCreate):
            // the copy failed or was cancelled, WinData is as it was and holds data0.pkg, so it starts.
            // The screen goes with the start: a toast keeps the reason in view.
            if (mMode == Mode.FIRST_RUN && GameFilesJob.isPackageCopy(job.kind)
                && GameFiles.hasGameData(mActivity)) {
                Toast.makeText(mActivity, job.message, Toast.LENGTH_LONG).show();
                mHost.onGameFilesInstalled();
            }
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
