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
    std::string result = chars != nullptr ? chars : "";
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
    jmethodID describe = env->GetMethodID(throwable, "toString", "()Ljava/lang/String;");
    std::string message = ToString(env, static_cast<jstring>(env->CallObjectMethod(thrown, describe)));
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
        jclass connectionClass = env->FindClass("java/net/HttpURLConnection");
        jclass streamClass = env->FindClass("java/io/InputStream");
        if (urlClass == nullptr || connectionClass == nullptr || streamClass == nullptr) {
            response.error = TakeException(env);
            return response;
        }
        jmethodID urlFromString = env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");
        jmethodID urlRelative = env->GetMethodID(urlClass, "<init>", "(Ljava/net/URL;Ljava/lang/String;)V");
        jmethodID urlToString = env->GetMethodID(urlClass, "toString", "()Ljava/lang/String;");
        jmethodID urlProtocol = env->GetMethodID(urlClass, "getProtocol", "()Ljava/lang/String;");
        jmethodID urlHost = env->GetMethodID(urlClass, "getHost", "()Ljava/lang/String;");
        jmethodID openConnection = env->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;");
        jmethodID setFollow = env->GetMethodID(connectionClass, "setInstanceFollowRedirects", "(Z)V");
        jmethodID setConnectTimeout = env->GetMethodID(connectionClass, "setConnectTimeout", "(I)V");
        jmethodID setReadTimeout = env->GetMethodID(connectionClass, "setReadTimeout", "(I)V");
        jmethodID setProperty = env->GetMethodID(connectionClass, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
        jmethodID responseCode = env->GetMethodID(connectionClass, "getResponseCode", "()I");
        jmethodID headerField = env->GetMethodID(connectionClass, "getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;");
        jmethodID contentLength = env->GetMethodID(connectionClass, "getContentLengthLong", "()J");
        jmethodID inputStream = env->GetMethodID(connectionClass, "getInputStream", "()Ljava/io/InputStream;");
        jmethodID errorStream = env->GetMethodID(connectionClass, "getErrorStream", "()Ljava/io/InputStream;");
        jmethodID disconnect = env->GetMethodID(connectionClass, "disconnect", "()V");
        jmethodID read = env->GetMethodID(streamClass, "read", "([B)I");
        jmethodID close = env->GetMethodID(streamClass, "close", "()V");
        if (env->ExceptionCheck()) {
            response.error = TakeException(env);
            return response;
        }

        const auto stallMs = static_cast<jint>(std::chrono::duration_cast<std::chrono::milliseconds>(request.stallTimeout).count());
        std::string currentUrl = request.url;
        std::string originalHost;
        for (int redirect = 0; redirect <= kMaximumRedirects; ++redirect) {
            // Each redirect hop gets its own local reference frame so ART's 64-ref table cannot overflow.
            const LocalFrame hopFrame(env, kLocalReferencesPerRequest);
            jobject url = env->NewObject(urlClass, urlFromString, env->NewStringUTF(currentUrl.c_str()));
            if (env->ExceptionCheck() || url == nullptr) {
                response.error = TakeException(env);
                return response;
            }
            const std::string scheme = ToString(env, static_cast<jstring>(env->CallObjectMethod(url, urlProtocol)));
            const std::string host = ToString(env, static_cast<jstring>(env->CallObjectMethod(url, urlHost)));
            if (scheme != kHttpsScheme) {
                response.error = "Only https addresses are allowed.";
                return response;
            }
            if (originalHost.empty()) {
                originalHost = host;
            }
            jobject connection = env->CallObjectMethod(url, openConnection);
            if (env->ExceptionCheck()) {
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
                env->CallVoidMethod(connection, setProperty, env->NewStringUTF(header.name.c_str()),
                                    env->NewStringUTF(header.value.c_str()));
                if (failed()) {
                    return response;
                }
            }
            const jint status = env->CallIntMethod(connection, responseCode);
            if (env->ExceptionCheck()) {
                response.error = TakeException(env);
                env->CallVoidMethod(connection, disconnect);
                return response;
            }
            if (IsHttpRedirectionStatus(status)) {
                const std::string location = ToString(
                    env, static_cast<jstring>(env->CallObjectMethod(connection, headerField,
                                                                    env->NewStringUTF(kLocationHeader.data()))));
                env->CallVoidMethod(connection, disconnect);
                if (location.empty()) {
                    response.error = "A redirect had no location.";
                    return response;
                }
                jobject resolved = env->NewObject(urlClass, urlRelative, url, env->NewStringUTF(location.c_str()));
                if (env->ExceptionCheck() || resolved == nullptr) {
                    response.error = TakeException(env);
                    return response;
                }
                currentUrl = ToString(env, static_cast<jstring>(env->CallObjectMethod(resolved, urlToString)));
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
            std::uint64_t received = 0U;
            if (stream != nullptr) {
                jbyteArray buffer = env->NewByteArray(kReadChunkBytes);
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
                    env->GetByteArrayRegion(buffer, 0, count, reinterpret_cast<jbyte*>(chunk.data()));
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
                env->CallVoidMethod(stream, close);
                static_cast<void>(TakeException(env));
            }
            env->CallVoidMethod(connection, disconnect);
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
