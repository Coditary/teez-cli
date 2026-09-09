#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "teez/cli/list_format.hpp"
#include "teez/cli/project_commands.hpp"
#include "teez/cli/run_output.hpp"
#include "teez/core/context.hpp"
#include "teez/core/coverage_reporter.hpp"
#include "teez/core/test_reporter.hpp"

namespace teez::cli {

struct ParseResult {
    bool ok = false;
    std::string error;
    /// When set, main should return this code immediately (e.g. after printing --help).
    std::optional<int> exit_code;
    teez::core::RunContext context;
    ListOutputOptions list;
    RunOutputOptions run;
    bool update_snapshots = false;
    bool with_coverage = false;
    teez::core::CoverageReportCliOverrides coverage;
    std::optional<std::filesystem::path> config_file;
    std::optional<std::string> profile;
    ProjectCommandOptions project;
    teez::core::TestReportCliOverrides test_report;
};

/// Parses CLI arguments into a RunContext.
/// Expected usage: teez run <path> | teez list <path> | teez coverage <path> |
/// teez discover <path> | teez config [path] | teez init [path]
ParseResult parse_args(int argc, char** argv);

/// Vector wrapper for tests — forwards to argc/argv parsing.
ParseResult parse_args(const std::vector<std::string>& args);

} // namespace teez::cli
