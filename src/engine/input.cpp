#include "engine/input.hpp"
#include <cstring>

namespace zq::engine {

bool Input::keys_[static_cast<int>(Key::KEY_COUNT)] = {};
bool Input::prev_keys_[static_cast<int>(Key::KEY_COUNT)] = {};
bool Input::latched_[static_cast<int>(Key::KEY_COUNT)] = {};
int Input::mouse_dx_ = 0;
int Input::mouse_dy_ = 0;

void Input::Init() {
    ClearAll();
}

void Input::ClearAll() {
    std::memset(keys_, 0, sizeof(keys_));
    std::memset(prev_keys_, 0, sizeof(prev_keys_));
    std::memset(latched_, 0, sizeof(latched_));
    mouse_dx_ = 0;
    mouse_dy_ = 0;
}

void Input::Poll() {
    std::memcpy(prev_keys_, keys_, sizeof(keys_));
    // clear latched presses from the previous frame (latches are captured
    // during event processing, after Poll)
    std::memset(latched_, 0, sizeof(latched_));
    mouse_dx_ = 0;
    mouse_dy_ = 0;
}

Key Input::ScancodeToKey(int scancode) {
    switch (scancode) {
        case 26: return Key::WKEY;   // SDL_SCANCODE_W
        case 22: return Key::SKEY;   // SDL_SCANCODE_S
        case 4:  return Key::AKEY;   // SDL_SCANCODE_A
        case 7:  return Key::DKEY;   // SDL_SCANCODE_D
        case 44: return Key::SPACE;  // SDL_SCANCODE_SPACE
        case 40: return Key::ENTER;  // SDL_SCANCODE_RETURN
        case 41: return Key::ESCAPE; // SDL_SCANCODE_ESCAPE
        case 82: return Key::ARROW_UP;    // SDL_SCANCODE_UP
        case 81: return Key::ARROW_DOWN;  // SDL_SCANCODE_DOWN
        case 80: return Key::ARROW_LEFT;  // SDL_SCANCODE_LEFT
        case 79: return Key::ARROW_RIGHT; // SDL_SCANCODE_RIGHT
        case 43: return Key::TAB;         // SDL_SCANCODE_TAB
        case 42: return Key::BACKSPACE;   // SDL_SCANCODE_BACKSPACE
        case 225: return Key::LSHIFT;     // SDL_SCANCODE_LSHIFT
        case 229: return Key::RSHIFT;     // SDL_SCANCODE_RSHIFT
        case 224: return Key::LCTRL;      // SDL_SCANCODE_LCTRL
        case 228: return Key::RCTRL;      // SDL_SCANCODE_RCTRL
        case 226: return Key::LALT;       // SDL_SCANCODE_LALT
        case 230: return Key::RALT;       // SDL_SCANCODE_RALT
        case 20:  return Key::Q;          // SDL_SCANCODE_Q
        case 8:   return Key::E;          // SDL_SCANCODE_E
        case 21:  return Key::R;          // SDL_SCANCODE_R
        case 9:   return Key::F;          // SDL_SCANCODE_F
        case 14:  return Key::C;          // SDL_SCANCODE_C
        case 13:  return Key::G;          // SDL_SCANCODE_G
        case 29:  return Key::Z;          // SDL_SCANCODE_Z
        case 27:  return Key::X;          // SDL_SCANCODE_X
        case 25:  return Key::V;          // SDL_SCANCODE_V
        case 30:  return Key::NUM1;       // SDL_SCANCODE_1
        case 31:  return Key::NUM2;
        case 32:  return Key::NUM3;
        case 33:  return Key::NUM4;
        case 34:  return Key::NUM5;
        case 35:  return Key::NUM6;
        case 36:  return Key::NUM7;
        case 37:  return Key::NUM8;
        case 38:  return Key::NUM9;
        case 39:  return Key::NUM0;
        default: return Key::NONE;
    }
}

void Input::HandleKeyEvent(int sdl_scancode, bool down) {
    Key key = ScancodeToKey(sdl_scancode);
    if (key != Key::NONE) {
        if (down && !keys_[static_cast<int>(key)])
            latched_[static_cast<int>(key)] = true; // capture press edge even if released same frame
        keys_[static_cast<int>(key)] = down;
    }
}

void Input::HandleMouseButton(int button, bool down) {
    Key key = Key::NONE;
    if (button == 1) key = Key::MOUSE1;       // SDL_BUTTON_LEFT = 1
    else if (button == 3) key = Key::MOUSE2;   // SDL_BUTTON_RIGHT = 3
    else if (button == 2) key = Key::MOUSE3;   // SDL_BUTTON_MIDDLE = 2
    
    if (key != Key::NONE) {
        keys_[static_cast<int>(key)] = down;
    }
}

void Input::HandleMouseMotion(int xrel, int yrel) {
    mouse_dx_ += xrel;
    mouse_dy_ += yrel;
}

void Input::SetKeyState(Key key, bool down) {
    int idx = static_cast<int>(key);
    if (idx >= 0 && idx < static_cast<int>(Key::KEY_COUNT)) {
        keys_[idx] = down;
    }
}

bool Input::IsKeyDown(Key key) {
    int idx = static_cast<int>(key);
    return idx >= 0 && idx < static_cast<int>(Key::KEY_COUNT) && keys_[idx];
}

bool Input::IsKeyJustPressed(Key key) {
    int idx = static_cast<int>(key);
    return idx >= 0 && idx < static_cast<int>(Key::KEY_COUNT) && keys_[idx] && !prev_keys_[idx];
}

bool Input::WasKeyPressed(Key key) {
    int idx = static_cast<int>(key);
    return idx >= 0 && idx < static_cast<int>(Key::KEY_COUNT) && latched_[idx];
}

bool Input::IsKeyJustReleased(Key key) {
    int idx = static_cast<int>(key);
    return idx >= 0 && idx < static_cast<int>(Key::KEY_COUNT) && !keys_[idx] && prev_keys_[idx];
}

int Input::GetMouseDeltaX() { return mouse_dx_; }
int Input::GetMouseDeltaY() { return mouse_dy_; }

String Input::GetKeyString(Key key) {
    switch (key) {
        case Key::WKEY: return String("W");
        case Key::SKEY: return String("S");
        case Key::AKEY: return String("A");
        case Key::DKEY: return String("D");
        case Key::SPACE: return String("Space");
        case Key::ENTER: return String("Enter");
        case Key::ESCAPE: return String("Escape");
        case Key::ARROW_UP: return String("Up");
        case Key::ARROW_DOWN: return String("Down");
        case Key::ARROW_LEFT: return String("Left");
        case Key::ARROW_RIGHT: return String("Right");
        case Key::TAB: return String("Tab");
        case Key::BACKSPACE: return String("Backspace");
        case Key::LSHIFT: return String("LShift");
        case Key::RSHIFT: return String("RShift");
        case Key::LCTRL: return String("LCtrl");
        case Key::RCTRL: return String("RCtrl");
        case Key::MOUSE1: return String("Mouse1");
        case Key::MOUSE2: return String("Mouse2");
        case Key::MOUSE3: return String("Mouse3");
        default: return String("Unknown");
    }
}

Key Input::GetKeyFromName(const String& name) {
    if (name == "W" || name == "w") return Key::WKEY;
    if (name == "S" || name == "s") return Key::SKEY;
    if (name == "A" || name == "a") return Key::AKEY;
    if (name == "D" || name == "d") return Key::DKEY;
    if (name == "Space" || name == "space") return Key::SPACE;
    if (name == "Enter" || name == "enter") return Key::ENTER;
    if (name == "Escape" || name == "escape") return Key::ESCAPE;
    if (name == "Up" || name == "up") return Key::ARROW_UP;
    if (name == "Down" || name == "down") return Key::ARROW_DOWN;
    if (name == "Left" || name == "left") return Key::ARROW_LEFT;
    if (name == "Right" || name == "right") return Key::ARROW_RIGHT;
    if (name == "Tab" || name == "tab") return Key::TAB;
    if (name == "Mouse1" || name == "mouse1") return Key::MOUSE1;
    if (name == "Mouse2" || name == "mouse2") return Key::MOUSE2;
    if (name == "Mouse3" || name == "mouse3") return Key::MOUSE3;
    return Key::NONE;
}

} // namespace zq::engine
