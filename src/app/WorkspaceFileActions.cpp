#include "app/WorkspaceFileActions.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

bool prepareWorkspaceFileRequest(const std::filesystem::path& selected, bool directory,
    WorkspaceFileRequest& request, std::string& error) {
    try {
        if(selected.empty()) throw std::runtime_error("No workspace file selected");
        const auto path=std::filesystem::absolute(selected).lexically_normal();
        WorkspaceFileRequest candidate;
        candidate.directory=directory;
        candidate.target=directory ? (std::filesystem::is_directory(path)?path:path.parent_path()) : path;
        if(directory) {
            if(!std::filesystem::is_directory(candidate.target)) throw std::runtime_error("Directory does not exist");
        } else {
            if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("Source file does not exist");
            auto extension=path.extension().u8string();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            static const std::set<std::string> textExtensions{ ".glsl", ".frag", ".vert", ".geom", ".shaderscene", ".md", ".txt" };
            if(!textExtensions.count(extension) && path.filename()!="LICENSE")
                throw std::runtime_error("Only source/Manifest/documentation text can be opened in the editor");
        }
        request=std::move(candidate);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
bool launchWorkspaceFileRequest(const WorkspaceFileRequest& request,std::string& error) {
    WorkspaceFileRequest checked;
    if(!prepareWorkspaceFileRequest(request.target,request.directory,checked,error)) return false;
#ifdef _WIN32
    // No command shell, association-based execution or source-content evaluation.
    wchar_t systemDirectory[MAX_PATH]{};
    const auto length=GetSystemDirectoryW(systemDirectory,MAX_PATH);
    if(!length || length>=MAX_PATH){error="Cannot locate the Windows text editor";return false;}
    const auto editor=std::filesystem::path(systemDirectory)/L"notepad.exe";
    const auto arguments=L"\""+checked.target.wstring()+L"\"";
    const auto result=ShellExecuteW(nullptr,L"open",checked.directory?checked.target.c_str():editor.c_str(),
        checked.directory?nullptr:arguments.c_str(),nullptr,SW_SHOWNORMAL);
    const auto code=reinterpret_cast<INT_PTR>(result);
    if(code<=32){error="Windows open failed: "+std::to_string(code);return false;}
    error.clear();return true;
#else
    error="External editor/directory opening is currently available on Windows only";return false;
#endif
}
