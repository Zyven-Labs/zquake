#pragma once
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include "vulkan/render_command.hpp"

namespace zq::vk {

class RenderThread {
public:
    RenderThread();
    ~RenderThread();
    
    void Start();
    void Stop();
    bool IsRunning() const;
    
    void SubmitCommand(const RenderCommand& cmd);
    void SubmitCommands(const std::vector<RenderCommand>& cmds);
    
private:
    void ThreadLoop();
    
    std::vector<RenderCommand> commands_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
};

}
