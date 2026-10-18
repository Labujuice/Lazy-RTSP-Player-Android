package com.kenny.lazyplayer

import android.view.Surface
import androidx.annotation.Keep

@Keep
class NativePlayer {

    interface PlayerListener {
        fun onStateChanged(state: Int, message: String)
    }

    companion object {
        const val STATE_IDLE = 0
        const val STATE_CONNECTING = 1
        const val STATE_PROBING = 2
        const val STATE_PLAYING = 3
        const val STATE_DISCONNECTED = 4
        const val STATE_ERROR = 5

        init {
            System.loadLibrary("lazyplayer")
        }
    }

    private var nativeHandle: Long = 0
    private var listener: PlayerListener? = null
    private val telemetryCache = TelemetryData()

    init {
        nativeHandle = nativeCreate()
    }

    fun setListener(listener: PlayerListener?) {
        this.listener = listener
    }

    fun setSurface(surface: Surface?) {
        if (nativeHandle != 0L) {
            nativeSetSurface(nativeHandle, surface)
        }
    }

    fun start(url: String, transport: TransportType, pipeline: PipelineCombination, udpPort: Int = 5600): Boolean {
        if (nativeHandle == 0L) return false
        return nativeStart(nativeHandle, url, transport.id, pipeline.id, udpPort)
    }

    fun stop() {
        if (nativeHandle != 0L) {
            nativeStop(nativeHandle)
        }
    }

    fun startRecording(filePath: String): Boolean {
        if (nativeHandle == 0L) return false
        return nativeStartRecording(nativeHandle, filePath)
    }

    fun stopRecording() {
        if (nativeHandle != 0L) {
            nativeStopRecording(nativeHandle)
        }
    }

    fun takeSnapshot(filePath: String): Boolean {
        if (nativeHandle == 0L) return false
        return nativeTakeSnapshot(nativeHandle, filePath)
    }

    fun getTelemetry(): TelemetryData {
        if (nativeHandle != 0L) {
            nativeGetTelemetry(nativeHandle, telemetryCache)
        }
        return telemetryCache
    }

    fun release() {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
        listener = null
    }

    @Keep
    private fun onNativeStateChanged(state: Int, message: String) {
        listener?.onStateChanged(state, message)
    }

    // Native JNI functions
    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativeStart(handle: Long, url: String, transport: Int, pipeline: Int, udpPort: Int): Boolean
    private external fun nativeStop(handle: Long)
    private external fun nativeStartRecording(handle: Long, filePath: String): Boolean
    private external fun nativeStopRecording(handle: Long)
    private external fun nativeTakeSnapshot(handle: Long, filePath: String): Boolean
    private external fun nativeGetTelemetry(handle: Long, outTelemetry: TelemetryData)
}
