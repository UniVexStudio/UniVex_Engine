// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "android_activity.h"

#include <android/native_activity.h>
#include <jni.h>

namespace UVE::Window {

namespace {

/// The fields UniVexActivity keeps the safe area in.
inline constexpr const char* kSafeInsetLeftField = "safeInsetLeft";
inline constexpr const char* kSafeInsetTopField = "safeInsetTop";
inline constexpr const char* kSafeInsetRightField = "safeInsetRight";
inline constexpr const char* kSafeInsetBottomField = "safeInsetBottom";
inline constexpr const char* kIntFieldSignature = "I";

/// Attaches the calling thread to Java for its lifetime when it was not attached already.
class ScopedJniEnv final {
public:
    explicit ScopedJniEnv(JavaVM* const vm) : m_vm(vm) {
        if (m_vm == nullptr) {
            return;
        }
        if (m_vm->GetEnv(reinterpret_cast<void**>(&m_env), JNI_VERSION_1_6) == JNI_EDETACHED) {
            m_attached = m_vm->AttachCurrentThread(&m_env, nullptr) == JNI_OK;
            if (!m_attached) {
                m_env = nullptr;
            }
        }
    }
    ~ScopedJniEnv() {
        if (m_attached) {
            m_vm->DetachCurrentThread();
        }
    }
    ScopedJniEnv(const ScopedJniEnv&) = delete;
    ScopedJniEnv& operator=(const ScopedJniEnv&) = delete;

    [[nodiscard]] JNIEnv* Env() const noexcept { return m_env; }

private:
    JavaVM* m_vm = nullptr;
    JNIEnv* m_env = nullptr;
    bool m_attached = false;
};

/// Reads one int field of `object`, or 0 when its class has no such field.
[[nodiscard]] int ReadIntField(JNIEnv* const env, jobject const object, jclass const type, const char* const name) {
    const jfieldID field = env->GetFieldID(type, name, kIntFieldSignature);
    if (field == nullptr) {
        env->ExceptionClear();  // NoSuchFieldError: a plain NativeActivity
        return 0;
    }
    return env->GetIntField(object, field);
}

inline constexpr const char* kPickFileMethod = "pickFile";
inline constexpr const char* kPickFileSignature = "()V";
inline constexpr const char* kTakePickedFileMethod = "takePickedFile";
inline constexpr const char* kTakePickedFileSignature = "()Ljava/lang/String;";
inline constexpr const char* kPickFilesMethod = "pickFiles";
inline constexpr const char* kPickFolderMethod = "pickFolder";
inline constexpr const char* kTakePickedPathsMethod = "takePickedPaths";
inline constexpr const char* kGetClipboardMethod = "getClipboardText";
inline constexpr const char* kGetClipboardSignature = "()Ljava/lang/String;";
inline constexpr const char* kSetClipboardMethod = "setClipboardText";
inline constexpr const char* kSetClipboardSignature = "(Ljava/lang/String;)V";
inline constexpr const char* kKeyEventClass = "android/view/KeyEvent";
inline constexpr const char* kKeyEventConstructor = "(II)V";
inline constexpr const char* kUnicodeCharMethod = "getUnicodeChar";
inline constexpr const char* kUnicodeCharSignature = "(I)I";

[[nodiscard]] bool CallVoidActivityMethod(ANativeActivity* const activity, const char* const methodName) {
    if (activity == nullptr) {
        return false;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return false;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID method = type != nullptr ? env->GetMethodID(type, methodName, kPickFileSignature) : nullptr;
    bool started = false;
    if (method != nullptr) {
        env->CallVoidMethod(activity->clazz, method);
        started = !env->ExceptionCheck();
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
    return started;
}

[[nodiscard]] std::optional<std::string> TakeOptionalStringActivityMethod(ANativeActivity* const activity,
                                                                             const char* const methodName) {
    if (activity == nullptr) {
        return std::string{};
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return std::string{};
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID take =
        type != nullptr ? env->GetMethodID(type, methodName, kTakePickedFileSignature) : nullptr;
    std::optional<std::string> result = std::string{}; // no picker: nothing will ever arrive
    if (take != nullptr) {
        const auto picked = static_cast<jstring>(env->CallObjectMethod(activity->clazz, take));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (picked == nullptr) {
            result = std::nullopt;
        } else {
            const char* const chars = env->GetStringUTFChars(picked, nullptr);
            result = chars != nullptr ? std::string(chars) : std::string{};
            if (chars != nullptr) {
                env->ReleaseStringUTFChars(picked, chars);
            }
            env->DeleteLocalRef(picked);
        }
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
    return result;
}

} // namespace

unsigned int AndroidKeyCharacter(ANativeActivity* const activity, const int action, const int keyCode,
                                    const int metaState) {
    if (activity == nullptr) {
        return 0U;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return 0U;
    }
    const jclass type = env->FindClass(kKeyEventClass);
    if (type == nullptr) {
        env->ExceptionClear();
        return 0U;
    }
    const jmethodID constructor = env->GetMethodID(type, "<init>", kKeyEventConstructor);
    const jmethodID unicode = env->GetMethodID(type, kUnicodeCharMethod, kUnicodeCharSignature);
    if (constructor == nullptr || unicode == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(type);
        return 0U;
    }
    const jobject event = env->NewObject(type, constructor, static_cast<jint>(action), static_cast<jint>(keyCode));
    jint character = 0;
    if (event != nullptr) {
        character = env->CallIntMethod(event, unicode, static_cast<jint>(metaState));
        env->DeleteLocalRef(event);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        character = 0;
    }
    env->DeleteLocalRef(type);
    return character > 0 ? static_cast<unsigned int>(character) : 0U;
}

bool StartAndroidFilePick(ANativeActivity* const activity) {
    return CallVoidActivityMethod(activity, kPickFileMethod);
}

std::optional<std::string> TakeAndroidPickedFile(ANativeActivity* const activity) {
    return TakeOptionalStringActivityMethod(activity, kTakePickedFileMethod);
}

bool StartAndroidMultiFilePick(ANativeActivity* const activity) {
    return CallVoidActivityMethod(activity, kPickFilesMethod);
}

bool StartAndroidFolderPick(ANativeActivity* const activity) {
    return CallVoidActivityMethod(activity, kPickFolderMethod);
}

std::optional<std::string> TakeAndroidPickedPaths(ANativeActivity* const activity) {
    return TakeOptionalStringActivityMethod(activity, kTakePickedPathsMethod);
}

std::optional<std::string> ReadAndroidClipboard(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return std::nullopt;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return std::nullopt;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID getClipboard =
        type != nullptr ? env->GetMethodID(type, kGetClipboardMethod, kGetClipboardSignature) : nullptr;
    std::optional<std::string> result;
    if (getClipboard != nullptr) {
        const auto value = static_cast<jstring>(env->CallObjectMethod(activity->clazz, getClipboard));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (value != nullptr) {
            const char* const chars = env->GetStringUTFChars(value, nullptr);
            result = chars != nullptr ? std::string(chars) : std::string{};
            if (chars != nullptr) {
                env->ReleaseStringUTFChars(value, chars);
            }
            env->DeleteLocalRef(value);
        }
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
    return result;
}

void WriteAndroidClipboard(ANativeActivity* const activity, const char* const text) {
    if (activity == nullptr) {
        return;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID setClipboard =
        type != nullptr ? env->GetMethodID(type, kSetClipboardMethod, kSetClipboardSignature) : nullptr;
    if (setClipboard != nullptr) {
        const jstring value = env->NewStringUTF(text != nullptr ? text : "");
        if (value != nullptr) {
            env->CallVoidMethod(activity->clazz, setClipboard, value);
            env->DeleteLocalRef(value);
        }
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
}

std::string TakeAndroidLaunchProject(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return {};
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return {};
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID take =
        type != nullptr ? env->GetMethodID(type, "takeLaunchProject", "()Ljava/lang/String;") : nullptr;
    std::string result;
    if (take != nullptr) {
        const auto value = static_cast<jstring>(env->CallObjectMethod(activity->clazz, take));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (value != nullptr) {
            const char* const chars = env->GetStringUTFChars(value, nullptr);
            if (chars != nullptr) {
                result.assign(chars);
                env->ReleaseStringUTFChars(value, chars);
            }
            env->DeleteLocalRef(value);
        }
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
    return result;
}

namespace {

[[nodiscard]] bool ReadBoolField(JNIEnv* const env, jobject const object, jclass const type, const char* const name) {
    const jfieldID field = env->GetFieldID(type, name, "Z");
    if (field == nullptr) {
        env->ExceptionClear();
        return false;
    }
    return env->GetBooleanField(object, field) != JNI_FALSE;
}

[[nodiscard]] std::string CallStringMethod(JNIEnv* const env, jobject const object, jclass const type,
                                              const char* const name) {
    const jmethodID method = env->GetMethodID(type, name, "()Ljava/lang/String;");
    if (method == nullptr) {
        env->ExceptionClear();
        return {};
    }
    std::string out;
    const auto value = static_cast<jstring>(env->CallObjectMethod(object, method));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (value != nullptr) {
        const char* const chars = env->GetStringUTFChars(value, nullptr);
        if (chars != nullptr) {
            out.assign(chars);
            env->ReleaseStringUTFChars(value, chars);
        }
        env->DeleteLocalRef(value);
    }
    return out;
}

} // namespace

AndroidSafeArea ReadAndroidSafeArea(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return {};
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return {};
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    if (type == nullptr) {
        env->ExceptionClear();
        return {};
    }
    AndroidSafeArea area;
    area.left = ReadIntField(env, activity->clazz, type, kSafeInsetLeftField);
    area.top = ReadIntField(env, activity->clazz, type, kSafeInsetTopField);
    area.right = ReadIntField(env, activity->clazz, type, kSafeInsetRightField);
    area.bottom = ReadIntField(env, activity->clazz, type, kSafeInsetBottomField);
    area.imeBottom = ReadIntField(env, activity->clazz, type, "imeInsetBottom");
    env->DeleteLocalRef(type);
    return area;
}

bool HasAndroidHardwareInput(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return false;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return false;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    if (type == nullptr) {
        env->ExceptionClear();
        return false;
    }
    const bool connected = ReadBoolField(env, activity->clazz, type, "hardwareInputConnected");
    env->DeleteLocalRef(type);
    return connected;
}

bool HasAndroidAllFilesAccess(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return false;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return false;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID method = type != nullptr ? env->GetMethodID(type, "hasAllFilesAccess", "()Z") : nullptr;
    bool granted = false;
    if (method != nullptr) {
        granted = env->CallBooleanMethod(activity->clazz, method) != JNI_FALSE;
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            granted = false;
        }
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
    return granted;
}

void RequestAndroidAllFilesAccess(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID method = type != nullptr ? env->GetMethodID(type, "requestAllFilesAccess", "()V") : nullptr;
    if (method != nullptr) {
        env->CallVoidMethod(activity->clazz, method);
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
}

AndroidStoragePaths ReadAndroidStoragePaths(ANativeActivity* const activity) {
    AndroidStoragePaths paths;
    if (activity == nullptr) {
        return paths;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return paths;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    if (type == nullptr) {
        env->ExceptionClear();
        return paths;
    }
    paths.publicStorageRoot = CallStringMethod(env, activity->clazz, type, "getPublicStorageRoot");
    paths.publicDocuments = CallStringMethod(env, activity->clazz, type, "getPublicDocumentsDir");
    paths.publicDownloads = CallStringMethod(env, activity->clazz, type, "getPublicDownloadsDir");
    paths.appExternalDocuments = CallStringMethod(env, activity->clazz, type, "getAppExternalDocumentsDir");
    env->DeleteLocalRef(type);
    return paths;
}

bool SetAndroidSoftKeyboard(ANativeActivity* const activity, const bool visible) {
    if (activity == nullptr) {
        return false;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return false;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID method =
        type != nullptr ? env->GetMethodID(type, "setKeyboardVisible", "(Z)V") : nullptr;
    bool handled = false;
    if (method != nullptr) {
        env->CallVoidMethod(activity->clazz, method, visible ? JNI_TRUE : JNI_FALSE);
        handled = !env->ExceptionCheck();
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
    return handled;
}

std::vector<std::string> DrainAndroidImeEvents(ANativeActivity* const activity) {
    std::vector<std::string> events;
    if (activity == nullptr) {
        return events;
    }
    const ScopedJniEnv scope(activity->vm);
    JNIEnv* const env = scope.Env();
    if (env == nullptr) {
        return events;
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    const jmethodID method =
        type != nullptr ? env->GetMethodID(type, "drainImeEvents", "()Ljava/lang/String;") : nullptr;
    if (method != nullptr) {
        const auto joined = static_cast<jstring>(env->CallObjectMethod(activity->clazz, method));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (joined != nullptr) {
            const char* const chars = env->GetStringUTFChars(joined, nullptr);
            if (chars != nullptr) {
                const std::string payload(chars);
                env->ReleaseStringUTFChars(joined, chars);
                std::size_t start = 0U;
                while (start <= payload.size()) {
                    const std::size_t sep = payload.find('\x1E', start);
                    const std::size_t end = sep == std::string::npos ? payload.size() : sep;
                    if (end > start) {
                        events.emplace_back(payload.substr(start, end - start));
                    }
                    if (sep == std::string::npos) {
                        break;
                    }
                    start = sep + 1U;
                }
            }
            env->DeleteLocalRef(joined);
        }
    }
    env->ExceptionClear();
    if (type != nullptr) {
        env->DeleteLocalRef(type);
    }
    return events;
}

} // namespace UVE::Window
