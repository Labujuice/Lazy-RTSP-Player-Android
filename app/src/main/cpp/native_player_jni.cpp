#include <jni.h>
#include <android/native_window_jni.h>
#include "player_engine.h"
#include "common/logger.h"

using namespace player;

struct PlayerContext {
    std::unique_ptr<PlayerEngine> engine;
    JavaVM* jvm = nullptr;
    jobject playerObjRef = nullptr;
    jmethodID onStateChangedMethod = nullptr;
};

extern "C" {

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    LOGI("UltraLowLatencyPlayer JNI_OnLoad");
    return JNI_VERSION_1_6;
}

JNIEXPORT jlong JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeCreate(JNIEnv* env, jobject thiz) {
    auto* ctx = new PlayerContext();
    env->GetJavaVM(&ctx->jvm);
    ctx->playerObjRef = env->NewGlobalRef(thiz);

    jclass clazz = env->GetObjectClass(thiz);
    ctx->onStateChangedMethod = env->GetMethodID(clazz, "onNativeStateChanged", "(ILjava/lang/String;)V");

    ctx->engine = std::make_unique<PlayerEngine>();
    ctx->engine->setStateCallback([ctx](int state, const std::string& msg) {
        if (!ctx->jvm || !ctx->playerObjRef || !ctx->onStateChangedMethod) return;

        JNIEnv* env = nullptr;
        bool attached = false;
        if (ctx->jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            if (ctx->jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            } else {
                return;
            }
        }

        jstring jMsg = env->NewStringUTF(msg.c_str());
        env->CallVoidMethod(ctx->playerObjRef, ctx->onStateChangedMethod, state, jMsg);
        env->DeleteLocalRef(jMsg);

        if (attached) {
            ctx->jvm->DetachCurrentThread();
        }
    });

    LOGI("NativePlayer created: %p", ctx);
    return reinterpret_cast<jlong>(ctx);
}

JNIEXPORT void JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeDestroy(JNIEnv* env, jobject /*thiz*/, jlong handle) {
    auto* ctx = reinterpret_cast<PlayerContext*>(handle);
    if (!ctx) return;

    if (ctx->engine) {
        ctx->engine->stop();
    }

    if (ctx->playerObjRef) {
        env->DeleteGlobalRef(ctx->playerObjRef);
        ctx->playerObjRef = nullptr;
    }

    delete ctx;
    LOGI("NativePlayer destroyed: %p", ctx);
}

JNIEXPORT void JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeSetSurface(JNIEnv* env, jobject /*thiz*/, jlong handle, jobject surface) {
    auto* ctx = reinterpret_cast<PlayerContext*>(handle);
    if (!ctx || !ctx->engine) return;

    ANativeWindow* window = nullptr;
    if (surface) {
        window = ANativeWindow_fromSurface(env, surface);
    }
    ctx->engine->setNativeWindow(window);
}

JNIEXPORT jboolean JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeStart(JNIEnv* env, jobject /*thiz*/, jlong handle,
                                                  jstring url, jint transport, jint pipeline, jint udpPort) {
    auto* ctx = reinterpret_cast<PlayerContext*>(handle);
    if (!ctx || !ctx->engine) return JNI_FALSE;

    const char* urlStr = env->GetStringUTFChars(url, nullptr);
    std::string streamUrl = urlStr ? urlStr : "";
    env->ReleaseStringUTFChars(url, urlStr);

    bool res = ctx->engine->start(streamUrl, static_cast<TransportType>(transport),
                                  static_cast<PipelineCombination>(pipeline), udpPort);
    return res ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeStop(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    auto* ctx = reinterpret_cast<PlayerContext*>(handle);
    if (ctx && ctx->engine) {
        ctx->engine->stop();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeStartRecording(JNIEnv* env, jobject /*thiz*/, jlong handle, jstring filePath) {
    auto* ctx = reinterpret_cast<PlayerContext*>(handle);
    if (!ctx || !ctx->engine) return JNI_FALSE;

    const char* pathStr = env->GetStringUTFChars(filePath, nullptr);
    std::string path = pathStr ? pathStr : "";
    env->ReleaseStringUTFChars(filePath, pathStr);

    return ctx->engine->startRecording(path) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeStopRecording(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    auto* ctx = reinterpret_cast<PlayerContext*>(handle);
    if (ctx && ctx->engine) {
        ctx->engine->stopRecording();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeTakeSnapshot(JNIEnv* env, jobject /*thiz*/, jlong handle, jstring filePath) {
    auto* ctx = reinterpret_cast<PlayerContext*>(handle);
    if (!ctx || !ctx->engine) return JNI_FALSE;

    const char* pathStr = env->GetStringUTFChars(filePath, nullptr);
    std::string path = pathStr ? pathStr : "";
    env->ReleaseStringUTFChars(filePath, pathStr);

    return ctx->engine->takeSnapshot(path) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_kenny_lazyplayer_NativePlayer_nativeGetTelemetry(JNIEnv* env, jobject /*thiz*/, jlong handle, jobject outTelemetry) {
    auto* ctx = reinterpret_cast<PlayerContext*>(handle);
    if (!ctx || !ctx->engine || !outTelemetry) return;

    TelemetrySnapshot snap = ctx->engine->getTelemetry();

    jclass clazz = env->GetObjectClass(outTelemetry);

    jfieldID fPipeline = env->GetFieldID(clazz, "pipelineCombination", "Ljava/lang/String;");
    jfieldID fTransport = env->GetFieldID(clazz, "transportProtocol", "Ljava/lang/String;");
    jfieldID fCodecRes = env->GetFieldID(clazz, "codecResolution", "Ljava/lang/String;");
    jfieldID fBitrate = env->GetFieldID(clazz, "bitrateKbps", "D");
    jfieldID fRenderFps = env->GetFieldID(clazz, "renderFps", "D");
    jfieldID fSourceFps = env->GetFieldID(clazz, "sourceFps", "I");
    jfieldID fDecodeTime = env->GetFieldID(clazz, "decodeTimeMs", "D");
    jfieldID fDropJitter = env->GetFieldID(clazz, "droppedFramesJitter", "J");
    jfieldID fDropLoss = env->GetFieldID(clazz, "droppedFramesLoss", "J");
    jfieldID fCpuApp = env->GetFieldID(clazz, "cpuRateApp", "D");
    jfieldID fCpuTotal = env->GetFieldID(clazz, "cpuRateTotal", "D");
    jfieldID fRamPss = env->GetFieldID(clazz, "ramPssMb", "D");
    jfieldID fRamRss = env->GetFieldID(clazz, "ramRssMb", "D");
    jfieldID fGpu = env->GetFieldID(clazz, "gpuState", "Ljava/lang/String;");

    if (fPipeline) {
        jstring s = env->NewStringUTF(snap.pipelineCombination.c_str());
        env->SetObjectField(outTelemetry, fPipeline, s);
        env->DeleteLocalRef(s);
    }
    if (fTransport) {
        jstring s = env->NewStringUTF(snap.transportProtocol.c_str());
        env->SetObjectField(outTelemetry, fTransport, s);
        env->DeleteLocalRef(s);
    }
    if (fCodecRes) {
        jstring s = env->NewStringUTF(snap.codecResolution.c_str());
        env->SetObjectField(outTelemetry, fCodecRes, s);
        env->DeleteLocalRef(s);
    }
    if (fGpu) {
        jstring s = env->NewStringUTF(snap.gpuState.c_str());
        env->SetObjectField(outTelemetry, fGpu, s);
        env->DeleteLocalRef(s);
    }

    if (fBitrate) env->SetDoubleField(outTelemetry, fBitrate, snap.bitrateKbps);
    if (fRenderFps) env->SetDoubleField(outTelemetry, fRenderFps, snap.renderFps);
    if (fSourceFps) env->SetIntField(outTelemetry, fSourceFps, snap.sourceFps);
    if (fDecodeTime) env->SetDoubleField(outTelemetry, fDecodeTime, snap.decodeTimeMs);
    if (fDropJitter) env->SetLongField(outTelemetry, fDropJitter, static_cast<jlong>(snap.droppedFramesJitter));
    if (fDropLoss) env->SetLongField(outTelemetry, fDropLoss, static_cast<jlong>(snap.droppedFramesLoss));
    if (fCpuApp) env->SetDoubleField(outTelemetry, fCpuApp, snap.cpuRateApp);
    if (fCpuTotal) env->SetDoubleField(outTelemetry, fCpuTotal, snap.cpuRateTotal);
    if (fRamPss) env->SetDoubleField(outTelemetry, fRamPss, snap.ramPssMb);
    if (fRamRss) env->SetDoubleField(outTelemetry, fRamRss, snap.ramRssMb);
}

} // extern "C"
