// Independent CPU evaluation of the published GPU data, plus rendering/lifetime oracles.
std::vector<glm::vec3> spatialOracle(const pathtracer::RenderImage& raw,const iris::vulkan::TemporalGuideFrame& frame) {
    auto input=raw.linearPixels();const auto albedo=raw.albedoPixels();const auto variance=raw.variancePixels();
    const auto eligible=[&](std::size_t i){const auto flags=static_cast<unsigned>(frame.pixels[i].normal.w);return (flags&1U) && !(flags&4U) && raw.sampleCounts[i]>1 && raw.primaryHitCount[i]==raw.sampleCounts[i];};
    constexpr float kernel[]{1.f/16,4.f/16,6.f/16,4.f/16,1.f/16};
    const auto lum=[](glm::vec3 v){return glm::dot(v,glm::vec3(.2126f,.7152f,.0722f));};
    for(int pass=0;pass<2;++pass){auto output=input;
        for(unsigned y=0;y<raw.height;++y)for(unsigned x=0;x<raw.width;++x){
            const auto i=static_cast<std::size_t>(y)*raw.width+x;if(!eligible(i))continue;
            const auto& a=frame.pixels[i];glm::vec3 sum(0);float total=0;
            const auto colorScale=std::max(4*std::sqrt(variance[i]),.01f);
            for(int ky=-2;ky<=2;++ky)for(int kx=-2;kx<=2;++kx){
                const int sx=static_cast<int>(x)+kx*(1<<pass),sy=static_cast<int>(y)+ky*(1<<pass);
                if(sx<0 || sy<0 || sx>=static_cast<int>(raw.width) || sy>=static_cast<int>(raw.height))continue;
                const auto j=static_cast<std::size_t>(sy)*raw.width+sx;const auto& b=frame.pixels[j];
                if(!eligible(j) || a.identity!=b.identity || a.previousNormal.w!=b.previousNormal.w)continue;
                const auto delta=albedo[j]-albedo[i];
                float weight=kernel[kx+2]*kernel[ky+2];
                weight*=std::exp(-std::abs(lum(input[j])-lum(input[i]))/colorScale);
                weight*=std::exp(-glm::dot(delta,delta)/.0625f);
                weight*=std::pow(std::max(glm::dot(glm::vec3(a.normal),glm::vec3(b.normal)),0.f),64.f);
                weight*=std::exp(-std::abs(a.motionDepth.z-b.motionDepth.z)/std::max(a.motionDepth.z*.02f,.00001f));
                sum+=input[j]*weight;total+=weight;
            }
            if(total>0)output[i]=sum/total;
        }
        input=std::move(output);
    }
    return input;
}
int spatialAcceptance(const std::filesystem::path& root,const std::filesystem::path& out) {
    using iris::vulkan::VulkanPathTracer;
    auto model=std::make_shared<ModelData>(makeGroundPlaneData());
    bool emissionOnly=false;
    model->materials[0].baseColorFactor={.5f,.3f,.15f,1};model->materials[0].roughnessFactor=.8f;
    const auto snapshot=[&](bool empty=false){
        pathtracer::SnapshotCamera camera;camera.position={0,5,0};camera.view=glm::lookAt(camera.position,glm::vec3(0),glm::vec3(0,0,-1));camera.verticalFieldOfViewRadians=glm::radians(70.f);
        pathtracer::SceneSnapshotLighting light;light.environment.backgroundColor=emissionOnly?glm::vec3(0):glm::vec3(.6f,.7f,.9f);
        pathtracer::SceneSnapshotBuilder builder(camera,light);if(!empty)builder.addModel(model,0x123456789ULL,"spatial plane",glm::mat4(1));return builder.finish();
    };
    pathtracer::RenderSettings settings;settings.width=127;settings.height=73;settings.samplesPerPixel=4;settings.maxDepth=3;settings.seed=20261026;
    const auto compare=[&](VulkanPathTracer& pt,const char* name){
        const auto raw=pt.image();const auto beforeRawBytes=pt.statistics().rawReadbackBytes;const auto samples=pt.completedSamples();
        pt.setSpatialDenoise(true);const auto filtered=pt.spatialImage();const auto pixels=filtered.linearPixels();
        require(pt.statistics().guideReadbackBytes==0,"Spatial filter read primary guides back to CPU");
        require(pt.statistics().rawReadbackBytes==beforeRawBytes && pt.completedSamples()==samples,"Spatial filter changed sampling/Raw readback");
        const auto guides=pt.temporalGuides();const auto expected=spatialOracle(raw,guides);float maximum=0;std::size_t bypass=0;
        const auto rawPixels=raw.linearPixels();
        for(std::size_t i=0;i<pixels.size();++i){for(int c=0;c<3;++c)maximum=std::max(maximum,std::abs(pixels[i][c]-expected[i][c]));
            const auto flags=static_cast<unsigned>(guides.pixels[i].normal.w);
            if(!(flags&1U) || (flags&4U) || raw.sampleCounts[i]<2 || raw.primaryHitCount[i]!=raw.sampleCounts[i]){
                require(glm::length(pixels[i]-rawPixels[i])<.000002f,"Spatial filter changed transmission/coverage/miss/1SPP bypass");++bypass;
            }
        }
        require(maximum<=.0002f,"GPU spatial output differs from independent CPU oracle");
        const auto& after=pt.image();require(raw.sum==after.sum && raw.albedoSum==after.albedoSum && raw.normalSum==after.normalSum && raw.depthSum==after.depthSum && raw.primaryHitCount==after.primaryHitCount && raw.directSum==after.directSum && raw.indirectSum==after.indirectSum && raw.luminanceSum==after.luminanceSum && raw.luminanceSquaredSum==after.luminanceSquaredSum && raw.sampleCounts==after.sampleCounts,"Spatial pass modified Raw/AOVs");
        const auto dispatches=pt.statistics().spatialDispatches;pt.spatialImage();require(pt.statistics().spatialDispatches==dispatches,"Cached spatial image repeated GPU work");
        pt.setSpatialDenoise(false);pt.renderPreview(1,true,true);require(pt.completedSamples()==samples,"Raw toggle restarted sampling");
        const auto expectedDisplay=pathtracer::makeDisplayRgba8BottomUp(raw,pathtracer::RenderOutput::Beauty);int delta=0;
        for(std::size_t i=0;i<expectedDisplay.size();++i)delta=std::max(delta,std::abs(int(expectedDisplay[i])-int(pt.previewRgba()[i])));
        require(delta<=1,"Raw toggle did not restore original display");
        pathtracer::writeRenderOutput(raw,pathtracer::RenderOutput::Beauty,out/(std::string(name)+"-raw"),{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
        pathtracer::writeRenderOutput(filtered,pathtracer::RenderOutput::Beauty,out/(std::string(name)+"-spatial"),{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
        require(pt.statistics().validationWarnings==0 && pt.statistics().validationErrors==0,"Spatial validation diagnostics");
        std::cout<<"GPU spatial "<<name<<" maxOracleError="<<maximum<<" bypass="<<bypass<<" samples="<<samples<<" PASS\n";return filtered;
    };
    VulkanPathTracer pt(root/"shaders/vulkan",snapshot(),settings,true);while(pt.renderPass(nullptr,false)){}
    const auto lowRaw=pt.image();const auto filtered=compare(pt,"plane");
    auto referenceSettings=settings;referenceSettings.samplesPerPixel=256;
    VulkanPathTracer reference(root/"shaders/vulkan",snapshot(),referenceSettings,true);while(reference.renderPass(nullptr,false)){}
    const auto rawError=error(lowRaw,reference.image()),filteredError=error(filtered,reference.image());
    std::cout<<"GPU spatial noise oracle RMSE raw="<<rawError<<" spatial="<<filteredError<<"\n";
    require(rawError>1e-5 && filteredError<rawError*.9,"Spatial noise plane did not reduce RMSE by 10 percent");
    for(int lifetime=0;lifetime<2;++lifetime){
        VulkanPathTracer streaming(root/"shaders/vulkan",snapshot(),settings,true);streaming.setSpatialDenoise(true);
        bool toggled=false;
        for(unsigned poll=0;streaming.completedSamples()<settings.samplesPerPixel && poll<10000;++poll){
            if(!streaming.renderPreview(1,true,false))std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(streaming.completedSamples()==2 && !toggled){
                streaming.spatialImage();streaming.setSpatialDenoise(false);
                // Change display mode with the third sample already in flight.
                streaming.renderPreview(1,true,false);streaming.setSpatialDenoise(true);toggled=true;
            }
        }
        require(toggled && streaming.completedSamples()==settings.samplesPerPixel,"Spatial async sampling failed to progress");
        require(streaming.statistics().rawReadbackBytes==0 && streaming.statistics().guideReadbackBytes==0,"Interactive spatial filtering read Raw/Guide back to CPU");
        const auto& actual=streaming.image();
        require(actual.sum==lowRaw.sum && actual.albedoSum==lowRaw.albedoSum && actual.normalSum==lowRaw.normalSum && actual.depthSum==lowRaw.depthSum && actual.primaryHitCount==lowRaw.primaryHitCount && actual.directSum==lowRaw.directSum && actual.indirectSum==lowRaw.indirectSum && actual.luminanceSum==lowRaw.luminanceSum && actual.luminanceSquaredSum==lowRaw.luminanceSquaredSum && actual.sampleCounts==lowRaw.sampleCounts,"Spatial filtering between samples changed later Raw/AOV accumulation");
        require(streaming.statistics().validationWarnings==0 && streaming.statistics().validationErrors==0,"Async spatial validation diagnostics");
    }
    std::cout<<"GPU spatial two-lifetime async interleaved filtering / in-flight Raw-Spatial toggle / zero Raw+Guide readback / fresh-backend exact AOVs PASS\n";
    settings.samplesPerPixel=1;require(pt.restart(snapshot(),settings),"Spatial 1SPP restart failed");pt.renderPass(nullptr,false);compare(pt,"one-spp");
    model->materials[0].transmissionFactor=.8f;settings.samplesPerPixel=4;
    {VulkanPathTracer glass(root/"shaders/vulkan",snapshot(),settings,true);while(glass.renderPass(nullptr,false)){}compare(glass,"transmission");}
    model->materials[0].transmissionFactor=0;model->materials[0].emissiveFactor={.3f,.7f,1};settings.maxDepth=1;emissionOnly=true;
    {VulkanPathTracer constant(root/"shaders/vulkan",snapshot(),settings,true);while(constant.renderPass(nullptr,false)){}const auto raw=constant.image();const auto result=compare(constant,"constant");require(error(raw,result)<.000002,"Constant field gained bias");}
    emissionOnly=false;
    {auto checker=std::make_shared<ModelData>(makeGlassCheckerboardData());pathtracer::SnapshotCamera camera;camera.position={0,1,5};camera.view=glm::lookAt(camera.position,glm::vec3(0,1,0),glm::vec3(0,1,0));
        pathtracer::SceneSnapshotLighting light;light.environment.backgroundColor={.3f,.4f,.5f};pathtracer::SceneSnapshotBuilder builder(camera,light);builder.addModel(checker,7,"checker",glm::mat4(1));
        settings.maxDepth=3;VulkanPathTracer edges(root/"shaders/vulkan",builder.finish(),settings,true);while(edges.renderPass(nullptr,false)){}compare(edges,"material-edges");}
    for(const auto size:{glm::uvec2(1,1),glm::uvec2(1280,720)}){
        settings.width=size.x;settings.height=size.y;settings.samplesPerPixel=2;
        {VulkanPathTracer empty(root/"shaders/vulkan",snapshot(true),settings,true);while(empty.renderPass(nullptr,false)){}compare(empty,size.x==1?"empty-one":"empty-720p");}
    }
    settings.width=127;settings.height=73;settings.samplesPerPixel=2;model->materials[0].emissiveFactor={0,0,0};
    {VulkanPathTracer cancelled(root/"shaders/vulkan",snapshot(),settings,true);cancelled.renderPass(nullptr,false);cancelled.setSpatialDenoise(true);cancelled.spatialImage();std::atomic<bool> stop{true};cancelled.renderPass(&stop,false);
        bool rejected=false;try{cancelled.spatialImage();}catch(const std::exception&){rejected=true;}require(rejected,"Cancelled spatial result published");
        require(cancelled.restart(snapshot(),settings),"Spatial cancellation recovery failed");while(cancelled.renderPass(nullptr,false)){}compare(cancelled,"recovered");}
    {const auto isolated=out/"isolated-shaders";std::filesystem::create_directories(isolated);
        for(const auto file:{"pt.rgen.spv","pt.rmiss.spv","pt.rchit.spv","pt-guides.comp.spv"})std::filesystem::copy_file(root/"shaders/vulkan"/file,isolated/file);
        VulkanPathTracer missing(isolated,snapshot(),settings,true);while(missing.renderPass(nullptr,false)){}const auto raw=missing.image();missing.setSpatialDenoise(true);bool rejected=false;
        try{missing.spatialImage();}catch(const std::exception&){rejected=true;}require(rejected && missing.image().sum==raw.sum,"Missing spatial shader corrupted Raw");
        std::filesystem::copy_file(root/"shaders/vulkan/pt-spatial.comp.spv",isolated/"pt-spatial.comp.spv");compare(missing,"shader-recovered");}
    std::cout<<"GPU spatial PASS: independent CPU oracle, noise reduction, Raw isolation, material edges, transmission/miss/coverage/1SPP bypass, raw toggle, cached resolve, 1x1/720p, cancellation and missing-shader recovery; strict validation\n";return 0;
}
