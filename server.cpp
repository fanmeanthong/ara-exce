#include "SomeipBinding.h"
#include "AraExec.h"   // dùng ara::exec

#include <iostream>
#include <thread>
#include <fstream>
#include <cstdlib>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace ara;

struct ManifestCfg {
    std::string appName      = "RadarServiceApp";
    std::string exeName      = "RadarService";
    std::string defaultMode  = "NormalMode";
    std::string restartPolicy = "on-failure"; // always | on-failure | no
};

static ManifestCfg LoadManifest(const std::string& path) {
    ManifestCfg cfg;
    try {
        std::ifstream f(path);
        if (!f) {
            std::cerr << "[Manifest] Không mở được file: " << path
                      << " (dùng giá trị mặc định)\n";
            return cfg;
        }
        json j; f >> j;
        auto& m = j.at("applicationManifest");
        if (m.contains("name")) cfg.appName = m["name"].get<std::string>();
        if (m.contains("executables") && m["executables"].is_array() && !m["executables"].empty()) {
            const auto& exe = m["executables"][0];
            if (exe.contains("name")) cfg.exeName = exe["name"].get<std::string>();
        }
        if (m.contains("defaultMode"))   cfg.defaultMode  = m["defaultMode"].get<std::string>();
        if (m.contains("restartPolicy")) cfg.restartPolicy = m["restartPolicy"].get<std::string>();
    } catch (const std::exception& e) {
        std::cerr << "[Manifest] Lỗi parse: " << e.what()
                  << " (dùng giá trị mặc định)\n";
    }
    return cfg;
}

static std::string pick_manifest_path(int argc, char** argv) {
    if (argc > 1) return argv[1];
    if (const char* p = std::getenv("RADAR_MANIFEST")) return std::string(p);
    return "./manifest.json";
}

static bool to_auto_restart(const std::string& policy) {
    if (policy == "always")     return true;
    if (policy == "on-failure") return true;
    if (policy == "no")         return false;
    return true; // mặc định
}

int main(int argc, char** argv) {
    // 1) Đọc manifest
    const std::string manifestPath = pick_manifest_path(argc, argv);
    ManifestCfg manifest = LoadManifest(manifestPath);

    // 2) Lấy App Mode thực thi: ưu tiên APP_MODE, fallback defaultMode
    std::string activeMode = manifest.defaultMode;
    if (const char* envMode = std::getenv("APP_MODE")) {
        activeMode = envMode;
    }
    const bool isDiagnostic = (activeMode == "DiagnosticMode");

    // 3) Xác định autoRestart từ RestartPolicy
    const bool autoRestart = to_auto_restart(manifest.restartPolicy);

    std::cout << "[Manifest] name=" << manifest.appName
              << ", exe=" << manifest.exeName
              << ", defaultMode=" << manifest.defaultMode
              << ", activeMode=" << activeMode
              << ", restartPolicy=" << manifest.restartPolicy
              << ", autoRestart=" << std::boolalpha << autoRestart << "\n";

    // 4) Khởi tạo "ara::exec" mô phỏng
    exec::ApplicationClient execCli(manifest.exeName, autoRestart);
    execCli.RegisterApplication();
    execCli.SetStopHandler([] {
        std::cout << "[RadarService] Cleanup before stop...\n";
    });
    execCli.Start();

    // 5) Khởi tạo SOME/IP service
    com::SomeipSkeleton skeleton(manifest.exeName,
        [&](const com::Message& msg) {
            try {
                std::string cfgStr(msg.payload.begin(), msg.payload.end());
                std::cout << "[Server] Calibrate called with: " << cfgStr
                          << " (mode=" << activeMode << ")\n";

                // Diagnostic: chỉ trả read-only, không hiệu chuẩn
                if (isDiagnostic && cfgStr != "CrashMe") {
                    std::string resp = "DIAG-ONLY: Calibration disabled in DiagnosticMode";
                    com::Message m{msg.method, std::vector<uint8_t>(resp.begin(), resp.end())};
                    skeleton.SendResponse(m);
                    return;
                }

                // Mô phỏng lỗi nghiệp vụ để test RestartPolicy
                if (cfgStr == "CrashMe") {
                    throw std::runtime_error("💥 Simulated crash in RadarService");
                }

                // Normal: xử lý hiệu chuẩn
                std::string resp = "Calibrated OK: " + cfgStr;
                com::Message m{msg.method, std::vector<uint8_t>(resp.begin(), resp.end())};
                skeleton.SendResponse(m);
            } catch (...) {
                // Báo crash cho "ExecM" (mô phỏng) -> restart theo policy
                execCli.Crash();
            }
        });

    skeleton.OfferService();

    // 6) Giữ tiến trình
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
