#include <catch2/catch_test_macros.hpp>
#include "core/container/array.hpp"
#include "core/container/string.hpp"
#include "core/container/hash_map.hpp"

TEST_CASE("Array operations", "[array]") {
    using namespace zq;
    
    SECTION("Push and pop") {
        Array<int> arr;
        arr.PushBack(1);
        arr.PushBack(2);
        arr.PushBack(3);
        REQUIRE(arr.size() == 3);
        REQUIRE(arr[0] == 1);
        REQUIRE(arr[1] == 2);
        REQUIRE(arr[2] == 3);
    }
    
    SECTION("Remove") {
        Array<int> arr;
        arr.PushBack(1);
        arr.PushBack(2);
        arr.PushBack(3);
        arr.RemoveAt(1);
        REQUIRE(arr.size() == 2);
        REQUIRE(arr[0] == 1);
        REQUIRE(arr[1] == 3);
    }
    
    SECTION("Find") {
        Array<int> arr;
        arr.PushBack(10);
        arr.PushBack(20);
        arr.PushBack(30);
        REQUIRE(arr.Find(20) == 1);
        REQUIRE(arr.Find(99) == Array<int>::npos);
    }
}

TEST_CASE("String operations", "[string]") {
    using namespace zq;
    
    SECTION("Concatenation") {
        String a("Hello");
        String b(" World");
        String result = a + b;
        REQUIRE(result == "Hello World");
    }
    
    SECTION("Substring") {
        String s("Hello World");
        String sub = s.substr(0, 5);
        REQUIRE(sub == "Hello");
    }
    
    SECTION("Split") {
        String s("a,b,c");
        auto parts = s.Split(',');
        REQUIRE(parts.size() == 3);
        REQUIRE(parts[0] == "a");
        REQUIRE(parts[1] == "b");
        REQUIRE(parts[2] == "c");
    }
    
    SECTION("To number") {
        String s("42");
        REQUIRE(s.ToInt() == 42);
    }
}

TEST_CASE("HashMap operations", "[hashmap]") {
    using namespace zq;
    
    SECTION("Insert and get") {
        HashMap<String, int> map;
        map.Insert("one", 1);
        map.Insert("two", 2);
        map.Insert("three", 3);
        REQUIRE(map.Get("one") == 1);
        REQUIRE(map.Get("two") == 2);
        REQUIRE(map.Get("three") == 3);
    }
    
    SECTION("Contains") {
        HashMap<String, int> map;
        map.Insert("key", 42);
        REQUIRE(map.Contains("key"));
        REQUIRE(!map.Contains("missing"));
    }
    
    SECTION("Remove") {
        HashMap<String, int> map;
        map.Insert("key", 42);
        map.Remove("key");
        REQUIRE(map.Size() == 0);
    }
}
