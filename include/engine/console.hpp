#pragma once
#include "core/container/string.hpp"
#include "core/container/array.hpp"
#include "core/container/hash_map.hpp"
#include <functional>

namespace zq::engine {

class Console {
public:
    Console();
    ~Console() = default;
    
    static constexpr int MAX_LINES = 512;
    static constexpr int MAX_HISTORY = 64;
    static constexpr int MAX_INPUT = 256;
    
    static Console& Instance();
    static void Init();
    void Shutdown();
    
    // Command execution
    void ExecuteString(const String& cmd);
    
    // Text output
    void Print(const String& text);
    void Println(const String& text);
    
    // Command registration
    using CommandFunc = std::function<void(const String& args)>;
    void AddCommand(const String& name, CommandFunc func);
    
    // Console UI
    void Toggle();
    bool IsActive() const;
    
    // Input handling for console line editing
    void HandleChar(char c);
    void HandleBackspace();
    void HandleDelete();
    void HandleEnter();
    void HandleTab();
    void HistoryUp();
    void HistoryDown();
    void CursorLeft();
    void CursorRight();
    void ClearInput();
    
    // Accessors for rendering
    const String& GetInputLine() const { return input_line_; }
    int GetCursorPos() const { return cursor_pos_; }
    int GetLineCount() const { return line_count_; }
    const String& GetLine(int index) const;
    
private:
    void ExecuteLine(const String& line);
    
    String input_line_;
    int cursor_pos_ = 0;
    
    // Ring buffer of printed text lines
    String lines_[MAX_LINES];
    int line_count_ = 0;
    int line_start_ = 0;
    
    // Command registry
    HashMap<String, CommandFunc> commands_;
    
    // Command history
    String history_[MAX_HISTORY];
    int history_count_ = 0;
    int history_pos_ = 0;
    
    bool active_ = false;
};

}
