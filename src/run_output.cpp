#include "teez/cli/run_output.hpp"

#include <cstdio>
#include <cstring>

#include <unistd.h>

#include "teez/cli/run_output_ui.hpp"
#include "teez/cli/run_output_ui_model.hpp"

namespace {

constexpr const char* kGreen = "\033[32m";
constexpr const char* kRed = "\033[31m";
constexpr const char* kYellow = "\033[33m";
constexpr const char* kDim = "\033[2m";
constexpr const char* kReset = "\033[0m";

bool is_harness_internal_event(const std::string& type) {
    return type == "harness" || type == "harness_loaded" || type == "harness_phase" ||
           type == "harness_capture";
}

bool is_live_ui_scroll_event(const nlohmann::json& event) {
    if (!event.is_object() || !event.contains("event")) {
        return false;
    }
    const std::string type = event.at("event").get<std::string>();
    return type != "start" && type != "coverage" && type != "phase" &&
           !is_harness_internal_event(type);
}

std::string append_label_suffix(const std::string& text, const char* suffix) {
    const std::size_t suffix_len = std::strlen(suffix);
    if (text.size() >= suffix_len &&
        text.compare(text.size() - suffix_len, suffix_len, suffix) == 0) {
        return text;
    }
    return text + suffix;
}

bool text_has_marker(const std::string& text, const char* marker) {
    return text.find(marker) != std::string::npos;
}

bool writing_to_stdout_tty(const std::ostream& out) {
    return out.rdbuf() == std::cout.rdbuf() && ::isatty(STDOUT_FILENO) != 0;
}

std::string colorize_text(const std::string& text, const char* code, bool enabled) {
    if (!enabled) {
        return text;
    }
    return std::string(code) + text + kReset;
}

} // namespace

namespace teez::cli {

void append_test_duration(std::ostream& out, const nlohmann::json& event, bool colors) {
    if (!event.contains("duration_ms") || !event.at("duration_ms").is_number()) {
        return;
    }

    const auto formatted = format_test_duration_ms(event.at("duration_ms").get<double>());
    if (formatted.empty()) {
        return;
    }

    out << colorize_text("  " + formatted, kYellow, colors);
}

RunOutputWriter::Mode RunOutputWriter::resolve_mode(const RunOutputOptions& options,
                                                    std::ostream& out) {
    if (options.ui && live_ui_available() && writing_to_stdout_tty(out) && !options.json) {
        return Mode::LiveUi;
    }
    if (options.progress && options.json) {
        return Mode::NdjsonStream;
    }
    if (options.json) {
        return Mode::AggregatedJson;
    }
    if (options.progress) {
        return Mode::ProgressText;
    }
    return Mode::Human;
}

RunOutputWriter::RunOutputWriter(RunOutputOptions options, std::ostream& out)
    : options_(options), mode_(resolve_mode(options, out)), out_(out),
      colors_(writing_to_stdout_tty(out)) {
    if (mode_ == Mode::LiveUi) {
        live_ui_ = std::make_unique<LiveUiReporter>(out_);
    }
}

RunOutputWriter::~RunOutputWriter() = default;

std::string RunOutputWriter::colorize(const std::string& text, const char* code, bool enabled) {
    if (!enabled) {
        return text;
    }
    return std::string(code) + text + kReset;
}

std::string RunOutputWriter::short_test_label(const std::string& id) {
    if (const auto pos = id.rfind("::"); pos != std::string::npos) {
        return id.substr(pos + 2);
    }
    if (const auto pos = id.rfind(" > "); pos != std::string::npos) {
        return id.substr(pos + 3);
    }
    if (const auto pos = id.rfind('/'); pos != std::string::npos) {
        return id.substr(pos + 1);
    }
    return id;
}

void RunOutputWriter::write_human_event(const nlohmann::json& event) {
    if (!event.is_object() || !event.contains("event")) {
        return;
    }

    const std::string type = event.at("event").get<std::string>();
    if (type == "start" || type == "phase" || is_harness_internal_event(type) ||
        type == "coverage") {
        return;
    }

    if (type == "output") {
        const std::string text = event.value("text", "");
        if (!text.empty()) {
            out_ << colorize("  " + text, kDim, colors_);
            if (!text.empty() && text.back() != '\n') {
                out_ << '\n';
            }
        }
        return;
    }

    const std::string id = event.value("id", "");
    const std::string suite = id.empty() ? std::string{} : id.substr(0, id.find("::"));
    if (!suite.empty() && suite != current_suite_) {
        current_suite_ = suite;
        out_ << '\n' << colorize(suite, kDim, colors_) << '\n';
    }

    if (type == "pass") {
        out_ << colorize("  ✓ ", kGreen, colors_) << short_test_label(id);
        append_test_duration(out_, event, colors_);
        out_ << '\n';
    } else if (type == "fail" || type == "error") {
        out_ << colorize("  ✗ ", kRed, colors_) << short_test_label(id);
        if (event.contains("msg")) {
            out_ << colorize(" — " + event.at("msg").get<std::string>(), kRed, colors_);
        }
        append_test_duration(out_, event, colors_);
        out_ << '\n';
    } else if (type == "skip") {
        const std::string label = text_has_marker(short_test_label(id), "(skipped)")
                                      ? short_test_label(id)
                                      : append_label_suffix(short_test_label(id), " (skipped)");
        out_ << colorize("  ↓ ", kYellow, colors_) << label;
        append_test_duration(out_, event, colors_);
        out_ << '\n';
    } else if (type == "todo") {
        const std::string label = short_test_label(id);
        if (!text_has_marker(label, "(todo)") && !text_has_marker(label, "(planned)")) {
            out_ << colorize("  ○ ", kDim, colors_) << append_label_suffix(label, " (todo)");
        } else {
            out_ << colorize("  ○ ", kDim, colors_) << label;
        }
        append_test_duration(out_, event, colors_);
        out_ << '\n';
    } else if (type == "retry") {
        out_ << colorize("  ↻ ", kYellow, colors_) << short_test_label(id) << " (retry)\n";
    } else {
        out_ << "  " << type;
        if (!id.empty()) {
            out_ << ' ' << id;
        }
        out_ << '\n';
    }
}

void RunOutputWriter::write_progress_text_event(const nlohmann::json& event) {
    if (!event.is_object() || !event.contains("event")) {
        return;
    }

    const std::string type = event.at("event").get<std::string>();
    out_ << type;
    if (event.contains("id")) {
        out_ << ' ' << event.at("id").get<std::string>();
    }
    if (event.contains("msg")) {
        out_ << ' ' << event.at("msg").get<std::string>();
    }
    if (event.contains("text")) {
        out_ << ' ' << event.at("text").get<std::string>();
    }
    out_ << '\n';
}

void RunOutputWriter::write_summary_line() {
    const int passed = counts_["pass"];
    const int failed = counts_["fail"] + counts_["error"];
    const int skipped = counts_["skip"];
    const int todo = counts_["todo"];
    const int total = passed + failed + skipped + todo;

    if (total == 0 && counts_.count("output") > 0 && counts_["output"] > 0) {
        return;
    }

    out_ << '\n';
    if (failed > 0) {
        out_ << colorize(std::to_string(failed) + " failed", kRed, colors_);
        if (passed > 0) {
            out_ << colorize(", " + std::to_string(passed) + " passed", kGreen, colors_);
        }
    } else if (passed > 0) {
        out_ << colorize(std::to_string(passed) + " passed", kGreen, colors_);
    } else if (skipped > 0) {
        out_ << colorize(std::to_string(skipped) + " skipped", kYellow, colors_);
    } else if (todo > 0) {
        out_ << colorize(std::to_string(todo) + " todo", kDim, colors_);
    } else {
        out_ << "no tests reported";
    }

    if (total > 0) {
        out_ << " (" << total << ')';
    }
    out_ << '\n';
}

void RunOutputWriter::on_event(const nlohmann::json& event) {
    if (mode_ == Mode::LiveUi && live_ui_) {
        if (is_live_ui_scroll_event(event)) {
            live_ui_->before_scroll_output();
            write_human_event(event);
            out_.flush();
            std::fflush(stdout);
        }
        live_ui_->on_event(event);
        if (event.is_object() && event.contains("event")) {
            ++counts_[event.at("event").get<std::string>()];
        }
        return;
    }

    if (mode_ == Mode::AggregatedJson) {
        events_.push_back(event);
    } else if (mode_ == Mode::NdjsonStream) {
        out_ << event.dump() << '\n';
        out_.flush();
    } else if (mode_ == Mode::ProgressText) {
        write_progress_text_event(event);
    } else {
        write_human_event(event);
    }

    if (event.is_object() && event.contains("event")) {
        const std::string type = event.at("event").get<std::string>();
        ++counts_[type];
    }
}

void RunOutputWriter::finish() {
    if (mode_ == Mode::LiveUi && live_ui_) {
        live_ui_->finish();
        return;
    }

    if (mode_ == Mode::AggregatedJson) {
        nlohmann::json summary = nlohmann::json::object();
        summary["passed"] = counts_["pass"];
        summary["failed"] = counts_["fail"] + counts_["error"];
        summary["skipped"] = counts_["skip"];
        summary["todo"] = counts_["todo"];
        summary["total"] = summary["passed"].get<int>() + summary["failed"].get<int>() +
                           summary["skipped"].get<int>() + summary["todo"].get<int>();

        nlohmann::json payload = nlohmann::json::object();
        payload["events"] = events_;
        payload["summary"] = summary;
        out_ << payload.dump(2) << '\n';
        return;
    }

    if (mode_ == Mode::Human || mode_ == Mode::ProgressText) {
        write_summary_line();
    }
}

} // namespace teez::cli
