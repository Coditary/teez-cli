#include "teez/cli/run_output_ui_model.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace teez::cli {

namespace {

std::string suite_from_id(const std::string& id) {
    if (const auto pos = id.find("::"); pos != std::string::npos) {
        return id.substr(0, pos);
    }
    return {};
}

void append_pass_fail_counts(std::vector<FooterSegment>& segments, int passed_count,
                             int failed_count) {
    bool wrote_value = false;
    if (passed_count > 0) {
        segments.push_back({std::to_string(passed_count) + " passed", FooterColor::Green});
        wrote_value = true;
    }
    if (failed_count > 0) {
        if (wrote_value) {
            segments.push_back({" | ", FooterColor::Gray});
        }
        segments.push_back({std::to_string(failed_count) + " failed", FooterColor::Red});
        wrote_value = true;
    }
    if (!wrote_value) {
        segments.push_back({std::to_string(passed_count) + " passed", FooterColor::Green});
    }
}

void append_total_suffix(std::vector<FooterSegment>& segments, int total_count) {
    segments.push_back({" (" + std::to_string(total_count) + ')', FooterColor::Gray});
}

} // namespace

std::string stat_label(std::string_view text) {
    constexpr std::size_t width = 11;
    std::string out;
    if (text.size() < width) {
        out.append(width - text.size(), ' ');
    }
    out.append(text);
    return out;
}

std::string short_test_label(const std::string& id) {
    if (const auto pos = id.rfind(" > "); pos != std::string::npos) {
        return id.substr(pos + 3);
    }
    if (const auto pos = id.rfind("::"); pos != std::string::npos) {
        return id.substr(pos + 2);
    }
    if (const auto pos = id.rfind('/'); pos != std::string::npos) {
        return id.substr(pos + 1);
    }
    return id;
}

std::string format_active_label(const std::string& id, const std::string& phase) {
    std::string label;
    if (phase == "beforeAll" || phase == "afterAll") {
        if (const auto pos = id.find("::"); pos != std::string::npos) {
            label = id.substr(pos + 2);
        } else {
            label = id;
        }
    } else {
        label = short_test_label(id);
    }
    return "  ◌ " + label + " · " + phase;
}

std::string format_duration_seconds(double seconds) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << seconds << 's';
    return out.str();
}

std::string format_test_duration_ms(double milliseconds) {
    if (milliseconds < 0.0) {
        return {};
    }
    if (milliseconds < 1000.0) {
        return std::to_string(static_cast<long long>(milliseconds + 0.5)) + "ms";
    }
    return format_duration_seconds(milliseconds / 1000.0);
}

std::string format_coverage_percent(double rate) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << (rate * 100.0) << '%';
    return out.str();
}

std::string format_clock_time(const std::chrono::system_clock::time_point& time_point) {
    const std::time_t raw = std::chrono::system_clock::to_time_t(time_point);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &raw);
#else
    localtime_r(&raw, &local_time);
#endif
    std::ostringstream out;
    out << std::put_time(&local_time, "%H:%M:%S");
    return out.str();
}

std::string join_segment_text(const std::vector<FooterSegment>& segments) {
    std::string out;
    for (const FooterSegment& segment : segments) {
        out += segment.text;
    }
    return out;
}

void FooterState::ensure_started() {
    if (started_) {
        return;
    }
    started_ = true;
    started_steady_ = std::chrono::steady_clock::now();
    started_system_ = std::chrono::system_clock::now();
}

void FooterState::set_test_output_started() {
    test_output_started_ = true;
}

void FooterState::mark_finished() {
    finished_ = true;
    finished_steady_ = std::chrono::steady_clock::now();
    running_by_id_.clear();
}

bool FooterState::should_paint_footer() const {
    return test_output_started_ || !running_by_id_.empty() ||
           passed_ + failed_ + skipped_ + todo_ > 0 || has_coverage_;
}

int FooterState::band_lines() const {
    return kStatsFooterLines + kMaxRunningSlots;
}

void FooterState::on_phase(const std::string& id, const std::string& phase,
                           const std::string& state) {
    if (id.empty() || phase.empty()) {
        return;
    }

    ensure_started();
    track_suite_file(id);

    if (state == "start") {
        running_by_id_[id] = ActivePhase{id, phase, ++active_order_};
    } else if (state == "end") {
        running_by_id_.erase(id);
    }
}

void FooterState::on_coverage(const nlohmann::json& event) {
    ensure_started();
    has_coverage_ = true;
    coverage_.line_rate = event.value("line_rate", 0.0);
    coverage_.lines_hit = event.value("lines_hit", 0);
    coverage_.lines_found = event.value("lines_found", 0);

    if (event.contains("branch_rate") && event["branch_rate"].is_number()) {
        coverage_.branch_rate = event["branch_rate"].get<double>();
    } else {
        coverage_.branch_rate.reset();
    }
    if (event.contains("function_rate") && event["function_rate"].is_number()) {
        coverage_.function_rate = event["function_rate"].get<double>();
    } else {
        coverage_.function_rate.reset();
    }
}

void FooterState::on_result(const std::string& id, const std::string& type) {
    ensure_started();
    if (!id.empty()) {
        track_suite_file(id);
    }

    if (type == "pass") {
        ++passed_;
        track_file_result(id, type);
        clear_active_for_id(id);
    } else if (type == "fail" || type == "error") {
        ++failed_;
        track_file_result(id, type);
        clear_active_for_id(id);
    } else if (type == "skip") {
        ++skipped_;
        track_file_result(id, type);
        clear_active_for_id(id);
    } else if (type == "todo") {
        ++todo_;
        track_file_result(id, type);
        clear_active_for_id(id);
    }
}

std::vector<std::string> FooterState::running_lines() const {
    std::vector<ActivePhase> ordered;
    ordered.reserve(running_by_id_.size());
    for (const auto& [_, entry] : running_by_id_) {
        ordered.push_back(entry);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const ActivePhase& a, const ActivePhase& b) { return a.order < b.order; });

    std::vector<std::string> lines;
    if (static_cast<int>(ordered.size()) > kMaxRunningSlots) {
        const int hidden = static_cast<int>(ordered.size()) - (kMaxRunningSlots - 1);
        for (int i = 0; i < kMaxRunningSlots - 1; ++i) {
            lines.push_back(format_active_label(ordered[static_cast<std::size_t>(i)].id,
                                                ordered[static_cast<std::size_t>(i)].phase));
        }
        lines.push_back("  ◌ +" + std::to_string(hidden) + " more running in parallel");
    } else {
        for (const ActivePhase& entry : ordered) {
            lines.push_back(format_active_label(entry.id, entry.phase));
        }
    }

    while (static_cast<int>(lines.size()) < kMaxRunningSlots) {
        lines.emplace_back();
    }
    return lines;
}

std::vector<FooterStatLine> FooterState::stat_lines() const {
    return {
        FooterStatLine{"Test Files", test_files_segments()},
        FooterStatLine{"Tests", tests_segments()},
        FooterStatLine{"Coverage", coverage_segments()},
        FooterStatLine{"Start at", start_at_segments()},
        FooterStatLine{"Duration", duration_segments()},
    };
}

FooterColor FooterState::coverage_value_color() const {
    if (!has_coverage_) {
        return FooterColor::Dim;
    }
    if (coverage_.line_rate >= 0.8) {
        return FooterColor::Green;
    }
    if (coverage_.line_rate >= 0.5) {
        return FooterColor::Yellow;
    }
    if (coverage_.line_rate > 0.0) {
        return FooterColor::Red;
    }
    return FooterColor::Dim;
}

std::vector<FooterSegment> FooterState::test_files_segments() const {
    int files_passed = 0;
    int files_failed = 0;
    for (const auto& [_, stats] : file_stats_) {
        if (stats.failed > 0) {
            ++files_failed;
        } else if (stats.passed + stats.skipped + stats.todo > 0) {
            ++files_passed;
        }
    }

    std::vector<FooterSegment> segments;
    append_pass_fail_counts(segments, files_passed, files_failed);
    append_total_suffix(segments, static_cast<int>(file_stats_.size()));
    return segments;
}

std::vector<FooterSegment> FooterState::tests_segments() const {
    const int total = passed_ + failed_ + skipped_ + todo_;
    std::vector<FooterSegment> segments;
    append_pass_fail_counts(segments, passed_, failed_);

    if (skipped_ > 0) {
        segments.push_back({" | ", FooterColor::Gray});
        segments.push_back({std::to_string(skipped_) + " skipped", FooterColor::Yellow});
    }
    if (todo_ > 0) {
        segments.push_back({" | ", FooterColor::Gray});
        segments.push_back({std::to_string(todo_) + " todo", FooterColor::Dim});
    }
    append_total_suffix(segments, total);
    return segments;
}

std::vector<FooterSegment> FooterState::coverage_segments() const {
    if (!has_coverage_) {
        return {{std::string{"—"}, FooterColor::Gray}};
    }

    std::vector<FooterSegment> segments;
    segments.push_back(
        {format_coverage_percent(coverage_.line_rate) + " lines", coverage_value_color()});

    if (coverage_.lines_found > 0) {
        segments.push_back({" (" + std::to_string(coverage_.lines_hit) + '/' +
                                std::to_string(coverage_.lines_found) + ')',
                            FooterColor::Gray});
    }
    if (coverage_.branch_rate.has_value()) {
        segments.push_back({" | " + format_coverage_percent(*coverage_.branch_rate) + " branches",
                            FooterColor::Gray});
    }
    if (coverage_.function_rate.has_value()) {
        segments.push_back(
            {" | " + format_coverage_percent(*coverage_.function_rate) + " functions",
             FooterColor::Gray});
    }
    return segments;
}

std::vector<FooterSegment> FooterState::start_at_segments() const {
    return {{format_clock_time(started_system_), FooterColor::Default}};
}

std::vector<FooterSegment> FooterState::duration_segments() const {
    std::vector<FooterSegment> segments;
    segments.push_back({format_duration_seconds(elapsed_seconds()), FooterColor::Default});
    if (!finished_) {
        segments.push_back({" (running)", FooterColor::Gray});
    }
    return segments;
}

void FooterState::track_file_result(const std::string& id, const std::string& type) {
    const std::string file = suite_from_id(id);
    if (file.empty()) {
        return;
    }

    FileStats& stats = file_stats_[file];
    if (type == "pass") {
        ++stats.passed;
    } else if (type == "fail" || type == "error") {
        ++stats.failed;
    } else if (type == "skip") {
        ++stats.skipped;
    } else if (type == "todo") {
        ++stats.todo;
    }
}

void FooterState::track_suite_file(const std::string& id) {
    const std::string file = suite_from_id(id);
    if (file.empty()) {
        return;
    }
    file_stats_.emplace(file, FileStats{});
}

void FooterState::clear_active_for_id(const std::string& id) {
    if (id.empty()) {
        return;
    }
    for (auto it = running_by_id_.begin(); it != running_by_id_.end();) {
        if (it->second.id == id) {
            it = running_by_id_.erase(it);
        } else {
            ++it;
        }
    }
}

double FooterState::elapsed_seconds() const {
    const auto end = finished_ ? finished_steady_ : std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - started_steady_).count();
}

} // namespace teez::cli
