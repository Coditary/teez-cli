#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <functional>
#include <sstream>
#include <string>
#include <unistd.h>

#include "footer_test_helpers.hpp"
#include "teez/cli/run_output.hpp"
#include "teez/cli/run_output_ui.hpp"

namespace {

using teez::cli::footer_test::CheckResult;
using teez::cli::footer_test::check_active_label_indent;
using teez::cli::footer_test::check_demo_footer;
using teez::cli::footer_test::check_footer_consistency;
using teez::cli::footer_test::check_output_has;
using teez::cli::footer_test::check_output_lacks;
using teez::cli::footer_test::check_stat_label_width;
using teez::cli::footer_test::check_stat_line_text;
using teez::cli::footer_test::check_duration_finalized;
using teez::cli::footer_test::replay_demo_run;

std::string capture_stdout(const std::function<void()>& action) {
    fflush(stdout);

    int pipefd[2]{};
    if (pipe(pipefd) != 0) {
        return {};
    }

    const int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    setvbuf(stdout, nullptr, _IONBF, 0);

    action();

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    std::string captured;
    char buffer[4096];
    ssize_t bytes = 0;
    while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        captured.append(buffer, static_cast<std::size_t>(bytes));
    }
    close(pipefd[0]);
    return captured;
}

void require_detects_violation(const CheckResult& result) {
    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.message.empty());
}

}  // namespace

TEST_CASE("negative: demo footer validator accepts the golden replay", "[run_output_ui][negative]") {
    REQUIRE(check_demo_footer(replay_demo_run()).ok);
}

TEST_CASE("negative: demo footer validator rejects unfinished runs", "[run_output_ui][negative]") {
    require_detects_violation(check_demo_footer(replay_demo_run(false)));
}

TEST_CASE("negative: demo footer validator rejects inflated pass counts", "[run_output_ui][negative]") {
    auto state = replay_demo_run();
    state.on_result("ui.teez.lua::default::UI demo / unit > case 99", "pass");
    require_detects_violation(check_demo_footer(state));
}

TEST_CASE("negative: demo footer validator rejects wrong coverage text", "[run_output_ui][negative]") {
    auto state = replay_demo_run();
    state.on_coverage({{"event", "coverage"},
                       {"line_rate", 0.5},
                       {"lines_hit", 22},
                       {"lines_found", 45},
                       {"source", "progress"}});
    require_detects_violation(check_demo_footer(state));
}

TEST_CASE("negative: consistency check rejects running entries after finish", "[run_output_ui][negative]") {
    auto state = replay_demo_run();
    state.on_phase("ui.teez.lua::default::UI demo / unit > case 99", "afterEach", "start");
    require_detects_violation(check_footer_consistency(state));
}

TEST_CASE("negative: duration finalization check rejects unfinished runs", "[run_output_ui][negative]") {
    require_detects_violation(check_duration_finalized(replay_demo_run(false)));
}

TEST_CASE("negative: stat label width check rejects left-padded labels", "[run_output_ui][negative]") {
    require_detects_violation(check_stat_label_width("Tests", 10));
    require_detects_violation(check_stat_label_width("Tests", 12));
}

TEST_CASE("negative: stat line check rejects vitest typos", "[run_output_ui][negative]") {
    const auto state = replay_demo_run();
    require_detects_violation(check_stat_line_text(state, "Tests", "39 passed | 3 skipped | 3 todo (44)"));
    require_detects_violation(check_stat_line_text(state, "Test Files", "2 passed (1)"));
}

TEST_CASE("negative: active label indent check rejects single-space marker", "[run_output_ui][negative]") {
    require_detects_violation(check_active_label_indent(" ◌ case 01 · beforeEach"));
}

TEST_CASE("negative: output guard rejects missing required footer text", "[run_output_ui][negative]") {
    require_detects_violation(check_output_has("no stats here", "Test Files"));
}

TEST_CASE("negative: output guard rejects forbidden footer text", "[run_output_ui][negative]") {
    require_detects_violation(check_output_lacks("Test Files  1 failed (1)", "1 failed"));
}

TEST_CASE("RunOutputWriter --ui on stringstream must not paint live footer", "[run_output_ui][negative]") {
    std::ostringstream out;
    teez::cli::RunOutputWriter writer({.ui = true}, out);
    writer.on_event({{"event", "pass"}, {"id", "suite::alpha"}});
    writer.on_event({{"event", "fail"}, {"id", "suite::beta"}, {"msg", "nope"}});
    writer.finish();

    const std::string output = out.str();
    REQUIRE(check_output_lacks(output, "Test Files").ok);
    REQUIRE(check_output_lacks(output, "\033[").ok);
    REQUIRE(check_output_has(output, "1 failed").ok);
}

#ifdef TEEZ_ENABLE_UI

TEST_CASE("negative: LiveUiReporter must not claim passes without results", "[run_output_ui][negative]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        reporter.on_event({{"event", "start"}, {"id", "suite::alpha"}});
        reporter.finish();
    });

    REQUIRE(check_output_lacks(output, "1 passed").ok);
    REQUIRE(check_output_lacks(output, "1 failed").ok);
}

TEST_CASE("negative: LiveUiReporter must not show wrong coverage rate", "[run_output_ui][negative]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        reporter.on_event({{"event", "coverage"},
                           {"line_rate", 0.42},
                           {"lines_hit", 21},
                           {"lines_found", 50},
                           {"source", "progress"}});
        reporter.finish();
    });

    REQUIRE(check_output_has(output, "42.00% lines").ok);
    REQUIRE(check_output_lacks(output, "100.00% lines").ok);
}

TEST_CASE("negative: LiveUiReporter all-pass run must not contain red ANSI", "[run_output_ui][negative]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        reporter.on_event({{"event", "pass"}, {"id", "a.teez.lua::default::ok"}});
        reporter.finish();
    });

    REQUIRE(check_output_has(output, "1 passed").ok);
    REQUIRE(check_output_lacks(output, "\033[31m").ok);
}

TEST_CASE("negative: LiveUiReporter must not leak harness noise into footer", "[run_output_ui][negative]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        reporter.on_event({{"event", "harness_loaded"}, {"name", "hyperfine"}});
        reporter.on_event({{"event", "harness_capture"}, {"text", "benchmark output"}});
        reporter.finish();
    });

    REQUIRE(check_output_lacks(output, "hyperfine").ok);
    REQUIRE(check_output_lacks(output, "benchmark output").ok);
    REQUIRE(check_output_lacks(output, "harness").ok);
}

TEST_CASE("negative: capture_footer_ansi rejects missing failed stats", "[run_output_ui][negative]") {
    const auto state = replay_demo_run();
    const std::string rendered = teez::cli::capture_footer_ansi(state);
    REQUIRE(check_output_lacks(rendered, "1 failed").ok);
    REQUIRE(check_output_has(rendered, "39 passed").ok);
}

TEST_CASE("negative: corrupted fail footer is detected by output guard", "[run_output_ui][negative]") {
    teez::cli::FooterState state;
    state.on_result("ui.teez.lua::default::bad", "fail");
    state.mark_finished();

    const std::string rendered = teez::cli::capture_footer_ansi(state);
    REQUIRE(check_output_has(rendered, "1 failed").ok);
    REQUIRE(check_output_lacks(rendered, "39 passed").ok);
    REQUIRE(check_output_lacks(rendered, "\033[32m1 failed").ok);
}

#endif
