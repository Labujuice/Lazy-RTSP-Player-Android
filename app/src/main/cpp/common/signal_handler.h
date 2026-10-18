#pragma once

#include <csignal>
#include <functional>
#include <atomic>

namespace player {

using CrashCallback = std::function<void(int sig, const char* reason)>;

class SignalHandler {
public:
    static void init();
    static void setCrashCallback(CrashCallback callback);
    static bool hasEncounteredCrash();
    static void resetCrashFlag();

private:
    static void handleSignal(int sig, siginfo_t* info, void* context);
    static std::atomic<bool> s_crashed;
    static CrashCallback s_crashCallback;
    static struct sigaction s_oldSegv;
    static struct sigaction s_oldBus;
    static struct sigaction s_oldAbrt;
    static struct sigaction s_oldFpe;
};

} // namespace player
