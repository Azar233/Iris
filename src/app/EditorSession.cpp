#include "app/EditorSession.h"

#include <algorithm>
#include <utility>

void EditorSession::requestBackend(EditorRenderBackend backend) {
    if (backend_ == backend) return;
    backend_ = backend;
    commands_.push_back(EditorCommand{
        EditorCommandType::BackendChanged, 0U, static_cast<std::uint64_t>(backend)
    });
}

void EditorSession::requestActivity(EditorActivity activity) {
    if (activity == EditorActivity::Preview) requestPause(false);
    if (activity_ == activity) return;
    activity_ = activity;
    if (activity_ == EditorActivity::Edit) paused_ = false;
    commands_.push_back(EditorCommand{
        EditorCommandType::ActivityChanged, 0U, static_cast<std::uint64_t>(activity)
    });
}

void EditorSession::requestPause(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    commands_.push_back(EditorCommand{EditorCommandType::PauseChanged, 0U, 0U, paused});
}

void EditorSession::request(EditorCommand command) {
    if (command.type == EditorCommandType::Step || command.type == EditorCommandType::Reset)
        requestPause(true);
    commands_.push_back(command);
}

std::vector<EditorCommand> EditorSession::takeCommands() {
    std::vector<EditorCommand> result;
    result.reserve(commands_.size());
    while (!commands_.empty()) {
        result.push_back(commands_.front());
        commands_.pop_front();
    }
    return result;
}

void EditorSession::setFrame(int frame) {
    timeline_.setFrame(frame);
}

void EditorSession::setFrameRange(int startFrame, int endFrame) {
    timeline_.setFrameRange(startFrame, endFrame);
}

void EditorSession::setFramesPerSecond(int framesPerSecond) {
    timeline_.setFramesPerSecond(framesPerSecond);
}

double EditorSession::timeSeconds() const {
    return timeline_.timeSeconds();
}
