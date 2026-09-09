#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "teez/cli/list_format.hpp"

TEST_CASE("write_test_list prints flat ids with --flat semantics", "[list_format]") {
    std::ostringstream out;
    teez::cli::write_test_list({"alpha", "beta"}, {.tree = false}, out);

    REQUIRE(out.str() == "alpha\nbeta\n");
}

TEST_CASE("write_test_list renders pytest ids as tree by default", "[list_format]") {
    std::ostringstream out;
    teez::cli::write_test_list(
        {
            "tests/test_api.py::TestAuth::test_login",
            "tests/test_api.py::TestAuth::test_logout",
            "tests/test_api.py::TestHealth::test_ok",
        },
        {.tree = true},
        out);

    REQUIRE(out.str().find("tests/test_api.py") != std::string::npos);
    REQUIRE(out.str().find("├── TestAuth") != std::string::npos);
    REQUIRE(out.str().find("└── test_logout") != std::string::npos);
}

TEST_CASE("write_test_list renders catch2 tag paths as tree", "[list_format]") {
    std::ostringstream out;
    teez::cli::write_test_list(
        {
            "discovery/load_manifests reads pytest and worker plugin manifests",
            "discovery/discover_plugin matches pytest.ini in project root",
            "ctest/plugin/ctest plugin parse_line maps Passed to pass event",
        },
        {.tree = true},
        out);

    REQUIRE(out.str().find("ctest") != std::string::npos);
    REQUIRE(out.str().find("discovery") != std::string::npos);
    REQUIRE(out.str().find("│   └── plugin") != std::string::npos);
    REQUIRE(out.str().find("load_manifests reads pytest and worker plugin manifests") !=
            std::string::npos);
}

TEST_CASE("write_test_list emits NDJSON test events", "[list_format]") {
    std::ostringstream out;
    teez::cli::write_test_list({"one", "two"}, {.json = true}, out);

    REQUIRE(out.str() == R"({"event":"test","id":"one"})"
R"(
{"event":"test","id":"two"}
)");
}
