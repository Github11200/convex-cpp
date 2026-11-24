/**
 * Identifier Tests
 * 
 * Tests for identifier validation (table names, field names).
 */

#include "doctest.h"
#include "convex/identifier.hpp"

using namespace convex;

TEST_SUITE("Identifier Validation") {
    
    TEST_CASE("Valid identifiers") {
        CHECK(is_valid_identifier("foo"));
        CHECK(is_valid_identifier("Foo"));
        CHECK(is_valid_identifier("FOO"));
        CHECK(is_valid_identifier("foo123"));
        CHECK(is_valid_identifier("foo_bar"));
        CHECK(is_valid_identifier("_foo"));
        CHECK(is_valid_identifier("a"));
        CHECK(is_valid_identifier("A"));
        CHECK(is_valid_identifier("a1"));
        CHECK(is_valid_identifier("camelCase"));
        CHECK(is_valid_identifier("PascalCase"));
        CHECK(is_valid_identifier("snake_case"));
        CHECK(is_valid_identifier("SCREAMING_SNAKE"));
        CHECK(is_valid_identifier("_private"));
        CHECK(is_valid_identifier("__dunder__"));
        CHECK(is_valid_identifier("a_"));
    }
    
    TEST_CASE("Invalid identifiers - empty") {
        CHECK_FALSE(is_valid_identifier(""));
    }
    
    TEST_CASE("Invalid identifiers - starts with number") {
        CHECK_FALSE(is_valid_identifier("123"));
        CHECK_FALSE(is_valid_identifier("1foo"));
        CHECK_FALSE(is_valid_identifier("0_init"));
    }
    
    TEST_CASE("Invalid identifiers - special characters") {
        CHECK_FALSE(is_valid_identifier("foo-bar"));
        CHECK_FALSE(is_valid_identifier("foo.bar"));
        CHECK_FALSE(is_valid_identifier("foo bar"));
        CHECK_FALSE(is_valid_identifier("foo@bar"));
        CHECK_FALSE(is_valid_identifier("foo$bar"));
        CHECK_FALSE(is_valid_identifier("foo!"));
        CHECK_FALSE(is_valid_identifier("foo?"));
        CHECK_FALSE(is_valid_identifier("foo:bar"));
        CHECK_FALSE(is_valid_identifier("foo/bar"));
    }
    
    TEST_CASE("Invalid identifiers - all underscores") {
        CHECK_FALSE(is_valid_identifier("_"));
        CHECK_FALSE(is_valid_identifier("__"));
        CHECK_FALSE(is_valid_identifier("___"));
        CHECK_FALSE(is_valid_identifier("____"));
    }
    
    TEST_CASE("Invalid identifiers - too long") {
        std::string long_id(MAX_IDENTIFIER_LEN, 'a');
        CHECK(is_valid_identifier(long_id));
        
        std::string too_long(MAX_IDENTIFIER_LEN + 1, 'a');
        CHECK_FALSE(is_valid_identifier(too_long));
    }
    
    TEST_CASE("Invalid identifiers - unicode") {
        CHECK_FALSE(is_valid_identifier("föö"));
        CHECK_FALSE(is_valid_identifier("日本語"));
        CHECK_FALSE(is_valid_identifier("emoji😀"));
    }
    
    TEST_CASE("check_valid_identifier throws on invalid") {
        CHECK_NOTHROW(check_valid_identifier("valid"));
        CHECK_THROWS_AS(check_valid_identifier(""), std::invalid_argument);
        CHECK_THROWS_AS(check_valid_identifier("123"), std::invalid_argument);
        CHECK_THROWS_AS(check_valid_identifier("__"), std::invalid_argument);
    }
    
    TEST_CASE("Identifier class") {
        SUBCASE("Valid construction") {
            Identifier id("valid_name");
            CHECK(id.str() == "valid_name");
        }
        
        SUBCASE("Invalid construction throws") {
            CHECK_THROWS_AS(Identifier("123invalid"), std::invalid_argument);
            CHECK_THROWS_AS(Identifier(""), std::invalid_argument);
        }
        
        SUBCASE("try_from returns nullopt for invalid") {
            CHECK(Identifier::try_from("valid").has_value());
            CHECK_FALSE(Identifier::try_from("123").has_value());
            CHECK_FALSE(Identifier::try_from("").has_value());
        }
        
        SUBCASE("Equality") {
            Identifier a("foo");
            Identifier b("foo");
            Identifier c("bar");
            
            CHECK(a == b);
            CHECK(a != c);
        }
        
        SUBCASE("Ordering") {
            Identifier a("aaa");
            Identifier b("bbb");
            
            CHECK(a < b);
        }
        
        SUBCASE("String conversion") {
            Identifier id("test");
            std::string s = id;
            CHECK(s == "test");
        }
    }
}
