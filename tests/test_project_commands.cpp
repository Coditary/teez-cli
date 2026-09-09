#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "teez/cli/project_commands.hpp"

TEST_CASE("format_command_line quotes arguments with spaces", "[project_commands]") {
    teez::core::CommandSpec spec;
    spec.command = "pytest";
    spec.args = {"-q", "path/with space"};

    REQUIRE(teez::cli::format_command_line(spec) == "pytest -q 'path/with space'");
}

TEST_CASE("run_init_command creates starter config", "[project_commands]") {
    const auto dir = std::filesystem::temp_directory_path() / "teez_init_test";
    std::filesystem::remove_all(dir);

    const auto result = teez::cli::run_init_command(dir, {});

    REQUIRE(result.created);
    REQUIRE(std::filesystem::exists(result.config_path));
    std::ifstream config_file(result.config_path);
    std::string content((std::istreambuf_iterator<char>(config_file)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("projects") != std::string::npos);
}

TEST_CASE("run_init_command refuses existing config without force", "[project_commands]") {
    const auto dir = std::filesystem::temp_directory_path() / "teez_init_existing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "teez.config.lua") << "return {}\n";

    const auto result = teez::cli::run_init_command(dir, {});

    REQUIRE_FALSE(result.created);
    REQUIRE_FALSE(result.error.empty());
}

TEST_CASE("run_init_command can create demo test", "[project_commands]") {
    const auto dir = std::filesystem::temp_directory_path() / "teez_init_demo";
    std::filesystem::remove_all(dir);

    const auto result = teez::cli::run_init_command(dir, {.with_demo = true});

    REQUIRE(result.created);
    REQUIRE(result.demo_test_path.has_value());
    REQUIRE(std::filesystem::exists(*result.demo_test_path));
}
