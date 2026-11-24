// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

package com.univex.app;

import android.Manifest;
import android.app.NativeActivity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Insets;
import android.hardware.input.InputManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.text.InputType;
import android.view.DisplayCutout;
import android.view.InputDevice;
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
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

/**
 * The NativeActivity every UniVex app runs in, made full screen: the status and navigation bars are
 * hidden (a swipe from the edge shows them for a moment) and the surface reaches edge-to-edge under
 * display cutouts. Display cutout insets and soft-keyboard insets are tracked separately in surface
 * pixels so the 3D viewport and backgrounds can fill 100% edge-to-edge under camera punch-holes.
 *
 * It also attaches a hidden IME InputView so modern Android soft keyboards (Gboard, Samsung
 * Keyboard, SwiftKey) stream committed text and backspace/enter actions cleanly into native code,
 * tracks connected hardware mouse/keyboard devices to auto-hide touch viewport controllers,
 * exposes public storage paths and All Files Access permission helpers, opens Android's own file
 * picker for native code (pickFile, pickFiles, pickFolder, takePickedFile, takePickedPaths), and
 * bridges the system clipboard.
 */
public class UniVexActivity extends NativeActivity {
    private static final int PICK_FILE_REQUEST = 0x5576;
    private static final int STORAGE_PERMISSION_REQUEST = 0x5577;
    private static final int PICK_FILES_REQUEST = 0x5578;
    private static final int PICK_FOLDER_REQUEST = 0x5579;
    private static final String PICKED_FOLDER = "picked";
    private static final String PICKED_BATCH_PREFIX = "picked_batch_";
    private static final int COPY_BUFFER_BYTES = 1 << 16;
    private static final String ANY_TYPE = "*/*";
    private static final String PICK_ERROR_PREFIX = "error:";
    private static final long CLIPBOARD_READ_TIMEOUT_MS = 250L;
    private static final char IME_EVENT_SEPARATOR = '\u001E';
    private static final char PICK_PATH_SEPARATOR = '\u001E';
    private static final int MAX_FOLDER_IMPORT_DEPTH = 16;
    private static final int MAX_FOLDER_IMPORT_FILES = 4096;

    // The last single pick (Hub .uvpackage): null while running, "" when cancelled,
    // "error:<reason>" on failure, else the copied file path in cache.
    private volatile String pickedFile;
    private volatile boolean picking;
    // The last multi-file or folder pick (Editor Import): null while running, "" when cancelled,
    // "error:<reason>" on failure, else PICK_PATH_SEPARATOR-delimited staged paths in cache.
    private volatile String pickedPaths;
    private volatile boolean pickingMulti;
    private final AtomicLong pickBatchCounter = new AtomicLong(0L);
    private volatile String launchProject;

    // Hidden 1x1 focusable view hosting the soft keyboard InputConnection.
    private ImeBridgeView imeView;
    private volatile boolean imeActive;
    private final ConcurrentLinkedQueue<String> pendingImeEvents = new ConcurrentLinkedQueue<>();
    private InputManager.InputDeviceListener inputDeviceListener;

    // Read from native code (Window module, android_activity.cpp); written on the UI thread.
    // Display cutout insets only (kept separate from IME so backgrounds fill under camera bumps).
    public volatile int safeInsetLeft;
    public volatile int safeInsetTop;
    public volatile int safeInsetRight;
    public volatile int safeInsetBottom;
    // Soft keyboard bottom inset in surface pixels (0 while hidden).
    public volatile int imeInsetBottom;
    // True while an external hardware mouse, touchpad, or non-virtual alphabetic keyboard is attached.
    public volatile boolean hardwareInputConnected;

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
    @SuppressWarnings("deprecation")
    protected void onCreate(Bundle savedInstanceState) {
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        WindowManager.LayoutParams attributes = window.getAttributes();
        attributes.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        window.setAttributes(attributes);
        requestHighRefreshRate(window);
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
        registerHardwareInputListener();
        refreshHardwareInputState();
        hideSystemBars();
        decor.requestApplyInsets();
    }

    @Override
    protected void onResume() {
        super.onResume();
        requestHighRefreshRate(getWindow());
        refreshHardwareInputState();
        hideSystemBars();
    }

    @SuppressWarnings("deprecation")
    private void requestHighRefreshRate(Window window) {
        if (window == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }
        try {
            android.view.Display display = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                    ? getDisplay()
                    : getWindowManager().getDefaultDisplay();
            if (display == null) {
                return;
            }
            android.view.Display.Mode current = display.getMode();
            android.view.Display.Mode best = current;
            android.view.Display.Mode[] modes = display.getSupportedModes();
            if (modes != null && current != null) {
                for (android.view.Display.Mode mode : modes) {
                    if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                            && mode.getPhysicalHeight() == current.getPhysicalHeight()
                            && mode.getRefreshRate() > best.getRefreshRate() + 0.5f) {
                        best = mode;
                    }
                }
            }
            if (best != null && (current == null || best.getModeId() != current.getModeId())) {
                WindowManager.LayoutParams lp = window.getAttributes();
                lp.preferredDisplayModeId = best.getModeId();
                window.setAttributes(lp);
            }
        } catch (RuntimeException ignored) {
            // Headless or virtual displays may not expose mode switching.
        }
    }

    @Override
    protected void onDestroy() {
        unregisterHardwareInputListener();
        super.onDestroy();
    }

    private void registerHardwareInputListener() {
        InputManager manager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (manager == null) {
            return;
        }
        inputDeviceListener = new InputManager.InputDeviceListener() {
            @Override
            public void onInputDeviceAdded(int deviceId) {
                refreshHardwareInputState();
            }

            @Override
            public void onInputDeviceRemoved(int deviceId) {
                refreshHardwareInputState();
            }

            @Override
            public void onInputDeviceChanged(int deviceId) {
                refreshHardwareInputState();
            }
        };
        manager.registerInputDeviceListener(inputDeviceListener, null);
    }

    private void unregisterHardwareInputListener() {
        if (inputDeviceListener == null) {
            return;
        }
        InputManager manager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (manager != null) {
            manager.unregisterInputDeviceListener(inputDeviceListener);
        }
        inputDeviceListener = null;
    }

    private void refreshHardwareInputState() {
        boolean connected = false;
        try {
            int[] ids = InputDevice.getDeviceIds();
            if (ids != null) {
                for (int id : ids) {
                    InputDevice device = InputDevice.getDevice(id);
                    if (device == null || device.isVirtual()) {
                        continue;
                    }
                    int sources = device.getSources();
                    boolean isMouse = (sources & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
                            || (sources & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE;
                    boolean isKeyboard = (sources & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD
                            && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
                    if (isMouse || isKeyboard) {
                        connected = true;
                        break;
                    }
                }
            }
        } catch (RuntimeException ignored) {
            connected = false;
        }
        hardwareInputConnected = connected;
    }

    /** True when the app can directly read and write shared public storage (/storage/emulated/0). */
    public boolean hasAllFilesAccess() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                return Environment.isExternalStorageManager();
            }
            return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        } catch (RuntimeException ignored) {
            return false;
        }
    }

    /** Prompts the user to grant All Files Access (API 30+) or legacy external storage permission. */
    public void requestAllFilesAccess() {
        runOnUiThread(() -> {
            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.parse("package:" + getPackageName()));
                    try {
                        startActivity(intent);
                    } catch (RuntimeException fallback) {
                        startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                    }
                } else {
                    requestPermissions(new String[] {
                            Manifest.permission.READ_EXTERNAL_STORAGE,
                            Manifest.permission.WRITE_EXTERNAL_STORAGE
                    }, STORAGE_PERMISSION_REQUEST);
                }
            } catch (RuntimeException ignored) {
                // Settings activity unavailable on restricted/headless test images.
            }
        });
    }

    @SuppressWarnings("deprecation")
    public String getPublicStorageRoot() {
        try {
            File dir = Environment.getExternalStorageDirectory();
            return dir != null ? dir.getAbsolutePath() : "";
        } catch (RuntimeException ignored) {
            return "";
        }
    }

    @SuppressWarnings("deprecation")
    public String getPublicDocumentsDir() {
        try {
            File dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
            return dir != null ? dir.getAbsolutePath() : "";
        } catch (RuntimeException ignored) {
            return "";
        }
    }

    @SuppressWarnings("deprecation")
    public String getPublicDownloadsDir() {
        try {
            File dir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
            return dir != null ? dir.getAbsolutePath() : "";
        } catch (RuntimeException ignored) {
            return "";
        }
    }

    public String getAppExternalDocumentsDir() {
        try {
            File dir = getExternalFilesDir(Environment.DIRECTORY_DOCUMENTS);
            return dir != null ? dir.getAbsolutePath() : "";
        } catch (RuntimeException ignored) {
            return "";
        }
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

    /** Opens Android's native file manager with multi-select enabled; results arrive via takePickedPaths. */
    public void pickFiles() {
        if (pickingMulti) {
            return;
        }
        pickingMulti = true;
        pickedPaths = null;
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(ANY_TYPE);
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            try {
                startActivityForResult(intent, PICK_FILES_REQUEST);
            } catch (RuntimeException noPicker) {
                finishMultiPick(PICK_ERROR_PREFIX + "No Android file picker is available on this device.");
            }
        });
    }

    /** Opens Android's native folder picker (SAF tree); staged folder arrives via takePickedPaths. */
    public void pickFolder() {
        if (pickingMulti) {
            return;
        }
        pickingMulti = true;
        pickedPaths = null;
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try {
                startActivityForResult(intent, PICK_FOLDER_REQUEST);
            } catch (RuntimeException noPicker) {
                finishMultiPick(PICK_ERROR_PREFIX + "No Android folder picker is available on this device.");
            }
        });
    }

    /**
     * Returns PICK_PATH_SEPARATOR-delimited staged paths from pickFiles/pickFolder, "" when cancelled,
     * "error:<reason>" on failure, or null while running. A finished result is returned once.
     */
    public String takePickedPaths() {
        if (pickingMulti) {
            return null;
        }
        String result = pickedPaths;
        pickedPaths = null;
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
        if (requestCode == PICK_FILE_REQUEST) {
            Uri uri = resultCode == RESULT_OK && data != null ? data.getData() : null;
            if (uri == null) {
                finishPick("");
                return;
            }
            // Copied off the UI thread: an engine package is tens of megabytes.
            new Thread(() -> finishPick(copyToCache(uri))).start();
            return;
        }
        if (requestCode == PICK_FILES_REQUEST) {
            if (resultCode != RESULT_OK || data == null) {
                finishMultiPick("");
                return;
            }
            List<Uri> uris = new ArrayList<>();
            ClipData clip = data.getClipData();
            if (clip != null && clip.getItemCount() > 0) {
                for (int i = 0; i < clip.getItemCount(); ++i) {
                    Uri u = clip.getItemAt(i).getUri();
                    if (u != null) {
                        uris.add(u);
                    }
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
            if (uris.isEmpty()) {
                finishMultiPick("");
                return;
            }
            new Thread(() -> finishMultiPick(copyFilesToBatchCache(uris))).start();
            return;
        }
        if (requestCode == PICK_FOLDER_REQUEST) {
            Uri treeUri = resultCode == RESULT_OK && data != null ? data.getData() : null;
            if (treeUri == null) {
                finishMultiPick("");
                return;
            }
            new Thread(() -> finishMultiPick(copyTreeToBatchCache(treeUri))).start();
            return;
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    private void finishPick(String path) {
        pickedFile = path;
        picking = false;
    }

    private void finishMultiPick(String paths) {
        pickedPaths = paths;
        pickingMulti = false;
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
        if (!streamUriToFile(uri, target)) {
            target.delete();
            return PICK_ERROR_PREFIX + "Unable to copy the selected file into app storage.";
        }
        return target.getAbsolutePath();
    }

    private String copyFilesToBatchCache(List<Uri> uris) {
        File batchDir = new File(getCacheDir(),
                PICKED_BATCH_PREFIX + System.currentTimeMillis() + "_" + pickBatchCounter.incrementAndGet());
        if (!batchDir.isDirectory() && !batchDir.mkdirs()) {
            return PICK_ERROR_PREFIX + "Unable to create the app cache folder for importing.";
        }
        StringBuilder joined = new StringBuilder();
        int copied = 0;
        for (Uri uri : uris) {
            if (copied >= MAX_FOLDER_IMPORT_FILES) {
                break;
            }
            String safeName = sanitizeSegment(displayName(uri), "asset_" + copied);
            File target = uniqueChildFile(batchDir, safeName);
            if (streamUriToFile(uri, target)) {
                if (joined.length() > 0) {
                    joined.append(PICK_PATH_SEPARATOR);
                }
                joined.append(target.getAbsolutePath());
                ++copied;
            } else {
                target.delete();
            }
        }
        if (copied == 0) {
            deleteRecursive(batchDir);
            return PICK_ERROR_PREFIX + "Unable to copy any of the selected files into app storage.";
        }
        return joined.toString();
    }

    private String copyTreeToBatchCache(Uri treeUri) {
        String rootDocId;
        try {
            rootDocId = DocumentsContract.getTreeDocumentId(treeUri);
        } catch (IllegalArgumentException invalid) {
            return PICK_ERROR_PREFIX + "The selected Android folder could not be opened.";
        }
        File batchDir = new File(getCacheDir(),
                PICKED_BATCH_PREFIX + System.currentTimeMillis() + "_" + pickBatchCounter.incrementAndGet());
        String folderName = queryTreeFolderName(treeUri, rootDocId);
        File stagedRoot = new File(batchDir, sanitizeSegment(folderName, "ImportedFolder"));
        if (!stagedRoot.isDirectory() && !stagedRoot.mkdirs()) {
            return PICK_ERROR_PREFIX + "Unable to create the app cache folder for folder import.";
        }
        int[] copiedCount = new int[] {0};
        copyDocumentTreeRecursive(treeUri, rootDocId, stagedRoot, 0, copiedCount);
        if (copiedCount[0] == 0) {
            deleteRecursive(batchDir);
            return PICK_ERROR_PREFIX + "The selected folder contained no readable files.";
        }
        return stagedRoot.getAbsolutePath();
    }

    private String queryTreeFolderName(Uri treeUri, String docId) {
        Uri docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
        try (Cursor cursor = getContentResolver().query(docUri,
                new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                if (name != null && !name.isEmpty()) {
                    return name;
                }
            }
        } catch (RuntimeException ignored) {
            // Fall through to docId tail.
        }
        int slash = docId.lastIndexOf('/');
        int colon = docId.lastIndexOf(':');
        int cut = Math.max(slash, colon);
        if (cut >= 0 && cut + 1 < docId.length()) {
            return docId.substring(cut + 1);
        }
        return "ImportedFolder";
    }

    private void copyDocumentTreeRecursive(Uri treeUri, String parentDocId, File destDir, int depth, int[] copiedCount) {
        if (depth > MAX_FOLDER_IMPORT_DEPTH || copiedCount[0] >= MAX_FOLDER_IMPORT_FILES) {
            return;
        }
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId);
        String[] projection = new String[] {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE
        };
        try (Cursor cursor = getContentResolver().query(childrenUri, projection, null, null, null)) {
            if (cursor == null) {
                return;
            }
            while (cursor.moveToNext()) {
                if (copiedCount[0] >= MAX_FOLDER_IMPORT_FILES) {
                    break;
                }
                String childDocId = cursor.getString(0);
                String rawName = cursor.getString(1);
                String mime = cursor.getString(2);
                if (childDocId == null || rawName == null || rawName.isEmpty() || rawName.startsWith(".")) {
                    continue;
                }
                String safeName = sanitizeSegment(rawName, null);
                if (safeName == null) {
                    continue;
                }
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    File subDir = new File(destDir, safeName);
                    if (subDir.isDirectory() || subDir.mkdirs()) {
                        copyDocumentTreeRecursive(treeUri, childDocId, subDir, depth + 1, copiedCount);
                    }
                } else {
                    Uri childUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, childDocId);
                    File target = new File(destDir, safeName);
                    if (streamUriToFile(childUri, target)) {
                        copiedCount[0]++;
                    } else {
                        target.delete();
                    }
                }
            }
        } catch (RuntimeException ignored) {
            // Skip unreadable subtrees cleanly rather than failing the whole import.
        }
    }

    private boolean streamUriToFile(Uri uri, File target) {
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            if (input == null) {
                return false;
            }
            try (OutputStream output = new FileOutputStream(target)) {
                byte[] buffer = new byte[COPY_BUFFER_BYTES];
                for (int read; (read = input.read(buffer)) > 0; ) {
                    output.write(buffer, 0, read);
                }
            }
            return true;
        } catch (Exception failed) {
            return false;
        }
    }

    private static String sanitizeSegment(String raw, String fallback) {
        if (raw == null) {
            return fallback;
        }
        String leaf = new File(raw).getName().trim();
        if (leaf.isEmpty() || ".".equals(leaf) || "..".equals(leaf) || leaf.startsWith(".")) {
            return fallback;
        }
        String cleaned = leaf.replace('/', '_').replace('\\', '_');
        return cleaned.isEmpty() ? fallback : cleaned;
    }

    private static File uniqueChildFile(File dir, String filename) {
        File candidate = new File(dir, filename);
        if (!candidate.exists()) {
            return candidate;
        }
        int dot = filename.lastIndexOf('.');
        String stem = dot > 0 ? filename.substring(0, dot) : filename;
        String ext = dot > 0 ? filename.substring(dot) : "";
        for (int index = 2; index < 10000; ++index) {
            File alt = new File(dir, stem + "_" + index + ext);
            if (!alt.exists()) {
                return alt;
            }
        }
        return candidate;
    }

    private static void deleteRecursive(File file) {
        if (file == null || !file.exists()) {
            return;
        }
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        file.delete();
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

    // Display cutouts and the soft keyboard (IME) are tracked separately so the 3D viewport and
    // panel backgrounds always fill 100% edge-to-edge across camera punch-holes, while only
    // interactive top-bar icons dodge cutouts and the root shifts up only when the IME opens.
    @SuppressWarnings("deprecation")
    private void storeSafeInsets(WindowInsets insets) {
        DisplayCutout cutout = insets.getDisplayCutout();
        int left = cutout != null ? cutout.getSafeInsetLeft() : 0;
        int top = cutout != null ? cutout.getSafeInsetTop() : 0;
        int right = cutout != null ? cutout.getSafeInsetRight() : 0;
        int bottom = cutout != null ? cutout.getSafeInsetBottom() : 0;
        int imeBottom = 0;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Insets ime = insets.getInsets(WindowInsets.Type.ime());
            imeBottom = Math.max(0, ime.bottom);
        } else if (imeActive) {
            imeBottom = Math.max(0, insets.getSystemWindowInsetBottom() - bottom);
        }
        safeInsetLeft = left;
        safeInsetTop = top;
        safeInsetRight = right;
        safeInsetBottom = bottom;
        imeInsetBottom = imeBottom;
    }
}
