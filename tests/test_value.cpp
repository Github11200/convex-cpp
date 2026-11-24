/**
 * Value Type Tests
 * 
 * Tests for the convex::Value type which represents Convex values.
 */

#include "doctest.h"
#include "convex/value.hpp"
#include <cmath>
#include <limits>

using namespace convex;

TEST_SUITE("Value") {
    
    TEST_CASE("Null values") {
        SUBCASE("Default constructor creates null") {
            Value v;
            CHECK(v.is_null());
            CHECK(v.type() == Value::Type::Null);
        }
        
        SUBCASE("nullptr constructor creates null") {
            Value v(nullptr);
            CHECK(v.is_null());
        }
        
        SUBCASE("Null to_string") {
            Value v;
            CHECK(v.to_string() == "null");
        }
    }
    
    TEST_CASE("Int64 values") {
        SUBCASE("Positive integers") {
            Value v(42LL);
            CHECK(v.is_int64());
            CHECK(v.type() == Value::Type::Int64);
            CHECK(v.as_int64() == 42);
        }
        
        SUBCASE("Negative integers") {
            Value v(-123LL);
            CHECK(v.is_int64());
            CHECK(v.as_int64() == -123);
        }
        
        SUBCASE("Zero") {
            Value v(0LL);
            CHECK(v.is_int64());
            CHECK(v.as_int64() == 0);
        }
        
        SUBCASE("Large integers") {
            int64_t large = 9007199254740993LL;  // > 2^53 (JS safe integer limit)
            Value v(large);
            CHECK(v.as_int64() == large);
        }
        
        SUBCASE("Int64 limits") {
            Value max_val(std::numeric_limits<int64_t>::max());
            Value min_val(std::numeric_limits<int64_t>::min());
            CHECK(max_val.as_int64() == std::numeric_limits<int64_t>::max());
            CHECK(min_val.as_int64() == std::numeric_limits<int64_t>::min());
        }
        
        SUBCASE("Int constructor promotes to Int64") {
            Value v(42);
            CHECK(v.is_int64());
            CHECK(v.as_int64() == 42);
        }
        
        SUBCASE("Int64 to_string") {
            Value v(12345LL);
            CHECK(v.to_string() == "12345");
        }
    }
    
    TEST_CASE("Float64 values") {
        SUBCASE("Positive floats") {
            Value v(3.14159);
            CHECK(v.is_float64());
            CHECK(v.type() == Value::Type::Float64);
            CHECK(v.as_float64() == doctest::Approx(3.14159));
        }
        
        SUBCASE("Negative floats") {
            Value v(-2.71828);
            CHECK(v.is_float64());
            CHECK(v.as_float64() == doctest::Approx(-2.71828));
        }
        
        SUBCASE("Zero float") {
            Value v(0.0);
            CHECK(v.is_float64());
            CHECK(v.as_float64() == 0.0);
        }
        
        SUBCASE("Infinity") {
            Value pos_inf(std::numeric_limits<double>::infinity());
            Value neg_inf(-std::numeric_limits<double>::infinity());
            CHECK(std::isinf(pos_inf.as_float64()));
            CHECK(pos_inf.as_float64() > 0);
            CHECK(std::isinf(neg_inf.as_float64()));
            CHECK(neg_inf.as_float64() < 0);
        }
        
        SUBCASE("NaN") {
            Value v(std::nan(""));
            CHECK(v.is_float64());
            CHECK(std::isnan(v.as_float64()));
        }
    }
    
    TEST_CASE("Boolean values") {
        SUBCASE("True") {
            Value v(true);
            CHECK(v.is_boolean());
            CHECK(v.type() == Value::Type::Boolean);
            CHECK(v.as_boolean() == true);
            CHECK(v.to_string() == "true");
        }
        
        SUBCASE("False") {
            Value v(false);
            CHECK(v.is_boolean());
            CHECK(v.as_boolean() == false);
            CHECK(v.to_string() == "false");
        }
    }
    
    TEST_CASE("String values") {
        SUBCASE("Regular string") {
            Value v("hello");
            CHECK(v.is_string());
            CHECK(v.type() == Value::Type::String);
            CHECK(v.as_string() == "hello");
        }
        
        SUBCASE("std::string") {
            std::string s = "world";
            Value v(s);
            CHECK(v.is_string());
            CHECK(v.as_string() == "world");
        }
        
        SUBCASE("Empty string") {
            Value v("");
            CHECK(v.is_string());
            CHECK(v.as_string() == "");
        }
        
        SUBCASE("Unicode string") {
            Value v("こんにちは");
            CHECK(v.is_string());
            CHECK(v.as_string() == "こんにちは");
        }
        
        SUBCASE("String with special characters") {
            Value v("line1\nline2\ttab\"quote");
            CHECK(v.as_string() == "line1\nline2\ttab\"quote");
        }
        
        SUBCASE("String to_string includes quotes") {
            Value v("test");
            CHECK(v.to_string() == "\"test\"");
        }
    }
    
    TEST_CASE("Bytes values") {
        SUBCASE("Regular bytes") {
            Bytes data = {0x00, 0x01, 0x02, 0xFF};
            Value v(data);
            CHECK(v.is_bytes());
            CHECK(v.type() == Value::Type::Bytes);
            CHECK(v.as_bytes() == data);
        }
        
        SUBCASE("Empty bytes") {
            Bytes empty;
            Value v(empty);
            CHECK(v.is_bytes());
            CHECK(v.as_bytes().empty());
        }
    }
    
    TEST_CASE("Array values") {
        SUBCASE("Simple array") {
            Array arr = {Value(1LL), Value(2LL), Value(3LL)};
            Value v(arr);
            CHECK(v.is_array());
            CHECK(v.type() == Value::Type::Array);
            CHECK(v.as_array().size() == 3);
        }
        
        SUBCASE("Mixed type array") {
            Array arr = {Value(1LL), Value("two"), Value(true), Value()};
            Value v(arr);
            CHECK(v.as_array().size() == 4);
            CHECK(v.as_array()[0].as_int64() == 1);
            CHECK(v.as_array()[1].as_string() == "two");
            CHECK(v.as_array()[2].as_boolean() == true);
            CHECK(v.as_array()[3].is_null());
        }
        
        SUBCASE("Empty array") {
            Array empty;
            Value v(empty);
            CHECK(v.is_array());
            CHECK(v.as_array().empty());
        }
        
        SUBCASE("Nested array") {
            Array inner = {Value(1LL), Value(2LL)};
            Array outer = {Value(inner), Value(3LL)};
            Value v(outer);
            CHECK(v.as_array().size() == 2);
            CHECK(v.as_array()[0].is_array());
            CHECK(v.as_array()[0].as_array().size() == 2);
        }
        
        SUBCASE("Array index operator") {
            Array arr = {Value(10LL), Value(20LL), Value(30LL)};
            Value v(arr);
            CHECK(v[0].as_int64() == 10);
            CHECK(v[1].as_int64() == 20);
            CHECK(v[2].as_int64() == 30);
        }
        
        SUBCASE("Initializer list creates array") {
            Value v = {Value(1LL), Value(2LL)};
            CHECK(v.is_array());
            CHECK(v.as_array().size() == 2);
        }
    }
    
    TEST_CASE("Object values") {
        SUBCASE("Simple object") {
            Object obj = {{"name", Value("Alice")}, {"age", Value(30LL)}};
            Value v(obj);
            CHECK(v.is_object());
            CHECK(v.type() == Value::Type::Object);
            CHECK(v.as_object().size() == 2);
        }
        
        SUBCASE("Empty object") {
            Object empty;
            Value v(empty);
            CHECK(v.is_object());
            CHECK(v.as_object().empty());
        }
        
        SUBCASE("Object key access") {
            Object obj = {{"key", Value("value")}};
            Value v(obj);
            CHECK(v["key"].as_string() == "value");
        }
        
        SUBCASE("Object contains") {
            Object obj = {{"exists", Value(1LL)}};
            Value v(obj);
            CHECK(v.contains("exists"));
            CHECK_FALSE(v.contains("missing"));
        }
        
        SUBCASE("Nested object") {
            Object inner = {{"x", Value(1LL)}, {"y", Value(2LL)}};
            Object outer = {{"point", Value(inner)}, {"name", Value("origin")}};
            Value v(outer);
            CHECK(v["point"].is_object());
            CHECK(v["point"]["x"].as_int64() == 1);
            CHECK(v["point"]["y"].as_int64() == 2);
        }
    }
    
    TEST_CASE("Value equality") {
        SUBCASE("Null equality") {
            CHECK(Value() == Value());
            CHECK(Value() == Value(nullptr));
        }
        
        SUBCASE("Int64 equality") {
            CHECK(Value(42LL) == Value(42LL));
            CHECK_FALSE(Value(42LL) == Value(43LL));
        }
        
        SUBCASE("Float64 equality") {
            CHECK(Value(3.14) == Value(3.14));
            CHECK_FALSE(Value(3.14) == Value(3.15));
        }
        
        SUBCASE("String equality") {
            CHECK(Value("hello") == Value("hello"));
            CHECK_FALSE(Value("hello") == Value("world"));
        }
        
        SUBCASE("Different types are not equal") {
            CHECK_FALSE(Value(42LL) == Value(42.0));
            CHECK_FALSE(Value("42") == Value(42LL));
            CHECK_FALSE(Value() == Value(false));
        }
        
        SUBCASE("Array equality") {
            Array a1 = {Value(1LL), Value(2LL)};
            Array a2 = {Value(1LL), Value(2LL)};
            Array a3 = {Value(1LL), Value(3LL)};
            CHECK(Value(a1) == Value(a2));
            CHECK_FALSE(Value(a1) == Value(a3));
        }
        
        SUBCASE("Object equality") {
            Object o1 = {{"a", Value(1LL)}};
            Object o2 = {{"a", Value(1LL)}};
            Object o3 = {{"a", Value(2LL)}};
            CHECK(Value(o1) == Value(o2));
            CHECK_FALSE(Value(o1) == Value(o3));
        }
    }
    
    TEST_CASE("Type errors") {
        SUBCASE("Accessing wrong type throws") {
            Value v(42LL);
            CHECK_THROWS(v.as_string());
            CHECK_THROWS(v.as_boolean());
            CHECK_THROWS(v.as_float64());
            CHECK_THROWS(v.as_array());
            CHECK_THROWS(v.as_object());
        }
        
        SUBCASE("Index on non-array throws") {
            Value v(42LL);
            CHECK_THROWS(v[0]);
        }
        
        SUBCASE("Key on non-object throws") {
            Value v(42LL);
            CHECK_THROWS(v["key"]);
        }
    }
    
    TEST_CASE("Value to_string") {
        SUBCASE("Complex nested structure") {
            Array arr;
            arr.push_back(Value(1LL));
            arr.push_back(Value(2LL));
            Object obj;
            obj["name"] = Value("test");
            obj["values"] = Value(arr);
            Value v(obj);
            std::string s = v.to_string();
            CHECK(s.find("\"name\"") != std::string::npos);
            CHECK(s.find("\"test\"") != std::string::npos);
        }
    }
}
