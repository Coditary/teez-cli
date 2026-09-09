#include "teez/cli/run_output_ui.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <memory>

#include "teez/cli/run_output_ui_model.hpp"

#ifdef TEEZ_ENABLE_UI
#include <tuinator/tuinator.hpp>
#endif

namespace teez::cli {

namespace {

bool is_harness_internal_event(const std::string& type) {
    return type == "harness" || type == "harness_loaded" || type == "harness_phase" ||
           type == "harness_capture";
}

#ifdef TEEZ_ENABLE_UI

tuinator::Style to_tuinator_style(FooterColor color) {
    switch (color) {
    case FooterColor::Gray:
        return {.foreground = tuinator::Color::White, .dim = true};
    case FooterColor::Green:
        return {.foreground = tuinator::Color::Green};
    case FooterColor::Red:
        return {.foreground = tuinator::Color::Red};
    case FooterColor::Yellow:
        return {.foreground = tuinator::Color::Yellow};
    case FooterColor::Dim:
        return {.dim = true};
    case FooterColor::Default:
    default:
        return {};
    }
}

std::unique_ptr<tuinator::Widget> make_left_row(const std::string& text,
                                                tuinator::Style style = {}) {
    auto row = std::make_unique<tuinator::HBox>(tuinator::BoxOptions{.gap = 0});
    row->add_child(std::make_unique<tuinator::Label>(text, style));
    return row;
}

std::unique_ptr<tuinator::Widget> make_labeled_row(const FooterStatLine& line) {
    auto row = std::make_unique<tuinator::HBox>(tuinator::BoxOptions{.gap = 0});
    row->add_child(std::make_unique<tuinator::Label>(stat_label(line.label),
                                                     to_tuinator_style(FooterColor::Gray)));
    row->add_child(std::make_unique<tuinator::Label>(std::string(" "), tuinator::Style{}));
    for (const FooterSegment& segment : line.segments) {
        row->add_child(
            std::make_unique<tuinator::Label>(segment.text, to_tuinator_style(segment.color)));
    }
    return row;
}

std::unique_ptr<tuinator::Widget> build_footer_root(const FooterState& state) {
    auto root = std::make_unique<tuinator::VBox>(tuinator::BoxOptions{.gap = 0});
    const tuinator::Style running_style = to_tuinator_style(FooterColor::Yellow);

    for (const std::string& line : state.running_lines()) {
        if (line.empty()) {
            root->add_child(make_left_row(""));
        } else {
            root->add_child(make_left_row(line, running_style));
        }
    }

    for (const FooterStatLine& stat_line : state.stat_lines()) {
        root->add_child(make_labeled_row(stat_line));
    }
    return root;
}

void restore_terminal_cursor() {
    std::fputs("\033[?25h", stdout);
    std::fflush(stdout);
}

#endif

} // namespace

bool live_ui_available() {
#ifdef TEEZ_ENABLE_UI
    return true;
#else
    return false;
#endif
}

#ifdef TEEZ_ENABLE_UI

struct LiveUiReporter::Impl {
    ~Impl() {
        if (!state_.finished()) {
            finish();
        }
    }

    void erase_live_band() const {
        std::string clear;
        const int lines = state_.band_lines();
        for (int i = 0; i < lines; ++i) {
            clear += "\r\033[2K";
            if (i + 1 < lines) {
                clear += "\033[1A";
            }
        }
        clear += "\r";
        std::fputs(clear.c_str(), stdout);
        std::fflush(stdout);
    }

    void suspend_footer() {
        if (view == nullptr) {
            return;
        }

        erase_live_band();
        restore_terminal_cursor();
        (void)view.release();
        tuinator::flush_cli_output();
    }

    void before_scroll_output() {
        state_.set_test_output_started();
        suspend_footer();
    }

    void present_footer() {
        if (!state_.should_paint_footer()) {
            return;
        }

        if (view == nullptr) {
            view = std::make_unique<tuinator::InlineView>(tuinator::InlineBackendOptions{
                .height = state_.band_lines(),
                .min_height = state_.band_lines(),
                .clear_on_shutdown = false,
                .output = stdout,
            });
            view->set_root(build_footer_root(state_));
            tuinator::flush_cli_output();
            view->start();
            return;
        }

        view->set_root(build_footer_root(state_));
        view->present();
    }

    void on_event(const nlohmann::json& event) {
        if (!event.is_object() || !event.contains("event")) {
            return;
        }

        const std::string type = event.at("event").get<std::string>();
        if (is_harness_internal_event(type)) {
            return;
        }

        const std::string id = event.value("id", "");

        if (type == "phase") {
            state_.on_phase(id, event.value("phase", ""), event.value("state", ""));
            present_footer();
            return;
        }

        if (type == "coverage") {
            state_.on_coverage(event);
            present_footer();
            return;
        }

        if (type == "pass" || type == "fail" || type == "error" || type == "skip" ||
            type == "todo") {
            state_.on_result(id, type);
        }

        present_footer();
    }

    void detach_footer_keep_visible() {
        if (view == nullptr) {
            return;
        }

        view->finish();
        view.reset();
        tuinator::flush_cli_output();
    }

    void finish() {
        if (state_.finished()) {
            return;
        }

        state_.mark_finished();

        if (!state_.started()) {
            return;
        }

        present_footer();
        detach_footer_keep_visible();
    }

    FooterState state_;
    std::unique_ptr<tuinator::InlineView> view;
};

std::string capture_footer_ansi(const FooterState& state) {
    char* buffer = nullptr;
    size_t length = 0;
    FILE* capture = open_memstream(&buffer, &length);
    if (capture == nullptr) {
        return {};
    }

    tuinator::InlineView view(tuinator::InlineBackendOptions{
        .height = state.band_lines(),
        .min_height = state.band_lines(),
        .clear_on_shutdown = false,
        .output = capture,
    });
    view.set_root(build_footer_root(state));
    view.start();
    view.finish();

    std::fclose(capture);
    const std::string out(buffer != nullptr ? buffer : "", length);
    std::free(buffer);
    return out;
}

LiveUiReporter::LiveUiReporter(std::ostream& /*out*/) : impl_(std::make_unique<Impl>()) {}

LiveUiReporter::~LiveUiReporter() = default;

void LiveUiReporter::before_scroll_output() {
    impl_->before_scroll_output();
}

void LiveUiReporter::on_event(const nlohmann::json& event) {
    impl_->on_event(event);
}

void LiveUiReporter::finish() {
    impl_->finish();
}

#else

struct LiveUiReporter::Impl {};

LiveUiReporter::LiveUiReporter(std::ostream& /*out*/) : impl_(std::make_unique<Impl>()) {}

LiveUiReporter::~LiveUiReporter() = default;

void LiveUiReporter::before_scroll_output() {}

void LiveUiReporter::on_event(const nlohmann::json& /*event*/) {}

void LiveUiReporter::finish() {}

#endif

} // namespace teez::cli
