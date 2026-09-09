#include <catch2/catch_test_macros.hpp>
#include "core/fixed/fixed_t.hpp"
#include "core/fixed/fixed_math.hpp"

TEST_CASE("Fixed-point arithmetic", "[fixed]") {
    using namespace zq::math;
    
    SECTION("Addition") {
        fixed_t a(512);  // 2.0
        fixed_t b(256);  // 1.0
        fixed_t result = a + b;
        REQUIRE(result.asInt() == 3);
    }
    
    SECTION("Subtraction") {
        fixed_t a(512);  // 2.0
        fixed_t b(256);  // 1.0
        fixed_t result = a - b;
        REQUIRE(result.asInt() == 1);
    }
    
    SECTION("Multiplication") {
        fixed_t a(512);  // 2.0
        fixed_t b(256);  // 1.0
        fixed_t result = a * b;
        REQUIRE(result.asInt() == 2);
    }
    
    SECTION("Division") {
        fixed_t a(512);  // 2.0
        fixed_t b(256);  // 1.0
        fixed_t result = a / b;
        REQUIRE(result.asInt() == 2);
    }
}

TEST_CASE("Vec3 operations", "[fixed][vec3]") {
    using namespace zq::math;
    
    SECTION("Addition") {
        Vec3 a(FixedFromInt(1), FixedFromInt(2), FixedFromInt(3));
        Vec3 b(FixedFromInt(4), FixedFromInt(5), FixedFromInt(6));
        Vec3 result = a + b;
        REQUIRE(result[0].asInt() == 5);
        REQUIRE(result[1].asInt() == 7);
        REQUIRE(result[2].asInt() == 9);
    }
    
    SECTION("Dot product") {
        Vec3 a(FixedFromInt(1), FixedFromInt(0), FixedFromInt(0));
        Vec3 b(FixedFromInt(5), FixedFromInt(0), FixedFromInt(0));
        fixed_t result = a.Dot(b);
        REQUIRE(result.asInt() == 5);
    }
    
    SECTION("Cross product") {
        Vec3 a(FixedFromInt(1), FixedFromInt(0), FixedFromInt(0));
        Vec3 b(FixedFromInt(0), FixedFromInt(1), FixedFromInt(0));
        Vec3 result = a.Cross(b);
        REQUIRE(result[0].asInt() == 0);
        REQUIRE(result[1].asInt() == 0);
        REQUIRE(result[2].asInt() == 1);
    }
}

TEST_CASE("Trigonometric functions", "[fixed][trig]") {
    using namespace zq::math;
    
    SECTION("Sin of zero") {
        fixed_t zero(0);
        fixed_t result = Sin(zero);
        REQUIRE(result.asInt() == 0);
    }
    
    SECTION("Cos of zero") {
        fixed_t zero(0);
        fixed_t result = Cos(zero);
        REQUIRE(result.asInt() == 256); // cos(0) = 1.0 = 256 in fixed-point
    }
}
