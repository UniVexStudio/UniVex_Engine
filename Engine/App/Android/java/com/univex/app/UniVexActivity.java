// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

package com.univex.app;

import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Insets;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.DisplayCutout;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * The NativeActivity every UniVex app runs in, made full screen: the status and navigation bars are
 * hidden (a swipe from the edge shows them for a moment) and the surface reaches under display
 * cutouts. The parts of the surface a cutout or the soft keyboard covers are kept in the safeInset
 * fields, in surface pixels, for the native code to lay its interface out around.
 *
 * It also attaches a hidden IME InputView so modern Android soft keyboards (Gboard, Samsung
 * Keyboard, SwiftKey) stream committed text and backspace/enter actions cleanly into native code,
 * opens Android's own file picker for native code (pickFile, takePickedFile), and bridges the
 * system clipboard (getClipboardText, setClipboardText).
 */
public class UniVexActivity extends NativeActivity {
    private static final int PICK_FILE_REQUEST = 0x5576;
    private static final String PICKED_FOLDER = "picked";
    private static final int COPY_BUFFER_BYTES = 1 << 16;
    private static final String ANY_TYPE = "*/*";
    private static final String PICK_ERROR_PREFIX = "error:";
    private static final long CLIPBOARD_READ_TIMEOUT_MS = 250L;
    private static final char IME_EVENT_SEPARATOR = '\u001E';

    // The last pick: null while one is running or none was asked for, "" when cancelled,
    // "error:<reason>" on failure, else the copied file path in cache.
    private volatile String pickedFile;
    private volatile boolean picking;
    private volatile String launchProject;

    // Hidden 1x1 focusable view hosting the soft keyboard InputConnection.
    private ImeBridgeView imeView;
    private volatile boolean imeActive;
    private final ConcurrentLinkedQueue<String> pendingImeEvents = new ConcurrentLinkedQueue<>();

    // Read from native code (Window module, android_activity.cpp); written on the UI thread.
    public volatile int safeInsetLeft;
    public volatile int safeInsetTop;
    public volatile int safeInsetRight;
    public volatile int safeInsetBottom;

    private final class ImeBridgeView extends View {
        ImeBridgeView(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
            setClickable(false);
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return imeActive;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
            outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
            outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE
                    | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                    | EditorInfo.IME_FLAG_NO_FULLSCREEN;
            return new BaseInputConnection(this, false) {
                @Override
                public boolean commitText(CharSequence text, int newCursorPosition) {
                    if (text != null && text.length() > 0) {
                        pendingImeEvents.add("T:" + text.toString());
                    }
                    return true;
                }

                @Override
                public boolean setComposingText(CharSequence text, int newCursorPosition) {
                    // Forward incremental composition directly when a keyboard composes without
                    // calling commitText until space/enter (no-suggestions mode usually commits directly).
                    return super.setComposingText(text, newCursorPosition);
                }

                @Override
                public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                    int count = Math.min(Math.max(beforeLength, 0), 32);
                    for (int i = 0; i < count; ++i) {
                        pendingImeEvents.add("K:" + KeyEvent.KEYCODE_DEL);
                    }
                    int forward = Math.min(Math.max(afterLength, 0), 32);
                    for (int i = 0; i < forward; ++i) {
                        pendingImeEvents.add("K:" + KeyEvent.KEYCODE_FORWARD_DEL);
                    }
                    return true;
                }

                @Override
                public boolean sendKeyEvent(KeyEvent event) {
                    if (event != null && event.getAction() == KeyEvent.ACTION_DOWN) {
                        int keyCode = event.getKeyCode();
                        if (keyCode == KeyEvent.KEYCODE_DEL
                                || keyCode == KeyEvent.KEYCODE_FORWARD_DEL
                                || keyCode == KeyEvent.KEYCODE_ENTER
                                || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER
                                || keyCode == KeyEvent.KEYCODE_TAB
                                || keyCode == KeyEvent.KEYCODE_ESCAPE
                                || keyCode == KeyEvent.KEYCODE_DPAD_LEFT
                                || keyCode == KeyEvent.KEYCODE_DPAD_RIGHT
                                || keyCode == KeyEvent.KEYCODE_DPAD_UP
                                || keyCode == KeyEvent.KEYCODE_DPAD_DOWN) {
                            pendingImeEvents.add("K:" + keyCode);
                            return true;
                        }
                        int ch = event.getUnicodeChar();
                        if (ch >= 0x20 && ch != 0x7F) {
                            pendingImeEvents.add("T:" + new String(Character.toChars(ch)));
                            return true;
                        }
                    }
                    return true;
                }

                @Override
                public boolean performEditorAction(int editorAction) {
                    pendingImeEvents.add("K:" + KeyEvent.KEYCODE_ENTER);
                    setKeyboardVisible(false);
                    return true;
                }
            };
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Window window = getWindow();
        WindowManager.LayoutParams attributes = window.getAttributes();
        attributes.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        window.setAttributes(attributes);
        Intent launchIntent = getIntent();
        if (launchIntent != null) {
            String project = launchIntent.getStringExtra("project");
            if (project != null && !project.isEmpty()) {
                launchProject = project;
            }
        }
        super.onCreate(savedInstanceState);
        imeView = new ImeBridgeView(this);
        addContentView(imeView, new ViewGroup.LayoutParams(1, 1));
        View decor = window.getDecorView();
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            storeSafeInsets(insets);
            return view.onApplyWindowInsets(insets);
        });
        hideSystemBars();
        decor.requestApplyInsets();
    }

    /** Shows or hides the soft keyboard bound to the hidden IME bridge view. Called from native code. */
    public void setKeyboardVisible(final boolean visible) {
        imeActive = visible;
        runOnUiThread(() -> {
            if (imeView == null) {
                return;
            }
            InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm == null) {
                return;
            }
            if (visible) {
                imeView.requestFocus();
                imm.restartInput(imeView);
                imm.showSoftInput(imeView, InputMethodManager.SHOW_IMPLICIT);
            } else {
                imm.hideSoftInputFromWindow(imeView.getWindowToken(), 0);
                imeView.clearFocus();
                hideSystemBars();
            }
        });
    }

    /** Drains queued IME events ("T:<utf8>" or "K:<keycode>") separated by U+001E, or null when empty. */
    public String drainImeEvents() {
        String first = pendingImeEvents.poll();
        if (first == null) {
            return null;
        }
        StringBuilder builder = new StringBuilder(first);
        for (String next = pendingImeEvents.poll(); next != null; next = pendingImeEvents.poll()) {
            builder.append(IME_EVENT_SEPARATOR).append(next);
        }
        return builder.toString();
    }

    /** Returns a .uvproject path passed via Intent extra "project" once, then clears it. */
    public String takeLaunchProject() {
        String result = launchProject;
        launchProject = null;
        return result != null ? result : "";
    }

    /** Opens the system file picker; the result arrives through takePickedFile. Called from native code. */
    public void pickFile() {
        if (picking) {
            return;
        }
        picking = true;
        pickedFile = null;
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(ANY_TYPE);
            try {
                startActivityForResult(intent, PICK_FILE_REQUEST);
            } catch (RuntimeException noPicker) {
                finishPick(PICK_ERROR_PREFIX + "No Android file picker is available on this device.");
            }
        });
    }

    /**
     * The picked file's path in the app's cache, "" when cancelled, "error:<reason>" when failed,
     * or null while it is still running (or none was started). A finished result is returned once.
     */
    public String takePickedFile() {
        if (picking) {
            return null;
        }
        String result = pickedFile;
        pickedFile = null;
        return result;
    }

    /** Reads plain text from Android's system clipboard; safe to call from the native thread. */
    public String getClipboardText() {
        final String[] holder = new String[] {""};
        if (Looper.myLooper() == Looper.getMainLooper()) {
            holder[0] = readClipboardOnUiThread();
            return holder[0];
        }
        CountDownLatch latch = new CountDownLatch(1);
        runOnUiThread(() -> {
            try {
                holder[0] = readClipboardOnUiThread();
            } finally {
                latch.countDown();
            }
        });
        try {
            latch.await(CLIPBOARD_READ_TIMEOUT_MS, TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        return holder[0];
    }

    /** Copies plain text to Android's system clipboard; safe to call from the native thread. */
    public void setClipboardText(final String text) {
        final String value = text != null ? text : "";
        runOnUiThread(() -> {
            try {
                ClipboardManager manager = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                if (manager != null) {
                    manager.setPrimaryClip(ClipData.newPlainText("UniVex", value));
                }
            } catch (RuntimeException ignored) {
                // Background clipboard restrictions on Android 10+ are non-fatal.
            }
        });
    }

    private String readClipboardOnUiThread() {
        try {
            ClipboardManager manager = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (manager == null || !manager.hasPrimaryClip()) {
                return "";
            }
            ClipData clip = manager.getPrimaryClip();
            if (clip == null || clip.getItemCount() <= 0) {
                return "";
            }
            CharSequence coerced = clip.getItemAt(0).coerceToText(this);
            return coerced != null ? coerced.toString() : "";
        } catch (RuntimeException ignored) {
            return "";
        }
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
            return PICK_ERROR_PREFIX + "Unable to create the app cache folder for importing.";
        }
        File[] staleFiles = folder.listFiles();
        if (staleFiles != null) {
            for (File stale : staleFiles) {
                if (stale.isFile()) {
                    stale.delete();
                }
            }
        }
        File target = new File(folder, displayName(uri));
        try (InputStream input = getContentResolver().openInputStream(uri);
             OutputStream output = new FileOutputStream(target)) {
            if (input == null) {
                target.delete();
                return PICK_ERROR_PREFIX + "Unable to open the selected file.";
            }
            byte[] buffer = new byte[COPY_BUFFER_BYTES];
            for (int read; (read = input.read(buffer)) > 0; ) {
                output.write(buffer, 0, read);
            }
            return target.getAbsolutePath();
        } catch (Exception failed) {
            target.delete();
            String reason = failed.getMessage();
            return PICK_ERROR_PREFIX + "Unable to copy the selected file into app storage"
                    + (reason != null && !reason.isEmpty() ? ": " + reason : ".");
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

    // Display cutouts plus the on-screen keyboard (API 30+ edge-to-edge) keep interactive UI visible;
    // transient swipe-revealed system bars are excluded so the layout does not jump.
    private void storeSafeInsets(WindowInsets insets) {
        DisplayCutout cutout = insets.getDisplayCutout();
        int left = cutout != null ? cutout.getSafeInsetLeft() : 0;
        int top = cutout != null ? cutout.getSafeInsetTop() : 0;
        int right = cutout != null ? cutout.getSafeInsetRight() : 0;
        int bottom = cutout != null ? cutout.getSafeInsetBottom() : 0;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Insets ime = insets.getInsets(WindowInsets.Type.ime());
            if (ime.bottom > bottom) {
                bottom = ime.bottom;
            }
        }
        safeInsetLeft = left;
        safeInsetTop = top;
        safeInsetRight = right;
        safeInsetBottom = bottom;
    }
}
