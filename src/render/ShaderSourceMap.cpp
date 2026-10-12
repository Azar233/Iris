#include "render/ShaderSourceMap.h"
#include <regex>
#include <sstream>

std::vector<ShaderSourceDiagnostic> mapShaderDiagnostics(const std::string& log,const ShaderSourceMap& map) {
    static const std::regex nvidia(R"(^\s*(\d+)\((\d+)\)\s*:\s*(.*)$)");
    static const std::regex colon(R"(^\s*(?:ERROR:\s*|WARNING:\s*)?(\d+):(\d+)(?:\(\d+\))?:\s*(.*)$)");
    std::vector<ShaderSourceDiagnostic> result; std::istringstream stream(log);std::string line;
    while(std::getline(stream,line)) {
        if(line.empty())continue;
        ShaderSourceDiagnostic item;item.pass=map.pass;item.message=line;
        std::smatch match;
        if(std::regex_match(line,match,nvidia) || std::regex_match(line,match,colon)) {
            try {
                const auto source=std::stoul(match[1].str()), number=std::stoul(match[2].str());
                if(source<map.locations.size()) {
                    item.file=map.locations[source].file;item.generated=map.locations[source].generated;item.mapped=true;
                    if(number<=2147483647UL)item.line=static_cast<int>(number);
                    item.message=match[3].str();
                }
            } catch(const std::exception&) {} // Unknown/overflowed logs remain raw.
        }
        result.push_back(std::move(item));
    }
    return result;
}
std::string formatShaderDiagnostics(const std::vector<ShaderSourceDiagnostic>& diagnostics) {
    std::ostringstream out;
    for(const auto& d:diagnostics) {
        out<<"Pass "<<(d.pass.empty()?"(unknown)":d.pass)<<" | ";
        if(d.mapped)out<<(d.generated?"Generated ":"")<<d.file.generic_u8string()<<":"<<(d.line>0?std::to_string(d.line):"unknown line");
        else out<<"Unmapped driver diagnostic";
        out<<" | "<<d.message<<'\n';
    }
    return out.str();
}
ShaderSourceError::ShaderSourceError(const std::filesystem::path& path,const std::string& log,const ShaderSourceMap& map)
    :std::runtime_error("Shader compile failed ("+path.u8string()+"):\n"+formatShaderDiagnostics(mapShaderDiagnostics(log,map))+"Raw driver log:\n"+log),
     sources(map),diagnostics(mapShaderDiagnostics(log,map)) {}
