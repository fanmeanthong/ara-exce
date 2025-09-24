// ExecManager.h - Simple in-process Execution Manager for demo
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <iostream>
#include <algorithm>

namespace ara {
namespace execm { // avoid name clash with ara::exec (ApplicationClient)

// Application states
enum class AppState { kRegistered, kRunning, kStopped, kTerminated, kCrashed };
// Restart policy
enum class RestartPolicy { kNo, kAlways, kOnFailure };

// Application configuration
struct AppConfig {
    std::string appId;                  // e.g. "RadarService"
    RestartPolicy policy{RestartPolicy::kOnFailure};
    int maxRestarts{-1};                // -1 = unlimited
    std::string defaultMode{"NormalMode"};
    std::vector<std::string> modes{"NormalMode","DiagnosticMode"};
};

// Application runtime state
struct AppRuntime {
    AppState state{AppState::kStopped};
    int restartCount{0};
    std::string activeMode;
    std::function<void()> startFn;      // provided by app
    std::function<void()> stopFn;       // provided by app
};

struct AppRegistration {
    AppConfig cfg;
    AppRuntime rt;
};

using StateListener = std::function<void(const std::string& appId, AppState)>;

// Main Execution Manager class
class ExecManager {
public:
    static ExecManager& Instance() {
        static ExecManager inst;
        return inst;
    }

    // Register app with policy, mode & start/stop callbacks
    bool Register(const AppConfig& cfg,
                  std::function<void()> startFn,
                  std::function<void()> stopFn) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = apps_.find(cfg.appId);
        if (it != apps_.end()) {
            std::cerr << "[ExecMgr][WARN] App already registered: " << cfg.appId << "\n";
            return false;
        }
        AppRegistration reg;
        reg.cfg = cfg;
        reg.rt.startFn = std::move(startFn);
        reg.rt.stopFn  = std::move(stopFn);
        reg.rt.activeMode = cfg.defaultMode;
        reg.rt.state = AppState::kRegistered;
        apps_.emplace(cfg.appId, std::move(reg));
        Notify(cfg.appId, AppState::kRegistered);
        return true;
    }

    // Set/validate App Mode; fallback to default if invalid
    void SetMode(const std::string& appId, const std::string& requested) {
        std::lock_guard<std::mutex> lk(mu_);
        auto* reg = Find(appId);
        if (!reg) return;
        if (reg->cfg.modes.empty() ||
            std::find(reg->cfg.modes.begin(), reg->cfg.modes.end(), requested) != reg->cfg.modes.end()) {
            reg->rt.activeMode = requested;
            return;
        }
        std::cerr << "[ExecMgr][WARN] APP_MODE=\"" << requested
                  << "\" is invalid for " << appId
                  << ". Fallback to \"" << reg->cfg.defaultMode << "\"\n";
        reg->rt.activeMode = reg->cfg.defaultMode;
    }

    std::string GetMode(const std::string& appId) {
        std::lock_guard<std::mutex> lk(mu_);
        auto* reg = Find(appId); if (!reg) return {};
        return reg->rt.activeMode;
    }

    // Start/Stop controlled by ExecManager, calls app callbacks
    void Start(const std::string& appId) {
        std::lock_guard<std::mutex> lk(mu_);
        auto* reg = Find(appId); if (!reg) return;
        if (reg->rt.state == AppState::kRunning) return;
        if (reg->rt.startFn) reg->rt.startFn();
        reg->rt.state = AppState::kRunning;
        Notify(appId, AppState::kRunning);
    }

    void Stop(const std::string& appId) {
        std::lock_guard<std::mutex> lk(mu_);
        auto* reg = Find(appId); if (!reg) return;
        if (reg->rt.stopFn) reg->rt.stopFn();
        reg->rt.state = AppState::kStopped;
        Notify(appId, AppState::kStopped);
    }

    // App reports crash → ExecManager decides restart based on policy/maxRestarts
    void OnCrash(const std::string& appId) {
        std::lock_guard<std::mutex> lk(mu_);
        auto* reg = Find(appId); if (!reg) return;
        reg->rt.state = AppState::kCrashed;
        Notify(appId, AppState::kCrashed);

        const bool allow = (reg->cfg.policy == RestartPolicy::kAlways) ||
                           (reg->cfg.policy == RestartPolicy::kOnFailure);
        const bool below_limit = (reg->cfg.maxRestarts < 0) ||
                                 (reg->rt.restartCount < reg->cfg.maxRestarts);

        if (allow && below_limit) {
            reg->rt.restartCount++;
            // restart = stop + start
            if (reg->rt.stopFn) reg->rt.stopFn();
            if (reg->rt.startFn) reg->rt.startFn();
            reg->rt.state = AppState::kRunning;
            Notify(appId, AppState::kRunning);
        } else {
            reg->rt.state = AppState::kTerminated;
            Notify(appId, AppState::kTerminated);
            std::cerr << "[ExecMgr] App " << appId << " terminated (policy/limit)\n";
        }
    }

    // Subscribe to state change events
    void Subscribe(StateListener cb) {
        std::lock_guard<std::mutex> lk(mu_);
        listeners_.push_back(std::move(cb));
    }

    AppState GetState(const std::string& appId) {
        std::lock_guard<std::mutex> lk(mu_);
        auto* reg = Find(appId); if (!reg) return AppState::kTerminated;
        return reg->rt.state;
    }

private:
    ExecManager() = default;

    AppRegistration* Find(const std::string& appId) {
        auto it = apps_.find(appId);
        return (it == apps_.end()) ? nullptr : &it->second;
    }

    // Notify all listeners of state change
    void Notify(const std::string& appId, AppState st) {
        for (auto& f : listeners_) {
            try { f(appId, st); } catch (...) {}
        }
    }

    std::mutex mu_;
    std::unordered_map<std::string, AppRegistration> apps_;
    std::vector<StateListener> listeners_;
};

// Convert AppState to string
inline const char* ToString(AppState s) {
    switch (s) {
        case AppState::kRegistered: return "Registered";
        case AppState::kRunning:    return "Running";
        case AppState::kStopped:    return "Stopped";
        case AppState::kTerminated: return "Terminated";
        case AppState::kCrashed:    return "Crashed";
    }
    return "Unknown";
}

// Parse restart policy from string
inline RestartPolicy ParsePolicy(const std::string& p) {
    if (p == "no")         return RestartPolicy::kNo;
    if (p == "always")     return RestartPolicy::kAlways;
    return RestartPolicy::kOnFailure; // default
}

} // namespace execm
} // namespace ara
