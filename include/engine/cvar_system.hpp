#pragma once
#include "core/container/string.hpp"
#include <functional>

namespace zq::engine {

enum class CVarFlags {
    None = 0,
    ReadOnly = 1 << 0,
    Replicated = 1 << 1,
    Cheat = 1 << 2
};

class CVar {
public:
    CVar(const String& name, const String& default_value, CVarFlags flags = CVarFlags::None);
    
    String GetName() const;
    String GetValue() const;
    void SetValue(const String& value);
    bool IsReadOnly() const;
    String GetDefault() const;
    
    int32_t GetInt() const;
    void SetInt(int32_t value);
    float GetFloat() const;
    void SetFloat(float value);
    
private:
    String name_;
    String value_;
    String default_;
    CVarFlags flags_;
};

class CVarSystem {
public:
    static CVar* RegisterCVar(const String& name, const String& default_value, CVarFlags flags = CVarFlags::None);
    static CVar* GetCVar(const String& name);
    static void Execute(const String& command);
    static void WriteConfig(const String& filename);
    static void LoadConfig(const String& filename);
};

}
