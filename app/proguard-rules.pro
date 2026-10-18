# Proguard rules for UltraLowLatencyRTSPPlayer

-keepattributes *Annotation*
-keepattributes Signature
-keepattributes InnerClasses

# Preserve JNI methods and native player callbacks
-keepclasseswithmembernames class * {
    native <methods>;
}

-keep class com.kenny.lazyplayer.NativePlayer {
    public *;
    protected *;
}

-keep class com.kenny.lazyplayer.TelemetryData {
    public *;
    protected *;
}

-keep class com.kenny.lazyplayer.PipelineConfig {
    public *;
    protected *;
}

-keepclassmembers class * {
    @androidx.annotation.Keep *;
}
