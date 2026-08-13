#pragma once

#include "codex_task_focus_layout.h"

#include <array>
#include <cstddef>


namespace codex_task_focus_layout {

enum class TaskFocusPreviewScenario {
    PendingDeck,
    RunningDeck,
    MixedBoundary,
    LongCjkDetail,
    EmptyDeck,
    RecordingDetail,
};


struct TaskFocusPreviewData {
    const char* project = "";
    const char* title = "";
    const char* reply = "";
    std::array<TaskTrackState, kMaxTrackNodes> states{};
    size_t count = 0;
    size_t focused_index = 0;
    bool detail = false;
    bool recording = false;
    bool connected = true;
};


inline TaskFocusPreviewData task_focus_preview(TaskFocusPreviewScenario scenario)
{
    TaskFocusPreviewData data;
    data.project = "StopWatch";
    data.title = "修复语音输入提前结束";
    data.reply =
        "语音传输竞态已经修复。手表结束录音后会可靠发送结束帧，Agent "
        "也不会再因为工作线程退出顺序而卡在录音状态。\n\n"
        "下一步建议进行两轮真机录音：一轮短句，一轮连续讲话三十秒。";
    data.states = {
        TaskTrackState::Pending,
        TaskTrackState::Pending,
        TaskTrackState::Pending,
        TaskTrackState::Running,
        TaskTrackState::Running,
        TaskTrackState::Running,
        TaskTrackState::Running,
        TaskTrackState::Running,
    };
    data.count = 5;

    switch (scenario) {
    case TaskFocusPreviewScenario::PendingDeck:
        break;
    case TaskFocusPreviewScenario::RunningDeck:
        data.project = "Codex Watch";
        data.title = "Build the authenticated task routing pipeline";
        data.focused_index = 3;
        break;
    case TaskFocusPreviewScenario::MixedBoundary:
        data.project = "Agent";
        data.title = "整理协议实现并核对正式构建验证结果";
        data.focused_index = 3;
        break;
    case TaskFocusPreviewScenario::LongCjkDetail:
        data.project = "超长项目名称用于验证圆形屏幕安全区域";
        data.title = "验证超长中文任务标题在三行限制内不会遮挡状态与回复";
        data.detail = true;
        break;
    case TaskFocusPreviewScenario::EmptyDeck:
        data.project = "";
        data.title = "";
        data.reply = "";
        data.count = 0;
        break;
    case TaskFocusPreviewScenario::RecordingDetail:
        data.detail = true;
        data.recording = true;
        data.focused_index = 3;
        break;
    }
    return data;
}

}  // namespace codex_task_focus_layout
