#include "signal_handler.h"
#include "logger.h"
#include <cstring>
#include <unistd.h>

namespace player {

std::atomic<bool> SignalHandler::s_crashed{false};
CrashCallback SignalHandler::s_crashCallback = nullptr;
struct sigaction SignalHandler::s_oldSegv{};
struct sigaction SignalHandler::s_oldBus{};
struct sigaction SignalHandler::s_oldAbrt{};
struct sigaction SignalHandler::s_oldFpe{};

void SignalHandler::init() {
    struct sigaction sa{};
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handleSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    sigaction(SIGSEGV, &sa, &s_oldSegv);
    sigaction(SIGBUS, &sa, &s_oldBus);
    sigaction(SIGABRT, &sa, &s_oldAbrt);
    sigaction(SIGFPE, &sa, &s_oldFpe);
    LOGI("SignalHandler initialized for crash isolation (SIGSEGV, SIGBUS, SIGABRT, SIGFPE)");
}

void SignalHandler::setCrashCallback(CrashCallback callback) {
    s_crashCallback = std::move(callback);
}

bool SignalHandler::hasEncounteredCrash() {
    return s_crashed.load();
}

void SignalHandler::resetCrashFlag() {
    s_crashed.store(false);
}

void SignalHandler::handleSignal(int sig, siginfo_t* info, void* /*context*/) {
    s_crashed.store(true);
    const char* sigName = "UNKNOWN";
    if (sig == SIGSEGV) sigName = "SIGSEGV";
    else if (sig == SIGBUS) sigName = "SIGBUS";
    else if (sig == SIGABRT) sigName = "SIGABRT";
    else if (sig == SIGFPE) sigName = "SIGFPE";

    void* faultAddr = info ? info->si_addr : nullptr;
    LOGE("CRASH DETECTED! Intercepted signal %s (%d) at address %p", sigName, sig, faultAddr);

    if (s_crashCallback) {
        s_crashCallback(sig, sigName);
    }

    // Call previous handler or exit safely if unrecoverable
    // For hardware codec crashes, fallback callback can reinitialize software decoder.
    if (sig == SIGSEGV && s_oldSegv.sa_sigaction) {
        s_oldSegv.sa_sigaction(sig, info, nullptr);
    } else if (sig == SIGBUS && s_oldBus.sa_sigaction) {
        s_oldBus.sa_sigaction(sig, info, nullptr);
    } else if (sig == SIGABRT && s_oldAbrt.sa_sigaction) {
        s_oldAbrt.sa_sigaction(sig, info, nullptr);
    } else {
        _exit(1);
    }
}

} // namespace player
