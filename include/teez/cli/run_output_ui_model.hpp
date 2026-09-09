#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace teez::cli {

enum class FooterColor {
    Default,
    Gray,
    Green,
    Red,
    Yellow,
    Dim,
};

struct FooterSegment {
    std::string text;
    FooterColor color = FooterColor::Default;
};

struct FooterStatLine {
    std::string label;
    std::vector<FooterSegment> segments;
};

/// Vitest-style right-aligned label column (11 characters).
std::string stat_label(std::string_view text);

std::string short_test_label(const std::string& id);
std::string format_active_label(const std::string& id, const std::string& phase);
std::string format_duration_seconds(double seconds);
std::string format_test_duration_ms(double milliseconds);
std::string format_coverage_percent(double rate);
std::string format_clock_time(const std::chrono::system_clock::time_point& time_point);

std::string join_segment_text(const std::vector<FooterSegment>& segments);

/// Pure footer state used by the live UI band (testable without a TTY).
class FooterState {
  public:
    static constexpr int kStatsFooterLines = 5;
    static constexpr int kMaxRunningSlots = 2;

    void ensure_started();
    void set_test_output_started();
    void mark_finished();

    bool should_paint_footer() const;
    int band_lines() const;

    void on_phase(const std::string& id, const std::string& phase, const std::string& state);
    void on_coverage(const nlohmann::json& event);
    void on_result(const std::string& id, const std::string& type);

    std::vector<std::string> running_lines() const;
    std::vector<FooterStatLine> stat_lines() const;

    int passed() const {
        return passed_;
    }
    int failed() const {
        return failed_;
    }
    int skipped() const {
        return skipped_;
    }
    int todo() const {
        return todo_;
    }
    bool finished() const {
        return finished_;
    }
    bool started() const {
        return started_;
    }
    bool has_coverage() const {
        return has_coverage_;
    }
    std::size_t running_count() const {
        return running_by_id_.size();
    }

  private:
    struct ActivePhase {
        std::string id;
        std::string phase;
        int order = 0;
    };

    struct FileStats {
        int passed = 0;
        int failed = 0;
        int skipped = 0;
        int todo = 0;
    };

    struct CoverageStats {
        double line_rate = 0.0;
        std::optional<double> branch_rate;
        std::optional<double> function_rate;
        int lines_hit = 0;
        int lines_found = 0;
    };

    FooterColor coverage_value_color() const;
    std::vector<FooterSegment> test_files_segments() const;
    std::vector<FooterSegment> tests_segments() const;
    std::vector<FooterSegment> coverage_segments() const;
    std::vector<FooterSegment> start_at_segments() const;
    std::vector<FooterSegment> duration_segments() const;
    void track_file_result(const std::string& id, const std::string& type);
    void track_suite_file(const std::string& id);
    void clear_active_for_id(const std::string& id);
    double elapsed_seconds() const;

    std::map<std::string, FileStats> file_stats_;
    std::map<std::string, ActivePhase> running_by_id_;
    int active_order_ = 0;
    CoverageStats coverage_;
    bool has_coverage_ = false;
    int passed_ = 0;
    int failed_ = 0;
    int skipped_ = 0;
    int todo_ = 0;
    bool finished_ = false;
    bool started_ = false;
    bool test_output_started_ = false;
    std::chrono::steady_clock::time_point started_steady_{};
    std::chrono::steady_clock::time_point finished_steady_{};
    std::chrono::system_clock::time_point started_system_{};
};

#ifdef TEEZ_ENABLE_UI
/// Renders the current footer band to a string (ANSI) for snapshot-style tests.
std::string capture_footer_ansi(const FooterState& state);
#endif

} // namespace teez::cli
