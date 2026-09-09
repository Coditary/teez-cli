#include "teez/cli/parser.hpp"

#include <optional>

#include <CLI/CLI.hpp>

#include "teez/core/coverage_reporter.hpp"
#include "teez/core/test_reporter.hpp"

namespace teez::cli {

namespace {

struct CtestFilterBindings {
    std::optional<std::string> regex;
    std::optional<std::string> exclude;
    std::optional<std::string> label;
    std::optional<std::string> exclude_label;
};

struct TestFilterBindings {
    std::optional<std::string> file;
    std::vector<std::string> types;
    std::optional<std::string> pattern;
    std::optional<std::string> name;
    std::optional<std::string> name_pattern;
};

void add_ctest_filter_options(CLI::App* cmd, CtestFilterBindings& filters) {
    cmd->add_option("-R,--regex", filters.regex, "Run only tests matching this regex (CTest -R)");
    cmd->add_option("-E,--exclude", filters.exclude,
                    "Exclude tests matching this regex (CTest -E)");
    cmd->add_option("-L,--label", filters.label, "Run only tests with this label (CTest -L)");
    cmd->add_option("--exclude-label", filters.exclude_label,
                    "Exclude tests with this label (CTest -LE)");
}

void add_test_filter_options(CLI::App* cmd, TestFilterBindings& filters) {
    cmd->add_option("-f,--file", filters.file, "Run tests in files matching this regex");
    cmd->add_option("-t,--type", filters.types, "Run tests with this type (repeatable)");
    cmd->add_option("-p,--pattern", filters.pattern,
                    "Run tests whose serialized id contains this substring");
    cmd->add_option("-n,--name", filters.name, "Run tests whose name matches this glob pattern");
    cmd->add_option("--name-pattern", filters.name_pattern,
                    "Run tests whose name matches this regex pattern");
}

void apply_ctest_filters(teez::core::RunContext& context, const CtestFilterBindings& filters) {
    context.ctest.regex = filters.regex;
    context.ctest.exclude = filters.exclude;
    context.ctest.label = filters.label;
    context.ctest.exclude_label = filters.exclude_label;
}

void apply_test_filters(teez::core::RunContext& context, const TestFilterBindings& filters) {
    context.filters.file_regex = filters.file;
    context.filters.types = filters.types;
    context.filters.id_substring = filters.pattern;
    context.filters.name_glob = filters.name;
    context.filters.name_regex = filters.name_pattern;
}

std::string validate_test_filters(const TestFilterBindings& filters) {
    if (filters.name.has_value() && filters.name_pattern.has_value()) {
        return "-n and --name-pattern cannot be used together";
    }
    return {};
}

ParseResult parse_impl(CLI::App* cmd, std::string& target_path,
                       const CtestFilterBindings& ctest_filters,
                       const TestFilterBindings& test_filters, const std::string& command,
                       const ListOutputOptions& list_options, const RunOutputOptions& run_options,
                       bool update_snapshots, bool with_coverage,
                       const teez::core::CoverageReportCliOverrides& coverage,
                       const std::optional<std::filesystem::path>& config_file,
                       const std::optional<std::string>& profile) {
    ParseResult result;

    if (cmd->parsed()) {
        const auto filter_error = validate_test_filters(test_filters);
        if (!filter_error.empty()) {
            result.error = filter_error;
            return result;
        }

        if (target_path.empty()) {
            target_path = ".";
        }
        result.context.command = command;
        result.context.target_path = target_path;
        apply_ctest_filters(result.context, ctest_filters);
        apply_test_filters(result.context, test_filters);
        result.list = list_options;
        result.run = run_options;
        result.update_snapshots = update_snapshots;
        result.with_coverage = with_coverage;
        result.coverage = coverage;
        result.config_file = config_file;
        result.profile = profile;
        result.ok = true;
    }

    return result;
}

} // namespace

ParseResult parse_args(int argc, char** argv) {
    ParseResult result;

    if (argc <= 0 || argv == nullptr) {
        result.error = "no arguments provided";
        return result;
    }

    CLI::App app{"teez — test everything easy"};
    app.require_subcommand(1);

    std::optional<std::string> config_path;
    app.add_option("--config", config_path,
                   "Path to teez.config.lua (default: ./teez.config.lua in the working directory)");

    std::optional<std::string> profile_name;
    app.add_option("--profile", profile_name,
                   "Active config profile (overrides teez.config.lua profile)");

    std::string target_path;
    CtestFilterBindings ctest_filters;
    TestFilterBindings test_filters;
    bool update_snapshots = false;
    bool with_coverage = false;
    bool run_progress = false;
    bool run_json = false;
    bool run_simple = false;

    app.add_flag("--update-snapshots", update_snapshots,
                 "Update golden snapshot files for worker tests");

    auto* run_cmd = app.add_subcommand("run", "Run tests in a directory");
    run_cmd->add_option("path", target_path, "Target directory (defaults to current directory)");
    add_ctest_filter_options(run_cmd, ctest_filters);
    add_test_filter_options(run_cmd, test_filters);
    run_cmd->add_flag("--update-snapshots", update_snapshots,
                      "Update golden snapshot files for worker tests");
    run_cmd->add_flag("--progress", run_progress,
                      "Stream individual test events (use with --json for NDJSON)");
    run_cmd->add_flag("--simple", run_simple,
                      "Plain scrolling output without the live terminal status band");
    run_cmd->add_flag("--json", run_json,
                      "Emit JSON output (summary object, or NDJSON stream with --progress)");
    run_cmd->add_flag("--with-coverage", with_coverage,
                      "Run via plugin build_coverage_run and collect coverage");

    std::optional<std::string> report_reporter;
    std::optional<std::string> report_output;
    run_cmd
        ->add_option("--report", report_reporter,
                     "Write test results to a report format: json, junit")
        ->check(CLI::IsMember(teez::core::test_reporter_names()));
    run_cmd->add_option("--report-output", report_output, "Destination file for --report");

    auto* list_cmd = app.add_subcommand("list", "List discovered tests without running them");
    list_cmd->add_option("path", target_path, "Target directory (defaults to current directory)");
    add_ctest_filter_options(list_cmd, ctest_filters);
    add_test_filter_options(list_cmd, test_filters);
    bool list_flat = false;
    bool list_json = false;
    list_cmd->add_flag("--flat", list_flat, "Print one test id per line without tree formatting");
    list_cmd->add_flag("--json", list_json, "Emit one NDJSON object per test");

    auto* coverage_cmd =
        app.add_subcommand("coverage", "Export a coverage report to another format");
    coverage_cmd->add_option(
        "path", target_path,
        "Project directory for teez.config.lua (defaults to current directory)");
    std::optional<std::string> coverage_reporter;
    std::optional<std::string> coverage_input;
    std::optional<std::string> coverage_output;
    std::optional<double> coverage_min_line_rate;
    coverage_cmd
        ->add_option("--reporter", coverage_reporter,
                     "Output format: json, msgpack, lcov, cobertura, junit")
        ->check(CLI::IsMember(teez::core::coverage_reporter_names()));
    coverage_cmd->add_option("--input", coverage_input, "Coverage input file (LCOV or Cobertura)");
    coverage_cmd->add_option("--output", coverage_output,
                             "Destination file for the exported report");
    coverage_cmd->add_option("--min-line-rate", coverage_min_line_rate,
                             "Fail junit export when line rate is below this threshold");

    auto* discover_cmd =
        app.add_subcommand("discover", "Show which plugin and commands teez would use for a path");
    discover_cmd->add_option("path", target_path,
                             "Target directory (defaults to current directory)");
    bool discover_json = false;
    discover_cmd->add_flag("--json", discover_json, "Emit machine-readable JSON");

    auto* config_cmd = app.add_subcommand("config", "Show or validate teez.config.lua");
    config_cmd->add_option("path", target_path, "Target directory (defaults to current directory)");
    bool config_validate = false;
    bool config_json = false;
    config_cmd->add_flag("--validate", config_validate, "Validate config and project paths");
    config_cmd->add_flag("--json", config_json, "Emit machine-readable JSON");

    auto* init_cmd = app.add_subcommand("init", "Create a starter teez.config.lua");
    init_cmd->add_option("path", target_path,
                         "Directory to initialize (defaults to current directory)");
    bool init_force = false;
    bool init_with_demo = false;
    bool init_json = false;
    init_cmd->add_flag("--force", init_force, "Overwrite existing files");
    init_cmd->add_flag("--with-demo", init_with_demo, "Also create tests/smoke.teez.lua");
    init_cmd->add_flag("--json", init_json, "Emit machine-readable JSON");

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp& e) {
        result.exit_code = app.exit(e);
        result.ok = true;
        return result;
    } catch (const CLI::CallForAllHelp& e) {
        result.exit_code = app.exit(e);
        result.ok = true;
        return result;
    } catch (const CLI::ParseError& e) {
        result.error = e.what();
        return result;
    }

    teez::core::CoverageReportCliOverrides coverage_overrides;
    if (coverage_reporter.has_value()) {
        coverage_overrides.reporter = coverage_reporter;
    }
    if (coverage_input.has_value()) {
        coverage_overrides.input = *coverage_input;
    }
    if (coverage_output.has_value()) {
        coverage_overrides.output = *coverage_output;
    }
    if (coverage_min_line_rate.has_value()) {
        coverage_overrides.min_line_rate = coverage_min_line_rate;
    }

    std::optional<std::filesystem::path> config_file;
    if (config_path.has_value()) {
        config_file = *config_path;
    }

    if (run_cmd->parsed()) {
        const bool run_live_ui = !run_simple && !run_json && !run_progress;
        auto parsed = parse_impl(run_cmd, target_path, ctest_filters, test_filters, "run", {},
                                 {.progress = run_progress, .json = run_json, .ui = run_live_ui},
                                 update_snapshots, with_coverage, coverage_overrides, config_file,
                                 profile_name);
        if (report_reporter.has_value()) {
            parsed.test_report.reporter = report_reporter;
        }
        if (report_output.has_value()) {
            parsed.test_report.output = *report_output;
        }
        return parsed;
    }

    if (coverage_cmd->parsed()) {
        return parse_impl(coverage_cmd, target_path, ctest_filters, test_filters, "coverage", {},
                          {}, update_snapshots, false, coverage_overrides, config_file,
                          profile_name);
    }

    if (discover_cmd->parsed()) {
        auto parsed =
            parse_impl(discover_cmd, target_path, ctest_filters, test_filters, "discover", {}, {},
                       update_snapshots, false, coverage_overrides, config_file, profile_name);
        parsed.project.json = discover_json;
        return parsed;
    }

    if (config_cmd->parsed()) {
        auto parsed =
            parse_impl(config_cmd, target_path, ctest_filters, test_filters, "config", {}, {},
                       update_snapshots, false, coverage_overrides, config_file, profile_name);
        parsed.project.json = config_json;
        parsed.project.validate = config_validate;
        return parsed;
    }

    if (init_cmd->parsed()) {
        auto parsed =
            parse_impl(init_cmd, target_path, ctest_filters, test_filters, "init", {}, {},
                       update_snapshots, false, coverage_overrides, config_file, profile_name);
        parsed.project.json = init_json;
        parsed.project.force = init_force;
        parsed.project.with_demo = init_with_demo;
        return parsed;
    }

    return parse_impl(list_cmd, target_path, ctest_filters, test_filters, "list",
                      {.tree = !list_flat && !list_json, .json = list_json}, {}, update_snapshots,
                      false, coverage_overrides, config_file, profile_name);
}

ParseResult parse_args(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {.ok = false, .error = "no arguments provided"};
    }

    std::vector<std::string> storage = args;
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& arg : storage) {
        argv.push_back(arg.data());
    }
    return parse_args(static_cast<int>(argv.size()), argv.data());
}

} // namespace teez::cli
