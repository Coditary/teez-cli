#pragma once

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "teez/core/context.hpp"
#include "teez/core/plugin.hpp"
#include "teez/core/teez_config.hpp"

namespace teez::cli {

struct ProjectCommandOptions {
    bool json = false;
    bool validate = false;
    bool force = false;
    bool with_demo = false;
};

struct InitResult {
    bool created = false;
    std::filesystem::path config_path;
    std::optional<std::filesystem::path> demo_test_path;
    std::string error;
};

std::string format_command_line(const teez::core::CommandSpec& spec);

int run_discover_command(const std::filesystem::path& plugins_dir,
                         const teez::core::TeezConfig& config,
                         const teez::core::RunContext& context,
                         const ProjectCommandOptions& options, std::ostream& out);

int run_config_command(const teez::core::TeezConfigResolveOptions& resolve_options,
                       const ProjectCommandOptions& options, std::ostream& out);

InitResult run_init_command(const std::filesystem::path& target_dir,
                            const ProjectCommandOptions& options);

int write_init_result(const InitResult& result, const ProjectCommandOptions& options,
                      std::ostream& out);

} // namespace teez::cli
