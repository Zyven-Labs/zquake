#pragma once
#include "core/container/string.hpp"

namespace zq::engine {

class Game {
public:
    enum class State { menu, play, paused, loadgame, endgame };
    
    static Game& Instance();
    static void Update(State state);
    static State GetState();
    static void Cmd_Start_f();
    static void Cmd_Quit_f();
    
private:
    Game() = default;
    State state_ = State::menu;
};

}
