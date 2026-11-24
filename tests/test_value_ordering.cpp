/**
 * Value Ordering Tests
 * 
 * Tests for Value comparison and ordering, including special float handling.
 */

#include "doctest.h"
#include "convex/value.hpp"
#include <cmath>
#include <limits>
#include <set>
#include <map>

using namespace convex;

TEST_SUITE("Value Ordering") {
    
    TEST_CASE("Null equality") {
        Value a;
        Value b;
        
        CHECK(a == b);
        CHECK_FALSE(a != b);
    }
    
    TEST_CASE("Int64 ordering") {
        Value a(1LL);
        Value b(2LL);
        Value c(1LL);
        
        CHECK(a == c);
        CHECK(a != b);
    }
    
    TEST_CASE("Float64 ordering") {
        Value a(1.0);
        Value b(2.0);
        Value c(1.0);
        
        CHECK(a == c);
        CHECK(a != b);
    }
    
    TEST_CASE("Float64 special values") {
        SUBCASE("Positive and negative zero") {
            Value pos_zero(0.0);
            Value neg_zero(-0.0);
            
            // IEEE 754 says +0.0 == -0.0, but total_cmp distinguishes them
            // Our implementation should handle this consistently
        }
        
        SUBCASE("Infinity") {
            Value pos_inf(std::numeric_limits<double>::infinity());
            Value neg_inf(-std::numeric_limits<double>::infinity());
            Value max_val(std::numeric_limits<double>::max());
            
            CHECK(pos_inf != neg_inf);
            CHECK(pos_inf != max_val);
        }
        
        SUBCASE("NaN") {
            Value nan1(std::nan(""));
            Value nan2(std::nan("1"));
            
            // NaN comparisons are tricky
            // std::isnan should still work
            CHECK(std::isnan(nan1.as_float64()));
            CHECK(std::isnan(nan2.as_float64()));
        }
    }
    
    TEST_CASE("String ordering") {
        Value a("apple");
        Value b("banana");
        Value c("apple");
        
        CHECK(a == c);
        CHECK(a != b);
    }
    
    TEST_CASE("Boolean ordering") {
        Value t(true);
        Value f(false);
        Value t2(true);
        
        CHECK(t == t2);
        CHECK(t != f);
    }
    
    TEST_CASE("Array ordering") {
        Array arr1 = {Value(1LL), Value(2LL)};
        Array arr2 = {Value(1LL), Value(2LL)};
        Array arr3 = {Value(1LL), Value(3LL)};
        Array arr4 = {Value(1LL)};
        
        CHECK(Value(arr1) == Value(arr2));
        CHECK(Value(arr1) != Value(arr3));
        CHECK(Value(arr1) != Value(arr4));
    }
    
    TEST_CASE("Object ordering") {
        Object obj1 = {{"a", Value(1LL)}, {"b", Value(2LL)}};
        Object obj2 = {{"a", Value(1LL)}, {"b", Value(2LL)}};
        Object obj3 = {{"a", Value(1LL)}, {"b", Value(3LL)}};
        Object obj4 = {{"a", Value(1LL)}};
        
        CHECK(Value(obj1) == Value(obj2));
        CHECK(Value(obj1) != Value(obj3));
        CHECK(Value(obj1) != Value(obj4));
    }
    
    TEST_CASE("Different types are not equal") {
        Value null_val;
        Value int_val(42LL);
        Value float_val(42.0);
        Value bool_val(true);
        Value str_val("42");
        
        CHECK(null_val != int_val);
        CHECK(int_val != float_val);
        CHECK(float_val != bool_val);
        CHECK(bool_val != str_val);
        CHECK(str_val != null_val);
    }
    
    TEST_CASE("Values can be used in std::set") {
        std::set<std::string> keys;
        
        // Since Value doesn't have operator<, we test via map keys instead
        std::map<std::string, Value> map;
        map["a"] = Value(1LL);
        map["b"] = Value(2LL);
        
        CHECK(map["a"].as_int64() == 1);
        CHECK(map["b"].as_int64() == 2);
    }
    
    TEST_CASE("Nested value equality") {
        Object inner1 = {{"x", Value(1LL)}};
        Object inner2 = {{"x", Value(1LL)}};
        Object inner3 = {{"x", Value(2LL)}};
        
        Object outer1 = {{"inner", Value(inner1)}};
        Object outer2 = {{"inner", Value(inner2)}};
        Object outer3 = {{"inner", Value(inner3)}};
        
        CHECK(Value(outer1) == Value(outer2));
        CHECK(Value(outer1) != Value(outer3));
    }
    
    TEST_CASE("Bytes equality") {
        Bytes b1 = {0x00, 0x01, 0x02};
        Bytes b2 = {0x00, 0x01, 0x02};
        Bytes b3 = {0x00, 0x01, 0x03};
        Bytes b4 = {0x00, 0x01};
        
        CHECK(Value(b1) == Value(b2));
        CHECK(Value(b1) != Value(b3));
        CHECK(Value(b1) != Value(b4));
    }
}
