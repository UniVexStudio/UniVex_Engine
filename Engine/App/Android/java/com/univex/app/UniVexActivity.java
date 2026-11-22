// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

package com.univex.app;

import android.app.NativeActivity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The NativeActivity every UniVex app runs in, made full screen: the status and navigation bars are
 * hidden (a swipe from the edge shows them for a moment) and the surface reaches under display
 * cutouts. The parts of the surface a cutout covers are kept in the safeInset fields, in surface
 * pixels, for the native code to lay its interface out around.
 *
 * It also opens Android's own file picker for native code (pickFile, takePickedFile): an app may
 * not read the shared Downloads folder directly, so the chosen document is copied into the app's
 * cache and handed over as a path.
 */
public class UniVexActivity extends NativeActivity {
    private static final int PICK_FILE_REQUEST = 0x5576;
    private static final String PICKED_FOLDER = "picked";
    private static final int COPY_BUFFER_BYTES = 1 << 16;
    private static final String ANY_TYPE = "*/*";

    // The last pick: null while one is running or none was asked for, "" when cancelled or failed.
    private volatile String pickedFile;
    private volatile boolean picking;

    // Read from native code (Window module, android_activity.cpp); written on the UI thread.
    public volatile int safeInsetLeft;
    public volatile int safeInsetTop;
    public volatile int safeInsetRight;
    public volatile int safeInsetBottom;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Window window = getWindow();
        WindowManager.LayoutParams attributes = window.getAttributes();
        attributes.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        window.setAttributes(attributes);
        super.onCreate(savedInstanceState);
        window.getDecorView().setOnApplyWindowInsetsListener((view, insets) -> {
            storeSafeInsets(insets);
            return view.onApplyWindowInsets(insets);
        });
        hideSystemBars();
    }

    /** Opens the system file picker; the result arrives through takePickedFile. Called from native code. */
    public void pickFile() {
        picking = true;
        pickedFile = null;
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(ANY_TYPE);
            try {
                startActivityForResult(intent, PICK_FILE_REQUEST);
            } catch (RuntimeException noPicker) {
                finishPick("");
            }
        });
    }

    /**
     * The picked file's path in the app's cache, "" when the pick was cancelled or failed, or null
     * while it is still running (or none was started). A finished result is returned once.
     */
    public String takePickedFile() {
        if (picking) {
            return null;
        }
        String result = pickedFile;
        pickedFile = null;
        return result;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_FILE_REQUEST) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        Uri uri = resultCode == RESULT_OK && data != null ? data.getData() : null;
        if (uri == null) {
            finishPick("");
            return;
        }
        // Copied off the UI thread: an engine package is tens of megabytes.
        new Thread(() -> finishPick(copyToCache(uri))).start();
    }

    private void finishPick(String path) {
        pickedFile = path;
        picking = false;
    }

    private String copyToCache(Uri uri) {
        File folder = new File(getCacheDir(), PICKED_FOLDER);
        if (!folder.isDirectory() && !folder.mkdirs()) {
            return "";
        }
        File target = new File(folder, displayName(uri));
        try (InputStream input = getContentResolver().openInputStream(uri);
             OutputStream output = new FileOutputStream(target)) {
            if (input == null) {
                return "";
            }
            byte[] buffer = new byte[COPY_BUFFER_BYTES];
            for (int read; (read = input.read(buffer)) > 0; ) {
                output.write(buffer, 0, read);
            }
            return target.getAbsolutePath();
        } catch (Exception failed) {
            return "";
        }
    }

    // The document's own file name (its extension tells native code what it is), without folders.
    private String displayName(Uri uri) {
        String name = null;
        try (Cursor cursor = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                name = cursor.getString(0);
            }
        } catch (RuntimeException unreadable) {
            name = null;
        }
        if (name == null || name.isEmpty()) {
            name = uri.getLastPathSegment();
        }
        name = name == null ? PICKED_FOLDER : new File(name).getName();
        return name.isEmpty() ? PICKED_FOLDER : name;
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // Dialogs, the keyboard and the recents screen bring the bars back; hide them again.
        if (hasFocus) {
            hideSystemBars();
        }
    }

    // The pre-R flags, and setDecorFitsSystemWindows (Android 15 lays every window out edge to edge
    // and deprecates it), are what Android 9 to 14 still need.
    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    // Only cutouts count: bars a swipe shows float over the surface for a moment, and laying the
    // interface out around them would make it jump.
    private void storeSafeInsets(WindowInsets insets) {
        DisplayCutout cutout = insets.getDisplayCutout();
        safeInsetLeft = cutout != null ? cutout.getSafeInsetLeft() : 0;
        safeInsetTop = cutout != null ? cutout.getSafeInsetTop() : 0;
        safeInsetRight = cutout != null ? cutout.getSafeInsetRight() : 0;
        safeInsetBottom = cutout != null ? cutout.getSafeInsetBottom() : 0;
    }
}
