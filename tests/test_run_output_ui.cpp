#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <unistd.h>

#include "teez/cli/run_output_ui.hpp"
#include "teez/cli/run_output_ui_model.hpp"

namespace {

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

void feed(teez::cli::LiveUiReporter& reporter, const nlohmann::json& event) {
    reporter.on_event(event);
}

}  // namespace

TEST_CASE("live_ui_available reflects build configuration", "[run_output_ui]") {
#ifdef TEEZ_ENABLE_UI
    REQUIRE(teez::cli::live_ui_available());
#else
    REQUIRE_FALSE(teez::cli::live_ui_available());
#endif
}

#ifdef TEEZ_ENABLE_UI

TEST_CASE("LiveUiReporter ignores harness internal events", "[run_output_ui]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        feed(reporter, {{"event", "harness_loaded"}, {"name", "hyperfine"}});
        feed(reporter, {{"event", "harness"}, {"action", "benchmark"}});
        feed(reporter, {{"event", "harness_phase"}, {"name", "baseline"}, {"state", "start"}});
        feed(reporter, {{"event", "harness_capture"}, {"text", "noise"}});
        reporter.finish();
    });

    REQUIRE(output.find("harness") == std::string::npos);
    REQUIRE(output.find("hyperfine") == std::string::npos);
}

TEST_CASE("LiveUiReporter paints running phase in footer band", "[run_output_ui]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        feed(reporter,
             {{"event", "phase"},
              {"id", "ui.teez.lua::default::UI demo / unit > case 01"},
              {"phase", "beforeEach"},
              {"state", "start"}});
        reporter.finish();
    });

    REQUIRE(output.find("beforeEach") != std::string::npos);
    REQUIRE(output.find("case 01") != std::string::npos);
    REQUIRE(output.find("Tests") != std::string::npos);
}

TEST_CASE("LiveUiReporter updates stats after scroll output and finish", "[run_output_ui]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        feed(reporter, {{"event", "start"}, {"id", "suite::alpha"}});

        reporter.before_scroll_output();
        fputs("  ✓ alpha\n", stdout);
        fflush(stdout);
        feed(reporter, {{"event", "pass"}, {"id", "suite::alpha"}});

        reporter.before_scroll_output();
        fputs("  ✗ beta\n", stdout);
        fflush(stdout);
        feed(reporter, {{"event", "fail"}, {"id", "suite::beta"}, {"msg", "nope"}});

        reporter.finish();
    });

    REQUIRE(output.find("1 failed") != std::string::npos);
    REQUIRE(output.find("1 passed") != std::string::npos);
    REQUIRE(output.find("Test Files") != std::string::npos);
}

TEST_CASE("LiveUiReporter renders coverage updates in footer", "[run_output_ui]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        feed(reporter,
             {{"event", "coverage"},
              {"line_rate", 0.42},
              {"lines_hit", 21},
              {"lines_found", 50},
              {"branch_rate", 0.33},
              {"function_rate", 0.66},
              {"source", "progress"}});
        reporter.finish();
    });

    REQUIRE(output.find("Coverage") != std::string::npos);
    REQUIRE(output.find("42.00% lines") != std::string::npos);
    REQUIRE(output.find("33.00% branches") != std::string::npos);
    REQUIRE(output.find("66.00% functions") != std::string::npos);
}

TEST_CASE("LiveUiReporter uses colored ANSI segments for pass and fail stats", "[run_output_ui]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        feed(reporter, {{"event", "pass"}, {"id", "a.teez.lua::default::ok"}});
        feed(reporter, {{"event", "fail"}, {"id", "a.teez.lua::default::bad"}});
        reporter.finish();
    });

    REQUIRE(output.find("\033[32m") != std::string::npos);
    REQUIRE(output.find("\033[31m") != std::string::npos);
}

TEST_CASE("LiveUiReporter handles skip todo error and phase end branches", "[run_output_ui][branches]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        feed(reporter, {{"event", "skip"}, {"id", "a.teez.lua::default::skipped"}});
        feed(reporter, {{"event", "todo"}, {"id", "a.teez.lua::default::later"}});
        feed(reporter, {{"event", "error"}, {"id", "a.teez.lua::default::broken"}});
        feed(reporter,
             {{"event", "phase"},
              {"id", "a.teez.lua::default::running"},
              {"phase", "test"},
              {"state", "start"}});
        feed(reporter,
             {{"event", "phase"},
              {"id", "a.teez.lua::default::running"},
              {"phase", "test"},
              {"state", "end"}});
        reporter.finish();
    });

    REQUIRE(output.find("1 skipped") != std::string::npos);
    REQUIRE(output.find("1 todo") != std::string::npos);
    REQUIRE(output.find("1 failed") != std::string::npos);
}

TEST_CASE("LiveUiReporter ignores invalid events and no-op finish paths", "[run_output_ui][branches]") {
    capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        reporter.on_event(nlohmann::json::array());
        reporter.on_event(nlohmann::json::object());
        reporter.finish();
        reporter.finish();
    });

    capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        reporter.before_scroll_output();
        reporter.finish();
    });
}

TEST_CASE("LiveUiReporter destructor finishes an unfinished run", "[run_output_ui][branches]") {
    const std::string output = capture_stdout([]() {
        auto reporter = std::make_unique<teez::cli::LiveUiReporter>();
        feed(*reporter, {{"event", "pass"}, {"id", "a.teez.lua::default::ok"}});
    });

    REQUIRE(output.find("1 passed") != std::string::npos);
}

TEST_CASE("LiveUiReporter repaints an existing footer band", "[run_output_ui][branches]") {
    const std::string output = capture_stdout([]() {
        teez::cli::LiveUiReporter reporter;
        feed(reporter, {{"event", "pass"}, {"id", "a.teez.lua::default::one"}});
        feed(reporter, {{"event", "pass"}, {"id", "a.teez.lua::default::two"}});
        reporter.finish();
    });

    REQUIRE(output.find("2 passed") != std::string::npos);
}

TEST_CASE("capture_footer_ansi renders empty running slots and dim segments", "[run_output_ui][branches]") {
    teez::cli::FooterState state;
    state.ensure_started();
    state.mark_finished();
    const std::string rendered = teez::cli::capture_footer_ansi(state);
    REQUIRE(rendered.find("0 passed") != std::string::npos);
}

#endif
