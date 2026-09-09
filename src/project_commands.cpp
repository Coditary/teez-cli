#include "teez/cli/project_commands.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "teez/core/discovery.hpp"
#include "teez/core/exit_code.hpp"
#include "teez/core/plugin.hpp"
#include "teez/core/plugin_config.hpp"
#include "teez/core/runner_config.hpp"

namespace teez::cli {

namespace {

std::string shell_quote(const std::string& value) {
    if (value.find_first_of(" \t\"'$\\") == std::string::npos) {
        return value;
    }

    std::string quoted = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

nlohmann::json command_spec_to_json(const teez::core::CommandSpec& spec) {
    nlohmann::json command = nlohmann::json::object();
    command["command"] = spec.command;
    command["args"] = spec.args;
    command["line"] = format_command_line(spec);
    return command;
}

nlohmann::json discovery_match_to_json(const teez::core::DiscoveryMatch& match) {
    nlohmann::json plugin = nlohmann::json::object();
    plugin["name"] = match.manifest.name;
    plugin["file"] = match.manifest.plugin_file.string();
    plugin["priority"] = match.manifest.priority;
    plugin["matched"] = nlohmann::json::array();
    for (const auto& reason : match.reasons) {
        plugin["matched"].push_back({{"kind", reason.kind}, {"value", reason.value}});
    }

    plugin["requires"] = nlohmann::json::array();
    for (const auto& tool : match.manifest.tool_requires) {
        plugin["requires"].push_back(
            {{"tool", tool}, {"available", teez::core::is_tool_available(tool)}});
    }

    return plugin;
}

std::optional<teez::core::CommandSpec>
build_plugin_command(const std::filesystem::path& plugin_path,
                     const teez::core::RunContext& context) {
    try {
        teez::core::Plugin plugin(plugin_path);
        return plugin.build_command(context);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<teez::core::CommandSpec>
build_plugin_list_command(const std::filesystem::path& plugin_path,
                          const teez::core::RunContext& context) {
    try {
        teez::core::Plugin plugin(plugin_path);
        return plugin.build_list_command(context);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

teez::core::RunContext runner_context_for_match(const teez::core::RunContext& context,
                                                const teez::core::DiscoveryMatch& match,
                                                const teez::core::DiscoveryContext& discovery) {
    teez::core::RunContext runner_context = context;
    runner_context.runner_name = match.manifest.name;
    runner_context.runner_options = teez::core::resolve_effective_runner_options(
        match.manifest.include, match.manifest.exclude, discovery.runners, match.manifest.name);
    return runner_context;
}

void write_discover_runner_text(const teez::core::RunContext& context,
                                const teez::core::DiscoveryMatch& match, std::ostream& out) {
    out << "Runner:  " << match.manifest.name << " ("
        << match.manifest.plugin_file.filename().string() << ")\n";
    out << "Priority:" << match.manifest.priority << '\n';

    if (!match.reasons.empty()) {
        out << "Matched:";
        for (const auto& reason : match.reasons) {
            out << ' ' << reason.kind << '=' << reason.value;
        }
        out << '\n';
    }

    if (!match.manifest.tool_requires.empty()) {
        out << "Requires:";
        for (const auto& tool : match.manifest.tool_requires) {
            out << ' ' << tool << '(' << (teez::core::is_tool_available(tool) ? "ok" : "missing")
                << ')';
        }
        out << '\n';
    }

    if (const auto run_command = build_plugin_command(match.manifest.plugin_file, context)) {
        out << "Run:     " << format_command_line(*run_command) << '\n';
    }

    teez::core::RunContext list_context = context;
    list_context.command = "list";
    if (const auto list_command =
            build_plugin_list_command(match.manifest.plugin_file, list_context)) {
        out << "List:    " << format_command_line(*list_command) << '\n';
    }
}

void write_discover_text(const std::filesystem::path& project_path,
                         const teez::core::TeezConfig& config,
                         const teez::core::RunContext& context,
                         const teez::core::DiscoveryContext& discovery,
                         const std::vector<teez::core::DiscoveryMatch>& matches,
                         std::ostream& out) {
    out << "Target:  " << project_path.string() << '\n';

    if (!config.empty()) {
        out << "Config:  " << config.root().string() << '\n';
    } else {
        out << "Config:  (none)\n";
    }

    if (matches.empty()) {
        out << "Runners: (no match)\n";
        return;
    }

    out << "Runners: " << matches.size() << '\n';
    for (std::size_t index = 0; index < matches.size(); ++index) {
        if (index > 0) {
            out << '\n';
        }
        write_discover_runner_text(runner_context_for_match(context, matches[index], discovery),
                                   matches[index], out);
    }
}

} // namespace

std::string format_command_line(const teez::core::CommandSpec& spec) {
    std::ostringstream line;
    line << spec.command;
    for (const auto& arg : spec.args) {
        line << ' ' << shell_quote(arg);
    }
    return line.str();
}

int run_discover_command(const std::filesystem::path& plugins_dir,
                         const teez::core::TeezConfig& config,
                         const teez::core::RunContext& context,
                         const ProjectCommandOptions& options, std::ostream& out) {
    const auto project_paths = config.resolve_project_paths(context.target_path);
    const teez::core::DiscoveryContext discovery =
        teez::core::make_discovery_context(config, plugins_dir);

    if (options.json) {
        nlohmann::json payload = nlohmann::json::object();
        payload["target"] = std::filesystem::absolute(context.target_path).string();
        payload["config"] = nlohmann::json::object();
        payload["config"]["loaded"] = !config.empty();
        if (!config.empty()) {
            payload["config"]["root"] = config.root().string();
        }
        payload["projects"] = nlohmann::json::array();

        for (const auto& project_path : project_paths) {
            teez::core::RunContext project_context = context;
            project_context.target_path = project_path;

            nlohmann::json entry = nlohmann::json::object();
            entry["path"] = project_path.string();
            entry["runners"] = nlohmann::json::array();

            const auto matches = teez::core::find_all_runner_matches(project_path, discovery);
            for (const auto& match : matches) {
                const auto runner_context =
                    runner_context_for_match(project_context, match, discovery);
                nlohmann::json runner_entry = discovery_match_to_json(match);
                if (const auto run_command =
                        build_plugin_command(match.manifest.plugin_file, runner_context)) {
                    runner_entry["commands"]["run"] = command_spec_to_json(*run_command);
                }
                teez::core::RunContext list_context = runner_context;
                list_context.command = "list";
                if (const auto list_command =
                        build_plugin_list_command(match.manifest.plugin_file, list_context)) {
                    runner_entry["commands"]["list"] = command_spec_to_json(*list_command);
                }
                entry["runners"].push_back(std::move(runner_entry));
            }

            payload["projects"].push_back(std::move(entry));
        }

        out << payload.dump(2) << '\n';
        return teez::core::kExitSuccess;
    }

    bool first = true;
    for (const auto& project_path : project_paths) {
        if (!first) {
            out << '\n';
        }
        first = false;

        teez::core::RunContext project_context = context;
        project_context.target_path = project_path;
        const auto matches = teez::core::find_all_runner_matches(project_path, discovery);
        write_discover_text(project_path, config, project_context, discovery, matches, out);
    }

    return teez::core::kExitSuccess;
}

int run_config_command(const teez::core::TeezConfigResolveOptions& resolve_options,
                       const ProjectCommandOptions& options, std::ostream& out) {
    teez::core::TeezConfigSummary summary;
    if (options.validate) {
        summary = teez::core::validate_config(resolve_options);
    } else {
        try {
            const auto config = teez::core::TeezConfig::resolve(resolve_options);
            summary = teez::core::summarize_config(config, resolve_options.target_path);
            if (!config.empty()) {
                summary.config_path =
                    resolve_options.config_file.has_value()
                        ? std::filesystem::absolute(*resolve_options.config_file)
                        : std::filesystem::absolute(resolve_options.search_dir / "teez.config.lua");
            }
        } catch (const std::exception& ex) {
            summary.errors.push_back(ex.what());
        }
    }

    if (options.json) {
        nlohmann::json payload = nlohmann::json::object();
        payload["loaded"] = summary.loaded;
        if (!summary.config_path.empty()) {
            payload["config_path"] = summary.config_path.string();
        }
        if (!summary.root.empty()) {
            payload["root"] = summary.root.string();
        }
        payload["applies_to_target"] = summary.applies_to_target;
        if (summary.profile.has_value()) {
            payload["profile"] = *summary.profile;
        }

        payload["projects"] = nlohmann::json::array();
        for (const auto& project : summary.resolved_projects) {
            payload["projects"].push_back(project.string());
        }

        payload["valid"] = summary.errors.empty();
        payload["errors"] = summary.errors;
        out << payload.dump(2) << '\n';
        return summary.errors.empty() ? teez::core::kExitSuccess : teez::core::kExitFailure;
    }

    if (!summary.config_path.empty()) {
        out << "Config:  " << summary.config_path.string() << '\n';
    } else {
        out << "Config:  (none)\n";
    }

    if (summary.loaded) {
        out << "Root:    " << summary.root.string() << '\n';
        out << "Applies: " << (summary.applies_to_target ? "yes" : "no") << '\n';
        if (summary.profile.has_value()) {
            out << "Profile: " << *summary.profile << '\n';
        }
    }

    if (!summary.resolved_projects.empty()) {
        out << "Projects:\n";
        for (const auto& project : summary.resolved_projects) {
            out << "  - " << project.string() << '\n';
        }
    }

    for (const auto& error : summary.errors) {
        out << "Error:   " << error << '\n';
    }

    return summary.errors.empty() ? teez::core::kExitSuccess : teez::core::kExitFailure;
}

InitResult run_init_command(const std::filesystem::path& target_dir,
                            const ProjectCommandOptions& options) {
    InitResult result;
    std::error_code ec;
    const auto abs_target = std::filesystem::absolute(target_dir, ec);
    if (ec) {
        result.error = "invalid target directory: " + target_dir.string();
        return result;
    }

    std::filesystem::create_directories(abs_target, ec);
    if (ec) {
        result.error = "failed to create directory: " + abs_target.string();
        return result;
    }

    result.config_path = abs_target / "teez.config.lua";
    if (std::filesystem::exists(result.config_path) && !options.force) {
        result.error = "config already exists: " + result.config_path.string();
        return result;
    }

    const std::string config_body = R"(-- teez workspace config
return {
    profile = os.getenv("CI") and "ci" or "local",
    plugins_dir = "plugins",
    parallel_runners = true,

    -- Optional explicit runner plugins (tried in order; name + optional version per manifest)
    -- plugins = { "vitest" },
    -- harnesses = { "process" },

    runners = {
        -- Optional overrides for manifest include/exclude and plugin-specific fields
        -- vitest = {
        --     include = { "tests/**/*.test.ts" },
        --     exclude = { "**/*.teez.lua" },
        --     pattern = "unit",                 -- plugin-specific
        --     args = { "--pool=threads" },      -- plugin-specific
        -- },
        -- teez = {
        --     include = { "tests/**/*.teez.lua" },
        -- },
        -- ctest = {
        --     build_dir = "build",
        --     regex = "^unit_",
        --     exclude = "slow",
        --     parallel = 2,
        -- },
    },

    -- Worker test types: type name -> glob or list of globs
    -- types = {
    --     unit = "tests/unit/**",
    --     integration = { "tests/integration/**", "e2e/**" },
    --     system = "tests/system/**",
    -- },

    profiles = {
        ci = {
            test_report = { reporter = "junit", output = "reports/junit.xml" },
        },
        ["local"] = {
            test_report = { reporter = "json" },
        },
    },

    projects = {
        -- "tests",
    },
}
)";

    std::ofstream config_file(result.config_path);
    if (!config_file.is_open()) {
        result.error = "failed to write config: " + result.config_path.string();
        return result;
    }
    config_file << config_body;
    result.created = true;

    const auto plugins_dir = abs_target / "plugins";
    std::filesystem::create_directories(plugins_dir, ec);
    const auto plugins_readme = plugins_dir / "README.md";
    if (!std::filesystem::exists(plugins_readme) || options.force) {
        std::ofstream readme(plugins_readme);
        if (readme.is_open()) {
            readme << "# teez plugins\n\n"
                   << "Place runner and harness manifests here (`*.json` + referenced `.lua` "
                      "files).\n\n"
                   << "Example `vitest.json`:\n\n"
                   << "```json\n"
                   << "{\n"
                   << "  \"kind\": \"runner\",\n"
                   << "  \"name\": \"vitest\",\n"
                   << "  \"version\": \"1\",\n"
                   << "  \"plugin\": \"vitest.lua\",\n"
                   << "  \"anchors\": [\"vitest.config.ts\", \"vite.config.ts\"],\n"
                   << "  \"include\": [\"**/*.{test,spec}.{ts,tsx,js,jsx}\"],\n"
                   << "  \"exclude\": [\"**/*.teez.lua\", \"**/*.py\"],\n"
                   << "  \"priority\": 120\n"
                   << "}\n"
                   << "```\n\n"
                   << "Select it from `teez.config.lua` with `plugins = { \"vitest\" }`.\n";
        }
    }

    if (options.with_demo) {
        const auto tests_dir = abs_target / "tests";
        std::filesystem::create_directories(tests_dir, ec);
        result.demo_test_path = tests_dir / "smoke.teez.lua";

        if (std::filesystem::exists(*result.demo_test_path) && !options.force) {
            result.error = "demo test already exists: " + result.demo_test_path->string();
            result.created = false;
            return result;
        }

        std::ofstream demo_file(*result.demo_test_path);
        if (!demo_file.is_open()) {
            result.error = "failed to write demo test: " + result.demo_test_path->string();
            result.created = false;
            return result;
        }

        demo_file << R"(test.describe("Smoke", function()
    test.it("passes a basic assertion", function(t)
        t.assert_true(true)
    end)
end)
)";
    }

    return result;
}

int write_init_result(const InitResult& result, const ProjectCommandOptions& options,
                      std::ostream& out) {
    if (!result.error.empty()) {
        if (options.json) {
            out << nlohmann::json{{"created", false}, {"error", result.error}}.dump(2) << '\n';
        }
        return teez::core::kExitFailure;
    }

    if (options.json) {
        nlohmann::json payload = nlohmann::json::object();
        payload["created"] = result.created;
        payload["config_path"] = result.config_path.string();
        if (result.demo_test_path.has_value()) {
            payload["demo_test"] = result.demo_test_path->string();
        }
        out << payload.dump(2) << '\n';
        return teez::core::kExitSuccess;
    }

    out << "Created: " << result.config_path.string() << '\n';
    if (result.demo_test_path.has_value()) {
        out << "Demo:    " << result.demo_test_path->string() << '\n';
    }
    return teez::core::kExitSuccess;
}

} // namespace teez::cli
