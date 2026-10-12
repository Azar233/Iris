#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

struct ShaderSourceLocation {
    std::filesystem::path file;
    bool generated{false};
};
struct ShaderSourceMap {
    std::string pass;
    std::filesystem::path root;
    std::vector<ShaderSourceLocation> locations;
    int add(const std::filesystem::path& file,bool generated=false) {
        const auto normalized=generated?file:std::filesystem::weakly_canonical(file);
        for(std::size_t i=0;i<locations.size();++i)
            if(locations[i].file==normalized && locations[i].generated==generated)return static_cast<int>(i);
        locations.push_back({normalized,generated});return static_cast<int>(locations.size()-1);
    }
};
struct ShaderSourceDiagnostic {
    std::string pass,message;
    std::filesystem::path file;
    int line{0}; // Zero is explicitly unknown, never guessed from EOF.
    bool generated{false},mapped{false};
};
inline const char* shaderMainImageAdapter() {
    return "// Iris generated mainImage adapter\n"
        "layout(location = 0) out vec4 irisMainImageOutput;\n"
        "void main() {\n"
        "    vec4 irisMainImageColor = vec4(0.0);\n"
        "    mainImage(irisMainImageColor, gl_FragCoord.xy);\n"
        "    irisMainImageOutput = irisMainImageColor;\n"
        "}\n";
}
std::vector<ShaderSourceDiagnostic> mapShaderDiagnostics(const std::string& log,const ShaderSourceMap& map);
std::string formatShaderDiagnostics(const std::vector<ShaderSourceDiagnostic>& diagnostics);
class ShaderSourceError : public std::runtime_error {
public:
    ShaderSourceMap sources;
    std::vector<ShaderSourceDiagnostic> diagnostics;
    ShaderSourceError(const std::filesystem::path& path,const std::string& log,const ShaderSourceMap& map);
};
