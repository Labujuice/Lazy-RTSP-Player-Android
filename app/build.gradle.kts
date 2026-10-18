import com.android.build.gradle.internal.api.BaseVariantOutputImpl
import java.io.File

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val verChangeLogFile = rootProject.file("VerChangeLog.md")
val currentVersion = if (verChangeLogFile.exists()) {
    verChangeLogFile.useLines { lines ->
        val firstLine = lines.firstOrNull() ?: "# Current Version: 1.0.0"
        firstLine.replace("# Current Version:", "").trim()
    }
} else {
    "1.0.0"
}

// SemVer (X.Y.Z) to versionCode: X * 10000 + Y * 100 + Z
val versionParts = currentVersion.split(".")
val computedVersionCode = if (versionParts.size == 3) {
    versionParts[0].toInt() * 10000 + versionParts[1].toInt() * 100 + versionParts[2].toInt()
} else {
    10000
}

android {
    namespace = "com.kenny.lazyplayer"
    compileSdk = 34

    // Dynamically pick installed NDK if available in SDK path
    val sdkDir = System.getenv("ANDROID_SDK_ROOT") ?: System.getenv("ANDROID_HOME") ?: "/opt/android-sdk"
    val installedNdkDir = File(sdkDir, "ndk")
    if (installedNdkDir.exists() && installedNdkDir.list()?.isNotEmpty() == true) {
        ndkVersion = installedNdkDir.list()!!.sortedDescending().first()
    }

    defaultConfig {
        applicationId = "com.kenny.lazyplayer"
        minSdk = 24
        targetSdk = 34
        versionCode = computedVersionCode
        versionName = currentVersion

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                cppFlags("-std=c++17 -Wall -Wextra -O3")
                arguments(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_TOOLCHAIN=clang"
                )
            }
        }

        ndk {
            abiFilters.addAll(listOf("arm64-v8a", "armeabi-v7a", "x86_64"))
        }

        applicationVariants.all {
            val variantName = buildType.name
            outputs.all {
                val output = this as BaseVariantOutputImpl
                output.outputFileName = if (variantName == "release") {
                    "Lazy_RTSP_Player_${currentVersion}.apk"
                } else {
                    "Lazy_RTSP_Player_${currentVersion}_${variantName}.apk"
                }
            }
        }
    }

    signingConfigs {
        create("release") {
            storeFile = file("keystore/release.keystore")
            storePassword = "lazyplayer123"
            keyAlias = "lazyplayer"
            keyPassword = "lazyplayer123"
            enableV1Signing = true
            enableV2Signing = true
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            signingConfig = signingConfigs.getByName("release")
            buildConfigField("boolean", "DEFAULT_OSD_VISIBLE", "false")
            buildConfigField("int", "LOG_LEVEL", "4") // LOG_LEVEL_ERROR
        }
        debug {
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-DEBUG"
            signingConfig = signingConfigs.getByName("release")
            buildConfigField("boolean", "DEFAULT_OSD_VISIBLE", "true")
            buildConfigField("int", "LOG_LEVEL", "1") // LOG_LEVEL_VERBOSE
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlin {
        compilerOptions {
            jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
        }
    }

    buildFeatures {
        viewBinding = true
        buildConfig = true
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.0")

    testImplementation("junit:junit:4.13.2")
}
