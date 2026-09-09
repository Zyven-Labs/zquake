#pragma once
#include "core/container/string.hpp"

namespace zq::engine {

enum class Key {
    NONE,
    WKEY, SKEY, AKEY, DKEY,
    SPACE, ENTER, ESCAPE,
    ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT,
    TAB, BACKSPACE, LSHIFT, RSHIFT,
    LCTRL, RCTRL, LALT, RALT,
    Q, E, R, F, C, G, Z, X, V,
    NUM1, NUM2, NUM3, NUM4, NUM5, NUM6, NUM7, NUM8, NUM9, NUM0,
    MOUSE1, MOUSE2, MOUSE3,
    KEY_COUNT
};

class Input {
public:
    static void Init();
    static void Poll();
    static void HandleKeyEvent(int sdl_scancode, bool down);
    static void HandleMouseButton(int button, bool down);
    static void HandleMouseMotion(int xrel, int yrel);
    
    static bool IsKeyDown(Key key);
    static bool IsKeyJustPressed(Key key);
    static bool IsKeyJustReleased(Key key);
    static bool WasKeyPressed(Key key);   // pressed this frame (latch, edge captured during events)
    static String GetKeyString(Key key);
    static Key GetKeyFromName(const String& name);
    
    static int GetMouseDeltaX();
    static int GetMouseDeltaY();
    
    // For testing: simulate key state directly
    static void SetKeyState(Key key, bool down);
    static void ClearAll();
    
private:
    static Key ScancodeToKey(int scancode);
    
    static bool keys_[static_cast<int>(Key::KEY_COUNT)];
    static bool prev_keys_[static_cast<int>(Key::KEY_COUNT)];
    static bool latched_[static_cast<int>(Key::KEY_COUNT)];
    static int mouse_dx_;
    static int mouse_dy_;
};

}
