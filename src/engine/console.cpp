#include "engine/console.hpp"
#include "engine/cvar_system.hpp"
#include "engine/game.hpp"
#include "core/logging/logger.hpp"

namespace zq::engine {

Console::Console() = default;

Console& Console::Instance() {
    static Console instance;
    return instance;
}

void Console::Init() {
    auto& c = Instance();
    
    // Register built-in commands
    c.AddCommand("help", [](const String& args) {
        Console::Instance().Println("Available commands:");
        Console::Instance().Println("  help       - Show this help");
        Console::Instance().Println("  echo       - Print text");
        Console::Instance().Println("  clear      - Clear console");
        Console::Instance().Println("  quit       - Exit game");
        Console::Instance().Println("  set        - Set a CVar value");
        Console::Instance().Println("  cvar_list  - List all CVars");
        Console::Instance().Println("  exec       - Execute a config file");
        Console::Instance().Println("Type a CVar name to view its value");
        (void)args;
    });
    
    c.AddCommand("echo", [](const String& args) {
        Console::Instance().Println(args);
    });
    
    c.AddCommand("clear", [](const String& args) {
        (void)args;
        Console::Instance().line_count_ = 0;
        Console::Instance().line_start_ = 0;
    });
    
    c.AddCommand("quit", [](const String& args) {
        (void)args;
        Game::Cmd_Quit_f();
    });
    
    c.AddCommand("cvar_list", [](const String& args) {
        (void)args;
        Console::Instance().Println("Use 'cvarlist' to list CVars");
    });
    
    log::Info("Console initialized");
}

void Console::Shutdown() {
    log::Info("Console shutdown");
}

void Console::Print(const String& text) {
    lines_[line_start_] = text;
    line_start_ = (line_start_ + 1) % MAX_LINES;
    if (line_count_ < MAX_LINES) line_count_++;
}

void Console::Println(const String& text) {
    Print(text);
}

const String& Console::GetLine(int index) const {
    if (index < 0 || index >= line_count_) {
        static String empty;
        return empty;
    }
    int idx = (line_start_ - line_count_ + index + MAX_LINES) % MAX_LINES;
    if (idx < 0 || idx >= MAX_LINES) idx = 0;
    return lines_[idx];
}

void Console::AddCommand(const String& name, CommandFunc func) {
    commands_.Insert(name, func);
}

void Console::ExecuteString(const String& cmd) {
    Println(String("] ") + cmd);
    
    auto parts = cmd.Split(' ');
    if (parts.size() == 0 || parts[0].empty()) return;
    
    String command_name = parts[0];
    
    // Build args string from remaining parts
    String args;
    for (size_t i = 1; i < parts.size(); i++) {
        if (i > 1) args += ' ';
        args += parts[i];
    }
    
    // Check if it's a registered command
    if (commands_.Contains(command_name)) {
        commands_[command_name](args);
        return;
    }
    
    // Check if it's a CVar assignment (name value)
    if (args.size() > 0) {
        CVar* cvar = CVarSystem::GetCVar(command_name);
        if (cvar) {
            cvar->SetValue(args);
            Println(command_name + " = " + args);
            return;
        }
    }
    
    // Check if it's a CVar read
    CVar* cvar = CVarSystem::GetCVar(command_name);
    if (cvar) {
        Println(command_name + " = " + cvar->GetValue());
        return;
    }
    
    // Try CVarSystem::Execute (legacy path)
    CVarSystem::Execute(cmd);
    
    Println("Unknown command: " + command_name);
}

void Console::ExecuteLine(const String& line) {
    ExecuteString(line);
}

void Console::HandleChar(char c) {
    if (cursor_pos_ < MAX_INPUT - 1 && input_line_.size() < MAX_INPUT - 1) {
        size_t len = input_line_.size();
        if (cursor_pos_ < (int)len) {
            String before = input_line_.substr(0, cursor_pos_);
            String after = input_line_.substr(cursor_pos_);
            input_line_ = before;
            input_line_ += c;
            input_line_ += after;
        } else {
            input_line_ += c;
        }
        cursor_pos_++;
    }
}

void Console::HandleBackspace() {
    if (cursor_pos_ > 0 && input_line_.size() > 0) {
        String before = input_line_.substr(0, cursor_pos_ - 1);
        String after = input_line_.substr(cursor_pos_);
        input_line_ = before + after;
        cursor_pos_--;
    }
}

void Console::HandleDelete() {
    if (cursor_pos_ < (int)input_line_.size()) {
        String before = input_line_.substr(0, cursor_pos_);
        String after = input_line_.substr(cursor_pos_ + 1);
        input_line_ = before + after;
    }
}

void Console::HandleEnter() {
    String cmd = input_line_.Trim();
    if (cmd.size() > 0) {
        // Add to history
        if (history_count_ < MAX_HISTORY) {
            history_[history_count_++] = cmd;
        } else {
            // Shift history
            for (int i = 1; i < MAX_HISTORY; i++) {
                history_[i - 1] = history_[i];
            }
            history_[MAX_HISTORY - 1] = cmd;
        }
        history_pos_ = history_count_;
        
        ExecuteLine(cmd);
    }
    input_line_ = String();
    cursor_pos_ = 0;
}

void Console::HandleTab() {
    // Simple tab completion: try to match CVars or commands
    if (input_line_.empty()) return;
    
    // Find first word
    String prefix = input_line_;
    if (prefix.Find(' ') != String::npos) {
        prefix = prefix.substr(0, prefix.Find(' '));
    }
    
    // TODO: iterate commands_ and cvar_registry for matching prefixes
    // For now, no-op
}

void Console::HistoryUp() {
    if (history_pos_ > 0) {
        history_pos_--;
        input_line_ = history_[history_pos_];
        cursor_pos_ = input_line_.size();
    }
}

void Console::HistoryDown() {
    if (history_pos_ < history_count_ - 1) {
        history_pos_++;
        input_line_ = history_[history_pos_];
        cursor_pos_ = input_line_.size();
    } else {
        history_pos_ = history_count_;
        input_line_ = String();
        cursor_pos_ = 0;
    }
}

void Console::CursorLeft() {
    if (cursor_pos_ > 0) cursor_pos_--;
}

void Console::CursorRight() {
    if (cursor_pos_ < (int)input_line_.size()) cursor_pos_++;
}

void Console::Toggle() {
    active_ = !active_;
}

void Console::ClearInput() {
    input_line_ = String();
    cursor_pos_ = 0;
}

bool Console::IsActive() const {
    return active_;
}

} // namespace zq::engine
