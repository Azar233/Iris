#pragma once
#include <filesystem>
#include <string>

struct WorkspaceFileRequest {
    std::filesystem::path target;
    bool directory{false};
};
// Source requests are plain text edits, never execution of the selected file.
bool prepareWorkspaceFileRequest(const std::filesystem::path& selected, bool directory,
    WorkspaceFileRequest& request, std::string& error);
bool launchWorkspaceFileRequest(const WorkspaceFileRequest& request, std::string& error);
