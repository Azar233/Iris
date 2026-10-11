#include "plugin/ShaderScene.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / "IrisShaderSceneCpuTests";
        std::filesystem::create_directories(root);
        std::ofstream(root / "image.frag") << "#version 330 core\nvoid main(){}\n";
        const std::string valid = R"({"version":1,"id":"test","resourceRoot":".","fragment":"image.frag","entry":"main","output":"display-rgba8","camera":"project","author":"test","license":"MIT","parameters":[{"id":"gain","label":"Gain","type":"float","default":0.5,"min":0,"max":1}]})";
        const auto path = root / "test.shaderscene";
        const auto write = [&](const std::string& input) { std::ofstream(path, std::ios::trunc) << input; };
        iris::ShaderScenePackage package; std::string error;
        write(valid); check(iris::loadShaderScenePackage(path, package, error), error.c_str());
        check(package.parameters.value("gain")->number == 0.5f, "Float default missing");
        const auto reject = [&](std::string input) {
            write(input); check(!iris::loadShaderScenePackage(path, package, error), "Invalid package accepted");
            check(package.id == "test" && package.parameters.size() == 1, "Failed load replaced live package");
            check(!error.empty(), "Missing rejection diagnostic");
        };
        const auto replace = [&](const std::string& from, const std::string& to) {
            auto changed = valid; changed.replace(changed.find(from), from.size(), to); reject(changed);
        };
        replace("\"version\":1", "\"version\":2");
        replace("\"version\":1", "\"version\":1,\"version\":1");
        replace("\"version\":1", "\"version\":1,\"passes\":[]");
        replace("image.frag", "missing.frag");
        replace("image.frag", "../outside.frag");
        replace("\"resourceRoot\":\".\"", "\"resourceRoot\":\"..\"");
        replace("\"default\":0.5", "\"default\":2");
        replace("\"type\":\"float\"", "\"type\":\"color\"");
        replace("\"id\":\"gain\"", "\"id\":\"irisTime\"");
        replace("\"entry\":\"main\"", "\"entry\":\"mainImage\"");
        replace("\"output\":\"display-rgba8\"", "\"output\":\"hdr\"");
        reject("{");
        write(valid); check(iris::loadShaderScenePackage(path, package, error), "Recovery failed");
        std::filesystem::remove_all(root);
        std::cout << "ShaderScene version, strict fields, paths, parameter range and transactional recovery passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
