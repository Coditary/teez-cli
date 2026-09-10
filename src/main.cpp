#include <iostream>
#include <string_view>

#include <algorithm>
#include <cstdlib>

#include "teez/worker/runtime_config.hpp"

#include "teez/cli/list_format.hpp"
#include "teez/cli/parser.hpp"
#include "teez/cli/project_commands.hpp"
#include "teez/cli/run_output.hpp"
#include "teez/core/config.hpp"
#include "teez/core/context.hpp"
#include "teez/core/coverage_reporter.hpp"
#include "teez/core/discovery.hpp"
#include "teez/core/exit_code.hpp"
#include "teez/core/install_paths.hpp"
#include "teez/core/plugin_config.hpp"
#include "teez/core/runner.hpp"
#include "teez/core/signals.hpp"
#include "teez/core/teez_config.hpp"
#include "teez/core/test_report.hpp"
#include "teez/core/test_reporter.hpp"

#ifdef TEEZ_EMBEDDED_WORKER
#include "teez/worker/runner.hpp"

#ifndef TEEZ_WORKER_RUNTIME_DIR
#error "TEEZ_WORKER_RUNTIME_DIR must be defined when TEEZ_EMBEDDED_WORKER is set"
#endif
#endif

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

namespace {

#ifdef TEEZ_EMBEDDED_WORKER
bool arg_is_worker_flag(std::string_view value) {
    return value == "--update-snapshots" || value == "--list";
}

int run_embedded_worker(int argc, char** argv) {
    bool list_only = false;
    int arg_index = 2;
    while (arg_index < argc && arg_is_worker_flag(argv[arg_index])) {
        if (std::string_view(argv[arg_index]) == "--update-snapshots") {
            setenv("TEEZ_UPDATE_SNAPSHOTS", "1", 1);
        } else if (std::string_view(argv[arg_index]) == "--list") {
            list_only = true;
        }
        ++arg_index;
    }

    if (arg_index >= argc) {
        std::cerr << "usage: teez worker [--update-snapshots] [--list] <directory>\n";
        return teez::core::kExitFailure;
    }

    try {
        if (list_only) {
            const auto tests = teez::worker::list_worker(argv[arg_index], TEEZ_WORKER_RUNTIME_DIR);
            for (const auto& id : tests) {
                std::cout << id << '\n';
            }
            return teez::core::kExitSuccess;
        }
        return teez::worker::run_worker(argv[arg_index], TEEZ_WORKER_RUNTIME_DIR, std::cout);
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return teez::core::kExitFailure;
    }
}
#endif

void configure_embedded_worker() {
#ifdef TEEZ_EMBEDDED_WORKER
    teez::worker::set_use_embedded_runtime(true);
    teez::core::set_in_process_worker(
        [](const std::filesystem::path& target_path, std::ostream& out) -> int {
            return teez::worker::run_worker(target_path, TEEZ_WORKER_RUNTIME_DIR, out);
        });
    teez::core::set_in_process_worker_list(
        [](const std::filesystem::path& target_path) -> std::vector<std::string> {
            return teez::worker::list_worker(target_path, TEEZ_WORKER_RUNTIME_DIR);
        });
#endif
}

void configure_worker_binary(int argc, char** argv) {
    if (argc <= 0 || argv[0] == nullptr) {
        return;
    }

    const auto cli_path = std::filesystem::absolute(argv[0]);

#ifdef TEEZ_EMBEDDED_WORKER
    teez::core::set_worker_binary(cli_path);
    teez::core::set_worker_subcommand("worker");
    return;
#endif

    const auto try_set_worker = [](const std::filesystem::path& candidate) {
        if (!candidate.empty() && std::filesystem::exists(candidate)) {
            teez::core::set_worker_binary(candidate);
            return true;
        }
        return false;
    };

    const auto cli_project_root = cli_path.parent_path().parent_path();
    if (!try_set_worker(cli_project_root.parent_path() / "teez-worker" / "build" / "teez-worker")) {
        try_set_worker(cli_path.parent_path().parent_path() / "teez-worker" / "teez-worker");
    }
}

void write_resolved_test_report(const teez::core::TestRunReport& report,
                                const teez::core::TestReportCliOverrides& options) {
    if (!options.reporter.has_value()) {
        return;
    }

    std::filesystem::path output_path;
    if (options.output.has_value()) {
        output_path = *options.output;
    } else {
        output_path = std::filesystem::path(
            std::string("teez-report") +
            teez::core::test_reporter_by_name(*options.reporter).default_extension());
    }

    teez::core::write_test_report(report, *options.reporter, output_path);
    std::cout << "Report:  " << std::filesystem::absolute(output_path).string() << '\n';
}

teez::core::TestConfigMetadata
build_test_config_metadata(const teez::cli::ParseResult& parsed,
                           const std::vector<std::filesystem::path>& project_paths) {
    teez::core::TestConfigMetadata config_meta;
    config_meta.projects = project_paths;

    const auto config_summary =
        teez::core::summarize_config(teez::core::active_config(), parsed.context.target_path);
    if (!config_summary.config_path.empty()) {
        config_meta.path = config_summary.config_path;
    } else if (parsed.config_file.has_value()) {
        config_meta.path = std::filesystem::absolute(*parsed.config_file);
    }

    return config_meta;
}

teez::core::TestSuiteBegin build_suite_begin(const std::filesystem::path& project_path,
                                             const teez::core::DiscoveryContext& discovery) {
    teez::core::TestSuiteBegin suite_begin;
    suite_begin.path = project_path;
    if (const auto match = teez::core::find_runner_match(project_path, discovery)) {
        suite_begin.plugin = teez::core::TestPluginInfo{
            .name = match->manifest.name,
            .file = match->manifest.plugin_file,
        };
    }
    return suite_begin;
}

} // namespace

int main(int argc, char** argv) {
#ifdef TEEZ_EMBEDDED_WORKER
    if (argc >= 2 && std::string_view(argv[1]) == "worker") {
        return run_embedded_worker(argc, argv);
    }
#endif

    teez::core::install_signal_handlers();
    configure_embedded_worker();
    configure_worker_binary(argc, argv);

    const auto parsed = teez::cli::parse_args(argc, argv);
    if (parsed.exit_code.has_value()) {
        return *parsed.exit_code;
    }
    if (!parsed.ok) {
        std::cerr << "error: " << parsed.error << '\n';
        return teez::core::kExitFailure;
    }

    if (parsed.context.command == "init") {
        const auto init_result =
            teez::cli::run_init_command(parsed.context.target_path, parsed.project);
        return teez::cli::write_init_result(init_result, parsed.project, std::cout);
    }

    const auto validation_error = teez::core::validate_run_context(parsed.context);
    if (!validation_error.empty()) {
        std::cerr << "error: " << validation_error << '\n';
        return teez::core::kExitFailure;
    }

    if (parsed.context.command == "config") {
        const teez::core::TeezConfigResolveOptions config_options{
            .search_dir = std::filesystem::current_path(),
            .target_path = parsed.context.target_path,
            .config_file = parsed.config_file,
            .profile = parsed.profile,
        };
        return teez::cli::run_config_command(config_options, parsed.project, std::cout);
    }

    try {
        const teez::core::TeezConfigResolveOptions config_options{
            .search_dir = std::filesystem::current_path(),
            .target_path = parsed.context.target_path,
            .config_file = parsed.config_file,
            .profile = parsed.profile,
        };
        auto config = teez::core::TeezConfig::resolve(config_options);
#ifndef TEEZ_EMBEDDED_WORKER
        if (const auto worker_bin = config.get_string("worker_bin"); worker_bin.has_value()) {
            teez::core::set_worker_binary(*worker_bin);
        }
#endif

        if (parsed.context.command == "coverage") {
            const auto options =
                teez::core::resolve_coverage_report_options(config.data(), parsed.coverage);
            teez::core::export_coverage_report(options);
            teez::core::set_active_config(std::move(config));
            return teez::core::kExitSuccess;
        }

        if (parsed.context.command == "discover") {
            teez::core::set_active_config(std::move(config));
            return teez::cli::run_discover_command(teez::core::resolve_bundled_plugins_dir(),
                                                   teez::core::active_config(), parsed.context,
                                                   parsed.project, std::cout);
        }

        teez::core::set_active_config(std::move(config));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return teez::core::kExitFailure;
    }

    if (parsed.update_snapshots) {
        setenv("TEEZ_UPDATE_SNAPSHOTS", "1", 1);
    }

    const teez::core::DiscoveryContext discovery = teez::core::make_discovery_context(
        teez::core::active_config(), teez::core::resolve_bundled_plugins_dir());
    const auto project_paths =
        teez::core::active_config().resolve_project_paths(parsed.context.target_path);
    const auto test_report_options = teez::core::resolve_test_report_cli_overrides(
        teez::core::active_config().data(), parsed.test_report);

    try {
        if (parsed.context.command == "list") {
            std::vector<std::string> tests;
            for (const auto& project_path : project_paths) {
                teez::core::RunContext project_context = parsed.context;
                project_context.target_path = project_path;
                const auto project_tests = teez::core::list_context(discovery, project_context);
                tests.insert(tests.end(), project_tests.begin(), project_tests.end());
            }
            teez::cli::write_test_list(tests, parsed.list, std::cout);
            return teez::core::kExitSuccess;
        }

        if (parsed.with_coverage) {
            teez::cli::RunOutputWriter writer(parsed.run, std::cout);
            teez::core::TestRunCollector collector;
            collector.begin_run(parsed.context.command);
            collector.set_config(build_test_config_metadata(parsed, project_paths));
            int exit_code = teez::core::kExitSuccess;
            for (const auto& project_path : project_paths) {
                teez::core::RunContext project_context = parsed.context;
                project_context.target_path = project_path;
                collector.begin_suite(build_suite_begin(project_path, discovery));
                exit_code = std::max(
                    exit_code, teez::core::run_coverage_context(discovery, project_context,
                                                                [&](const nlohmann::json& event) {
                                                                    collector.on_event(event);
                                                                    writer.on_event(event);
                                                                }));
            }
            writer.finish();
            collector.finish_run(exit_code);
            write_resolved_test_report(collector.build(), test_report_options);
            return exit_code;
        }

        teez::cli::RunOutputWriter writer(parsed.run, std::cout);
        teez::core::TestRunCollector collector;
        collector.begin_run(parsed.context.command);
        collector.set_config(build_test_config_metadata(parsed, project_paths));
        int exit_code = teez::core::kExitSuccess;
        for (const auto& project_path : project_paths) {
            teez::core::RunContext project_context = parsed.context;
            project_context.target_path = project_path;
            collector.begin_suite(build_suite_begin(project_path, discovery));
            exit_code =
                std::max(exit_code, teez::core::run_context(discovery, project_context,
                                                            [&](const nlohmann::json& event) {
                                                                collector.on_event(event);
                                                                writer.on_event(event);
                                                            }));
        }
        writer.finish();
        collector.finish_run(exit_code);
        write_resolved_test_report(collector.build(), test_report_options);
        return exit_code;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return teez::core::kExitFailure;
    }
}
