#pragma once
#include "core/container/string.hpp"

namespace zq::engine {

class Host {
public:
    enum class State {
        Startup,
        Running,
        Paused,
        Shutdown
    };
    
    static Host& Instance();
    
    bool Initialize();
    void Shutdown();
    void RunFrame();
    void Quit();
    
    State GetState() const;
    double GetTime() const;
    double GetFrameTime() const;
    
private:
    Host() = default;
    ~Host() = default;
    
    State state_ = State::Startup;
    double time_ = 0.0;
    double frame_time_ = 0.016;
    bool running_ = true;
};

}
