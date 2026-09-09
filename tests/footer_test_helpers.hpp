#pragma once

#include "teez/cli/run_output_ui_model.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace teez::cli::footer_test {

struct CheckResult {
    bool ok = true;
    std::string message;

    static CheckResult pass() { return {}; }

    static CheckResult fail(std::string message) {
        CheckResult result;
        result.ok = false;
        result.message = std::move(message);
        return result;
    }
};

inline teez::cli::FooterStatLine find_stat_line(const teez::cli::FooterState& state,
                                                const std::string& label) {
    for (const auto& line : state.stat_lines()) {
        if (line.label == label) {
            return line;
        }
    }
    return {};
}

inline teez::cli::FooterState replay_demo_run(bool finished = true) {
    teez::cli::FooterState state;
    const std::string test_id = "ui.teez.lua::default::UI demo / unit > case 01";

    state.on_coverage({{"event", "coverage"},
                       {"line_rate", 0.0},
                       {"lines_hit", 0},
                       {"lines_found", 45},
                       {"source", "progress"}});
    state.on_phase(test_id, "beforeEach", "start");
    state.on_phase(test_id, "beforeEach", "end");
    state.on_phase(test_id, "test", "start");
    state.on_phase(test_id, "test", "end");
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
    if (finished) {
        state.mark_finished();
    }
    return state;
}

inline CheckResult check_stat_label_width(std::string_view label, std::size_t width = 11) {
    const std::string rendered = teez::cli::stat_label(label);
    if (rendered.size() != width) {
        return CheckResult::fail("stat_label('" + std::string(label) + "') width is " +
                                 std::to_string(rendered.size()) + ", expected " + std::to_string(width));
    }
    return CheckResult::pass();
}

inline CheckResult check_counts(const teez::cli::FooterState& state, int passed, int failed, int skipped,
                                int todo) {
    if (state.passed() != passed || state.failed() != failed || state.skipped() != skipped ||
        state.todo() != todo) {
        return CheckResult::fail("counts mismatch: got " + std::to_string(state.passed()) + " passed, " +
                                 std::to_string(state.failed()) + " failed, " +
                                 std::to_string(state.skipped()) + " skipped, " +
                                 std::to_string(state.todo()) + " todo");
    }
    return CheckResult::pass();
}

inline CheckResult check_stat_line_text(const teez::cli::FooterState& state, const std::string& label,
                                        const std::string& expected_text) {
    const auto line = find_stat_line(state, label);
    const std::string actual = teez::cli::join_segment_text(line.segments);
    if (actual != expected_text) {
        return CheckResult::fail(label + " line is '" + actual + "', expected '" + expected_text + "'");
    }
    return CheckResult::pass();
}

inline CheckResult check_footer_consistency(const teez::cli::FooterState& state) {
    if (state.running_lines().size() != static_cast<std::size_t>(teez::cli::FooterState::kMaxRunningSlots)) {
        return CheckResult::fail("running_lines size is not kMaxRunningSlots");
    }
    if (state.band_lines() != teez::cli::FooterState::kStatsFooterLines + teez::cli::FooterState::kMaxRunningSlots) {
        return CheckResult::fail("band_lines is not 7");
    }
    if (state.finished() && state.running_count() > 0) {
        return CheckResult::fail("finished footer still has running entries");
    }
    if (state.finished()) {
        const auto duration = find_stat_line(state, "Duration");
        const std::string duration_text = teez::cli::join_segment_text(duration.segments);
        if (duration_text.find("(running)") != std::string::npos) {
            return CheckResult::fail("finished footer still shows (running)");
        }
    }
    return CheckResult::pass();
}

inline CheckResult check_demo_footer(const teez::cli::FooterState& state) {
    if (!state.finished()) {
        return CheckResult::fail("demo footer must be finished");
    }

    CheckResult result = check_counts(state, 39, 0, 3, 3);
    if (!result.ok) {
        return result;
    }
    result = check_stat_line_text(state, "Tests", "39 passed | 3 skipped | 3 todo (45)");
    if (!result.ok) {
        return result;
    }
    result = check_stat_line_text(state, "Test Files", "1 passed (1)");
    if (!result.ok) {
        return result;
    }
    result = check_stat_line_text(state, "Coverage", "100.00% lines (45/45)");
    if (!result.ok) {
        return result;
    }
    return check_footer_consistency(state);
}

inline CheckResult check_output_lacks(std::string_view output, std::string_view forbidden) {
    if (output.find(forbidden) != std::string::npos) {
        return CheckResult::fail("output unexpectedly contains '" + std::string(forbidden) + "'");
    }
    return CheckResult::pass();
}

inline CheckResult check_output_has(std::string_view output, std::string_view required) {
    if (output.find(required) == std::string::npos) {
        return CheckResult::fail("output is missing '" + std::string(required) + "'");
    }
    return CheckResult::pass();
}

inline CheckResult check_duration_finalized(const teez::cli::FooterState& state) {
    if (!state.finished()) {
        return CheckResult::fail("duration is not finalized yet");
    }
    const auto duration = find_stat_line(state, "Duration");
    const std::string duration_text = teez::cli::join_segment_text(duration.segments);
    if (duration_text.find("(running)") != std::string::npos) {
        return CheckResult::fail("finalized footer still shows (running)");
    }
    return CheckResult::pass();
}

inline CheckResult check_active_label_indent(const std::string& line) {
    if (line.rfind("  ◌ ", 0) != 0) {
        return CheckResult::fail("active label does not start with two-space hollow marker");
    }
    if (line.rfind(" ◌ ", 0) == 0) {
        return CheckResult::fail("active label uses single-space indentation");
    }
    return CheckResult::pass();
}

}  // namespace teez::cli::footer_test
