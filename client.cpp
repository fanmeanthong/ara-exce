#include "SomeipBinding.h"
#include "AraExec.h"  // dùng ara::exec

#include <iostream>
#include <thread>
#include <fstream>
#include <cstdlib>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace ara;

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
    } catch (...) { /* optional manifest for client */ }
    return v;
}

static std::string pick_manifest_path(int argc, char** argv) {
    if (argc > 1) return argv[1];
    if (const char* p = std::getenv("RADAR_MANIFEST")) return std::string(p);
    return "./manifest.json";
}

int main(int argc, char** argv) {
    // Đọc manifest để in thông tin (không bắt buộc)
    const std::string manifestPath = pick_manifest_path(argc, argv);
    ManifestView mv = LoadManifestLite(manifestPath);
    std::cout << "[Client] Service manifest hint: name=" << mv.appName
              << ", defaultMode=" << mv.defaultMode << "\n";

    // AppClient của client: không tự restart
    exec::ApplicationClient execCli("RadarClient", false);
    execCli.RegisterApplication();
    execCli.Start();

    // SOME/IP proxy
    com::SomeipProxy proxy("RadarClient");
    proxy.FindService(RADAR_INSTANCE_ID);

    proxy.RegisterResponseHandler(CALIBRATE_METHOD_ID, [](const std::vector<uint8_t>& data) {
        std::string resp(data.begin(), data.end());
        std::cout << "[Client] Received response: " << resp << std::endl;
    });

    // Gửi request bình thường
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::string req = "Config_X";
        proxy.MethodCall(CALIBRATE_METHOD_ID, std::vector<uint8_t>(req.begin(), req.end()));
    });

    // Gửi request gây crash server sau 5s
    std::thread t2([&] {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::string req = "CrashMe";
        proxy.MethodCall(CALIBRATE_METHOD_ID, std::vector<uint8_t>(req.begin(), req.end()));
    });

    t.join();
    t2.join();

    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
