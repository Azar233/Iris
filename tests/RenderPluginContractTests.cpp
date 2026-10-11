#include "plugin/RenderPluginContract.h"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class Function> void rejects(Function function) {
    bool rejected=false;
    try { function(); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"Invalid plugin contract/binding was accepted");
}
}
int main() {
    try {
        auto valid=iris::postProcessContract();
        iris::validatePluginContract(valid);
        iris::validatePluginContract(iris::enscapeContract());
        auto bad=valid;
        bad.passes[0].reads.push_back("bloom");
        rejects([&]{iris::validatePluginContract(bad);}); // same-frame cycle
        bad=valid;bad.passes[0].reads.push_back("history");
        rejects([&]{iris::validatePluginContract(bad);}); // history must be explicit
        bad=valid;bad.passes[1].writes.push_back("resolved");
        rejects([&]{iris::validatePluginContract(bad);}); // multiple writers
        bad=valid;bad.passes[2].reads.push_back("unknown");
        rejects([&]{iris::validatePluginContract(bad);});
        bad=valid;bad.passes[0].previousFrameReads.push_back("hdr");
        rejects([&]{iris::validatePluginContract(bad);});
        bad=valid;bad.resources.push_back(valid.resources.front());
        rejects([&]{iris::validatePluginContract(bad);});
        bad=valid;bad.passes.pop_back();
        rejects([&]{iris::validatePluginContract(bad);});
        std::vector<iris::RenderTextureBinding> bindings={
            {"hdr",11,iris::TextureFormat::HdrColor,320,180},
            {"display",12,iris::TextureFormat::DisplayColor,320,180}};
        iris::validatePluginBindings(valid,bindings,320,180); // optional inputs absent
        auto invalid=bindings;invalid[0].texture=0;
        rejects([&]{iris::validatePluginBindings(valid,invalid,320,180);});
        invalid=bindings;invalid[1].texture=11;
        rejects([&]{iris::validatePluginBindings(valid,invalid,320,180);});
        invalid=bindings;invalid[0].format=iris::TextureFormat::Depth;
        rejects([&]{iris::validatePluginBindings(valid,invalid,320,180);});
        invalid=bindings;invalid[0].width=319;
        rejects([&]{iris::validatePluginBindings(valid,invalid,320,180);});
        invalid=bindings;invalid.push_back(bindings[0]);
        rejects([&]{iris::validatePluginBindings(valid,invalid,320,180);});
        iris::validatePluginBindings(valid,bindings,320,180);
        invalid=bindings;invalid.push_back({"cloud",13,iris::TextureFormat::HdrColor,160,90});
        invalid.push_back({"cloudDepth",14,iris::TextureFormat::Data,160,90});
        invalid.push_back({"rays",15,iris::TextureFormat::Data,160,90});
        iris::validatePluginBindings(valid,invalid,320,180);
        invalid.back().width=159;
        rejects([&]{iris::validatePluginBindings(valid,invalid,320,180);});
        invalid.back().width=160;invalid.back().format=iris::TextureFormat::HdrColor;
        rejects([&]{iris::validatePluginBindings(valid,invalid,320,180);});
        invalid=bindings;invalid.push_back({"undeclared",13,iris::TextureFormat::Data,320,180});
        rejects([&]{iris::validatePluginBindings(valid,invalid,320,180);});
        std::cout<<"Plugin pass/resource contracts: PASS\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
