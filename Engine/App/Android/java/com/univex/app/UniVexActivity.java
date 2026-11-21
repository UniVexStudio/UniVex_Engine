// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

package com.univex.app;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The NativeActivity every UniVex app runs in, made full screen: the status and navigation bars are
 * hidden (a swipe from the edge shows them for a moment) and the surface reaches under display
 * cutouts. The parts of the surface a cutout covers are kept in the safeInset fields, in surface
 * pixels, for the native code to lay its interface out around.
 */
public class UniVexActivity extends NativeActivity {
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
