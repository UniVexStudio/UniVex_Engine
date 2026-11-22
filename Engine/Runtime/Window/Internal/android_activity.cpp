// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "android_activity.h"

#include <android/native_activity.h>
#include <jni.h>

namespace UVE::Window {

namespace {

/// The fields UniVexActivity keeps the safe area in.
inline constexpr const char* kSafeInsetLeftFieldUVE = "safeInsetLeft";
inline constexpr const char* kSafeInsetTopFieldUVE = "safeInsetTop";
inline constexpr const char* kSafeInsetRightFieldUVE = "safeInsetRight";
inline constexpr const char* kSafeInsetBottomFieldUVE = "safeInsetBottom";
inline constexpr const char* kIntFieldSignatureUVE = "I";

/// Attaches the calling thread to Java for its lifetime when it was not attached already.
class ScopedJniEnvUVE final {
public:
    explicit ScopedJniEnvUVE(JavaVM* const vm) : m_vm(vm) {
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
    ~ScopedJniEnvUVE() {
        if (m_attached) {
            m_vm->DetachCurrentThread();
        }
    }
    ScopedJniEnvUVE(const ScopedJniEnvUVE&) = delete;
    ScopedJniEnvUVE& operator=(const ScopedJniEnvUVE&) = delete;

    [[nodiscard]] JNIEnv* EnvUVE() const noexcept { return m_env; }

private:
    JavaVM* m_vm = nullptr;
    JNIEnv* m_env = nullptr;
    bool m_attached = false;
};

/// Reads one int field of `object`, or 0 when its class has no such field.
[[nodiscard]] int ReadIntFieldUVE(JNIEnv* const env, jobject const object, jclass const type, const char* const name) {
    const jfieldID field = env->GetFieldID(type, name, kIntFieldSignatureUVE);
    if (field == nullptr) {
        env->ExceptionClear();  // NoSuchFieldError: a plain NativeActivity
        return 0;
    }
    return env->GetIntField(object, field);
}

inline constexpr const char* kKeyEventClassUVE = "android/view/KeyEvent";
inline constexpr const char* kKeyEventConstructorUVE = "(II)V";
inline constexpr const char* kUnicodeCharMethodUVE = "getUnicodeChar";
inline constexpr const char* kUnicodeCharSignatureUVE = "(I)I";

} // namespace

unsigned int AndroidKeyCharacterUVE(ANativeActivity* const activity, const int action, const int keyCode,
                                    const int metaState) {
    if (activity == nullptr) {
        return 0U;
    }
    const ScopedJniEnvUVE scope(activity->vm);
    JNIEnv* const env = scope.EnvUVE();
    if (env == nullptr) {
        return 0U;
    }
    const jclass type = env->FindClass(kKeyEventClassUVE);
    if (type == nullptr) {
        env->ExceptionClear();
        return 0U;
    }
    const jmethodID constructor = env->GetMethodID(type, "<init>", kKeyEventConstructorUVE);
    const jmethodID unicode = env->GetMethodID(type, kUnicodeCharMethodUVE, kUnicodeCharSignatureUVE);
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

AndroidSafeAreaUVE ReadAndroidSafeAreaUVE(ANativeActivity* const activity) {
    if (activity == nullptr) {
        return {};
    }
    const ScopedJniEnvUVE scope(activity->vm);
    JNIEnv* const env = scope.EnvUVE();
    if (env == nullptr) {
        return {};
    }
    const jclass type = env->GetObjectClass(activity->clazz);
    if (type == nullptr) {
        env->ExceptionClear();
        return {};
    }
    AndroidSafeAreaUVE area;
    area.left = ReadIntFieldUVE(env, activity->clazz, type, kSafeInsetLeftFieldUVE);
    area.top = ReadIntFieldUVE(env, activity->clazz, type, kSafeInsetTopFieldUVE);
    area.right = ReadIntFieldUVE(env, activity->clazz, type, kSafeInsetRightFieldUVE);
    area.bottom = ReadIntFieldUVE(env, activity->clazz, type, kSafeInsetBottomFieldUVE);
    env->DeleteLocalRef(type);
    return area;
}

} // namespace UVE::Window
