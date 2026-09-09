#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include <nlohmann/json.hpp>

#include "teez/cli/run_output.hpp"

namespace {

std::string capture_events(teez::cli::RunOutputOptions options,
                           const std::vector<nlohmann::json>& events) {
    std::ostringstream out;
    teez::cli::RunOutputWriter writer(options, out);
    for (const auto& event : events) {
        writer.on_event(event);
    }
    writer.finish();
    return out.str();
}

}  // namespace

TEST_CASE("RunOutputWriter streams NDJSON with --progress --json", "[run_output]") {
    const auto output = capture_events({.progress = true, .json = true},
                                       {{{"event", "pass"}, {"id", "alpha"}}});

    REQUIRE(output.find("\"event\":\"pass\"") != std::string::npos);
    REQUIRE(output.find("\"id\":\"alpha\"") != std::string::npos);
}

TEST_CASE("RunOutputWriter emits aggregated JSON with --json only", "[run_output]") {
    const auto output = capture_events({.json = true},
                                       {{{"event", "pass"}, {"id", "alpha"}},
                                        {{"event", "fail"}, {"id", "beta"}, {"msg", "boom"}}});

    const auto json = nlohmann::json::parse(output);
    REQUIRE(json.at("events").size() == 2);
    REQUIRE(json.at("summary").at("passed") == 1);
    REQUIRE(json.at("summary").at("failed") == 1);
    REQUIRE(json.at("summary").at("total") == 2);
}

TEST_CASE("RunOutputWriter renders human output by default", "[run_output]") {
    const auto output = capture_events({},
                                       {{{"event", "start"}, {"id", "suite::test_one"}},
                                        {{"event", "pass"}, {"id", "suite::test_one"}},
                                        {{"event", "fail"}, {"id", "suite::test_two"}, {"msg", "nope"}}});

    REQUIRE(output.find("\"event\"") == std::string::npos);
    REQUIRE(output.find("test_one") != std::string::npos);
    REQUIRE(output.find("test_two") != std::string::npos);
    REQUIRE(output.find("1 failed") != std::string::npos);
    REQUIRE(output.find("1 passed") != std::string::npos);
}

TEST_CASE("RunOutputWriter hides harness internal events in human output", "[run_output]") {
    const auto output = capture_events({},
                                       {{{"event", "harness_loaded"}, {"name", "hyperfine"}},
                                        {{"event", "harness"}, {"action", "benchmark"}},
                                        {{"event", "harness_phase"}, {"name", "baseline"}, {"state", "start"}},
                                        {{"event", "pass"},
                                         {"id", "experiment::Hyperfine demo > sleep 0.01 is faster than sleep 0.02"}},
                                        {{"event", "harness_phase"}, {"name", "baseline"}, {"state", "end"}},
                                        {{"event", "pass"},
                                         {"id", "experiment::Hyperfine demo > records a single command baseline"}}});

    REQUIRE(output.find("harness") == std::string::npos);
    REQUIRE(output.find("sleep 0.01 is faster than sleep 0.02") != std::string::npos);
    REQUIRE(output.find("2 passed") != std::string::npos);
}

TEST_CASE("RunOutputWriter renders progress text with --progress", "[run_output]") {
    const auto output = capture_events({.progress = true},
                                       {{{"event", "pass"}, {"id", "alpha"}}});

    REQUIRE(output.find("pass alpha") != std::string::npos);
    REQUIRE(output.find("\"event\"") == std::string::npos);
}

TEST_CASE("RunOutputWriter falls back to human output for --ui without a TTY", "[run_output]") {
    const auto output = capture_events({.ui = true},
                                       {{{"event", "start"}, {"id", "suite::alpha"}},
                                        {{"event", "pass"}, {"id", "suite::alpha"}},
                                        {{"event", "fail"}, {"id", "suite::beta"}, {"msg", "nope"}}});

    REQUIRE(output.find("alpha") != std::string::npos);
    REQUIRE(output.find("1 failed") != std::string::npos);
    REQUIRE(output.find("\033[") == std::string::npos);
}
