#include "SomeipBinding.h"   // Proxy/Skeleton SOME/IP
#include "AraExec.h"         // ara::exec::ApplicationClient (đã chuẩn hoá)
#include "ExecManager.h"     // ExecManager mô phỏng: policy/mode/restart

#include <iostream>
#include <thread>
#include <fstream>
#include <cstdlib>
#include <vector>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace ara;

//---------------- Manifest model & loader ----------------//
struct ManifestCfg {
    std::string appName       = "RadarServiceApp";
    std::string exeName       = "RadarService";
    std::string defaultMode   = "NormalMode";
    std::string restartPolicy = "on-failure";   // always | on-failure | no
    int         maxRestarts   = -1;             // -1 = unlimited (demo)
    std::vector<std::string> modes{"NormalMode","DiagnosticMode"};
};
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
static ManifestCfg LoadManifest(const std::string& path) {
    ManifestCfg cfg;
    try {
        std::ifstream f(path);
        if (!f) {
            std::cerr << "[Manifest] Cannot open file: " << path
                      << " (using default values)\n";
            return cfg;
        }
        json j; f >> j;
        auto& m = j.at("applicationManifest");
        if (m.contains("name")) cfg.appName = m["name"].get<std::string>();
        if (m.contains("executables") && m["executables"].is_array() && !m["executables"].empty()) {
            const auto& exe = m["executables"][0];
            if (exe.contains("name")) cfg.exeName = exe["name"].get<std::string>();
        }
        if (m.contains("defaultMode"))    cfg.defaultMode   = m["defaultMode"].get<std::string>();
        if (m.contains("restartPolicy"))  cfg.restartPolicy = m["restartPolicy"].get<std::string>();
        if (m.contains("maxRestarts"))    cfg.maxRestarts   = m["maxRestarts"].get<int>();

        if (m.contains("applicationModeDeclarations") && m["applicationModeDeclarations"].is_array()) {
            cfg.modes.clear();
            for (const auto& item : m["applicationModeDeclarations"]) {
                if (item.contains("name") && item["name"].is_string()) {
                    cfg.modes.push_back(item["name"].get<std::string>());
                }
            }
            if (cfg.modes.empty()) cfg.modes = {"NormalMode","DiagnosticMode"};
        }
    } catch (const std::exception& e) {
        std::cerr << "[Manifest] Parse error: " << e.what()
                  << " (using default values)\n";
    }
    return cfg;
}

static std::string pick_manifest_path(int argc, char** argv) {
    if (argc > 1) return argv[1];
    if (const char* p = std::getenv("RADAR_MANIFEST")) return std::string(p);
    return "./manifest.json";
}

//---------------- Server main ----------------//
int main(int argc, char** argv) {
    // 1) Read manifest
    const std::string manifestPath = pick_manifest_path(argc, argv);
    ManifestCfg manifest = LoadManifest(manifestPath);

    // 2) Get requested APP_MODE (may be invalid) and let ExecManager validate
    std::string requestedMode = manifest.defaultMode;
    if (const char* envMode = std::getenv("APP_MODE")) {
        requestedMode = envMode;
    }

    // 3) Create ApplicationClient (ara::exec) for this app
    exec::ApplicationClient appCli(manifest.exeName,
        /*autoRestart flag is not used here, ExecManager will control*/ true);

    appCli.RegisterApplication();
    appCli.SetStopHandler([] {
        std::cout << "[RadarService] Cleanup before stop...\n";
    });

    // 4) Register with ExecManager (policy/mode/restart)
    using ara::execm::ExecManager;
    using ara::execm::AppConfig;
    using ara::execm::ParsePolicy;

    AppConfig cfg;
    cfg.appId       = manifest.exeName;                     // "RadarService"
    cfg.policy      = ParsePolicy(manifest.restartPolicy);  // always / on-failure / no
    cfg.maxRestarts = manifest.maxRestarts;                 // -1 = unlimited
    cfg.defaultMode = manifest.defaultMode;
    cfg.modes       = manifest.modes;

    auto& em = ExecManager::Instance();

    em.Register(cfg,
        /*startFn*/ [&](){ appCli.Start(); },    // EM calls Start → delegate to ApplicationClient
        /*stopFn*/  [&](){ appCli.Stop();  }     // EM calls Stop  → delegate to ApplicationClient
    );

    // Validate & set mode (fallback if APP_MODE is invalid)
    em.SetMode(cfg.appId, requestedMode);
    const std::string activeMode = em.GetMode(cfg.appId);
    const bool isDiagnostic = (activeMode == "DiagnosticMode");

    // Subscribe to state events (compact log)
    em.Subscribe([](const std::string& id, ara::execm::AppState st){
        std::cout << "[ExecMgr][Event] " << id << " -> " << ara::execm::ToString(st) << "\n";
    });

    std::cout << "[Manifest] name="   << manifest.appName
              << ", exe="             << manifest.exeName
              << ", defaultMode="     << manifest.defaultMode
              << ", activeMode="      << activeMode
              << ", restartPolicy="   << manifest.restartPolicy
              << ", maxRestarts="     << manifest.maxRestarts << "\n";

    // 5) Start by ExecManager
    em.Start(cfg.appId);

    // 6) SOME/IP service (offer & handle)
    com::SomeipSkeleton skeleton(manifest.exeName,
        [&](const com::Message& msg) {
            try {
                std::string cfgStr(msg.payload.begin(), msg.payload.end());
                std::cout << "[Server] Calibrate called with: " << cfgStr
                          << " (mode=" << activeMode << ")\n";

                // Diagnostic: read-only, calibration disabled (unless testing crash)
                if (isDiagnostic && cfgStr != "CrashMe") {
                    std::string resp = "DIAG-ONLY: Calibration disabled in DiagnosticMode";
                    com::Message m{msg.method, std::vector<uint8_t>(resp.begin(), resp.end())};
                    skeleton.SendResponse(m);
                    return;
                }

                // Intentional error for testing → report crash to ExecManager (EM will decide restart/terminate)
                if (cfgStr == "CrashMe") {
                    throw std::runtime_error("💥 Simulated crash in RadarService");
                }

                // Normal: handle calibration
                std::string resp = "Calibrated OK: " + cfgStr;
                com::Message m{msg.method, std::vector<uint8_t>(resp.begin(), resp.end())};
                skeleton.SendResponse(m);
            } catch (...) {
                ExecManager::Instance().OnCrash(manifest.exeName);
            }
        });

    skeleton.OfferService();

    // 7) Keep process alive
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
