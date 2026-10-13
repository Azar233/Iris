#include "plugin/ShaderScene.h"
#include "plugin/ShaderMaterial.h"
#include "render/Renderer.h"
#include "asset/InputWatch.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>

void check(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
void testShaderSceneTexturesCpu(const std::filesystem::path&);
int main(int argc, char** argv) {
    try {
        const auto root = std::filesystem::temp_directory_path() / "IrisShaderSceneCpuTests";
        iris::ShaderSceneHistoryPolicy policy;bool policyChanged=false;std::string policyError;
        check(iris::updateShaderSceneHistoryPolicy(policy,policy,policyChanged,policyError) && !policyChanged,"No-op history policy changed");
        for(const float invalid:{0.f,-1.f,11.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            auto requested=policy;requested.maximumTimeStep=invalid;
            check(!iris::updateShaderSceneHistoryPolicy(policy,requested,policyChanged,policyError) && !policyChanged
                && policy==iris::ShaderSceneHistoryPolicy{},"Invalid history policy mutated settings");
        }
        check(iris::updateShaderSceneHistoryPolicy(policy,{false,false,.001f},policyChanged,policyError) && policyChanged,"Minimum history step rejected");
        check(iris::updateShaderSceneHistoryPolicy(policy,{true,true,10.f},policyChanged,policyError) && policyChanged,"Maximum history step rejected");
        std::filesystem::create_directories(root);
        std::ofstream(root / "image.frag") << "#version 330 core\nvoid main(){}\n";
        const std::string valid = R"({"version":1,"id":"test","resourceRoot":".","fragment":"image.frag","entry":"main","output":"display-rgba8","camera":"project","author":"test","license":"MIT","parameters":[{"id":"gain","label":"Gain","type":"float","default":0.5,"min":0,"max":1}]})";
        const auto path = root / "test.shaderscene";
        const auto missing = root / "watch-missing.raw";
        std::filesystem::remove(missing);
        {
            capture::InputWatch outer;
            {
                capture::InputWatch inner;
                capture::watchInput(missing); capture::watchInput(missing);
                check(inner.paths.size() == 1 && outer.paths.size() == 1, "Nested dependency watch/dedupe failed");
            }
            check(capture::activeInputWatch == &outer && !outer.stamps[0].changed(), "Dependency watch scope not restored");
            std::ofstream(missing) << "created";
            check(outer.stamps[0].changed(), "Missing input creation not detected");
            const auto created = capture::InputStamp::read(missing);
            std::ofstream(missing) << "different size";
            check(created.changed(), "Input byte count change not detected");
            std::filesystem::remove(missing);
            check(created.changed(), "Input deletion not detected");
        }
        check(capture::activeInputWatch == nullptr, "Dependency watch escaped scope");
        const auto write = [&](const std::string& input) { std::ofstream(path, std::ios::trunc) << input; };
        iris::ShaderScenePackage package; std::string error;
        write(valid); check(iris::loadShaderScenePackage(path, package, error), error.c_str());
        check(package.parameters.value("gain")->number == 0.5f, "Float default missing");
        RendererSettings settings; bool changed=false;
        ModuleParameterOverride gain;gain.id="gain";gain.value.type=ModuleParameterType::Float;gain.value.number=.8f;
        check(iris::setShaderSceneParameter(package,settings,gain,changed,error) && changed,"Generic override rejected");
        check(iris::shaderSceneParameterValues(package,settings)[0].number==.8f,"Generic override not consumed");
        check(iris::setShaderSceneParameter(package,settings,gain,changed,error) && !changed,"No-op reported changed");
        gain.value.number=2;
        check(!iris::setShaderSceneParameter(package,settings,gain,changed,error) && settings.shaderSceneParameters[0].value.number==.8f,"Out-of-range override mutated settings");
        gain.value.number=.5f;
        check(iris::setShaderSceneParameter(package,settings,gain,changed,error) && settings.shaderSceneParameters.empty(),"Default not canonicalized");
        gain.id="missing";check(!iris::setShaderSceneParameter(package,settings,gain,changed,error),"Unknown override accepted");
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
        const std::string channel = R"({"slot":0,"source":"Accum","frame":"previous","filter":"nearest","wrap":"clamp","mip":"none"})";
        const std::string v2 = R"({"version":2,"id":"graph","resourceRoot":".","entry":"main","camera":"project","author":"test","license":"MIT","parameters":[],"passes":[{"id":"Image","fragment":"image.frag","output":"display-rgba8","sizeDivisor":1,"channels":[{"slot":0,"source":"Accum","frame":"current","filter":"linear","wrap":"repeat","mip":"none"}]},{"id":"Accum","fragment":"image.frag","output":"linear-rgba16f","sizeDivisor":2,"channels":[)" + channel + "]}]}";
        write(v2); check(iris::loadShaderScenePackage(path, package, error), "Valid v2 graph rejected");
        check(package.executionOrder == std::vector<std::size_t>({1, 0}) && package.passes[1].channels[0].previousFrame,
            "Graph ordering or previous frame lost");
        const auto badV2 = [&](const std::string& from, const std::string& to) {
            auto changed = v2; changed.replace(changed.find(from), from.size(), to); write(changed);
            check(!iris::loadShaderScenePackage(path, package, error), "Invalid v2 graph accepted");
            check(package.id == "graph" && package.executionOrder.size() == 2, "Invalid graph replaced previous descriptor");
        };
        badV2("\"frame\":\"previous\"", "\"frame\":\"current\"");
        badV2("\"source\":\"Accum\"", "\"source\":\"missing\"");
        badV2("\"source\":\"Accum\"", "\"source\":\"Image\"");
        badV2("\"slot\":0", "\"slot\":4");
        badV2("\"mip\":\"none\"", "\"mip\":\"generate\"");
        badV2("\"output\":\"linear-rgba16f\"", "\"output\":\"display-rgba8\"");
        badV2("\"sizeDivisor\":2", "\"sizeDivisor\":3");
        badV2("\"id\":\"Accum\"", "\"id\":\"Image\"");
        badV2("\"version\":2", "\"version\":3");
        badV2("\"entry\":\"main\"", "\"entry\":\"auto\"");
        auto mainImage = v2;
        mainImage.replace(mainImage.find("\"entry\":\"main\""), 14, "\"entry\":\"mainImage\"");
        write(mainImage); check(iris::loadShaderScenePackage(path, package, error), "v2 mainImage rejected");
        check(package.mainImageEntry && package.executionOrder == std::vector<std::size_t>({1, 0}), "mainImage entry or graph lost");
        write(valid); check(iris::loadShaderScenePackage(path, package, error) && !package.mainImageEntry, "v1 main did not restore entry default");
        const auto materialPath = root / "test.shadermaterial";
        auto material = valid; material.replace(material.find("display-rgba8"),13,"linear-rgba16f");
        std::ofstream(materialPath) << material;
        check(iris::loadShaderScenePackage(materialPath,package,error) && package.modelMaterial,"Material package not accepted");
        gain.id="gain";gain.value.number=.8f;
        iris::ShaderMaterialBinding binding; binding.resource=materialPath.u8string(); binding.parameters={gain};
        check(iris::shaderMaterialValues(package,binding)[0].number==.8f,"Material override lost");
        binding.parameters.push_back(gain);
        bool rejected=false;try{iris::validateShaderMaterialBinding(binding);}catch(const std::exception&){rejected=true;}
        check(rejected,"Duplicate material override accepted");
        RendererSettings native;iris::validateShaderMaterialSettings(native);
        native.renderPath=RenderPath::Deferred;rejected=false;
        try{iris::validateShaderMaterialSettings(native);}catch(const std::exception&){rejected=true;}
        check(rejected,"Deferred material accepted");
        std::ofstream(materialPath) << valid;
        check(!iris::loadShaderScenePackage(materialPath,package,error),"Display output accepted as linear material");
        std::filesystem::remove_all(root);
        check(argc == 2, "Texture fixture directory required");
        testShaderSceneTexturesCpu(std::filesystem::u8path(argv[1]));
        std::cout << "ShaderScene version, strict fields, paths, parameter range and transactional recovery passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
