// android/app/src/main/java/org/openba/game/GameFilesActivity.java (docs/android/README.md, issue #96)
//
// "Game files": the game-files screen (GameFilesScreen.java, MANAGE) in an activity of its
// own, reached from the launcher icon's long-press menu (the dynamic shortcut OpenBAActivity
// publishes). A plain Activity, no SDL: it runs beside the game or without it. The saves' export and
// import work at any time; the packages can be replaced only while the game is not running.
package org.openba.game;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

public class GameFilesActivity extends Activity {
    private GameFilesScreen mScreen;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mScreen = new GameFilesScreen(this, GameFilesScreen.Mode.MANAGE, null);
        setContentView(mScreen.view());
    }

    @Override
    protected void onResume() {
        super.onResume();
        mScreen.resume();
    }

    @Override
    protected void onDestroy() {
        mScreen.destroy();
        super.onDestroy();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (!mScreen.onBackPressed()) {
            super.onBackPressed();
        }
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        mScreen.onActivityResult(requestCode, resultCode, data);
    }
}
