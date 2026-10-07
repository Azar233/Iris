#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "runtime/RenderJob.h"
#include "runtime/RasterFrameReport.h"
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream f(path);return std::string(std::istreambuf_iterator<char>(f),{});}
void writeJob(const std::filesystem::path& path,const std::filesystem::path& scene,int schema,
    const std::string& capture,const std::string& renderer="raster"){
    std::ofstream f(path);
    f << "{\"format\":\"MyRendererRenderJob\",\"schemaVersion\":" << schema
      << ",\"scene\":\"" << scene.generic_string() << "\",\"renderer\":\"" << renderer
      << "\",\"resolution\":[32,32],\"frames\":{\"start\":0,\"end\":0,\"fps\":24},"
      << "\"sampling\":{\"spp\":1,\"maxDepth\":1,\"seed\":42},\"aovs\":[\"beauty\"],"
      << "\"output\":{\"path\":\"frame\",\"formats\":[\"png\"]}" << capture << '}';
}
}
int main(){try{
    const auto root=std::filesystem::current_path()/"raster-capture-test";
    std::filesystem::create_directories(root);
    const auto path=root/"job.renderjob";
    const auto scene=std::filesystem::path(MYRENDERER_SOURCE_DIR)/"assets/scenes/fixtures/27_cloud_lab_regression.myscene";
    std::string error;RenderJob job;
    writeJob(path,scene,3,",\"raster\":{\"determinism\":false,\"warmupFrames\":7,\"temporalAccumulation\":true}");
    require(loadRenderJob(path,job,error),error.c_str());
    require(!job.rasterDeterminism&&job.rasterWarmupFrames==7&&job.rasterTemporalAccumulation,"capture fields must load");
    for(const auto& invalid:{std::string(""),std::string(",\"raster\":{\"determinism\":true,\"warmupFrames\":2,\"temporalAccumulation\":true}"),
        std::string(",\"raster\":{\"determinism\":false,\"warmupFrames\":241,\"temporalAccumulation\":false}"),
        std::string(",\"raster\":{\"determinism\":0,\"warmupFrames\":0,\"temporalAccumulation\":false}"),
        std::string(",\"raster\":{\"determinism\":true,\"warmupFrames\":-1,\"temporalAccumulation\":false}"),
        std::string(",\"raster\":{\"determinism\":true,\"warmupFrames\":1.5,\"temporalAccumulation\":false}"),
        std::string(",\"raster\":{\"determinism\":true,\"warmupFrames\":0}")}){
        writeJob(path,scene,3,invalid);
        require(!loadRenderJob(path,job,error),"invalid capture schema must fail");
        require(job.rasterWarmupFrames==7&&job.rasterTemporalAccumulation,"failed loads must preserve the previous job");
    }
    writeJob(path,scene,2,",\"raster\":{\"determinism\":true,\"warmupFrames\":0,\"temporalAccumulation\":false}");
    require(!loadRenderJob(path,job,error),"older schema cannot silently carry new controls");
    writeJob(path,scene,3,",\"raster\":{\"determinism\":true,\"warmupFrames\":0,\"temporalAccumulation\":false}","cpu-path-traced");
    require(!loadRenderJob(path,job,error),"CPU jobs cannot carry raster controls");
    writeJob(path,scene,2,"");require(loadRenderJob(path,job,error),error.c_str());
    require(job.rasterDeterminism&&job.rasterWarmupFrames==0&&!job.rasterTemporalAccumulation,"legacy raster defaults must be history-free");
    RendererSettings settings;settings.atmosphere.enabled=settings.atmosphere.cloudsEnabled=true;
    settings.water.surfaceOptics=true;settings.water.cloudReflectionStrength=0.7f;
    settings.temporalAaEnabled=settings.atmosphere.cloudTemporalEnabled=true;
    require(writeRasterFrameReport(root/"first.json",job,0,settings,"test GPU",error),error.c_str());
    require(writeRasterFrameReport(root/"second.json",job,0,settings,"test GPU",error),error.c_str());
    const auto report=read(root/"first.json");
    require(report==read(root/"second.json"),"unchanged metadata must repeat exactly");
    require(report.find("\"surfaceOptics\": true")!=std::string::npos
        && report.find("\"cloudReflectionStrength\"")!=std::string::npos,
        "Raster reports must identify the rendered water optics and reflection input");
    require(report.find("\"effectiveTaa\": false")!=std::string::npos
        &&report.find("\"effectiveCloudHistory\": false")!=std::string::npos,"determinism must override requested history in metadata");
    require(report.find("procedural-shared-field")!=std::string::npos,"report must not claim offline noise assets");
    settings.atmosphere.cloudOfflineNoise=true;
    require(writeRasterFrameReport(root/"offline.json",job,0,settings,"test GPU",error),error.c_str());
    const auto offline=read(root/"offline.json");
    require(offline.find("offline-rgba16-shared-field")!=std::string::npos
        && offline.find("4464bd382daa06f7")!=std::string::npos
        && offline.find("noise-v1-64-period4.cloudnoise")!=std::string::npos,
        "offline report must identify the asset actually used by the shared field");
    {
        const auto dependency=root/"model-buffer.bin";
        {std::ofstream f(dependency);f<<"mesh";}
        capture::InputManifest manifest;manifest.record(dependency);
        require(writeRasterFrameReport(root/"validated.json",job,0,settings,"test GPU",error,&manifest),error.c_str());
        require(read(root/"validated.json").find("\"complete\": false")!=std::string::npos,"Partial helper manifest must not claim binary coverage");
        {std::ofstream f(dependency);f<<"edit";}
        require(!writeRasterFrameReport(root/"rejected.json",job,0,settings,"test GPU",error,&manifest),"Changed input report must fail");
        require(!std::filesystem::exists(root/"rejected.json"),"Invalid input must not publish a report");
    }
    require(!writeRasterFrameReport(root/"absent"/"report.json",job,0,settings,"test GPU",error),"report write failure must propagate");
    std::cout<<"Raster capture schema and metadata: PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
