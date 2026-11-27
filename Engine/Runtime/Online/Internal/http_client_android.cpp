// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "http_client.h"

#if defined(__ANDROID__)

#include <jni.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>

namespace UVE::Online {
namespace {

/// The most redirects a request follows (release downloads redirect once, to storage).
inline constexpr int kMaximumRedirects = 5;
inline constexpr std::chrono::milliseconds kConnectTimeout{15000};
inline constexpr jsize kReadChunkBytes = 1 << 16;
inline constexpr std::string_view kHttpsScheme = "https";
inline constexpr std::string_view kAuthorizationHeader = "Authorization";
inline constexpr std::string_view kLocationHeader = "Location";
/// InputStream.read's value at the end of the stream.
inline constexpr jint kEndOfStream = -1;

std::atomic<JavaVM*> g_javaVm{nullptr};

/// The calling thread's JNI environment, attaching the thread for as long as this object lives.
class JniThread final {
public:
    JniThread() {
        JavaVM* const vm = g_javaVm.load();
        if (vm == nullptr) {
            return;
        }
        if (vm->GetEnv(reinterpret_cast<void**>(&m_env), JNI_VERSION_1_6) == JNI_EDETACHED) {
            m_attached = vm->AttachCurrentThread(&m_env, nullptr) == JNI_OK;
            if (!m_attached) {
                m_env = nullptr;
            }
        }
    }
    JniThread(const JniThread&) = delete;
    JniThread& operator=(const JniThread&) = delete;
    ~JniThread() {
        if (m_attached) {
            g_javaVm.load()->DetachCurrentThread();
        }
    }
    [[nodiscard]] JNIEnv* Env() const noexcept { return m_env; }

private:
    JNIEnv* m_env = nullptr;
    bool m_attached = false;
};

/// Local references made in a scope, released when it ends.
class LocalFrame final {
public:
    explicit LocalFrame(JNIEnv* env, const jint capacity) : m_env(env) { m_pushed = env->PushLocalFrame(capacity) == 0; }
    LocalFrame(const LocalFrame&) = delete;
    LocalFrame& operator=(const LocalFrame&) = delete;
    ~LocalFrame() {
        if (m_pushed) {
            m_env->PopLocalFrame(nullptr);
        }
    }

private:
    JNIEnv* m_env;
    bool m_pushed = false;
};
inline constexpr jint kLocalReferencesPerRequest = 64;

[[nodiscard]] std::string ToString(JNIEnv* env, jstring text) {
    if (text == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(text, nullptr);
    if (chars == nullptr) {
        env->ExceptionClear();
        return {};
    }
    std::string result(chars);
    env->ReleaseStringUTFChars(text, chars);
    return result;
}

/// Clears a pending Java exception and returns its description, or empty when there was none.
[[nodiscard]] std::string TakeException(JNIEnv* env) {
    jthrowable thrown = env->ExceptionOccurred();
    if (thrown == nullptr) {
        return {};
    }
    env->ExceptionClear();
    jclass throwable = env->FindClass("java/lang/Throwable");
    std::string message;
    if (throwable != nullptr) {
        jmethodID describe = env->GetMethodID(throwable, "toString", "()Ljava/lang/String;");
        if (describe != nullptr) {
            auto text = static_cast<jstring>(env->CallObjectMethod(thrown, describe));
            if (!env->ExceptionCheck() && text != nullptr) {
                message = ToString(env, text);
            }
            if (text != nullptr) {
                env->DeleteLocalRef(text);
            }
        }
        env->DeleteLocalRef(throwable);
    }
    env->DeleteLocalRef(thrown);
    env->ExceptionClear();
    return message.empty() ? std::string("The request failed.") : message;
}

/// Receives response bytes; returns false to stop the transfer.
using BodySink = std::function<bool(const char* data, std::size_t size)>;

class AndroidHttpClient final : public IHttpClient {
public:
    HttpResponse Get(const HttpRequest& request) override {
        std::string body;
        HttpResponse response = Perform(request, [&body](const char* data, const std::size_t size) {
            body.append(data, size);
            return true;
        }, nullptr);
        if (response.IsSuccess()) {
            response.body = std::move(body);
        }
        return response;
    }

    HttpResponse Download(const HttpRequest& request, const std::filesystem::path& file,
                                const HttpProgress& progress) override {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return {false, 0, {}, "Unable to write " + file.string()};
        }
        bool writeFailed = false;
        HttpResponse response = Perform(request, [&output, &writeFailed](const char* data, const std::size_t size) {
            output.write(data, static_cast<std::streamsize>(size));
            writeFailed = !output;
            return !writeFailed;
        }, &progress);
        output.close();
        if (writeFailed) {
            response.reached = false;
            response.error = "Unable to write " + file.string();
        }
        return response;
    }

private:
    /// Runs the request, following redirects by hand so credentials never travel to another host.
    /// A success body goes to `sink`; an error body is returned in the response.
    [[nodiscard]] static HttpResponse Perform(const HttpRequest& request, const BodySink& sink,
                                                    const HttpProgress* progress) {
        HttpResponse response;
        const JniThread thread;
        JNIEnv* const env = thread.Env();
        if (env == nullptr) {
            response.error = "HTTPS is not available: the app did not provide its Java VM.";
            return response;
        }
        const LocalFrame frame(env, kLocalReferencesPerRequest);
        jclass urlClass = env->FindClass("java/net/URL");
        if (urlClass == nullptr || env->ExceptionCheck()) {
            response.error = TakeException(env);
            return response;
        }
        jclass connectionClass = env->FindClass("java/net/HttpURLConnection");
        if (connectionClass == nullptr || env->ExceptionCheck()) {
            response.error = TakeException(env);
            return response;
        }
        jclass streamClass = env->FindClass("java/io/InputStream");
        if (streamClass == nullptr || env->ExceptionCheck()) {
            response.error = TakeException(env);
            return response;
        }
        const auto getMethod = [env](jclass cls, const char* name, const char* sig) -> jmethodID {
            if (env->ExceptionCheck()) {
                return nullptr;
            }
            return env->GetMethodID(cls, name, sig);
        };
        jmethodID urlFromString = getMethod(urlClass, "<init>", "(Ljava/lang/String;)V");
        jmethodID urlRelative = getMethod(urlClass, "<init>", "(Ljava/net/URL;Ljava/lang/String;)V");
        jmethodID urlToString = getMethod(urlClass, "toString", "()Ljava/lang/String;");
        jmethodID urlProtocol = getMethod(urlClass, "getProtocol", "()Ljava/lang/String;");
        jmethodID urlHost = getMethod(urlClass, "getHost", "()Ljava/lang/String;");
        jmethodID openConnection = getMethod(urlClass, "openConnection", "()Ljava/net/URLConnection;");
        jmethodID setFollow = getMethod(connectionClass, "setInstanceFollowRedirects", "(Z)V");
        jmethodID setConnectTimeout = getMethod(connectionClass, "setConnectTimeout", "(I)V");
        jmethodID setReadTimeout = getMethod(connectionClass, "setReadTimeout", "(I)V");
        jmethodID setProperty = getMethod(connectionClass, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
        jmethodID responseCode = getMethod(connectionClass, "getResponseCode", "()I");
        jmethodID headerField = getMethod(connectionClass, "getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;");
        jmethodID contentLength = getMethod(connectionClass, "getContentLengthLong", "()J");
        jmethodID inputStream = getMethod(connectionClass, "getInputStream", "()Ljava/io/InputStream;");
        jmethodID errorStream = getMethod(connectionClass, "getErrorStream", "()Ljava/io/InputStream;");
        jmethodID disconnect = getMethod(connectionClass, "disconnect", "()V");
        jmethodID read = getMethod(streamClass, "read", "([B)I");
        jmethodID close = getMethod(streamClass, "close", "()V");
        if (env->ExceptionCheck() || urlFromString == nullptr || urlRelative == nullptr ||
            urlToString == nullptr || urlProtocol == nullptr || urlHost == nullptr ||
            openConnection == nullptr || setFollow == nullptr || setConnectTimeout == nullptr ||
            setReadTimeout == nullptr || setProperty == nullptr || responseCode == nullptr ||
            headerField == nullptr || contentLength == nullptr || inputStream == nullptr ||
            errorStream == nullptr || disconnect == nullptr || read == nullptr || close == nullptr) {
            response.error = TakeException(env);
            if (response.error.empty()) {
                response.error = "Required Android HTTP JNI method lookup failed.";
            }
            return response;
        }

        const auto stallMs = static_cast<jint>(std::chrono::duration_cast<std::chrono::milliseconds>(request.stallTimeout).count());
        std::string currentUrl = request.url;
        std::string originalHost;
        for (int redirect = 0; redirect <= kMaximumRedirects; ++redirect) {
            // Each redirect hop gets its own local reference frame so ART's 64-ref table cannot overflow.
            const LocalFrame hopFrame(env, kLocalReferencesPerRequest);
            jstring urlStr = env->NewStringUTF(currentUrl.c_str());
            if (urlStr == nullptr || env->ExceptionCheck()) {
                response.error = TakeException(env);
                return response;
            }
            jobject url = env->NewObject(urlClass, urlFromString, urlStr);
            env->DeleteLocalRef(urlStr);
            if (env->ExceptionCheck() || url == nullptr) {
                response.error = TakeException(env);
                return response;
            }
            auto schemeObj = static_cast<jstring>(env->CallObjectMethod(url, urlProtocol));
            if (env->ExceptionCheck()) {
                response.error = TakeException(env);
                return response;
            }
            const std::string scheme = ToString(env, schemeObj);
            if (schemeObj != nullptr) env->DeleteLocalRef(schemeObj);
            auto hostObj = static_cast<jstring>(env->CallObjectMethod(url, urlHost));
            if (env->ExceptionCheck()) {
                response.error = TakeException(env);
                return response;
            }
            const std::string host = ToString(env, hostObj);
            if (hostObj != nullptr) env->DeleteLocalRef(hostObj);
            if (scheme != kHttpsScheme) {
                response.error = "Only https addresses are allowed.";
                return response;
            }
            if (originalHost.empty()) {
                originalHost = host;
            }
            jobject connection = env->CallObjectMethod(url, openConnection);
            if (env->ExceptionCheck() || connection == nullptr) {
                response.error = TakeException(env);
                return response;
            }
            // Every call can throw; JNI must not be used again while an exception is pending.
            const auto failed = [&] {
                if (!env->ExceptionCheck()) {
                    return false;
                }
                response.error = TakeException(env);
                env->CallVoidMethod(connection, disconnect);
                env->ExceptionClear();
                return true;
            };
            env->CallVoidMethod(connection, setFollow, JNI_FALSE);
            if (failed()) {
                return response;
            }
            env->CallVoidMethod(connection, setConnectTimeout, static_cast<jint>(kConnectTimeout.count()));
            if (failed()) {
                return response;
            }
            env->CallVoidMethod(connection, setReadTimeout, stallMs);
            if (failed()) {
                return response;
            }
            for (const HttpHeader& header : request.headers) {
                if (host != originalHost && header.name == kAuthorizationHeader) {
                    continue;
                }
                jstring headerKey = env->NewStringUTF(header.name.c_str());
                if (failed() || headerKey == nullptr) {
                    if (headerKey != nullptr) env->DeleteLocalRef(headerKey);
                    if (response.error.empty()) {
                        response.error = "JNI string allocation failed";
                        env->CallVoidMethod(connection, disconnect);
                        env->ExceptionClear();
                    }
                    return response;
                }
                jstring headerVal = env->NewStringUTF(header.value.c_str());
                if (failed() || headerVal == nullptr) {
                    if (headerVal != nullptr) env->DeleteLocalRef(headerVal);
                    env->DeleteLocalRef(headerKey);
                    if (response.error.empty()) {
                        response.error = "JNI string allocation failed";
                        env->CallVoidMethod(connection, disconnect);
                        env->ExceptionClear();
                    }
                    return response;
                }
                env->CallVoidMethod(connection, setProperty, headerKey, headerVal);
                env->DeleteLocalRef(headerVal);
                env->DeleteLocalRef(headerKey);
                if (failed()) {
                    return response;
                }
            }
            const jint status = env->CallIntMethod(connection, responseCode);
            if (env->ExceptionCheck()) {
                response.error = TakeException(env);
                env->CallVoidMethod(connection, disconnect);
                env->ExceptionClear();
                return response;
            }
            if (IsHttpRedirectionStatus(status)) {
                jstring locKey = env->NewStringUTF(kLocationHeader.data());
                std::string location;
                if (locKey != nullptr && !env->ExceptionCheck()) {
                    auto locObj = static_cast<jstring>(env->CallObjectMethod(connection, headerField, locKey));
                    if (!env->ExceptionCheck() && locObj != nullptr) {
                        location = ToString(env, locObj);
                    }
                    if (locObj != nullptr) env->DeleteLocalRef(locObj);
                }
                if (locKey != nullptr) env->DeleteLocalRef(locKey);
                env->ExceptionClear();
                env->CallVoidMethod(connection, disconnect);
                env->ExceptionClear();
                if (location.empty()) {
                    response.error = "A redirect had no location.";
                    return response;
                }
                jstring locStr = env->NewStringUTF(location.c_str());
                if (locStr == nullptr || env->ExceptionCheck()) {
                    response.error = TakeException(env);
                    return response;
                }
                jobject resolved = env->NewObject(urlClass, urlRelative, url, locStr);
                env->DeleteLocalRef(locStr);
                if (env->ExceptionCheck() || resolved == nullptr) {
                    response.error = TakeException(env);
                    return response;
                }
                auto resolvedStr = static_cast<jstring>(env->CallObjectMethod(resolved, urlToString));
                if (env->ExceptionCheck()) {
                    response.error = TakeException(env);
                    return response;
                }
                currentUrl = ToString(env, resolvedStr);
                if (resolvedStr != nullptr) env->DeleteLocalRef(resolvedStr);
                continue;
            }

            response.reached = true;
            response.status = status;
            const bool success = IsHttpSuccessStatus(status);
            jobject stream = env->CallObjectMethod(connection, success ? inputStream : errorStream);
            if (env->ExceptionCheck()) {
                // An error response may have no body at all.
                static_cast<void>(TakeException(env));
                stream = nullptr;
            }
            const jlong total = env->CallLongMethod(connection, contentLength);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            std::uint64_t received = 0U;
            if (stream != nullptr) {
                jbyteArray buffer = env->NewByteArray(kReadChunkBytes);
                if (buffer == nullptr || env->ExceptionCheck()) {
                    response.reached = false;
                    response.error = TakeException(env);
                    if (response.error.empty()) {
                        response.error = "Failed to allocate JNI read buffer.";
                    }
                    env->CallVoidMethod(stream, close);
                    env->ExceptionClear();
                    env->CallVoidMethod(connection, disconnect);
                    env->ExceptionClear();
                    return response;
                }
                std::vector<char> chunk(static_cast<std::size_t>(kReadChunkBytes));
                for (;;) {
                    const jint count = env->CallIntMethod(stream, read, buffer);
                    if (env->ExceptionCheck()) {
                        response.reached = false;
                        response.error = TakeException(env);
                        break;
                    }
                    if (count == kEndOfStream) {
                        break;
                    }
                    if (count < 0 || count > kReadChunkBytes) {
                        response.reached = false;
                        response.error = "Invalid HTTP stream read length";
                        break;
                    }
                    if (count == 0) {
                        continue;
                    }
                    env->GetByteArrayRegion(buffer, 0, count, reinterpret_cast<jbyte*>(chunk.data()));
                    if (env->ExceptionCheck()) {
                        response.reached = false;
                        response.error = TakeException(env);
                        break;
                    }
                    if (success) {
                        if (!sink(chunk.data(), static_cast<std::size_t>(count))) {
                            break;
                        }
                        received += static_cast<std::uint64_t>(count);
                        if (progress != nullptr && *progress &&
                            !(*progress)(received, total > 0 ? static_cast<std::uint64_t>(total) : 0U)) {
                            response.reached = false;
                            response.error = "Cancelled.";
                            break;
                        }
                    } else {
                        response.body.append(chunk.data(), static_cast<std::size_t>(count));
                    }
                }
                env->DeleteLocalRef(buffer);
                env->CallVoidMethod(stream, close);
                static_cast<void>(TakeException(env));
            }
            env->CallVoidMethod(connection, disconnect);
            env->ExceptionClear();
            return response;
        }
        response.error = "Too many redirects.";
        return response;
    }
};

} // namespace

void SetAndroidJavaVm(void* const javaVm) {
    g_javaVm.store(static_cast<JavaVM*>(javaVm));
}

std::unique_ptr<IHttpClient> CreatePlatformHttpClient() {
    return std::make_unique<AndroidHttpClient>();
}

} // namespace UVE::Online

#endif
