#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "fuzz_helpers.hpp"
#include "teez/cli/list_format.hpp"
#include "teez/cli/parser.hpp"
#include "teez/cli/run_output.hpp"
#include "teez/core/context.hpp"
#include "teez/core/coverage.hpp"
#include "teez/core/coverage_msgpack.hpp"
#include "teez/core/coverage_reporter.hpp"
#include "teez/core/discovery.hpp"
#include "teez/core/exit_code.hpp"
#include "teez/core/plugin.hpp"
#include "teez/core/runner.hpp"
#include "teez/core/teez_config.hpp"
#include "teez/core/test_filter.hpp"

#ifdef TEEZ_FUZZ_WORKER
#include "teez/worker/runner.hpp"
#endif

#ifndef TEEZ_PLUGIN_DIR
#error "TEEZ_PLUGIN_DIR must be defined"
#endif

#ifndef TEEZ_FUZZ_FIXTURES_DIR
#error "TEEZ_FUZZ_FIXTURES_DIR must be defined"
#endif

namespace {

constexpr const char* kPluginDir = TEEZ_PLUGIN_DIR;
const std::filesystem::path kFixturesDir = TEEZ_FUZZ_FIXTURES_DIR;
const std::filesystem::path kSampleLcov =
    std::filesystem::path(kPluginDir).parent_path() / "tests/fixtures/coverage/sample.lcov";
const std::filesystem::path kSampleCobertura = std::filesystem::path(kPluginDir).parent_path() /
                                               "tests/fixtures/coverage/sample.cobertura.xml";

enum class FuzzOp : std::uint8_t {
    CliArgs = 0,
    FilterJson = 1,
    ParseLineDummy = 2,
    ParseLineCtest = 3,
    ParseLinePytest = 4,
    ParseLineWorker = 5,
    PluginEventJson = 6,
    ManifestJson = 7,
    CoverageLcov = 8,
    CoverageCobertura = 9,
    CoverageMsgpack = 10,
    ConfigLua = 11,
    FilterGlobRegex = 12,
    ListContext = 13,
    RunOutputEvents = 14,
    DiscoverPlugin = 15,
    PluginBuildCommand = 16,
    PluginListTests = 17,
    CoveragePathMatch = 18,
    CoverageRoundtrip = 19,
    CoverageReporter = 20,
    WriteTestList = 21,
    NdjsonStream = 22,
    ValidateRunContext = 23,
    ResolveType = 24,
    ExitCodeNormalize = 25,
    RunWithPluginDummy = 26,
    CoverageDiff = 27,
    FindHarnessManifest = 28,
    ListContextFiltered = 29,
    NdjsonEventWriter = 30,
    WorkerRunLua = 31,
    Count,
};

std::filesystem::path plugin_path(const char* filename) {
    return std::filesystem::path(kPluginDir) / filename;
}

teez::core::RunContext make_run_context(const std::string& command,
                                        const std::filesystem::path& target) {
    return teez::core::RunContext{
        .command = command,
        .target_path = target,
    };
}

teez::core::RunContext run_context_from_payload(const std::string& payload) {
    auto context = make_run_context("run", kFixturesDir / "teez-worker-demo");
    if (payload.empty()) {
        return context;
    }

    const std::size_t limit = std::min(payload.size(), std::size_t{96});
    const auto candidate = kFixturesDir / payload.substr(0, limit);
    if (std::filesystem::exists(candidate)) {
        context.target_path = candidate;
    }

    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (!json.is_discarded() && json.is_object()) {
        context.filters = teez::core::filters_from_json(json);
        if (json.contains("command") && json["command"].is_string()) {
            context.command = json["command"].get<std::string>();
        }
        if (json.contains("target_path") && json["target_path"].is_string()) {
            const auto path = std::filesystem::path(json["target_path"].get<std::string>());
            if (std::filesystem::exists(path)) {
                context.target_path = path;
            }
        }
    }

    return context;
}

void fuzz_cli_args(const std::string& payload) {
    auto tokens = teez::fuzz::split_null_tokens(reinterpret_cast<const uint8_t*>(payload.data()),
                                                payload.size());
    if (tokens.empty()) {
        tokens = {"teez", "list", (kFixturesDir / "teez-worker-demo").string()};
    }
    if (tokens[0] != "teez") {
        tokens.insert(tokens.begin(), "teez");
    }

    const auto parsed = teez::cli::parse_args(tokens);
    if (!parsed.ok) {
        return;
    }

    teez::core::validate_run_context(parsed.context);
    teez::core::TeezConfig::resolve({.search_dir = std::filesystem::current_path(),
                                     .target_path = parsed.context.target_path,
                                     .config_file = parsed.config_file});
}

void fuzz_filter_json(const std::string& payload) {
    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (json.is_discarded()) {
        return;
    }

    const teez::core::TestFilters filters = teez::core::filters_from_json(json);
    const teez::core::TestDescriptor descriptor{
        .file = "tests/demo/smoke.teez.lua",
        .type = "system",
        .suites = {"Suite"},
        .name = "example test",
    };
    teez::core::matches_filter(descriptor, filters);
    teez::core::serialize_id(descriptor);
    teez::core::serialize_id(descriptor, teez::core::default_test_id_format());
}

void fuzz_parse_line(const std::filesystem::path& plugin, const std::string& payload) {
    teez::core::Plugin lua_plugin(plugin);
    const std::string event_json = lua_plugin.parse_line_ndjson(payload);
    if (!event_json.empty()) {
        teez::core::parse_plugin_event_json(event_json);
    }
}

void fuzz_manifest_json(const std::string& payload) {
    const auto manifest_dir = teez::fuzz::make_temp_dir("teez-fuzz-manifests", payload);
    std::ofstream(manifest_dir / "fuzz-manifest.json") << payload;
    teez::core::load_manifests(manifest_dir);
    teez::core::load_harness_manifests(manifest_dir);
}

void fuzz_coverage_lcov(const std::string& payload) {
    const auto path = teez::fuzz::write_temp_file("teez-fuzz", ".lcov", payload);
    teez::core::load_coverage_table_from_lcov(path);
}

void fuzz_coverage_cobertura(const std::string& payload) {
    const auto path = teez::fuzz::write_temp_file("teez-fuzz", ".xml", payload);
    teez::core::load_coverage_table_from_cobertura(path);
}

void fuzz_coverage_msgpack(const std::string& payload) {
    const std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
    teez::core::coverage_table_from_msgpack(bytes);
}

void fuzz_config_lua(const std::string& payload) {
    const auto project_dir = teez::fuzz::make_temp_dir("teez-fuzz-config", payload);
    std::ofstream(project_dir / "teez.config.lua") << payload;
    teez::core::TeezConfig::load(project_dir);
}

void fuzz_filter_glob_regex(const std::string& payload) {
    const teez::core::TestDescriptor descriptor{
        .file = "benchmark.teez.lua",
        .type = "experiment",
        .suites = {"Hyperfine demo"},
        .name = "sleep 0.01 is faster than sleep 0.02",
    };
    teez::core::string_glob_match(descriptor.name, payload);
    teez::core::regex_match(descriptor.file, payload);
    teez::core::apply_output_template(payload, descriptor, teez::core::TestIdFormat{});
}

void fuzz_list_context() {
    const teez::core::RunContext context =
        make_run_context("list", kFixturesDir / "teez-worker-demo");
    teez::core::list_context(kPluginDir, context);
}

void fuzz_list_context_filtered(const std::string& payload) {
    auto context = run_context_from_payload(payload);
    context.command = "list";
    teez::core::list_context(kPluginDir, context);
}

void fuzz_run_output_events(const std::string& payload) {
    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return;
    }

    std::ostringstream out;
    teez::cli::RunOutputWriter writer({}, out);
    writer.on_event(json);
    writer.finish();
}

void fuzz_discover_plugin(const std::string& payload) {
    const auto project = teez::fuzz::make_temp_dir("teez-fuzz-disc", payload);
    if (!payload.empty()) {
        switch (payload[0] % 4) {
        case 0:
            std::ofstream(project / "pytest.ini") << payload;
            break;
        case 1:
            std::ofstream(project / "CTestTestfile.cmake") << payload;
            break;
        case 2:
            std::ofstream(project / "fuzz.teez.lua") << payload;
            break;
        default:
            std::ofstream(project / "CMakeLists.txt") << payload;
            break;
        }
    }

    teez::core::discover_plugin(project, kPluginDir);
    teez::core::glob_match(project, payload.empty() ? "*.teez.lua" : payload);
    teez::core::load_manifests(std::filesystem::path(kPluginDir));
}

void fuzz_plugin_build_command(const std::string& payload) {
    const auto context = run_context_from_payload(payload);
    teez::core::Plugin dummy(plugin_path("dummy.lua"));
    dummy.build_command(context);
    teez::core::Plugin worker(plugin_path("teez-plugin-worker.lua"));
    worker.build_command(context);
}

void fuzz_plugin_list_tests(const std::string& payload) {
    const auto context = run_context_from_payload(payload);
    teez::core::Plugin worker(plugin_path("teez-plugin-worker.lua"));
    worker.list_tests(context);
    teez::core::Plugin ctest(plugin_path("teez-plugin-ctest.lua"));
    ctest.list_tests(context);
}

void fuzz_coverage_path_match(const std::string& payload) {
    const std::string path = payload.empty() ? "src/main.cpp" : payload;
    teez::core::normalize_coverage_path(path);
    teez::core::coverage_path_matches(path, payload.empty() ? "src/*" : payload);
}

void fuzz_coverage_roundtrip(const std::string& payload) {
    const bool use_cobertura = !payload.empty() && (payload[0] % 2 == 1);
    const auto path =
        teez::fuzz::write_temp_file("teez-fuzz", use_cobertura ? ".xml" : ".lcov", payload);
    const teez::core::CoverageTable table =
        use_cobertura ? teez::core::load_coverage_table_from_cobertura(path)
                      : teez::core::load_coverage_table_from_lcov(path);

    teez::core::coverage_table_to_json_string(table);
    const auto out_lcov = teez::fuzz::write_temp_file("teez-fuzz-out", ".lcov", "roundtrip");
    const auto out_xml = teez::fuzz::write_temp_file("teez-fuzz-out", ".xml", "roundtrip");
    teez::core::export_coverage_table_to_lcov(table, out_lcov);
    teez::core::export_coverage_table_to_cobertura(table, out_xml);
    teez::core::coverage_table_to_msgpack(table);
}

void fuzz_coverage_reporter(const std::string& payload) {
    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (json.is_discarded()) {
        return;
    }

    teez::core::CoverageReportCliOverrides cli;
    if (!payload.empty()) {
        cli.reporter = payload.substr(0, std::min(payload.size(), std::size_t{32}));
    }

    teez::core::resolve_coverage_report_options(json, cli);
    for (const auto& name : teez::core::coverage_reporter_names()) {
        teez::core::is_coverage_reporter_name(name);
        teez::core::make_coverage_reporter(name);
    }

    if (std::filesystem::exists(kSampleLcov)) {
        const auto table = teez::core::load_coverage_table_from_lcov(kSampleLcov);
        const auto out = teez::fuzz::write_temp_file("teez-fuzz-report", ".json", payload);
        teez::core::write_coverage_report(table, "json", out);
    }
}

void fuzz_write_test_list(const std::string& payload) {
    std::vector<std::string> tests;
    for (const auto& line : teez::fuzz::split_lines(payload)) {
        tests.push_back(line);
    }
    if (tests.empty()) {
        tests = {"system::Suite::smoke test", "unit::Core::parse_line"};
    }

    teez::cli::ListOutputOptions plain;
    teez::cli::ListOutputOptions tree{.tree = true};
    teez::cli::ListOutputOptions json{.json = true};
    std::ostringstream out;
    teez::cli::write_test_list(tests, plain, out);
    teez::cli::write_test_list(tests, tree, out);
    teez::cli::write_test_list(tests, json, out);
}

void fuzz_ndjson_stream(const std::string& payload) {
    std::ostringstream out;
    teez::cli::RunOutputWriter writer({}, out);
    teez::core::Plugin worker(plugin_path("teez-plugin-worker.lua"));

    for (const auto& line : teez::fuzz::split_lines(payload)) {
        const std::string event_json = worker.parse_line_ndjson(line);
        if (event_json.empty()) {
            continue;
        }
        const auto event = teez::core::parse_plugin_event_json(event_json);
        if (event.has_value()) {
            writer.on_event(*event);
        }
    }
    writer.finish();
}

void fuzz_validate_run_context(const std::string& payload) {
    teez::core::RunContext context = run_context_from_payload(payload);
    if (!payload.empty()) {
        context.target_path =
            std::filesystem::path(payload.substr(0, std::min(payload.size(), std::size_t{128})));
    }
    teez::core::validate_run_context(context);
    teez::core::path_exists(context.target_path);
}

void fuzz_resolve_type(const std::string& payload) {
    const std::string file = payload.empty() ? "tests/smoke.teez.lua" : payload;
    teez::core::resolve_type(file,
                             payload.empty() ? std::nullopt : std::optional<std::string>{payload});
}

void fuzz_exit_code_normalize(const std::string& payload) {
    const bool interrupted = !payload.empty() && (payload[0] & 1);
    const bool saw_failure = payload.size() > 1 && (payload[1] & 1);
    const int child_exit = payload.size() > 2 ? static_cast<int>(payload[2]) : 0;
    teez::core::normalize_exit_code(interrupted, saw_failure, child_exit);
}

void fuzz_run_with_plugin_dummy(const std::string& payload) {
    const auto context = run_context_from_payload(payload);
    std::ostringstream out;
    teez::core::run_with_plugin(plugin_path("dummy.lua"), context, out);
}

void fuzz_coverage_diff(const std::string& payload) {
    if (!std::filesystem::exists(kSampleLcov)) {
        return;
    }

    auto before = teez::core::load_coverage_table_from_lcov(kSampleLcov);
    std::string mutated = teez::core::coverage_table_to_json_string(before);
    mutated += payload;
    const auto path = teez::fuzz::write_temp_file("teez-fuzz-diff", ".lcov", mutated);
    const auto after = teez::core::load_coverage_table_from_lcov(path);
    teez::core::diff_coverage_tables(before, after);
    teez::core::check_coverage_thresholds(before, {});
}

void fuzz_find_harness_manifest(const std::string& payload) {
    const auto manifests = teez::core::load_harness_manifests(std::filesystem::path(kPluginDir));
    if (manifests.empty()) {
        return;
    }

    const std::string name = payload.empty() ? manifests.front().name : payload;
    teez::core::find_harness_manifest(name, std::filesystem::path(kPluginDir));
}

void fuzz_ndjson_event_writer(const std::string& payload) {
    std::ostringstream out;
    const auto on_event = teez::core::make_ndjson_event_writer(out);
    const auto json = nlohmann::json::parse(payload, nullptr, false);
    if (!json.is_discarded() && json.is_object()) {
        on_event(json);
        return;
    }

    for (const auto& line : teez::fuzz::split_lines(payload)) {
        const auto event = teez::core::parse_plugin_event_json(line);
        if (event.has_value()) {
            on_event(*event);
        }
    }
}

#ifdef TEEZ_FUZZ_WORKER
void fuzz_worker_run_lua(const std::string& payload) {
    const auto dir = teez::fuzz::make_temp_dir("teez-fuzz-worker", payload);
    std::ofstream(dir / "fuzz.teez.lua") << payload;
    std::ostringstream out;
    teez::worker::run_worker(dir, std::filesystem::path(TEEZ_WORKER_RUNTIME_DIR), out);
}
#endif

void dispatch(FuzzOp op, const std::string& payload) {
    switch (op) {
    case FuzzOp::CliArgs:
        fuzz_cli_args(payload);
        break;
    case FuzzOp::FilterJson:
        fuzz_filter_json(payload);
        break;
    case FuzzOp::ParseLineDummy:
        fuzz_parse_line(plugin_path("dummy.lua"), payload);
        break;
    case FuzzOp::ParseLineCtest:
        fuzz_parse_line(plugin_path("teez-plugin-ctest.lua"), payload);
        break;
    case FuzzOp::ParseLinePytest:
        fuzz_parse_line(plugin_path("teez-plugin-pytest.lua"), payload);
        break;
    case FuzzOp::ParseLineWorker:
        fuzz_parse_line(plugin_path("teez-plugin-worker.lua"), payload);
        break;
    case FuzzOp::PluginEventJson:
        teez::core::parse_plugin_event_json(payload);
        break;
    case FuzzOp::ManifestJson:
        fuzz_manifest_json(payload);
        break;
    case FuzzOp::CoverageLcov:
        fuzz_coverage_lcov(payload);
        break;
    case FuzzOp::CoverageCobertura:
        fuzz_coverage_cobertura(payload);
        break;
    case FuzzOp::CoverageMsgpack:
        fuzz_coverage_msgpack(payload);
        break;
    case FuzzOp::ConfigLua:
        fuzz_config_lua(payload);
        break;
    case FuzzOp::FilterGlobRegex:
        fuzz_filter_glob_regex(payload);
        break;
    case FuzzOp::ListContext:
        fuzz_list_context();
        break;
    case FuzzOp::RunOutputEvents:
        fuzz_run_output_events(payload);
        break;
    case FuzzOp::DiscoverPlugin:
        fuzz_discover_plugin(payload);
        break;
    case FuzzOp::PluginBuildCommand:
        fuzz_plugin_build_command(payload);
        break;
    case FuzzOp::PluginListTests:
        fuzz_plugin_list_tests(payload);
        break;
    case FuzzOp::CoveragePathMatch:
        fuzz_coverage_path_match(payload);
        break;
    case FuzzOp::CoverageRoundtrip:
        fuzz_coverage_roundtrip(payload);
        break;
    case FuzzOp::CoverageReporter:
        fuzz_coverage_reporter(payload);
        break;
    case FuzzOp::WriteTestList:
        fuzz_write_test_list(payload);
        break;
    case FuzzOp::NdjsonStream:
        fuzz_ndjson_stream(payload);
        break;
    case FuzzOp::ValidateRunContext:
        fuzz_validate_run_context(payload);
        break;
    case FuzzOp::ResolveType:
        fuzz_resolve_type(payload);
        break;
    case FuzzOp::ExitCodeNormalize:
        fuzz_exit_code_normalize(payload);
        break;
    case FuzzOp::RunWithPluginDummy:
        fuzz_run_with_plugin_dummy(payload);
        break;
    case FuzzOp::CoverageDiff:
        fuzz_coverage_diff(payload);
        break;
    case FuzzOp::FindHarnessManifest:
        fuzz_find_harness_manifest(payload);
        break;
    case FuzzOp::ListContextFiltered:
        fuzz_list_context_filtered(payload);
        break;
    case FuzzOp::NdjsonEventWriter:
        fuzz_ndjson_event_writer(payload);
        break;
    case FuzzOp::WorkerRunLua:
#ifdef TEEZ_FUZZ_WORKER
        fuzz_worker_run_lua(payload);
#endif
        break;
    case FuzzOp::Count:
        break;
    }
}

FuzzOp decode_op(std::uint8_t byte) {
    return static_cast<FuzzOp>(byte % static_cast<std::uint8_t>(FuzzOp::Count));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 2) {
        return 0;
    }

    const auto op1 = decode_op(data[0]);
    const std::string payload = teez::fuzz::payload_as_string(data + 1, size - 1);

    teez::fuzz::invoke_safely([&]() { dispatch(op1, payload); });

    if (size >= 3) {
        const auto op2 = decode_op(data[1]);
        if (op2 != op1) {
            teez::fuzz::invoke_safely([&]() { dispatch(op2, payload); });
        }
    }

    return 0;
}
