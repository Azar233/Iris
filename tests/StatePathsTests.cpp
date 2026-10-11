#include "app/StatePaths.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

int main() {
    const auto root = std::filesystem::temp_directory_path() / ("IrisStatePaths-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root / "legacy");
        const auto legacy = root / "legacy/MyRenderer.language";
        std::ofstream(legacy) << "en";
        const auto current = iris::migrateStateFile(root / "state", "language", legacy);
        std::string value; std::ifstream(current) >> value;
        if (value != "en" || !std::filesystem::exists(legacy)) throw std::runtime_error("Migration lost old preference");
        std::ofstream(current, std::ios::trunc) << "zh-CN";
        iris::migrateStateFile(root / "state", "language", legacy);
        std::ifstream(current) >> value;
        if (value != "zh-CN") throw std::runtime_error("Legacy preference overwrote Iris state");
        const auto missing = iris::migrateStateFile(root / "state", "recent-scene", root / "missing");
        if (std::filesystem::exists(missing)) throw std::runtime_error("Absent state created bogus file");
        bool rejected = false;
        try { iris::migrateStateFile(root / "state", "../outside"); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("State name escaped directory");
        std::filesystem::remove_all(root);
        std::cout << "Iris state migration / existing preference priority / missing input / leaf boundary PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(root); std::cerr << error.what() << '\n'; return 1;
    }
}
