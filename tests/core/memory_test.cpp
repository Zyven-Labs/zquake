#include <catch2/catch_test_macros.hpp>
#include "core/memory/allocator.hpp"

TEST_CASE("Stack allocator", "[memory]") {
    using namespace zq::memory;
    
    SECTION("Basic allocation") {
        StackAllocator alloc(1024);
        void* ptr = alloc.Allocate(128);
        REQUIRE(ptr != nullptr);
        REQUIRE(alloc.Used() == 128);
    }
    
    SECTION("Reset") {
        StackAllocator alloc(1024);
        alloc.Allocate(256);
        alloc.Reset();
        REQUIRE(alloc.Used() == 0);
    }
}

TEST_CASE("Pool allocator", "[memory]") {
    using namespace zq::memory;
    
    SECTION("Basic allocation") {
        PoolAllocator alloc(64, 16);
        void* ptr1 = alloc.Allocate(32);
        void* ptr2 = alloc.Allocate(32);
        REQUIRE(ptr1 != nullptr);
        REQUIRE(ptr2 != nullptr);
        REQUIRE(alloc.Used() == 2);
    }
    
    SECTION("Deallocation") {
        PoolAllocator alloc(64, 16);
        void* ptr = alloc.Allocate(32);
        alloc.Deallocate(ptr);
        REQUIRE(alloc.Used() == 0);
    }
}
