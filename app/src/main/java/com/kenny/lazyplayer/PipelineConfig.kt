package com.kenny.lazyplayer

import androidx.annotation.Keep

@Keep
enum class TransportType(val id: Int, val displayName: String) {
    RTSP_UDP(0, "RTSP over UDP (Force)"),
    RTSP_TCP(1, "RTSP over TCP (Interleaved)"),
    DIRECT_UDP(2, "Direct UDP Raw Stream");

    companion object {
        fun fromId(id: Int) = values().firstOrNull { it.id == id } ?: RTSP_UDP
    }
}

@Keep
enum class PipelineCombination(val id: Int, val displayName: String) {
    CPP_HW(0, "C++ Engine + Hardware Codec (Zero-Copy)"),
    CPP_SW(1, "C++ Engine + Software Codec (CPU)"),
    GSTREAMER_HW(2, "GStreamer + Hardware Codec"),
    GSTREAMER_SW(3, "GStreamer + Software Codec");

    companion object {
        fun fromId(id: Int) = values().firstOrNull { it.id == id } ?: CPP_HW
    }
}
