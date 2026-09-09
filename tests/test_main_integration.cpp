#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "teez/core/process.hpp"

#ifndef TEEZ_CLI_BIN
#error "TEEZ_CLI_BIN must be defined"
#endif
#ifndef TEEZ_FIXTURES_DIR
#error "TEEZ_FIXTURES_DIR must be defined"
#endif

namespace {

struct ProcessOutput {
    int exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;
};

ProcessOutput run_teez(const std::vector<std::string>& args) {
    teez::core::CommandSpec spec;
    spec.command = TEEZ_CLI_BIN;
    spec.args = args;

    const auto result = teez::core::run_command_capture(spec);
    return {result.exit_code, result.stdout_text, result.stderr_text};
}

const std::filesystem::path kWorkerFixture =
    std::filesystem::path(TEEZ_FIXTURES_DIR) / "teez-worker-demo";

}  // namespace

TEST_CASE("teez run executes worker demo fixture", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto output = run_teez({"run", kWorkerFixture.string()});

    REQUIRE(output.exit_code == 0);
    REQUIRE(output.stdout_text.find("passed") != std::string::npos);
}

TEST_CASE("teez run --progress --json streams NDJSON", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto output = run_teez({"run", kWorkerFixture.string(), "--progress", "--json"});

    REQUIRE(output.exit_code == 0);
    REQUIRE(output.stdout_text.find("\"event\":\"pass\"") != std::string::npos);
}

TEST_CASE("teez run --json emits aggregated summary JSON", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto output = run_teez({"run", kWorkerFixture.string(), "--json"});

    REQUIRE(output.exit_code == 0);
    const auto json = nlohmann::json::parse(output.stdout_text);
    REQUIRE(json.contains("events"));
    REQUIRE(json.contains("summary"));
    REQUIRE(json.at("summary").at("passed").get<int>() >= 1);
}

TEST_CASE("teez worker subcommand lists embedded worker tests", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto output = run_teez({"worker", "--list", kWorkerFixture.string()});

    REQUIRE(output.exit_code == 0);
    REQUIRE(output.stdout_text.find("checks string helpers") != std::string::npos);
}

TEST_CASE("teez list prints worker demo tests", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto output = run_teez({"list", kWorkerFixture.string(), "--flat"});

    REQUIRE(output.exit_code == 0);
    REQUIRE(output.stdout_text.find("smoke.teez.lua") != std::string::npos);
    REQUIRE(output.stdout_text.find("checks string helpers") != std::string::npos);
}

TEST_CASE("teez --help prints usage", "[cli][integration]") {
    const auto output = run_teez({"--help"});

    REQUIRE(output.exit_code == 0);
    REQUIRE(output.stdout_text.find("run") != std::string::npos);
    REQUIRE(output.stdout_text.find("list") != std::string::npos);
    REQUIRE(output.stderr_text.find("error:") == std::string::npos);
}

TEST_CASE("teez rejects unknown subcommand", "[cli][integration]") {
    const auto output = run_teez({"deploy", "."});

    REQUIRE(output.exit_code == 1);
    REQUIRE(output.stderr_text.find("error:") != std::string::npos);
}

TEST_CASE("teez run fails when no plugin matches target", "[cli][integration]") {
    const auto empty_dir = std::filesystem::temp_directory_path() / "teez_cli_no_plugin";
    std::filesystem::create_directories(empty_dir);

    const auto output = run_teez({"run", empty_dir.string()});

    REQUIRE(output.exit_code == 1);
    REQUIRE(output.stderr_text.find("no matching plugin") != std::string::npos);
}

TEST_CASE("teez list supports json output", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto output = run_teez({"list", kWorkerFixture.string(), "--json"});

    REQUIRE(output.exit_code == 0);
    REQUIRE(output.stdout_text.find("\"id\"") != std::string::npos);
    REQUIRE(output.stdout_text.find("smoke.teez.lua") != std::string::npos);
}

TEST_CASE("teez run rejects missing target path", "[cli][integration]") {
    const auto output = run_teez({"run", "/tmp/teez-path-that-does-not-exist-xyz"});

    REQUIRE(output.exit_code == 1);
    REQUIRE(output.stderr_text.find("error:") != std::string::npos);
}

TEST_CASE("teez run accepts --update-snapshots flag", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto output = run_teez({"run", kWorkerFixture.string(), "--update-snapshots"});

    REQUIRE(output.exit_code == 0);
}

TEST_CASE("teez run fails on invalid teez.config.lua", "[cli][integration]") {
    const auto project = std::filesystem::temp_directory_path() / "teez_cli_bad_config";
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    std::ofstream(project / "smoke.teez.lua") << "-- test\n";
    std::ofstream(project / "teez.config.lua") << "return { profile = \n";

    const auto output = run_teez(
        {"run", project.string(), "--config", (project / "teez.config.lua").string()});

    REQUIRE(output.exit_code == 1);
    REQUIRE(output.stderr_text.find("error:") != std::string::npos);
}

TEST_CASE("teez discover reports worker plugin for demo fixture", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto output = run_teez({"discover", kWorkerFixture.string()});

    REQUIRE(output.exit_code == 0);
    REQUIRE(output.stdout_text.find("teez-worker") != std::string::npos);
    REQUIRE(output.stdout_text.find("Run:") != std::string::npos);
}

TEST_CASE("teez config validates workspace config", "[cli][integration]") {
    const auto workspace = std::filesystem::temp_directory_path() / "teez_cli_config_validate";
    std::filesystem::remove_all(workspace);
    std::filesystem::create_directories(workspace / "fixtures" / "teez-worker-demo");
    std::ofstream(workspace / "teez.config.lua")
        << "return {\n"
           "  profile = \"local\",\n"
           "  projects = { \"fixtures/teez-worker-demo\" },\n"
           "}\n";

    const auto output = run_teez({"--config", (workspace / "teez.config.lua").string(), "config",
                                  workspace.string(), "--validate"});

    REQUIRE(output.exit_code == 0);
    REQUIRE(output.stdout_text.find("Projects:") != std::string::npos);
}

TEST_CASE("teez init creates starter config", "[cli][integration]") {
    const auto dir = std::filesystem::temp_directory_path() / "teez_cli_init";
    std::filesystem::remove_all(dir);

    const auto output = run_teez({"init", dir.string(), "--with-demo"});

    REQUIRE(output.exit_code == 0);
    REQUIRE(std::filesystem::exists(dir / "teez.config.lua"));
    REQUIRE(std::filesystem::exists(dir / "tests" / "smoke.teez.lua"));
    REQUIRE(output.stdout_text.find("Created:") != std::string::npos);
}

TEST_CASE("teez run --report junit writes report file", "[cli][integration]") {
    if (!std::filesystem::exists(kWorkerFixture / "smoke.teez.lua")) {
        SKIP("worker demo fixture missing");
    }

    const auto report_path =
        std::filesystem::temp_directory_path() / "teez_cli_junit_report.xml";
    std::filesystem::remove(report_path);

    const auto output = run_teez({"run", kWorkerFixture.string(), "--simple", "--report", "junit",
                                  "--report-output", report_path.string()});

    REQUIRE(output.exit_code == 0);
    REQUIRE(std::filesystem::exists(report_path));
    std::ifstream in(report_path);
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    REQUIRE(content.find("<testsuites") != std::string::npos);
    REQUIRE(content.find("<testcase") != std::string::npos);
    REQUIRE(output.stdout_text.find("Report:") != std::string::npos);
}
