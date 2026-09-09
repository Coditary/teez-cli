#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <nlohmann/json.hpp>

#include "teez/cli/run_output_ui_model.hpp"

namespace {

using teez::cli::FooterColor;
using teez::cli::FooterState;

const std::string kTestId = "ui.teez.lua::default::UI demo / unit > case 01";

teez::cli::FooterStatLine find_stat_line(const FooterState& state, const std::string& label) {
    for (const auto& line : state.stat_lines()) {
        if (line.label == label) {
            return line;
        }
    }
    return {};
}

bool segment_has_color(const std::vector<teez::cli::FooterSegment>& segments, FooterColor color) {
    return std::any_of(segments.begin(), segments.end(),
                       [&](const teez::cli::FooterSegment& segment) { return segment.color == color; });
}

FooterState replay_demo_run() {
    FooterState state;
    state.on_coverage({{"event", "coverage"},
                       {"line_rate", 0.0},
                       {"lines_hit", 0},
                       {"lines_found", 45},
                       {"source", "progress"}});
    state.on_phase(kTestId, "beforeEach", "start");
    state.on_phase(kTestId, "beforeEach", "end");
    state.on_phase(kTestId, "test", "start");
    state.on_phase(kTestId, "test", "end");
    state.set_test_output_started();

    for (int i = 1; i <= 39; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "case %02d", i);
        const std::string id = std::string("ui.teez.lua::default::UI demo / unit > ") + name;
        state.on_result(id, "pass");
    }
    for (int i = 1; i <= 3; ++i) {
        const std::string id = "ui.teez.lua::default::UI demo / unit > case " + std::to_string(40 + i);
        state.on_result(id, "skip");
    }
    for (int i = 1; i <= 3; ++i) {
        const std::string id = "ui.teez.lua::default::UI demo / system > case " + std::to_string(43 + i);
        state.on_result(id, "todo");
    }

    state.on_coverage({{"event", "coverage"},
                       {"line_rate", 1.0},
                       {"lines_hit", 45},
                       {"lines_found", 45},
                       {"source", "progress"}});
    state.mark_finished();
    return state;
}

}  // namespace

TEST_CASE("stat_label right-aligns labels in an 11 character column", "[run_output_ui]") {
    REQUIRE(teez::cli::stat_label("Test Files") == " Test Files");
    REQUIRE(teez::cli::stat_label("Tests") == "      Tests");
    REQUIRE(teez::cli::stat_label("Coverage") == "   Coverage");
    REQUIRE(teez::cli::stat_label("Start at") == "   Start at");
    REQUIRE(teez::cli::stat_label("Duration") == "   Duration");
    REQUIRE(teez::cli::stat_label("Test Files").size() == 11);
    REQUIRE(teez::cli::stat_label("Tests").size() == 11);
}

TEST_CASE("format_active_label matches scroll output indentation", "[run_output_ui]") {
    const std::string line =
        teez::cli::format_active_label(kTestId, "beforeEach");
    REQUIRE(line == "  ◌ case 01 · beforeEach");
    REQUIRE(line.rfind("  ◌ ", 0) == 0);
}

TEST_CASE("FooterState tracks phases and clears them on end", "[run_output_ui]") {
    FooterState state;
    state.on_phase(kTestId, "beforeEach", "start");
    REQUIRE(state.running_count() == 1);
    REQUIRE(state.running_lines().front() == "  ◌ case 01 · beforeEach");

    state.on_phase(kTestId, "beforeEach", "end");
    REQUIRE(state.running_count() == 0);
}

TEST_CASE("FooterState clears running entry after pass result", "[run_output_ui]") {
    FooterState state;
    state.on_phase(kTestId, "test", "start");
    state.on_result(kTestId, "pass");
    REQUIRE(state.passed() == 1);
    REQUIRE(state.running_count() == 0);
}

TEST_CASE("FooterState builds vitest-style stats segments", "[run_output_ui]") {
    const FooterState state = replay_demo_run();

    const auto tests = find_stat_line(state, "Tests");
    REQUIRE(teez::cli::join_segment_text(tests.segments) == "39 passed | 3 skipped | 3 todo (45)");
    REQUIRE(segment_has_color(tests.segments, FooterColor::Green));
    REQUIRE(segment_has_color(tests.segments, FooterColor::Yellow));
    REQUIRE(segment_has_color(tests.segments, FooterColor::Dim));

    const auto files = find_stat_line(state, "Test Files");
    REQUIRE(teez::cli::join_segment_text(files.segments) == "1 passed (1)");

    const auto coverage = find_stat_line(state, "Coverage");
    REQUIRE(teez::cli::join_segment_text(coverage.segments) == "100.00% lines (45/45)");
    REQUIRE(segment_has_color(coverage.segments, FooterColor::Green));

    const auto duration = find_stat_line(state, "Duration");
    REQUIRE(duration.segments.size() == 1);
    REQUIRE(duration.segments.front().text.find("(running)") == std::string::npos);
}

TEST_CASE("FooterState uses a seven line footer band", "[run_output_ui]") {
    FooterState state;
    REQUIRE(state.band_lines() == FooterState::kStatsFooterLines + FooterState::kMaxRunningSlots);
    REQUIRE(state.band_lines() == 7);
}

TEST_CASE("FooterState running panel overflows into summary line", "[run_output_ui]") {
    FooterState state;
    state.on_phase("a", "beforeEach", "start");
    state.on_phase("b", "beforeEach", "start");
    state.on_phase("c", "beforeEach", "start");

    const auto lines = state.running_lines();
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "  ◌ a · beforeEach");
    REQUIRE(lines[1] == "  ◌ +2 more running in parallel");
}

#ifdef TEEZ_ENABLE_UI

TEST_CASE("capture_footer_ansi renders colored demo footer", "[run_output_ui]") {
    const FooterState state = replay_demo_run();
    const std::string rendered = teez::cli::capture_footer_ansi(state);

    REQUIRE(rendered.find("Test Files") != std::string::npos);
    REQUIRE(rendered.find("39 passed") != std::string::npos);
    REQUIRE(rendered.find("3 skipped") != std::string::npos);
    REQUIRE(rendered.find("100.00% lines") != std::string::npos);
    REQUIRE(rendered.find("Duration") != std::string::npos);
    REQUIRE(rendered.find("\033[32m") != std::string::npos);
    REQUIRE(rendered.find("(running)") == std::string::npos);
}

TEST_CASE("short_test_label extracts the trailing test name", "[run_output_ui]") {
    REQUIRE(teez::cli::short_test_label(kTestId) == "case 01");
    REQUIRE(teez::cli::short_test_label("suite::nested::name") == "name");
    REQUIRE(teez::cli::short_test_label("path/to/file.teez.lua") == "file.teez.lua");
}

TEST_CASE("format_active_label uses suite name for hook phases", "[run_output_ui]") {
    const std::string id = "ui.teez.lua::default::UI demo / unit";
    REQUIRE(teez::cli::format_active_label(id, "beforeAll") == "  ◌ default::UI demo / unit · beforeAll");
    REQUIRE(teez::cli::format_active_label(id, "afterAll") == "  ◌ default::UI demo / unit · afterAll");
}

TEST_CASE("format helpers render stable duration and coverage text", "[run_output_ui]") {
    REQUIRE(teez::cli::format_duration_seconds(1.2) == "1.20s");
    REQUIRE(teez::cli::format_test_duration_ms(12) == "12ms");
    REQUIRE(teez::cli::format_test_duration_ms(1500) == "1.50s");
    REQUIRE(teez::cli::format_coverage_percent(0.5) == "50.00%");

    const auto noon = std::chrono::system_clock::from_time_t(0);
    REQUIRE(teez::cli::format_clock_time(noon).size() == 8);
}

TEST_CASE("FooterState should_paint_footer reflects lifecycle", "[run_output_ui]") {
    FooterState state;
    REQUIRE_FALSE(state.should_paint_footer());

    state.on_phase(kTestId, "test", "start");
    REQUIRE(state.should_paint_footer());

    state.on_phase(kTestId, "test", "end");
    REQUIRE_FALSE(state.should_paint_footer());

    state.set_test_output_started();
    REQUIRE(state.should_paint_footer());
}

TEST_CASE("FooterState counts error results as failed", "[run_output_ui]") {
    FooterState state;
    state.on_result(kTestId, "error");
    REQUIRE(state.failed() == 1);
    REQUIRE(state.passed() == 0);

    const auto tests = find_stat_line(state, "Tests");
    REQUIRE(teez::cli::join_segment_text(tests.segments) == "1 failed (1)");
    REQUIRE(segment_has_color(tests.segments, FooterColor::Red));
}

TEST_CASE("FooterState tracks mixed file outcomes", "[run_output_ui]") {
    FooterState state;
    state.on_result("good.teez.lua::default::passes", "pass");
    state.on_result("bad.teez.lua::default::fails", "fail");

    const auto files = find_stat_line(state, "Test Files");
    REQUIRE(teez::cli::join_segment_text(files.segments) == "1 passed | 1 failed (2)");
    REQUIRE(segment_has_color(files.segments, FooterColor::Green));
    REQUIRE(segment_has_color(files.segments, FooterColor::Red));
}

TEST_CASE("FooterState coverage color follows line rate thresholds", "[run_output_ui]") {
    auto coverage_text = [](double rate) {
        FooterState state;
        state.on_coverage({{"event", "coverage"}, {"line_rate", rate}, {"lines_hit", 1}, {"lines_found", 1}});
        return find_stat_line(state, "Coverage").segments.front().color;
    };

    REQUIRE(coverage_text(0.85) == FooterColor::Green);
    REQUIRE(coverage_text(0.60) == FooterColor::Yellow);
    REQUIRE(coverage_text(0.25) == FooterColor::Red);
    REQUIRE(coverage_text(0.0) == FooterColor::Dim);
}

TEST_CASE("FooterState coverage shows dash until first event", "[run_output_ui]") {
    FooterState state;
    const auto coverage = find_stat_line(state, "Coverage");
    REQUIRE(teez::cli::join_segment_text(coverage.segments) == "—");
    REQUIRE(coverage.segments.front().color == FooterColor::Gray);
}

TEST_CASE("FooterState coverage includes branch and function rates", "[run_output_ui]") {
    FooterState state;
    state.on_coverage({{"event", "coverage"},
                       {"line_rate", 0.75},
                       {"lines_hit", 3},
                       {"lines_found", 4},
                       {"branch_rate", 0.5},
                       {"function_rate", 1.0}});

    const auto coverage = find_stat_line(state, "Coverage");
    REQUIRE(teez::cli::join_segment_text(coverage.segments) ==
            "75.00% lines (3/4) | 50.00% branches | 100.00% functions");
}

TEST_CASE("FooterState duration shows running marker until finished", "[run_output_ui]") {
    FooterState state;
    state.ensure_started();

    const auto running = find_stat_line(state, "Duration");
    REQUIRE(running.segments.size() == 2);
    REQUIRE(running.segments.back().text == " (running)");

    state.mark_finished();
    const auto finished = find_stat_line(state, "Duration");
    REQUIRE(finished.segments.size() == 1);
    REQUIRE(finished.segments.front().text.find("(running)") == std::string::npos);
}

TEST_CASE("FooterState mark_finished clears running panel", "[run_output_ui]") {
    FooterState state;
    state.on_phase("a", "test", "start");
    state.on_phase("b", "beforeEach", "start");
    REQUIRE(state.running_count() == 2);

    state.mark_finished();
    REQUIRE(state.running_count() == 0);
    REQUIRE(state.running_lines() == std::vector<std::string>{"", ""});
}

TEST_CASE("FooterState ignores empty phase events", "[run_output_ui]") {
    FooterState state;
    state.on_phase("", "beforeEach", "start");
    state.on_phase(kTestId, "", "start");
    REQUIRE(state.running_count() == 0);
    REQUIRE_FALSE(state.started());
}

TEST_CASE("FooterState stat_lines keep vitest footer order", "[run_output_ui]") {
    FooterState state;
    state.ensure_started();
    const auto lines = state.stat_lines();
    REQUIRE(lines.size() == 5);
    REQUIRE(lines[0].label == "Test Files");
    REQUIRE(lines[1].label == "Tests");
    REQUIRE(lines[2].label == "Coverage");
    REQUIRE(lines[3].label == "Start at");
    REQUIRE(lines[4].label == "Duration");
}

TEST_CASE("FooterState running_lines always reserves two slots", "[run_output_ui]") {
    FooterState state;
    REQUIRE(state.running_lines() == std::vector<std::string>{"", ""});

    state.on_phase(kTestId, "test", "start");
    const auto lines = state.running_lines();
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "  ◌ case 01 · test");
    REQUIRE(lines[1].empty());
}

TEST_CASE("capture_footer_ansi includes failed stats with red ANSI", "[run_output_ui]") {
    FooterState state;
    state.on_result("ui.teez.lua::default::bad", "fail");
    state.mark_finished();

    const std::string rendered = teez::cli::capture_footer_ansi(state);
    REQUIRE(rendered.find("1 failed") != std::string::npos);
    REQUIRE(rendered.find("\033[31m") != std::string::npos);
}

TEST_CASE("capture_footer_ansi includes active phase line", "[run_output_ui]") {
    FooterState state;
    state.on_phase(kTestId, "afterEach", "start");

    const std::string rendered = teez::cli::capture_footer_ansi(state);
    REQUIRE(rendered.find("afterEach") != std::string::npos);
    REQUIRE(rendered.find("case 01") != std::string::npos);
}

TEST_CASE("FooterState tests line covers pass fail skip todo combinations", "[run_output_ui][branches]") {
    FooterState only_fail;
    only_fail.on_result("a.teez.lua::default::x", "fail");
    REQUIRE(teez::cli::join_segment_text(find_stat_line(only_fail, "Tests").segments) == "1 failed (1)");

    FooterState pass_and_fail;
    pass_and_fail.on_result("a.teez.lua::default::ok", "pass");
    pass_and_fail.on_result("a.teez.lua::default::bad", "fail");
    REQUIRE(teez::cli::join_segment_text(find_stat_line(pass_and_fail, "Tests").segments) ==
            "1 passed | 1 failed (2)");

    FooterState only_skip;
    only_skip.on_result("a.teez.lua::default::s", "skip");
    REQUIRE(teez::cli::join_segment_text(find_stat_line(only_skip, "Tests").segments) ==
            "0 passed | 1 skipped (1)");

    FooterState only_todo;
    only_todo.on_result("a.teez.lua::default::t", "todo");
    REQUIRE(teez::cli::join_segment_text(find_stat_line(only_todo, "Tests").segments) ==
            "0 passed | 1 todo (1)");
}

TEST_CASE("FooterState test files line covers failed and skipped-only files", "[run_output_ui][branches]") {
    FooterState failed_file;
    failed_file.on_result("bad.teez.lua::default::x", "fail");
    REQUIRE(teez::cli::join_segment_text(find_stat_line(failed_file, "Test Files").segments) ==
            "1 failed (1)");

    FooterState skipped_file;
    skipped_file.on_result("skip.teez.lua::default::x", "skip");
    REQUIRE(teez::cli::join_segment_text(find_stat_line(skipped_file, "Test Files").segments) ==
            "1 passed (1)");

    FooterState todo_file;
    todo_file.on_result("todo.teez.lua::default::x", "todo");
    REQUIRE(teez::cli::join_segment_text(find_stat_line(todo_file, "Test Files").segments) ==
            "1 passed (1)");
}

TEST_CASE("FooterState handles empty ids and clears optional coverage rates", "[run_output_ui][branches]") {
    FooterState state;
    state.on_result("", "pass");
    REQUIRE(state.passed() == 1);

    state.on_coverage({{"event", "coverage"},
                       {"line_rate", 0.5},
                       {"lines_hit", 1},
                       {"lines_found", 2},
                       {"branch_rate", 0.25},
                       {"function_rate", 0.75}});
    state.on_coverage({{"event", "coverage"},
                       {"line_rate", 0.6},
                       {"lines_hit", 3},
                       {"lines_found", 5}});

    const auto coverage = find_stat_line(state, "Coverage");
    REQUIRE(teez::cli::join_segment_text(coverage.segments) == "60.00% lines (3/5)");
}

TEST_CASE("FooterState phase tracking replaces and ends entries", "[run_output_ui][branches]") {
    FooterState state;
    state.on_phase(kTestId, "beforeEach", "start");
    state.on_phase(kTestId, "test", "start");
    REQUIRE(state.running_count() == 1);
    REQUIRE(state.running_lines().front().find("test") != std::string::npos);

    state.on_phase(kTestId, "test", "end");
    REQUIRE(state.running_count() == 0);

    state.on_phase("orphan", "afterAll", "end");
    REQUIRE(state.running_count() == 0);
}

TEST_CASE("FooterState shows zero passed when no tests have finished yet", "[run_output_ui][branches]") {
    FooterState state;
    state.ensure_started();
    REQUIRE(teez::cli::join_segment_text(find_stat_line(state, "Tests").segments) == "0 passed (0)");
}

TEST_CASE("format_active_label falls back to full id without suite separator", "[run_output_ui][branches]") {
    REQUIRE(teez::cli::format_active_label("plain-id", "beforeAll") == "  ◌ plain-id · beforeAll");
}

TEST_CASE("short_test_label prefers arrow path over suite separator", "[run_output_ui][branches]") {
    REQUIRE(teez::cli::short_test_label("suite::group > leaf") == "leaf");
}

#endif
