// AraExec.h
#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>


/**
 * Standardized: ara::exec instead of ara::excev.
 * - API remains the same as previous ApplicationClient.
 * - Macro EXCEV_SIMULATE_CRASH for demo auto-crash after 5s (off by default).
 * - ara::excev alias for backward compatibility.
 */
namespace ara {
namespace exec {

// Application states
enum class AppState { kRegistered, kRunning, kStopped, kTerminated, kCrashed };

class ApplicationClient {
    std::string app_id_;
    std::function<void()> stop_handler_;
    std::atomic<AppState> state_{AppState::kStopped};
    std::thread monitor_thread_;
    bool auto_restart_{true};

public:
    explicit ApplicationClient(const std::string& appId, bool autoRestart = true)
        : app_id_(appId), auto_restart_(autoRestart) {}

    // Register the application
    bool RegisterApplication() {
        std::cout << "[ExecM] Register app: " << app_id_ << std::endl;
        state_ = AppState::kRegistered;
        return true;
    }

    // Set stop handler callback
    void SetStopHandler(std::function<void()> cb) { stop_handler_ = std::move(cb); }

    // Start the application
    void Start() {
        std::cout << "[ExecM] Start app: " << app_id_ << std::endl;
        state_ = AppState::kRunning;

#if defined(EXCEV_SIMULATE_CRASH)
        // Demo: auto-crash after 5s to demonstrate RestartPolicy
        monitor_thread_ = std::thread([this]() {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(5s);
            if (state_ == AppState::kRunning) {
                std::cerr << "[ExecM] Crash detected in app " << app_id_ << std::endl;
                state_ = AppState::kCrashed;
                if (auto_restart_) Restart();
            }
        });
        monitor_thread_.detach();
#endif
    }

    // Stop the application
    void Stop() {
        std::cout << "[ExecM] Stop app: " << app_id_ << std::endl;
        state_ = AppState::kStopped;
        if (stop_handler_) stop_handler_();
    }

    // Restart the application
    void Restart() {
        std::cout << "[ExecM] Restarting app: " << app_id_ << std::endl;
        Stop();
        Start();
    }

    // Allow intentional crash from business logic thread
    void Crash() {
        std::cerr << "[ExecM] Crash detected in " << app_id_ << std::endl;
        state_ = AppState::kCrashed;
        if (auto_restart_) Restart();
    }
    // NEW: Check if APP_MODE is in modes; if not, warn & fallback
    static std::string ValidateMode(const std::string& requested,
                                    const std::string& fallback,
                                    const std::vector<std::string>& modes) {
        if (requested.empty()) return fallback;

        // If manifest has no mode list, allow any value to avoid blocking
        if (modes.empty()) return requested;

        for (const auto& m : modes) {
            if (m == requested) return requested;
        }
        std::cerr << "[Manifest][WARN] APP_MODE=\"" << requested
                << "\" is not in applicationModeDeclarations. "
                << "Fallback to defaultMode=\"" << fallback << "\"\n";
        return fallback;
    }

    // Get current application state
    AppState GetState() const { return state_; }
};

} // namespace exec

// ====== Backward compatibility: ara::excev is alias of ara::exec ======
namespace excev {
using AppState = ::ara::exec::AppState;
using ApplicationClient = ::ara::exec::ApplicationClient;
} // namespace excev

} // namespace ara
