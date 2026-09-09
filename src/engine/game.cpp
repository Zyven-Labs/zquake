#include "engine/game.hpp"

namespace zq::engine {
Game& Game::Instance() {
    static Game instance;
    return instance;
}
void Game::Update(State state) { Instance().state_ = state; }
Game::State Game::GetState() { return Instance().state_; }
void Game::Cmd_Start_f() { Update(State::play); }
void Game::Cmd_Quit_f() { Update(State::endgame); }
}
