// ============================================================================
// src/platform/android/net_android.cpp -- Platform::Net::HttpsGet on Android
// ============================================================================
// NOT RETAIL. The rules are in platform_net.h. Replaces src/platform/posix/net_posix.cpp (both
// bodies differ). The request itself is Java: org.openba.game.NetGet.get
// (android/app/src/main/java/org/openba/game/NetGet.java) runs it on the platform's
// HttpsURLConnection (the system's TLS stack and trust store, the certificate verified) and bounds
// the whole call by the timeout. This file is the JNI call to it.
//
// Threads. The caller is a native thread (ext.update_check's detached worker): SDL_GetAndroidJNIEnv
// attaches it to the VM and SDL's thread-key destructor detaches it when the thread ends. FindClass on
// such a thread sees the system class loader only, so the app's NetGet class is looked up once
// through the activity's own class loader (Context.getClassLoader) and kept as a global reference;
// CanHttpsGet (ext.update_check's Boot, the main thread) normally makes that lookup, before the worker
// starts. java.lang.String resolves through FindClass from any thread.
//
// Every JNI call that can throw is followed by an exception check; a pending exception is cleared
// and the call answers 0 with one `[Net]` line. Every local reference made here is deleted before
// return. The app needs android.permission.INTERNET (android/app/src/main/AndroidManifest.xml);
// without it the connect throws SecurityException, which NetGet turns into a failure: 0 and the line.
// ============================================================================

#include "platform_net.h"

#include <SDL3/SDL_system.h>

#include <jni.h>

#include <climits>
#include <cstdio>
#include <mutex>
#include <string>

namespace Platform {
namespace Net {

namespace {

std::mutex g_resolveMutex;
bool g_resolveTried = false;
jclass g_netGet = nullptr;   // global reference to org.openba.game.NetGet
jmethodID g_get = nullptr;   // NetGet.get

const char kGetSignature[] = "(Ljava/lang/String;Ljava/lang/String;II[I[Ljava/lang/String;)[B";

// True (and the exception cleared) when a Java exception is pending.
bool Threw(JNIEnv* env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    return true;
}

void DeleteLocal(JNIEnv* env, jobject ref) {
    if (ref != nullptr) env->DeleteLocalRef(ref);
}

// NetGet and its get() through the activity's class loader, once; logs the step that failed.
bool ResolveLocked(JNIEnv* env) {
    if (g_resolveTried) return g_netGet != nullptr;
    g_resolveTried = true;
    const char* failed = nullptr;
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    jclass activityClass = nullptr;
    jobject loader = nullptr;
    jclass loaderClass = nullptr;
    jstring name = nullptr;
    jclass netGet = nullptr;
    if (Threw(env) || activity == nullptr) {
        failed = "the activity";
    }
    if (failed == nullptr) {
        activityClass = env->GetObjectClass(activity);
        jmethodID getClassLoader =
            env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
        if (Threw(env) || getClassLoader == nullptr) {
            failed = "Context.getClassLoader";
        } else {
            loader = env->CallObjectMethod(activity, getClassLoader);
            if (Threw(env) || loader == nullptr) failed = "the activity's class loader";
        }
    }
    if (failed == nullptr) {
        loaderClass = env->GetObjectClass(loader);
        jmethodID loadClass =
            env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        if (Threw(env) || loadClass == nullptr) {
            failed = "ClassLoader.loadClass";
        } else {
            name = env->NewStringUTF("org.openba.game.NetGet");
            if (Threw(env) || name == nullptr) failed = "the class name string";
        }
        if (failed == nullptr) {
            netGet = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, name));
            if (Threw(env) || netGet == nullptr) failed = "org.openba.game.NetGet";
        }
    }
    if (failed == nullptr) {
        jmethodID get = env->GetStaticMethodID(netGet, "get", kGetSignature);
        if (Threw(env) || get == nullptr) {
            failed = "NetGet.get";
        } else {
            g_netGet = static_cast<jclass>(env->NewGlobalRef(netGet));
            g_get = get;
            if (g_netGet == nullptr) failed = "a global reference";
        }
    }
    DeleteLocal(env, netGet);
    DeleteLocal(env, name);
    DeleteLocal(env, loaderClass);
    DeleteLocal(env, loader);
    DeleteLocal(env, activityClass);
    DeleteLocal(env, activity);
    if (failed != nullptr) {
        std::fprintf(stderr, "[Net] lookup failed: %s did not resolve; no request is made\n", failed);
        return false;
    }
    return true;
}

bool Resolve(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(g_resolveMutex);
    return ResolveLocked(env);
}

}  // namespace

bool CanHttpsGet() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) return false;
    return Resolve(env);
}

int HttpsGet(const char* host, const char* path, const char* userAgent, int timeoutMs, size_t maxBytes,
             std::string& body) {
    body.clear();
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) {
        std::fprintf(stderr, "[Net] attach failed: no JNI environment on this thread\n");
        return 0;
    }
    if (!Resolve(env)) return 0;

    const std::string url = std::string("https://") + host + path;
    const jint cap = maxBytes > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<jint>(maxBytes);
    const char* failed = nullptr;
    int status = 0;
    jstring jurl = nullptr;
    jstring jagent = nullptr;
    jintArray statusOut = nullptr;
    jclass stringClass = nullptr;
    jobjectArray failureOut = nullptr;
    jbyteArray bytes = nullptr;
    jstring failure = nullptr;

    jurl = env->NewStringUTF(url.c_str());
    if (Threw(env) || jurl == nullptr) failed = "the URL string";
    if (failed == nullptr) {
        jagent = env->NewStringUTF(userAgent);
        if (Threw(env) || jagent == nullptr) failed = "the User-Agent string";
    }
    if (failed == nullptr) {
        statusOut = env->NewIntArray(1);
        if (Threw(env) || statusOut == nullptr) failed = "the status array";
    }
    if (failed == nullptr) {
        stringClass = env->FindClass("java/lang/String");
        if (Threw(env) || stringClass == nullptr) failed = "java.lang.String";
    }
    if (failed == nullptr) {
        failureOut = env->NewObjectArray(1, stringClass, nullptr);
        if (Threw(env) || failureOut == nullptr) failed = "the failure array";
    }
    if (failed == nullptr) {
        bytes = static_cast<jbyteArray>(
            env->CallStaticObjectMethod(g_netGet, g_get, jurl, jagent, timeoutMs, cap, statusOut, failureOut));
        if (Threw(env)) failed = "NetGet.get threw";
    }
    if (failed == nullptr) {
        jint code = 0;
        env->GetIntArrayRegion(statusOut, 0, 1, &code);
        if (Threw(env)) {
            failed = "the status read";
        } else {
            status = static_cast<int>(code);
        }
    }
    if (failed == nullptr && status != 0 && bytes != nullptr) {
        const jsize length = env->GetArrayLength(bytes);
        const size_t kept = static_cast<size_t>(length) < maxBytes ? static_cast<size_t>(length) : maxBytes;
        body.resize(kept);
        if (kept > 0) {
            env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(kept), reinterpret_cast<jbyte*>(&body[0]));
        }
        if (Threw(env)) {
            failed = "the body copy";
            body.clear();
            status = 0;
        }
    }
    if (failed == nullptr && status == 0) {
        failure = static_cast<jstring>(env->GetObjectArrayElement(failureOut, 0));
        if (Threw(env)) failure = nullptr;
        const char* text = failure != nullptr ? env->GetStringUTFChars(failure, nullptr) : nullptr;
        if (Threw(env)) text = nullptr;
        std::fprintf(stderr, "[Net] GET https://%s%s failed at %s\n", host, path,
                     text != nullptr ? text : "an unknown step");
        if (text != nullptr) env->ReleaseStringUTFChars(failure, text);
    }
    if (failed != nullptr) {
        std::fprintf(stderr, "[Net] GET https://%s%s failed: %s\n", host, path, failed);
        status = 0;
        body.clear();
    }
    DeleteLocal(env, failure);
    DeleteLocal(env, bytes);
    DeleteLocal(env, failureOut);
    DeleteLocal(env, stringClass);
    DeleteLocal(env, statusOut);
    DeleteLocal(env, jagent);
    DeleteLocal(env, jurl);
    return status;
}

}  // namespace Net
}  // namespace Platform
