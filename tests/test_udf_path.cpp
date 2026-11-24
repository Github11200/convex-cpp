/**
 * UdfPath Tests
 * 
 * Tests for UDF path parsing and validation.
 */

#include "doctest.h"
#include "convex/udf_path.hpp"

using namespace convex;

TEST_SUITE("FunctionName") {
    
    TEST_CASE("Valid function names") {
        CHECK_NOTHROW(FunctionName("list"));
        CHECK_NOTHROW(FunctionName("get"));
        CHECK_NOTHROW(FunctionName("sendMessage"));
        CHECK_NOTHROW(FunctionName("_private"));
        CHECK_NOTHROW(FunctionName("default"));
    }
    
    TEST_CASE("Invalid function names") {
        CHECK_THROWS(FunctionName(""));
        CHECK_THROWS(FunctionName("123"));
        CHECK_THROWS(FunctionName("foo-bar"));
    }
    
    TEST_CASE("Default export") {
        auto fn = FunctionName::default_export();
        CHECK(fn.str() == "default");
    }
}

TEST_SUITE("ModulePath") {
    
    TEST_CASE("Valid module paths") {
        CHECK_NOTHROW(ModulePath("messages"));
        CHECK_NOTHROW(ModulePath("api/users"));
        CHECK_NOTHROW(ModulePath("api/v2/auth"));
        CHECK_NOTHROW(ModulePath("_system"));
        CHECK_NOTHROW(ModulePath("_system/auth"));
        CHECK_NOTHROW(ModulePath("messages.js"));
    }
    
    TEST_CASE("Invalid module paths") {
        CHECK_THROWS(ModulePath(""));
        CHECK_THROWS(ModulePath("/messages"));
        CHECK_THROWS(ModulePath("messages/"));
        CHECK_THROWS(ModulePath("api//users"));
        CHECK_THROWS(ModulePath("foo bar"));
        CHECK_THROWS(ModulePath("a/../b"));
        CHECK_THROWS(ModulePath("a.ts"));
    }
    
    TEST_CASE("System modules") {
        ModulePath system("_system/auth");
        ModulePath normal("messages");
        
        CHECK(system.is_system());
        CHECK_FALSE(normal.is_system());
        CHECK_FALSE(ModulePath("_other").is_system());
        CHECK(ModulePath("actions/_deps/whoo.js").is_deps());
        CHECK(ModulePath("http.js").is_http());
        CHECK_FALSE(ModulePath("actions/http.js").is_http());
    }
    
    TEST_CASE("Canonicalize") {
        ModulePath m1("messages");
        ModulePath m2("messages.js");
        
        CHECK(m1.canonicalize() == "messages.js");
        CHECK(m2.canonicalize() == "messages.js");
    }
}

TEST_SUITE("UdfPath") {
    
    TEST_CASE("Parse with function name") {
        UdfPath path("messages:list");
        
        CHECK(path.module().str() == "messages");
        CHECK(path.function_name().has_value());
        CHECK(path.function_name()->str() == "list");
    }
    
    TEST_CASE("Parse without function name") {
        UdfPath path("messages");
        
        CHECK(path.module().str() == "messages");
        CHECK_FALSE(path.function_name().has_value());
    }
    
    TEST_CASE("Parse nested module with function") {
        UdfPath path("api/users:get");
        
        CHECK(path.module().str() == "api/users");
        CHECK(path.function_name().has_value());
        CHECK(path.function_name()->str() == "get");
    }
    
    TEST_CASE("System path") {
        UdfPath system("_system/auth:verify");
        UdfPath normal("messages:list");
        
        CHECK(system.is_system());
        CHECK_FALSE(normal.is_system());
    }
    
    TEST_CASE("Canonicalize") {
        UdfPath p1("messages:list");
        UdfPath p2("messages");
        
        CHECK(p1.canonicalize() == "messages.js:list");
        CHECK(p2.canonicalize() == "messages.js:default");
    }
    
    TEST_CASE("to_string") {
        UdfPath p1("messages:list");
        UdfPath p2("messages");
        
        CHECK(p1.to_string() == "messages:list");
        CHECK(p2.to_string() == "messages");
    }
    
    TEST_CASE("Equality") {
        UdfPath a("messages:list");
        UdfPath b("messages:list");
        UdfPath c("messages:get");
        UdfPath d("messages");
        
        CHECK(a == b);
        CHECK(a != c);
        CHECK(a != d);
    }
}

TEST_SUITE("CanonicalizedUdfPath") {
    
    TEST_CASE("From UdfPath with function") {
        UdfPath path("messages:list");
        auto canonical = CanonicalizedUdfPath::from_udf_path(path);
        
        CHECK(canonical.module().str() == "messages.js");
        CHECK(canonical.function().str() == "list");
    }
    
    TEST_CASE("From UdfPath without function uses default") {
        UdfPath path("messages");
        auto canonical = CanonicalizedUdfPath::from_udf_path(path);
        
        CHECK(canonical.module().str() == "messages.js");
        CHECK(canonical.function().str() == "default");
    }
    
    TEST_CASE("to_string") {
        UdfPath path("api/users:get");
        auto canonical = CanonicalizedUdfPath::from_udf_path(path);
        
        CHECK(canonical.to_string() == "api/users.js:get");
        CHECK(canonical.strip().to_string() == "api/users:get");
        CHECK(CanonicalizedUdfPath::from_udf_path(UdfPath("test")).strip().to_string() == "test");
    }
}
