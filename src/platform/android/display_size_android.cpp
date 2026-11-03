// ============================================================================
// src/platform/android/display_size_android.cpp -- platform_display_size.h on Android
// ============================================================================
// NOT RETAIL (issue #96). Replaces src/platform/posix/display_size_posix.cpp. The activity
// (OpenBAActivity.physicalDisplay, android/app/src/main/java/org/openba/game/OpenBAActivity.java)
// answers the whole display's DisplayMetrics from getRealMetrics: widthPixels, heightPixels, xdpi,
// ydpi and densityDpi. Each edge is its pixels over its dpi, the dpi checked against the density
// bucket (PlausibleDpi). SDL3 has no physical-size call of its own (SDL2's SDL_GetDisplayDPI is
// gone; SDLActivity.getDiagonal is the diagonal only), so this is one JNI call through the JNI
// environment and the activity SDL holds. The answer is asked once and kept; the result is logged
// once.
// ============================================================================

#include "platform_display_size.h"

#include <SDL3/SDL_system.h>

#include <android/log.h>
#include <jni.h>

namespace Platform {
namespace Display {

namespace {

const char kLogTag[] = "OpenBA";

// The five floats of OpenBAActivity.physicalDisplay, or false (logged) when the call fails.
bool AskActivity(float out[5]) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) return false;
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (activity == nullptr) return false;
    bool ok = false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID mid = env->GetMethodID(cls, "physicalDisplay", "()[F");
    if (mid != nullptr) {
        jfloatArray arr = static_cast<jfloatArray>(env->CallObjectMethod(activity, mid));
        if (!env->ExceptionCheck() && arr != nullptr && env->GetArrayLength(arr) >= 5) {
            env->GetFloatArrayRegion(arr, 0, 5, out);
            ok = !env->ExceptionCheck();
        }
        if (arr != nullptr) env->DeleteLocalRef(arr);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        ok = false;
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
}

}  // namespace

bool PhysicalScreenInches(float* widthInches, float* heightInches) {
    static bool asked = false;
    static bool known = false;
    static float width = 0.0f;
    static float height = 0.0f;
    if (!asked) {
        asked = true;
        float m[5] = {};
        if (AskActivity(m)) {
            const float xdpi = PlausibleDpi(m[2], m[4]);
            const float ydpi = PlausibleDpi(m[3], m[4]);
            if (m[0] > 0.0f && m[1] > 0.0f && xdpi > 0.0f && ydpi > 0.0f) {
                width = m[0] / xdpi;
                height = m[1] / ydpi;
                known = true;
            }
            __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                "[Display] %.0fx%.0f px, xdpi %.1f ydpi %.1f (density %.0f dpi): %.2fx%.2f in",
                                m[0], m[1], m[2], m[3], m[4], width, height);
        } else {
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                                "[Display] OpenBAActivity.physicalDisplay failed: the physical size is unknown");
        }
    }
    *widthInches = width;
    *heightInches = height;
    return known;
}

}  // namespace Display
}  // namespace Platform
