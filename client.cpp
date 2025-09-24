#include "SomeipBinding.h"
#include "AraExec.h"      // ara::exec::ApplicationClient (client does not auto-restart)
#include <iostream>
#include <thread>
#include <fstream>
#include <cstdlib>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace ara;

// Lightweight manifest reader for hint (not required for client)
struct ManifestView {
    std::string appName     = "RadarServiceApp";
    std::string defaultMode = "NormalMode";
};

static ManifestView LoadManifestLite(const std::string& path) {
    ManifestView v;
    try {
        std::ifstream f(path);
        if (!f) return v;
        json j; f >> j;
        auto& m = j.at("applicationManifest");
        if (m.contains("name"))        v.appName     = m["name"].get<std::string>();
        if (m.contains("defaultMode")) v.defaultMode = m["defaultMode"].get<std::string>();
    } catch (...) { /* optional */ }
    return v;
}

static std::string pick_manifest_path(int argc, char** argv) {
    if (argc > 1) return argv[1];
    if (const char* p = std::getenv("RADAR_MANIFEST")) return std::string(p);
    return "./manifest.json";
}

int main(int argc, char** argv) {
    // Print manifest hint (optional)
    const std::string manifestPath = pick_manifest_path(argc, argv);
    ManifestView mv = LoadManifestLite(manifestPath);
    std::cout << "[Client] Service manifest hint: name=" << mv.appName
              << ", defaultMode=" << mv.defaultMode << "\n";

    // Register client with Execution API (no auto-restart)
    exec::ApplicationClient appCli("RadarClient", false);
    appCli.RegisterApplication();
    appCli.Start();

    // SOME/IP proxy: find & call service
    com::SomeipProxy proxy("RadarClient");
    proxy.FindService(RADAR_INSTANCE_ID);

    proxy.RegisterResponseHandler(CALIBRATE_METHOD_ID, [](const std::vector<uint8_t>& data) {
        std::string resp(data.begin(), data.end());
        std::cout << "[Client] Received response: " << resp << std::endl;
    });

    // Send a normal request
    std::thread t1([&] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::string req = "Config_X";
        proxy.MethodCall(CALIBRATE_METHOD_ID, std::vector<uint8_t>(req.begin(), req.end()));
    });

    // After 5s, send a request that causes server crash to see EM restart
    std::thread t2([&] {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::string req = "CrashMe";
        proxy.MethodCall(CALIBRATE_METHOD_ID, std::vector<uint8_t>(req.begin(), req.end()));
    });

    t1.join();
    t2.join();

    // Keep client alive to observe server re-offer after restart
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
