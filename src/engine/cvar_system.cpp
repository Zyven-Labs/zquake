#include "engine/cvar_system.hpp"
#include "core/container/hash_map.hpp"
#include "core/container/array.hpp"
#include "core/logging/logger.hpp"
#include <cstdio>
#include <cstring>

namespace zq::engine {

CVar::CVar(const String& name, const String& default_value, CVarFlags flags)
    : name_(name), value_(default_value), default_(default_value), flags_(flags) {}

String CVar::GetName() const { return name_; }
String CVar::GetValue() const { return value_; }

void CVar::SetValue(const String& value) {
    if (static_cast<int>(flags_) & static_cast<int>(CVarFlags::ReadOnly)) return;
    value_ = value;
}

bool CVar::IsReadOnly() const { return (static_cast<int>(flags_) & static_cast<int>(CVarFlags::ReadOnly)) != 0; }

String CVar::GetDefault() const { return default_; }

int32_t CVar::GetInt() const {
    int32_t result = 0;
    int sign = 1;
    size_t i = 0;
    if (i < value_.size() && value_[i] == '-') { sign = -1; i++; }
    for (; i < value_.size(); i++) {
        char c = value_[i];
        if (c >= '0' && c <= '9') {
            result = result * 10 + (c - '0');
        } else {
            break;
        }
    }
    return result * sign;
}

void CVar::SetInt(int32_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    SetValue(String(buf));
}

float CVar::GetFloat() const {
    float result = 0.0f;
    sscanf(value_.c_str(), "%f", &result);
    return result;
}

void CVar::SetFloat(float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%f", value);
    SetValue(String(buf));
}

static HashMap<String, CVar*> cvar_registry;
static Array<CVar*> cvar_list;

CVar* CVarSystem::RegisterCVar(const String& name, const String& default_value, CVarFlags flags) {
    if (cvar_registry.Contains(name)) {
        return cvar_registry[name];
    }
    CVar* cvar = new CVar(name, default_value, flags);
    cvar_registry.Insert(name, cvar);
    cvar_list.PushBack(cvar);
    return cvar;
}

CVar* CVarSystem::GetCVar(const String& name) {
    if (!cvar_registry.Contains(name)) return nullptr;
    return cvar_registry[name];
}

void CVarSystem::Execute(const String& command) {
    auto parts = command.Split(' ');
    if (parts.size() >= 2) {
        CVar* cvar = GetCVar(parts[0]);
        if (cvar && !(cvar->IsReadOnly())) {
            String value;
            for (size_t i = 1; i < parts.size(); i++) {
                if (i > 1) value += ' ';
                value += parts[i];
            }
            cvar->SetValue(value);
        }
    }
}

void CVarSystem::WriteConfig(const String& filename) {
    std::FILE* f = std::fopen(filename.c_str(), "w");
    if (!f) {
        log::Error("CVarSystem: Cannot write config to file");
        return;
    }
    std::fprintf(f, "// zquake config\n");
    for (size_t i = 0; i < cvar_list.size(); i++) {
        CVar* cv = cvar_list[i];
        if (cv && !cv->IsReadOnly()) {
            std::fprintf(f, "%s \"%s\"\n", cv->GetName().c_str(), cv->GetValue().c_str());
        }
    }
    std::fclose(f);
    log::Info("Config written to ");
    log::Info(filename.c_str());
}

void CVarSystem::LoadConfig(const String& filename) {
    std::FILE* f = std::fopen(filename.c_str(), "r");
    if (!f) {
        log::Warn("CVarSystem: Cannot load config file");
        return;
    }
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        // Strip newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        
        // Skip comments and empty lines
        String s(line);
        String stripped = s.Trim();
        if (stripped.empty() || stripped[0] == '/' || stripped[0] == '#') continue;
        
        Execute(stripped);
    }
    std::fclose(f);
    log::Info("Config loaded from ");
    log::Info(filename.c_str());
}

} // namespace zq::engine
