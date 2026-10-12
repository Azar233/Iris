#include "app/WorkspaceFileActions.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
    const bool launch=argc==3 && std::string(argv[1])=="--launch";
    const auto root=launch?std::filesystem::absolute(std::filesystem::u8path(argv[2])):
        std::filesystem::temp_directory_path()/ ("Iris File Actions "+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if(launch) {
            const auto boundary=std::filesystem::absolute(".local/verification").lexically_normal();
            const auto relative=root.lexically_normal().lexically_relative(boundary);
            if(relative.empty() || relative=="." || *relative.begin()==".." || std::filesystem::exists(root))
                throw std::runtime_error("Native launch fixture must be a new directory within .local/verification");
        }
        std::filesystem::create_directories(root);
        const auto text=root/std::filesystem::u8path("含空格 source.frag");std::ofstream(text)<<"// read only";
        const auto binary=root/"do-not-run.exe";std::ofstream(binary)<<"not an executable";
        WorkspaceFileRequest request;std::string error;
        const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        require(prepareWorkspaceFileRequest(text,false,request,error) && request.target==text && !request.directory,"Unicode/space source request lost");
        const auto retained=request.target;
        require(!prepareWorkspaceFileRequest(binary,false,request,error) && request.target==retained,"Binary dispatched as editor input or failed request replaced state");
        require(!prepareWorkspaceFileRequest(root/"missing.frag",false,request,error),"Missing source accepted");
        require(prepareWorkspaceFileRequest(root/"missing.frag",true,request,error) && request.directory && request.target==root,"Missing file parent cannot be browsed");
        require(!prepareWorkspaceFileRequest({},true,request,error),"Empty selection accepted");
        std::ofstream(root/"LICENSE")<<"MIT";
        require(prepareWorkspaceFileRequest(root/"LICENSE",false,request,error),"LICENSE plain text rejected");
        if(launch) {
            require(prepareWorkspaceFileRequest(text,false,request,error) && launchWorkspaceFileRequest(request,error),error.c_str());
            require(prepareWorkspaceFileRequest(text,true,request,error) && launchWorkspaceFileRequest(request,error),error.c_str());
            std::cout<<"Windows editor/directory dispatch PASS (fixture retained for open applications)\n";
        } else std::filesystem::remove_all(root);
        std::cout<<"Workspace text-only editor requests / Unicode / missing / containing folder / transaction PASS\n";return 0;
    } catch(const std::exception& e){if(!launch)std::filesystem::remove_all(root);std::cerr<<e.what()<<'\n';return 1;}
}
