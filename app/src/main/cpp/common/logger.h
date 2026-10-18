#pragma once

#include <android/log.h>

#define LOG_TAG "UltraLowLatencyPlayer"

#ifndef LOG_LEVEL
#define LOG_LEVEL 1 // 1: VERBOSE, 2: DEBUG, 3: INFO, 4: ERROR
#endif

#if LOG_LEVEL <= 1
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#else
#define LOGV(...) ((void)0)
#endif

#if LOG_LEVEL <= 2
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#define LOGD(...) ((void)0)
#endif

#if LOG_LEVEL <= 3
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#define LOGW(...) ((void)0)
#endif

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
