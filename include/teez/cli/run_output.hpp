#pragma once

#include <iostream>
#include <map>
#include <memory>
#include <ostream>
#include <string>

#include <nlohmann/json.hpp>

namespace teez::cli {

struct RunOutputOptions {
    bool progress = false;
    bool json = false;
    bool ui = true;
};

class LiveUiReporter;

/// Formats teez run events for human, aggregated JSON, NDJSON, or live UI output.
class RunOutputWriter {
  public:
    RunOutputWriter(RunOutputOptions options, std::ostream& out = std::cout);
    ~RunOutputWriter();

    void on_event(const nlohmann::json& event);
    void finish();

  private:
    enum class Mode { Human, ProgressText, NdjsonStream, AggregatedJson, LiveUi };

    void write_human_event(const nlohmann::json& event);
    void write_progress_text_event(const nlohmann::json& event);
    void write_summary_line();

    static std::string short_test_label(const std::string& id);
    static std::string colorize(const std::string& text, const char* code, bool enabled);
    static Mode resolve_mode(const RunOutputOptions& options, std::ostream& out);

    RunOutputOptions options_;
    Mode mode_;
    std::ostream& out_;
    bool colors_;
    nlohmann::json events_ = nlohmann::json::array();
    std::map<std::string, int> counts_;
    std::string current_suite_;
    std::unique_ptr<LiveUiReporter> live_ui_;
};

} // namespace teez::cli
