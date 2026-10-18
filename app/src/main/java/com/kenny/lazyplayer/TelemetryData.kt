package com.kenny.lazyplayer

import androidx.annotation.Keep

@Keep
data class TelemetryData(
    var pipelineCombination: String = "C++ + HW Codec",
    var transportProtocol: String = "UDP (Force)",
    var codecResolution: String = "Probing...",
    var bitrateKbps: Double = 0.0,
    var renderFps: Double = 0.0,
    var sourceFps: Int = 0,
    var decodeTimeMs: Double = 0.0,
    var droppedFramesJitter: Long = 0,
    var droppedFramesLoss: Long = 0,
    var cpuRateApp: Double = 0.0,
    var cpuRateTotal: Double = 0.0,
    var ramPssMb: Double = 0.0,
    var ramRssMb: Double = 0.0,
    var gpuState: String = "N/A (Protected)"
)
