#include <catch2/catch_test_macros.hpp>
#include "engine/console.hpp"
#include "engine/cvar_system.hpp"
#include "engine/game.hpp"

static void fresh_console() {
    auto& c = zq::engine::Console::Instance();
    c.ClearInput();
    if (!c.IsActive()) c.Toggle();
    c.ClearInput();
}

TEST_CASE("Console line editing", "[console]") {
    using namespace zq::engine;
    auto& c = Console::Instance();
    fresh_console();

    SECTION("HandleChar appends text") {
        fresh_console();
        c.HandleChar('h'); c.HandleChar('e'); c.HandleChar('l'); c.HandleChar('l'); c.HandleChar('o');
        REQUIRE(c.GetInputLine() == "hello");
        REQUIRE(c.GetCursorPos() == 5);
    }

    SECTION("HandleBackspace removes char") {
        fresh_console();
        c.HandleChar('a'); c.HandleChar('b'); c.HandleChar('c');
        REQUIRE(c.GetInputLine() == "abc");
        c.HandleBackspace();
        REQUIRE(c.GetInputLine() == "ab");
        REQUIRE(c.GetCursorPos() == 2);
    }

    SECTION("Cursor movement") {
        fresh_console();
        c.HandleChar('x'); c.HandleChar('y'); c.HandleChar('z');
        c.CursorLeft(); REQUIRE(c.GetCursorPos() == 2);
        c.CursorLeft(); REQUIRE(c.GetCursorPos() == 1);
        c.CursorRight(); REQUIRE(c.GetCursorPos() == 2);
    }

    SECTION("Middle insertion") {
        fresh_console();
        c.HandleChar('a'); c.HandleChar('c');
        c.CursorLeft(); c.HandleChar('b');
        REQUIRE(c.GetInputLine() == "abc");
    }

    SECTION("HandleDelete removes at cursor") {
        fresh_console();
        c.HandleChar('a'); c.HandleChar('b'); c.HandleChar('c');
        c.CursorLeft(); c.CursorLeft();
        REQUIRE(c.GetCursorPos() == 1);
        c.HandleDelete();
        REQUIRE(c.GetInputLine() == "ac");
    }
}

TEST_CASE("CVarSystem operations", "[cvar]") {
    using namespace zq::engine;

    SECTION("Register and retrieve CVar") {
        CVar* cv = CVarSystem::RegisterCVar("test_var", "42", CVarFlags::None);
        REQUIRE(cv != nullptr);
        REQUIRE(cv->GetName() == "test_var");
        REQUIRE(cv->GetValue() == "42");
    }

    SECTION("Set and get CVar value") {
        CVar* cv = CVarSystem::RegisterCVar("test_var2", "10");
        REQUIRE(cv->GetValue() == "10");
        CVarSystem::Execute("test_var2 99");
        REQUIRE(cv->GetValue() == "99");
    }

    SECTION("CVar GetInt and GetFloat") {
        CVar* cv = CVarSystem::RegisterCVar("num_var", "256");
        REQUIRE(cv->GetInt() == 256);
        REQUIRE(cv->GetFloat() == 256.0f);
    }

    SECTION("ReadOnly CVar cannot be changed") {
        CVar* cv = CVarSystem::RegisterCVar("ro_var", "locked", CVarFlags::ReadOnly);
        REQUIRE(cv->IsReadOnly());
        cv->SetValue("unlocked");
        REQUIRE(cv->GetValue() == "locked");
    }

    SECTION("Negative int parsing") {
        CVar* cv = CVarSystem::RegisterCVar("neg_var", "-50");
        REQUIRE(cv->GetInt() == -50);
    }
}

TEST_CASE("Console ExecuteString", "[console]") {
    using namespace zq::engine;
    auto& c = Console::Instance();
    fresh_console();

    SECTION("CVar assignment through ExecuteString") {
        CVar* cv = CVarSystem::RegisterCVar("test_cvar3", "initial");
        c.ExecuteString("test_cvar3 changed");
        REQUIRE(cv->GetValue() == "changed");
    }

    SECTION("CVar read through ExecuteString") {
        CVarSystem::RegisterCVar("read_cvar3", "value123");
        c.ExecuteString("read_cvar3");
    }

    SECTION("Unknown command") {
        c.ExecuteString("nonexistent_command_xyz");
    }
}