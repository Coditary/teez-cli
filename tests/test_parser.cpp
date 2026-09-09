#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

#include "teez/cli/parser.hpp"

TEST_CASE("parse_args accepts 'teez run ./tests'", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "run", "./tests"});

    REQUIRE(result.ok);
    REQUIRE(result.context.command == "run");
    REQUIRE(result.context.target_path == "./tests");
}

TEST_CASE("parse_args rejects unknown subcommand", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "deploy", "./tests"});

    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("parse_args accepts 'teez run' without path", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "run"});

    REQUIRE(result.ok);
    REQUIRE(result.context.command == "run");
    REQUIRE(result.context.target_path == ".");
}

TEST_CASE("parse_args rejects empty args", "[parser]") {
    const auto result = teez::cli::parse_args({});

    REQUIRE_FALSE(result.ok);
}

TEST_CASE("parse_args accepts test filter flags", "[parser]") {
    const auto result = teez::cli::parse_args(
        {"teez", "run", ".", "-f", "tests/system/.*", "-t", "system", "-t", "integration", "-p",
         "::system::", "-n", "return 999*"});

    REQUIRE(result.ok);
    REQUIRE(result.context.filters.file_regex.has_value());
    REQUIRE(*result.context.filters.file_regex == "tests/system/.*");
    REQUIRE(result.context.filters.types.size() == 2);
    REQUIRE(result.context.filters.types[0] == "system");
    REQUIRE(result.context.filters.types[1] == "integration");
    REQUIRE(result.context.filters.id_substring.has_value());
    REQUIRE(*result.context.filters.id_substring == "::system::");
    REQUIRE(result.context.filters.name_glob.has_value());
    REQUIRE(*result.context.filters.name_glob == "return 999*");
}

TEST_CASE("parse_args rejects conflicting name filters", "[parser]") {
    const auto result =
        teez::cli::parse_args({"teez", "run", ".", "-n", "auth*", "--name-pattern", "^auth$"});

    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.find("-n and --name-pattern") != std::string::npos);
}

TEST_CASE("parse_args accepts ctest filter flags", "[parser]") {
    const auto result =
        teez::cli::parse_args({"teez", "run", ".", "-R", "discovery", "-E", "integration"});

    REQUIRE(result.ok);
    REQUIRE(result.context.target_path == ".");
    REQUIRE(result.context.ctest.regex.has_value());
    REQUIRE(*result.context.ctest.regex == "discovery");
    REQUIRE(result.context.ctest.exclude.has_value());
    REQUIRE(*result.context.ctest.exclude == "integration");
}

TEST_CASE("parse_args accepts ctest label flags", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "run", "build", "-L", "unit", "--exclude-label",
                                               "slow"});

    REQUIRE(result.ok);
    REQUIRE(result.context.target_path == "build");
    REQUIRE(*result.context.ctest.label == "unit");
    REQUIRE(*result.context.ctest.exclude_label == "slow");
}

TEST_CASE("parse_args accepts list subcommand with tree by default", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "list", ".", "-R", "discovery"});

    REQUIRE(result.ok);
    REQUIRE(result.context.command == "list");
    REQUIRE(result.context.target_path == ".");
    REQUIRE(result.list.tree);
    REQUIRE_FALSE(result.list.json);
    REQUIRE(*result.context.ctest.regex == "discovery");
}

TEST_CASE("parse_args accepts list --flat", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "list", ".", "--flat"});

    REQUIRE(result.ok);
    REQUIRE_FALSE(result.list.tree);
}

TEST_CASE("parse_args accepts --update-snapshots on run", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "run", "--update-snapshots", "."});

    REQUIRE(result.ok);
    REQUIRE(result.update_snapshots);
}

TEST_CASE("parse_args accepts global --update-snapshots", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "--update-snapshots", "run", "."});

    REQUIRE(result.ok);
    REQUIRE(result.update_snapshots);
}

TEST_CASE("parse_args accepts run output flags", "[parser]") {
    const auto progress_json = teez::cli::parse_args({"teez", "run", ".", "--progress", "--json"});
    REQUIRE(progress_json.ok);
    REQUIRE(progress_json.run.progress);
    REQUIRE(progress_json.run.json);

    const auto json_only = teez::cli::parse_args({"teez", "run", ".", "--json"});
    REQUIRE(json_only.ok);
    REQUIRE_FALSE(json_only.run.progress);
    REQUIRE(json_only.run.json);

    const auto default_run = teez::cli::parse_args({"teez", "run", "."});
    REQUIRE(default_run.ok);
    REQUIRE_FALSE(default_run.run.progress);
    REQUIRE_FALSE(default_run.run.json);
    REQUIRE(default_run.run.ui);

    const auto simple_run = teez::cli::parse_args({"teez", "run", ".", "--simple"});
    REQUIRE(simple_run.ok);
    REQUIRE_FALSE(simple_run.run.ui);

    const auto json_run = teez::cli::parse_args({"teez", "run", ".", "--json"});
    REQUIRE(json_run.ok);
    REQUIRE_FALSE(json_run.run.ui);
}

TEST_CASE("parse_args handles --help", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "--help"});

    REQUIRE(result.ok);
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code == 0);
}

TEST_CASE("parse_args accepts --config", "[parser]") {
    const auto result = teez::cli::parse_args(
        {"teez", "--config", "./custom/teez.config.lua", "run", "./tests"});

    REQUIRE(result.ok);
    REQUIRE(result.config_file.has_value());
    REQUIRE(*result.config_file == std::filesystem::path("./custom/teez.config.lua"));
}

TEST_CASE("parse_args accepts coverage subcommand with reporter flags", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "coverage", ".", "--reporter", "json",
                                               "--input", "coverage.lcov", "--output",
                                               "coverage/report.json"});

    REQUIRE(result.ok);
    REQUIRE(result.context.command == "coverage");
    REQUIRE(result.context.target_path == ".");
    REQUIRE(result.coverage.reporter == "json");
    REQUIRE(result.coverage.input == std::filesystem::path("coverage.lcov"));
    REQUIRE(result.coverage.output == std::filesystem::path("coverage/report.json"));
}

TEST_CASE("parse_args rejects unknown coverage reporter", "[parser]") {
    const auto result =
        teez::cli::parse_args({"teez", "coverage", ".", "--reporter", "html", "--input", "in.lcov"});

    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("parse_args accepts discover subcommand", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "discover", ".", "--json"});

    REQUIRE(result.ok);
    REQUIRE(result.context.command == "discover");
    REQUIRE(result.context.target_path == ".");
    REQUIRE(result.project.json);
}

TEST_CASE("parse_args accepts config subcommand", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "config", ".", "--validate", "--json"});

    REQUIRE(result.ok);
    REQUIRE(result.context.command == "config");
    REQUIRE(result.project.validate);
    REQUIRE(result.project.json);
}

TEST_CASE("parse_args accepts init subcommand", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "init", "sandbox", "--with-demo", "--force"});

    REQUIRE(result.ok);
    REQUIRE(result.context.command == "init");
    REQUIRE(result.context.target_path == "sandbox");
    REQUIRE(result.project.with_demo);
    REQUIRE(result.project.force);
}

TEST_CASE("parse_args accepts global profile flag", "[parser]") {
    const auto result = teez::cli::parse_args({"teez", "--profile", "ci", "run", "."});

    REQUIRE(result.ok);
    REQUIRE(result.profile == "ci");
}

TEST_CASE("parse_args accepts run report flags", "[parser]") {
    const auto result = teez::cli::parse_args(
        {"teez", "run", ".", "--report", "junit", "--report-output", "out/report.xml"});

    REQUIRE(result.ok);
    REQUIRE(result.test_report.reporter == "junit");
    REQUIRE(result.test_report.output == std::filesystem::path("out/report.xml"));
}
