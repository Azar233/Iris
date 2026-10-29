struct TemporalOraclePixel {
    glm::vec3 color{};
    glm::vec2 moments{};
    float count{};
    iris::vulkan::TemporalGuidePixel guide;
    bool eligible{};
};
struct TemporalOracleResult {
    std::vector<TemporalOraclePixel> pixels;
    unsigned accepted{},rejected{},repeated{};
};
TemporalOracleResult temporalOracle(const pathtracer::RenderImage& raw,const iris::vulkan::TemporalGuideFrame& frame,
    const std::vector<TemporalOraclePixel>& previous,bool repeat=false){
    TemporalOracleResult result;const auto colors=raw.linearPixels();result.pixels.resize(colors.size());
    const auto eligible=[&](std::size_t i){const auto f=static_cast<unsigned>(frame.pixels[i].normal.w);return (f&1U) && !(f&4U) && raw.sampleCounts[i] && raw.primaryHitCount[i]==raw.sampleCounts[i];};
    const auto lum=[](glm::vec3 c){return glm::dot(c,glm::vec3(.2126f,.7152f,.0722f));};
    for(unsigned y=0;y<raw.height;++y)for(unsigned x=0;x<raw.width;++x){
        const auto i=static_cast<std::size_t>(y)*raw.width+x;auto& next=result.pixels[i];const auto& g=frame.pixels[i];
        const float n=static_cast<float>(raw.sampleCounts[i]);next.color=colors[i];next.moments={raw.luminanceSum[i]/n,raw.luminanceSquaredSum[i]/n};next.count=std::min(n,32.f);next.guide=g;next.eligible=eligible(i);
        float total=0,count=0;glm::vec3 color(0);glm::vec2 moments(0);
        if(next.eligible && frame.historyValid && !previous.empty() && (static_cast<unsigned>(g.normal.w)&2U)){
            const float px=x+(repeat?0:g.motionDepth.x),py=y+(repeat?0:g.motionDepth.y);const int bx=static_cast<int>(std::floor(px)),by=static_cast<int>(std::floor(py));const float fx=px-bx,fy=py-by;
            for(int ky=0;ky<2;++ky)for(int kx=0;kx<2;++kx){
                const int sx=bx+kx,sy=by+ky;if(sx<0 || sy<0 || sx>=static_cast<int>(raw.width) || sy>=static_cast<int>(raw.height))continue;
                const auto& old=previous[static_cast<std::size_t>(sy)*raw.width+sx];
                if(!old.eligible || g.identity!=old.guide.identity || g.previousNormal.w!=old.guide.previousNormal.w
                    || glm::dot(glm::vec3(g.previousNormal),glm::vec3(old.guide.normal))<.9f
                    || std::abs(g.motionDepth.w-old.guide.motionDepth.z)>std::max(.0001f,.03f*g.motionDepth.w))continue;
                const float w=(kx?fx:1-fx)*(ky?fy:1-fy);total+=w;color+=old.color*w;moments+=old.moments*w;count+=old.count*w;
            }
        }
        if(total<.5f){if(next.eligible)++result.rejected;continue;}
        ++result.accepted;color/=total;moments/=total;count/=total;
        if(repeat){next.color=color;next.moments=moments;next.count=count;++result.repeated;continue;}
        glm::vec3 lo=next.color,hi=next.color,mean(0),second(0);float neighbors=0;
        for(int ky=-1;ky<=1;++ky)for(int kx=-1;kx<=1;++kx){
            const int sx=static_cast<int>(x)+kx,sy=static_cast<int>(y)+ky;if(sx<0 || sy<0 || sx>=static_cast<int>(raw.width) || sy>=static_cast<int>(raw.height))continue;
            const auto j=static_cast<std::size_t>(sy)*raw.width+sx;
            if(!eligible(j) || g.identity!=frame.pixels[j].identity || g.previousNormal.w!=frame.pixels[j].previousNormal.w)continue;
            const auto c=colors[j];lo=glm::min(lo,c);hi=glm::max(hi,c);mean+=c;second+=c*c;++neighbors;
        }
        mean/=neighbors;const auto sigma=glm::sqrt(glm::max(second/neighbors-mean*mean,glm::vec3(0)));
        const auto clamped=glm::clamp(color,glm::max(lo,mean-1.5f*sigma),glm::min(hi,mean+1.5f*sigma));
        const float oldVariance=std::max(moments.y-moments.x*moments.x,0.f),l=lum(clamped),alpha=std::min(.9f,count/(count+n));
        next.color=glm::mix(next.color,clamped,alpha);next.moments=glm::mix(next.moments,glm::vec2(l,l*l+oldVariance),alpha);next.count=std::min(count+n,32.f);
    }
    return result;
}
int temporalAcceptance(const std::filesystem::path& root,const std::filesystem::path& out){
    using iris::vulkan::VulkanPathTracer;
    auto plane=std::make_shared<ModelData>(makeGroundPlaneData());plane->materials[0].baseColorFactor={.5f,.3f,.15f,1};plane->materials[0].roughnessFactor=.8f;
    ObjLoader loader;auto cube=std::make_shared<ModelData>(loader.load(root/"assets/models/cube.obj").model);
    if(cube->materials.empty())cube->materials.emplace_back();for(auto& mesh:cube->meshes)for(auto& part:mesh.submeshes)if(part.materialIndex<0)part.materialIndex=0;
    cube->materials[0].baseColorFactor={.1f,.6f,.2f,1};cube->materials[0].roughnessFactor=.8f;
    float cameraX=0,cubeX=0;bool withCube=true,empty=false;
    const auto snapshot=[&](){
        pathtracer::SnapshotCamera camera;camera.position={cameraX,5,0};camera.view=glm::lookAt(camera.position,glm::vec3(cameraX,0,0),glm::vec3(0,0,-1));camera.verticalFieldOfViewRadians=glm::radians(70.f);
        pathtracer::SceneSnapshotLighting light;light.environment.backgroundColor={.6f,.7f,.9f};
        pathtracer::SceneSnapshotBuilder b(camera,light);
        if(!empty){b.addModel(plane,0x123456789ULL,"temporal plane",glm::mat4(1));if(withCube)b.addModel(cube,0x223456789ULL,"occluder",glm::translate(glm::mat4(1),glm::vec3(cubeX,.6f,0)));}return b.finish();
    };
    pathtracer::RenderSettings settings;settings.width=127;settings.height=73;settings.samplesPerPixel=2;settings.maxDepth=3;settings.seed=20261026;
    const auto rawEqual=[](const auto& a,const auto& b){return a.sum==b.sum && a.albedoSum==b.albedoSum && a.normalSum==b.normalSum && a.depthSum==b.depthSum && a.primaryHitCount==b.primaryHitCount && a.directSum==b.directSum && a.indirectSum==b.indirectSum && a.luminanceSum==b.luminanceSum && a.luminanceSquaredSum==b.luminanceSquaredSum && a.sampleCounts==b.sampleCounts;};
    std::vector<TemporalOraclePixel> previous;std::uint64_t totalRejected=0,totalAccepted=0;
    VulkanPathTracer pt(root/"shaders/vulkan",snapshot(),settings,true);pt.setTemporalDenoise(true);
    auto check=[&](const char* name,bool repeat=false,bool reset=false){
        while(pt.renderPass(nullptr,false)){}
        const auto bytes=pt.statistics().rawReadbackBytes,guideBytes=pt.statistics().guideReadbackBytes;
        const auto actual=pt.accumulatedImage();require(pt.statistics().rawReadbackBytes==bytes && pt.statistics().guideReadbackBytes==guideBytes,"Temporal pass read full Raw/Guide back to CPU");
        const auto raw=pt.image();const auto guide=pt.temporalGuides();auto expected=temporalOracle(raw,guide,reset?std::vector<TemporalOraclePixel>{}:previous,repeat);
        const auto colors=actual.linearPixels();float maximum=0;
        for(std::size_t i=0;i<colors.size();++i)for(int c=0;c<3;++c)maximum=std::max(maximum,std::abs(colors[i][c]-expected.pixels[i].color[c]));
        std::cerr<<"temporal diagnostic "<<name<<" max="<<maximum<<" GPU accept="<<pt.statistics().temporalAccepted<<" CPU="<<expected.accepted<<" history="<<guide.historyValid<<" previousFrame="<<guide.previousFrame<<" frame="<<guide.frame<<"\n";
        if(maximum>.0002f)for(std::size_t i=0;i<colors.size();++i)if(glm::length(colors[i]-expected.pixels[i].color)>.0002f){std::cerr<<"pixel "<<i<<" GPU "<<colors[i].r<<","<<colors[i].g<<","<<colors[i].b<<" CPU "<<expected.pixels[i].color.r<<","<<expected.pixels[i].color.g<<","<<expected.pixels[i].color.b<<" raw "<<raw.linearPixels()[i].r<<" motion "<<guide.pixels[i].motionDepth.x<<","<<guide.pixels[i].motionDepth.y<<"\n";break;}
        require(maximum<=.0002f,"Temporal GPU differs from independent CPU oracle");
        require(pt.statistics().temporalAccepted==expected.accepted && pt.statistics().temporalRejected==expected.rejected && pt.statistics().temporalRepeated==expected.repeated,"Temporal rejection/repeat counters differ from oracle");
        require(rawEqual(raw,pt.image()),"Temporal pass modified original Raw/AOVs");
        const auto dispatches=pt.statistics().temporalDispatches;pt.accumulatedImage();require(pt.statistics().temporalDispatches==dispatches,"Cached accumulated image repeated work");
        previous=std::move(expected.pixels);totalAccepted+=expected.accepted;totalRejected+=expected.rejected;
        require(pt.statistics().validationWarnings==0 && pt.statistics().validationErrors==0,"Temporal validation diagnostics");
        std::cout<<"GPU temporal "<<name<<" maxOracleError="<<maximum<<" accepted="<<expected.accepted<<" rejected="<<expected.rejected<<" repeated="<<expected.repeated<<" PASS\n";
        return actual;
    };
    check("initial",false,true);
    for(int frame=1;frame<=8;++frame){cameraX=frame*.037f;cubeX=frame*.16f;require(pt.restart(snapshot(),settings),"Temporal motion restart refused");pt.setTemporalFrame(frame,30);check("camera-rigid-disocclusion");}
    require(totalAccepted>1000 && totalRejected>100,"Temporal motion fixture did not exercise history and rejection");
    const auto beforeRepeat=pt.accumulatedImage();pt.setTemporalFrame(9,30);pt.renderPreview(1,true,true);const auto repeated=check("identical-raw",true);
    require(error(beforeRepeat,repeated)<.000001,"Repeated static Raw drifted");
    for(int frame=10;frame<30;++frame){pt.setTemporalFrame(frame,30);pt.renderPreview(1,true,true);const auto still=check("static-no-drift",true);require(error(beforeRepeat,still)<.000001,"Repeated Raw drifted across many timeline frames");}
    pt.setSpatialDenoise(true);const auto denoised=pt.spatialImage();
    require(error(pt.accumulatedImage(),repeated)<.000001,"Spatial toggle changed accumulated history");pt.setSpatialDenoise(false);
    pathtracer::writeRenderOutput(pt.image(),pathtracer::RenderOutput::Beauty,out/"motion-raw",{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
    pathtracer::writeRenderOutput(repeated,pathtracer::RenderOutput::Beauty,out/"motion-accumulated",{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
    pathtracer::writeRenderOutput(denoised,pathtracer::RenderOutput::Beauty,out/"motion-denoised",{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
    pt.setTemporalFrame(40,30);check("time-cut",false,true);require(pt.statistics().temporalAccepted==0,"Time cut accepted history");
    pt.setTemporalFrame(41,60);check("fps-cut",false,true);require(pt.statistics().temporalAccepted==0,"FPS change accepted history");
    pt.invalidateTemporalHistory();check("explicit-cut",false,true);require(pt.statistics().temporalAccepted==0,"Explicit cut accepted history");
    settings.seed++;require(pt.restart(snapshot(),settings),"Seed restart refused");check("seed-cut",false,true);require(pt.statistics().temporalAccepted==0,"Seed change accepted history");
    std::atomic<bool> cancel{true};pt.renderPass(&cancel,false);bool refused=false;try{pt.accumulatedImage();}catch(const std::exception&){refused=true;}require(refused,"Cancelled temporal export accepted");
    require(pt.restart(snapshot(),settings),"Cancel recovery failed");check("cancel-recovery",false,true);
    settings.width=65;settings.height=37;require(pt.restart(snapshot(),settings),"Resize restart refused");previous.clear();check("resize",false,true);
    pt.setTemporalDenoise(false);pt.setTemporalDenoise(true);check("toggle-reset",false,true);require(pt.statistics().temporalAccepted==0,"Reenabled temporal reused stale history");
    settings.samplesPerPixel=1;require(pt.restart(snapshot(),settings),"1SPP restart refused");check("one-spp-reset",false,true);
    for(int frame=42;frame<45;++frame){cameraX+=.09f;cubeX+=.1f;require(pt.restart(snapshot(),settings),"1SPP motion restart refused");pt.setTemporalFrame(frame,60);check("one-spp-motion");require(pt.statistics().temporalAccepted>100,"1SPP temporal path bypassed all history");}
    // Refraction and misses must use Raw even after history was established.
    auto glass=std::make_shared<ModelData>(*plane);glass->materials[0].transmissionFactor=1;
    plane=glass;require(!pt.restart(snapshot(),settings),"Material edit must require backend rebuild");
    {VulkanPathTracer glassPt(root/"shaders/vulkan",snapshot(),settings,true);glassPt.setTemporalDenoise(true);while(glassPt.renderPass(nullptr,false)){}
        const auto raw=glassPt.image(),filtered=glassPt.accumulatedImage();const auto g=glassPt.temporalGuides();const auto a=raw.linearPixels(),b=filtered.linearPixels();unsigned bypass=0;
        for(std::size_t i=0;i<a.size();++i)if((static_cast<unsigned>(g.pixels[i].normal.w)&4U) || !(static_cast<unsigned>(g.pixels[i].normal.w)&1U)){require(glm::length(a[i]-b[i])<.000001f,"Transmission/miss temporal bypass changed Raw");++bypass;}
        require(bypass>100,"Transmission fixture did not cover bypass");}
    // Async progressive display must leave every later Raw AOV identical to Raw-only rendering.
    plane=std::make_shared<ModelData>(makeGroundPlaneData());withCube=false;settings.width=127;settings.height=73;settings.samplesPerPixel=4;
    for(int lifetime=0;lifetime<2;++lifetime){VulkanPathTracer stream(root/"shaders/vulkan",snapshot(),settings,true);stream.setTemporalDenoise(true);stream.setSpatialDenoise(true);
        for(unsigned poll=0;stream.completedSamples()<settings.samplesPerPixel && poll<10000;++poll)if(!stream.renderPreview(1,true,false))std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(stream.completedSamples()==settings.samplesPerPixel,"Temporal async sampling stalled");require(stream.statistics().rawReadbackBytes==0 && stream.statistics().guideReadbackBytes==0,"Temporal preview read full Raw/Guide on CPU");
        VulkanPathTracer raw(root/"shaders/vulkan",snapshot(),settings,true);while(raw.renderPass(nullptr,false)){}require(rawEqual(raw.image(),stream.image()),"Temporal interleaving changed Raw/RNG");
        require(stream.statistics().validationWarnings==0 && stream.statistics().validationErrors==0,"Temporal async validation diagnostics");}
    {
        VulkanPathTracer moving(root/"shaders/vulkan",snapshot(),settings,true);moving.setTemporalDenoise(true);
        moving.renderPreview(1,true,false);cameraX+=.03f;
        require(moving.restart(snapshot(),settings),"Moving in-flight restart refused");
        require(moving.completedSamples()==0,"Old in-flight sample was counted for the new camera");
        require(std::any_of(moving.previewRgba().begin(),moving.previewRgba().end(),[](unsigned char value){return value!=0;}),"Continuous motion discarded the completed preview and displayed an empty frame");
        moving.renderPreview(1,true,true);require(moving.statistics().temporalAccepted>100,"In-flight motion failed to preserve temporal surface history");
        moving.renderPreview(1,true,false);moving.invalidateTemporalHistory();cameraX+=.03f;
        require(moving.restart(snapshot(),settings),"Pending cut restart refused");moving.renderPreview(1,true,true);
        require(moving.statistics().temporalAccepted==0,"Pending sample recreated history across an explicit cut");
        moving.renderPreview(1,true,false);std::atomic<bool> stop{true};moving.renderPass(&stop,false);
        require(moving.restart(snapshot(),settings),"Pending cancellation failed to recover");moving.renderPreview(1,true,true);
        require(moving.statistics().temporalAccepted==0,"Cancelled pending sample leaked into recovered history");
        require(moving.statistics().validationWarnings==0 && moving.statistics().validationErrors==0,"In-flight motion validation diagnostics");
    }
    {
        VulkanPathTracer progressive(root/"shaders/vulkan",snapshot(),settings,true);progressive.setTemporalDenoise(true);while(progressive.renderPass(nullptr,false)){}progressive.accumulatedImage();
        const auto fixed=temporalOracle(progressive.image(),progressive.temporalGuides(),{}).pixels;
        cameraX+=.1f;require(progressive.restart(snapshot(),settings),"Progressive temporal restart refused");progressive.setTemporalFrame(1,30);
        for(unsigned spp=1;spp<=settings.samplesPerPixel;++spp){require(progressive.renderPass(nullptr,false),"Progressive temporal missing sample");
            const auto colors=progressive.accumulatedImage().linearPixels();const auto expected=temporalOracle(progressive.image(),progressive.temporalGuides(),fixed);float maximum=0;
            for(std::size_t i=0;i<colors.size();++i)for(int c=0;c<3;++c)maximum=std::max(maximum,std::abs(colors[i][c]-expected.pixels[i].color[c]));
            require(maximum<=.0002f,"Progressive temporal counted overlapping Raw prefixes as independent history");
        }
        require(progressive.statistics().validationWarnings==0 && progressive.statistics().validationErrors==0,"Progressive temporal validation diagnostics");
    }
    {
        const auto directory=out/"missing-program";std::filesystem::create_directories(directory);
        std::filesystem::remove(directory/"pt-temporal.comp.spv");
        for(const auto& item:std::filesystem::directory_iterator(root/"shaders/vulkan"))if(item.path().extension()==".spv" && item.path().filename()!="pt-temporal.comp.spv")std::filesystem::copy_file(item.path(),directory/item.path().filename(),std::filesystem::copy_options::overwrite_existing);
        VulkanPathTracer broken(directory,snapshot(),settings,true);broken.setTemporalDenoise(true);while(broken.renderPass(nullptr,false)){}
        const auto raw=broken.image();const auto displayed=broken.previewRgba();bool rejected=false;try{broken.accumulatedImage();}catch(const std::exception&){rejected=true;}
        require(rejected && broken.previewRgba()==displayed && rawEqual(raw,broken.image()),"Missing temporal program corrupted Raw or published a failed preview");
        std::filesystem::copy_file(root/"shaders/vulkan/pt-temporal.comp.spv",directory/"pt-temporal.comp.spv",std::filesystem::copy_options::overwrite_existing);
        broken.accumulatedImage();require(broken.statistics().temporalAccepted==0,"Failed temporal resolve retained stale history");
        require(broken.statistics().validationWarnings==0 && broken.statistics().validationErrors==0,"Temporal failure recovery validation diagnostics");
    }
    std::cout<<"GPU temporal acceptance PASS: oracle, motion/disocclusion, clamp, repeated Raw, cuts, cancellation, resize, transmission, Raw/RNG, async two lifetimes\n";return 0;
}

int temporalSceneAcceptance(const std::filesystem::path& root,const std::filesystem::path& out){
    using iris::vulkan::VulkanPathTracer;
    std::ofstream metrics(out/"motion.csv");metrics<<"scene,frame,accepted,rejected,clamped,repeated,traceGpuMs,guideGpuMs,temporalGpuMs,spatialGpuMs,temporalCpuMs,allocatedBytes,temporalBytes,guideReadbackBytes\n";
    for(const char* name:{"sponza-atrium","stanford-glass","stanford-opaque"}){
        SceneDocument doc;std::string message;const auto scenePath=root/"assets/benchmarks/m3c"/(std::string(name)+".myscene");require(loadSceneDocument(scenePath,doc,message),message.c_str());
        AssimpImporter importer;std::vector<std::shared_ptr<ModelData>> models;
        for(const auto& e:doc.entities){require(e.parent==invalidSceneEntityId,"Temporal fixture requires flat entities");models.push_back(e.modelResource=="builtin:ground-plane"?std::make_shared<ModelData>(makeGroundPlaneData()):std::make_shared<ModelData>(importer.load(scenePath.parent_path()/e.modelResource).model));}
        pathtracer::RenderSettings settings;settings.width=1280;settings.height=720;settings.samplesPerPixel=4;settings.maxDepth=8;settings.seed=20261026;
        const auto snapshot=[&](int frame){
            Camera camera;auto orbit=doc.camera;orbit.yawDegrees+=.2f*frame;orbit.target.x+=orbit.distance*.002f*frame;camera.setOrbitState(orbit);
            pathtracer::SnapshotCamera c;c.position=camera.position();c.view=camera.viewMatrix();c.aspectRatio=static_cast<float>(settings.width)/settings.height;c.projection=camera.projectionMatrix(c.aspectRatio);c.verticalFieldOfViewRadians=glm::radians(camera.fieldOfView());
            auto lights=doc.renderer;lights.localLights=resolveSceneLocalLights(doc);pathtracer::SceneSnapshotBuilder builder(c,pathtracer::captureSceneLighting(lights,c.position.y));
            for(std::size_t i=0;i<models.size();++i){auto transform=doc.entities[i].transform.matrix();if(i==0 && std::string(name)!="sponza-atrium")transform=glm::translate(glm::mat4(1),glm::vec3(.03f*frame,0,0))*transform;
                builder.addModel(models[i],doc.entities[i].id,doc.entities[i].name,transform,doc.entities[i].tint,doc.entities[i].castsShadow);}return builder.finish();
        };
        const auto destination=out/name;std::filesystem::create_directories(destination);
        VulkanPathTracer pt(root/"shaders/vulkan",snapshot(0),settings,true);pt.setTemporalDenoise(true);pt.setSpatialDenoise(true);
        for(int frame=0;frame<6;++frame){
            if(frame)require(pt.restart(snapshot(frame),settings),"Production temporal restart refused");
            pt.setTemporalFrame(frame,30);while(pt.renderPass(nullptr,false)){}
            const auto accumulated=pt.accumulatedImage(),denoised=pt.spatialImage();const auto stats=pt.statistics();
            require(stats.guideReadbackBytes==0 && stats.rawReadbackBytes==0,"Production temporal resolve mapped full Raw/Guide");
            require(stats.validationWarnings==0 && stats.validationErrors==0,"Production temporal strict diagnostics");
            if(frame)require(stats.temporalAccepted>1000,"Production temporal motion accepted no meaningful history");else require(stats.temporalAccepted==0,"Fresh production frame used old history");
            metrics<<name<<','<<frame<<','<<stats.temporalAccepted<<','<<stats.temporalRejected<<','<<stats.temporalClamped<<','<<stats.temporalRepeated<<','<<stats.gpuMilliseconds<<','<<stats.guideGpuMilliseconds<<','<<stats.temporalGpuMilliseconds<<','<<stats.spatialGpuMilliseconds<<','<<stats.temporalCpuMilliseconds<<','<<stats.allocatedBytes<<','<<stats.temporalAllocatedBytes<<','<<stats.guideReadbackBytes<<'\n';metrics.flush();
            const auto raw=pt.image();const auto stem=destination/("frame_"+std::to_string(frame));
            pathtracer::writeRenderOutput(raw,pathtracer::RenderOutput::Beauty,std::filesystem::path(stem.string()+"-raw"),{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
            pathtracer::writeRenderOutput(accumulated,pathtracer::RenderOutput::Beauty,std::filesystem::path(stem.string()+"-accumulated"),{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
            pathtracer::writeRenderOutput(denoised,pathtracer::RenderOutput::Beauty,std::filesystem::path(stem.string()+"-denoised"),{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
            if(frame==0){for(const auto aov:{pathtracer::RenderOutput::Albedo,pathtracer::RenderOutput::Normal,pathtracer::RenderOutput::Depth,pathtracer::RenderOutput::Direct,pathtracer::RenderOutput::Indirect,pathtracer::RenderOutput::SampleCount,pathtracer::RenderOutput::Variance})pathtracer::writeRenderOutput(raw,aov,destination/(std::string("initial-")+renderOutputName(aov)),{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});}
            if(frame>=4){
                const auto g=pt.temporalGuides();std::ofstream guideFile(destination/(frame==4?"previous-guides.bin":"final-guides.bin"),std::ios::binary);guideFile.write(reinterpret_cast<const char*>(g.pixels.data()),static_cast<std::streamsize>(g.pixels.size()*sizeof(g.pixels[0])));
            }
            if(frame==5){
                auto refSettings=settings;refSettings.samplesPerPixel=64;VulkanPathTracer reference(root/"shaders/vulkan",snapshot(frame),refSettings,true);while(reference.renderPass(nullptr,false)){}
                pathtracer::writeRenderOutput(reference.image(),pathtracer::RenderOutput::Beauty,destination/"final-reference64",{pathtracer::RenderFileFormat::Png,pathtracer::RenderFileFormat::OpenExr});
                std::cout<<"Temporal production "<<name<<" final linear RMSE raw="<<error(raw,reference.image())<<" accumulated="<<error(accumulated,reference.image())<<" denoised="<<error(denoised,reference.image())<<"\n";
            }
            std::cout<<"Temporal production "<<name<<" frame="<<frame<<" accepted="<<stats.temporalAccepted<<" rejected="<<stats.temporalRejected<<" PASS\n";
        }
    }
    return 0;
}
