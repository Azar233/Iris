#include "app/WorkspaceShaderPackage.h"
#include <rapidjson/document.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
std::string field(const rapidjson::Value& object, const char* name) {
    if (!object.IsObject() || !object.HasMember(name) || !object[name].IsString())
        throw std::runtime_error(std::string("Missing string: ") + name);
    return object[name].GetString();
}
std::string readText(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::file_size(path, error) > 1024U * 1024U || error)
        throw std::runtime_error("Cannot inspect text input (missing or above 1 MiB): " + path.u8string());
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read: " + path.u8string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
std::filesystem::path inside(const std::filesystem::path& root, const std::filesystem::path& base,
                             const std::string& value) {
    const auto path = std::filesystem::weakly_canonical(base / std::filesystem::u8path(value));
    const auto relative = path.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        throw std::runtime_error("Resource leaves package root: " + value);
    return path;
}
class Inspector {
public:
    WorkspaceShaderPackage result;
    std::size_t sourceVisits{0};
    void add(const std::filesystem::path& path, const std::string& role,
             const std::string& owner, bool execution = true) {
        auto found = std::find_if(result.files.begin(), result.files.end(), [&](const auto& f){return f.path == path;});
        if (found == result.files.end()) {
            if (result.files.size() >= 256) throw std::runtime_error("Inspection exceeds 256 files");
            WorkspaceShaderFile f; f.path=path; f.role=role; f.executionInput=execution;
            std::error_code error;
            f.missing=!std::filesystem::is_regular_file(path, error);
            if (!f.missing) {f.sizeBytes=std::filesystem::file_size(path,error); if(error) f.sizeBytes=0;}
            result.files.push_back(std::move(f)); found=std::prev(result.files.end());
        }
        if (std::find(found->owners.begin(), found->owners.end(), owner)==found->owners.end()) found->owners.push_back(owner);
    }
    void source(const std::filesystem::path& path, const std::string& owner,
                std::set<std::filesystem::path>& stack, int depth = 0) {
        if (++sourceVisits > 256) throw std::runtime_error("Inspection exceeds 256 include visits");
        add(path, depth==0?"Pass source":"include",owner);
        if (!std::filesystem::is_regular_file(path)) {result.diagnostics.push_back("Missing source: "+path.u8string()); return;}
        if (depth>8 || !stack.insert(path).second) {result.diagnostics.push_back("Include cycle/depth: "+path.u8string());return;}
        std::istringstream lines(readText(path)); std::string line; int number=0;
        while(std::getline(lines,line)) {
            if(sourceVisits>=256) {result.diagnostics.push_back("Inspection include visit limit reached");break;}
            ++number;
            const auto first=line.find_first_not_of(" \t");
            if(first==std::string::npos || line.compare(first,8,"#include")!=0) continue;
            const auto quote=line.find('"',first+8), end=quote==std::string::npos?quote:line.find('"',quote+1);
            if(quote==std::string::npos || end==std::string::npos) {result.diagnostics.push_back(path.u8string()+":"+std::to_string(number)+": malformed include");continue;}
            try {source(inside(result.root,path.parent_path(),line.substr(quote+1,end-quote-1)),owner,stack,depth+1);}
            catch(const std::exception& e){result.diagnostics.push_back(path.u8string()+":"+std::to_string(number)+": "+e.what());}
        }
        stack.erase(path);
    }
};
}
WorkspaceShaderPackage inspectWorkspaceShaderPackage(const std::filesystem::path& manifest) {
    Inspector index;
    index.result.manifest=std::filesystem::absolute(manifest).lexically_normal();
    index.result.root=index.result.manifest.parent_path();
    try {
        index.add(index.result.manifest,"Manifest","Package");
        rapidjson::Document json; const auto text=readText(index.result.manifest);
        json.Parse(text.data(),text.size());
        if(json.HasParseError() || !json.IsObject()) throw std::runtime_error("Invalid Manifest JSON");
        index.result.id=field(json,"id"); index.result.author=field(json,"author"); index.result.license=field(json,"license");
        const auto parent=std::filesystem::weakly_canonical(index.result.root);
        index.result.root=inside(parent,parent,field(json,"resourceRoot"));
        std::map<std::string,std::string> textureOwners;
        if(json.HasMember("passes") && json["passes"].IsArray()) {
            if(json["passes"].Size()>4) throw std::runtime_error("Inspection supports at most 4 Passes");
            for(const auto& pass:json["passes"].GetArray()) {
                const auto id=field(pass,"id"); std::set<std::filesystem::path> stack;
                index.source(inside(index.result.root,index.result.root,field(pass,"fragment")),"Pass "+id,stack);
                if(pass.HasMember("channels") && pass["channels"].IsArray()) {
                    if(pass["channels"].Size()>4) throw std::runtime_error("Inspection supports at most 4 Channels");
                    for(const auto& channel:pass["channels"].GetArray()) {
                        if(!channel.HasMember("slot") || !channel["slot"].IsInt()) throw std::runtime_error("Channel slot must be an integer");
                        auto& owners=textureOwners[field(channel,"source")];
                        if(!owners.empty()) owners+="; ";
                        owners+="Pass "+id+" / Channel "+std::to_string(channel["slot"].GetInt());
                    }
                }
            }
        } else {
            std::set<std::filesystem::path> stack;
            index.source(inside(index.result.root,index.result.root,field(json,"fragment")),"Pass Image",stack);
        }
        if(json.HasMember("textures") && json["textures"].IsArray()) {
            if(json["textures"].Size()>8) throw std::runtime_error("Inspection supports at most 8 textures");
            for(const auto& texture:json["textures"].GetArray()) {
                const auto id=field(texture,"id"); const auto owner="Texture "+id+" | "+textureOwners[id];
                if(field(texture,"encoding")=="hash-v1") index.result.generatedInputs.push_back(owner+" (hash-v1, no file)");
                else index.add(inside(index.result.root,index.result.root,field(texture,"path")),"Texture",owner);
            }
        }
        for(const auto& directory:{index.result.manifest.parent_path(),index.result.root})
            for(const auto* name:{"README.md","LICENSE","LICENSE.md","LICENSE.txt"}) {
                const auto path=directory/name;
                if(std::filesystem::is_regular_file(path)) index.add(path,"Documentation","Package (not an execution input)",false);
            }
    } catch(const std::exception& error) {index.result.diagnostics.push_back(error.what());}
    std::sort(index.result.files.begin(),index.result.files.end(),[](const auto& a,const auto& b){return a.path<b.path;});
    return std::move(index.result);
}
