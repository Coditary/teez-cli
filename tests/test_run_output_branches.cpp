#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "teez/cli/run_output.hpp"

#ifdef TEEZ_ENABLE_UI
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <unistd.h>
#endif

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

TEST_CASE("RunOutputWriter human output covers retry and error branches", "[run_output][branches]") {
    const auto output = capture_events({},
                                       {{{"event", "retry"}, {"id", "suite::flaky"}},
                                        {{"event", "error"}, {"id", "suite::boom"}, {"msg", "kaboom"}}});

    REQUIRE(output.find("flaky (retry)") != std::string::npos);
    REQUIRE(output.find("boom") != std::string::npos);
    REQUIRE(output.find("kaboom") != std::string::npos);
}

TEST_CASE("RunOutputWriter human output covers skip and todo label branches", "[run_output][branches]") {
    const auto output = capture_events(
        {},
        {{{"event", "skip"}, {"id", "suite::already (skipped)"}},
         {{"event", "skip"}, {"id", "suite::needs suffix"}},
         {{"event", "todo"}, {"id", "suite::planned (todo)"}},
         {{"event", "todo"}, {"id", "suite::planned (planned)"}},
         {{"event", "todo"}, {"id", "suite::fresh"}}});

    REQUIRE(output.find("already (skipped)") != std::string::npos);
    REQUIRE(output.find("needs suffix (skipped)") != std::string::npos);
    REQUIRE(output.find("planned (todo)") != std::string::npos);
    REQUIRE(output.find("planned (planned)") != std::string::npos);
    REQUIRE(output.find("fresh (todo)") != std::string::npos);
}

TEST_CASE("RunOutputWriter human output covers output event branches", "[run_output][branches]") {
    const auto with_newline = capture_events({},
                                             {{{"event", "output"}, {"text", "line with newline\n"}},
                                              {{"event", "output"}, {"text", ""}}});
    REQUIRE(with_newline.find("line with newline") != std::string::npos);

    const auto without_newline = capture_events({},
                                                {{{"event", "output"}, {"text", "no trailing newline"}}});
    REQUIRE(without_newline.find("no trailing newline") != std::string::npos);
}

TEST_CASE("RunOutputWriter human output renders unknown events and suite headers", "[run_output][branches]") {
    const auto output = capture_events(
        {},
        {{{"event", "start"}, {"id", "alpha::one"}},
         {{"event", "pass"}, {"id", "alpha::one"}},
         {{"event", "start"}, {"id", "beta::two"}},
         {{"event", "pass"}, {"id", "beta::two"}},
         {{"event", "custom"}, {"id", "beta::two"}},
         {{"event", "bare"}}});

    REQUIRE(output.find("alpha") != std::string::npos);
    REQUIRE(output.find("beta") != std::string::npos);
    REQUIRE(output.find("  custom beta::two") != std::string::npos);
    REQUIRE(output.find("  bare\n") != std::string::npos);
}

TEST_CASE("RunOutputWriter human output uses path and arrow label fallbacks", "[run_output][branches]") {
    const auto output = capture_events(
        {},
        {{{"event", "pass"}, {"id", "dir/sub/file.teez.lua::suite > case arrow"}},
         {{"event", "pass"}, {"id", "plain-name"}}});

    REQUIRE(output.find("case arrow") != std::string::npos);
    REQUIRE(output.find("plain-name") != std::string::npos);
}

TEST_CASE("RunOutputWriter human output shows per-test duration", "[run_output][branches]") {
    const auto output = capture_events(
        {},
        {{{"event", "pass"}, {"id", "suite::fast"}, {"duration_ms", 12}},
         {{"event", "fail"}, {"id", "suite::slow"}, {"duration_ms", 1500}, {"msg", "nope"}},
         {{"event", "skip"}, {"id", "suite::later"}, {"duration_ms", 3}}});

    REQUIRE(output.find("fast") != std::string::npos);
    REQUIRE(output.find("12ms") != std::string::npos);
    REQUIRE(output.find("1.50s") != std::string::npos);
    REQUIRE(output.find("later") != std::string::npos);
    REQUIRE(output.find("3ms") != std::string::npos);
}

TEST_CASE("RunOutputWriter human output omits duration when missing", "[run_output][branches]") {
    const auto output = capture_events({}, {{{"event", "pass"}, {"id", "suite::plain"}}});
    REQUIRE(output.find("ms") == std::string::npos);
    REQUIRE(output.find("plain") != std::string::npos);
}

TEST_CASE("RunOutputWriter summary line covers alternate outcome branches", "[run_output][branches]") {
    REQUIRE(capture_events({}, {{{"event", "skip"}, {"id", "suite::s"}}}).find("1 skipped") !=
            std::string::npos);
    REQUIRE(capture_events({}, {{{"event", "todo"}, {"id", "suite::t"}}}).find("1 todo") !=
            std::string::npos);
    REQUIRE(capture_events({}, {}).find("no tests reported") != std::string::npos);
    REQUIRE(capture_events({}, {{{"event", "output"}, {"text", "only logs"}}}).find("no tests reported") ==
            std::string::npos);
    REQUIRE(capture_events({}, {{{"event", "output"}, {"text", "only logs"}}}).find("only logs") !=
            std::string::npos);
}

TEST_CASE("RunOutputWriter progress text includes optional fields", "[run_output][branches]") {
    const auto output = capture_events({.progress = true},
                                       {{{"event", "fail"},
                                         {"id", "suite::x"},
                                         {"msg", "reason"},
                                         {"text", "details"}}});

    REQUIRE(output.find("fail suite::x reason details") != std::string::npos);
}

TEST_CASE("RunOutputWriter ignores malformed events safely", "[run_output][branches]") {
    std::ostringstream out;
    teez::cli::RunOutputWriter writer({}, out);
    writer.on_event(nlohmann::json::array({1, 2, 3}));
    writer.on_event(nlohmann::json::object({{"id", "missing-type"}}));
    writer.finish();
    REQUIRE(out.str().find("no tests reported") != std::string::npos);
}

#ifdef TEEZ_ENABLE_UI

TEST_CASE("RunOutputWriter routes events through live UI on a pseudo TTY", "[run_output][branches]") {
    int master = -1;
    int slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
        SKIP("openpty is not available on this system");
    }

    const int saved_stdout = dup(STDOUT_FILENO);
    dup2(slave, STDOUT_FILENO);
    close(slave);
    setvbuf(stdout, nullptr, _IONBF, 0);

    {
        teez::cli::RunOutputWriter writer({.ui = true}, std::cout);
        writer.on_event({{"event", "start"}, {"id", "suite::alpha"}});
        writer.on_event({{"event", "pass"}, {"id", "suite::alpha"}});
        writer.on_event({{"event", "phase"},
                         {"id", "suite::alpha"},
                         {"phase", "afterEach"},
                         {"state", "start"}});
        writer.on_event({{"event", "coverage"},
                         {"line_rate", 0.5},
                         {"lines_hit", 1},
                         {"lines_found", 2},
                         {"source", "progress"}});
        writer.finish();
    }

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    std::string captured;
    char buffer[4096];
    ssize_t bytes = 0;
    while ((bytes = read(master, buffer, sizeof(buffer))) > 0) {
        captured.append(buffer, static_cast<std::size_t>(bytes));
    }
    close(master);

    REQUIRE(captured.find("1 passed") != std::string::npos);
    REQUIRE(captured.find("afterEach") != std::string::npos);
    REQUIRE(captured.find("50.00% lines") != std::string::npos);
}

#endif
