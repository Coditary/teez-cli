#pragma once

#include <iostream>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace teez::cli {

/// True when teez was built with Tuinator live UI support.
bool live_ui_available();

/// Vitest-style bottom status band (requires a TTY).
class LiveUiReporter {
  public:
    explicit LiveUiReporter(std::ostream& out = std::cout);
    ~LiveUiReporter();

    LiveUiReporter(const LiveUiReporter&) = delete;
    LiveUiReporter& operator=(const LiveUiReporter&) = delete;

    void before_scroll_output();
    void on_event(const nlohmann::json& event);
    void finish();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace teez::cli
