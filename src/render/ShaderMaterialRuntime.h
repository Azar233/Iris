#pragma once
#include "render/RenderItem.h"
#include "asset/InputWatch.h"
#include <map>
#include <memory>

class Shader;
class Camera;
struct RendererSettings;
class ShaderMaterialRuntime {
public:
    explicit ShaderMaterialRuntime(std::filesystem::path shaders);
    ~ShaderMaterialRuntime();
    bool prepare(const std::vector<RenderItem>&, const RendererSettings&, bool retainFailure, bool retry=false, bool pollInputs=true);
    Shader& bind(const RenderItem&, const Camera&, const RendererSettings&, int width, int height);
    const iris::ShaderScenePackage* package(const std::string&) const;
    const std::string& error() const { return error_; }
    bool empty() const { return entries_.empty(); }
private:
    struct Entry;
    std::filesystem::path vertex_;
    std::map<std::string,std::unique_ptr<Entry>> entries_;
    std::vector<capture::InputStamp> watched_;
    std::string error_;
    std::string valuesKey_;
};
