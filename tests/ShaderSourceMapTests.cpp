#include "render/ShaderSourceMap.h"
#include <iostream>
int main(){
    try {
        const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        ShaderSourceMap map;map.pass="Image";
        require(map.add("parent.frag")==0 && map.add("nested/include.glsl")==1,"source identity allocation failed");
        require(map.add("nested/../parent.frag")==0,"source alias acquired a second ID");
        require(map.add("<Iris mainImage adapter>",true)==2,"adapter source identity lost");
        auto diagnostics=mapShaderDiagnostics("1(7) : error C1503: undefined symbol\nERROR: 0:12: bad parent\n2:5(10): error: bad adapter\n",map);
        require(diagnostics.size()==3 && diagnostics[0].file.filename()=="include.glsl" && diagnostics[0].line==7,"NVIDIA include mapping failed");
        require(diagnostics[1].file.filename()=="parent.frag" && diagnostics[1].line==12,"colon-style parent mapping failed");
        require(diagnostics[2].generated && diagnostics[2].line==5,"adapter was attributed to authored file");
        auto unknown=mapShaderDiagnostics("0(0) : error EOF\n999(2) : unknown ID\n999999999999999999999999999(3) : overflow\nopaque vendor message",map);
        require(unknown.size()==4 && unknown[0].mapped && unknown[0].line==0 && !unknown[1].mapped && !unknown[2].mapped && !unknown[3].mapped,"unknown location was invented or log lost");
        require(formatShaderDiagnostics(unknown).find("unknown line")!=std::string::npos,"unknown line not explicit");
        ShaderSourceError error("parent.frag","1(7) : error test",map);
        require(error.diagnostics[0].pass=="Image" && std::string(error.what()).find("Raw driver log")!=std::string::npos,"structured error/raw evidence lost");
        std::cout<<"Shader source ID / alias / vendor log / original line / generated adapter / unknown fallback PASS\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
