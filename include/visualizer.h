#pragma once

#include "pcie_link.h"
#include <string>
#include <vector>

namespace pcie {

// ─────────────────────────────────────────────────────────────────
// Visualizer — rich console output for PCIe L0p handshake steps.
//
// Produces an annotated, arrow-diagram view of every message
// exchange, lane-status bar, and state transition so the reader
// can follow the protocol flow at a glance.
// ─────────────────────────────────────────────────────────────────

class Visualizer {
public:
    static constexpr int WIDTH = 76;

    // ── Section / header helpers ───────────────────────────────
    void banner(const std::string& title,
                const std::string& subtitle = "");
    void separator(char ch = '-');
    void section(const std::string& heading);
    void note(const std::string& text);
    void result(bool pass, const std::string& text);
    void blank();

    // ── Link / port status ─────────────────────────────────────
    void link_status(const PCIeLink& link);
    void port_states(const PCIePort& dsp, const PCIePort& usp);

    // ── Message arrow ──────────────────────────────────────────
    // dir: "→" (DSP to USP)  or  "←" (USP to DSP)
    void arrow(uint64_t time_ns,
               const std::string& from_name,
               const std::string& to_name,
               const Message& msg,
               const std::string& dir);

    // Simultaneous arrows (both directions at once)
    void crossed_arrows(uint64_t time_ns,
                        const Message& dsp_msg,
                        const Message& usp_msg);

    // ── Lane status bar ────────────────────────────────────────
    void lane_bar(const std::vector<Lane>& lanes,
                  const std::string& label = "");

    // ── Timing diagram (compact) ──────────────────────────────
    void event(uint64_t time_ns, const std::string& actor,
               const std::string& description);

    // ── End of scenario ────────────────────────────────────────
    void footer();

private:
    void print_centered(const std::string& text, int width = WIDTH);
    std::string time_str(uint64_t ns) const;
    char lane_glyph(LaneState s) const;
};

} // namespace pcie
