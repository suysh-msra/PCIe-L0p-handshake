#include "visualizer.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace pcie {

// ANSI colour helpers (degrade gracefully on dumb terminals)
static const char* BOLD   = "\033[1m";
static const char* DIM    = "\033[2m";
static const char* GREEN  = "\033[32m";
static const char* RED    = "\033[31m";
static const char* YELLOW = "\033[33m";
static const char* CYAN   = "\033[36m";
static const char* RESET  = "\033[0m";

// ─────────────────────────── private helpers ────────────────────

void Visualizer::print_centered(const std::string& text, int width) {
    int pad = std::max(0, (width - static_cast<int>(text.size())) / 2);
    std::cout << std::string(pad, ' ') << text << "\n";
}

std::string Visualizer::time_str(uint64_t ns) const {
    std::ostringstream os;
    os << "[" << std::setw(5) << ns << " ns]";
    return os.str();
}

char Visualizer::lane_glyph(LaneState s) const {
    switch (s) {
        case LaneState::Active:       return '#';   // ▓ (solid)
        case LaneState::Parked:       return '.';   // ░ (empty)
        case LaneState::Activating:   return '+';   // waking up
        case LaneState::Deactivating: return '~';   // going down
    }
    return '?';
}

// ─────────────────────────── banner / separators ────────────────

void Visualizer::banner(const std::string& title,
                         const std::string& subtitle) {
    std::cout << "\n";
    std::cout << BOLD << CYAN;
    std::cout << std::string(WIDTH, '=') << "\n";
    print_centered(title);
    if (!subtitle.empty())
        print_centered(subtitle);
    std::cout << std::string(WIDTH, '=') << RESET << "\n\n";
}

void Visualizer::separator(char ch) {
    std::cout << DIM << std::string(WIDTH, ch) << RESET << "\n";
}

void Visualizer::section(const std::string& heading) {
    std::cout << "\n" << BOLD << "  >> " << heading << RESET << "\n\n";
}

void Visualizer::note(const std::string& text) {
    std::cout << DIM << "     " << text << RESET << "\n";
}

void Visualizer::result(bool pass, const std::string& text) {
    if (pass)
        std::cout << GREEN << BOLD << "  [PASS] " << text << RESET << "\n";
    else
        std::cout << RED << BOLD << "  [FAIL] " << text << RESET << "\n";
}

void Visualizer::blank() {
    std::cout << "\n";
}

// ─────────────────────────── link / port status ─────────────────

void Visualizer::link_status(const PCIeLink& link) {
    auto w = to_str(link.active_width());
    std::string dsp_state = to_str(link.dsp().state());
    std::string usp_state = to_str(link.usp().state());

    std::cout << "\n";
    std::cout << "  " << BOLD << "Root Port [DSP]" << RESET
              << "                              "
              << BOLD << "Endpoint [USP]" << RESET << "\n";

    std::cout << "  +---------------+";
    std::string link_line = "  " + w + " Link  ";
    int link_pad = 34 - static_cast<int>(link_line.size());
    std::cout << std::string(std::max(link_pad, 1), ' ')
              << link_line
              << "+---------------+\n";

    std::ostringstream dsp_box, usp_box;
    dsp_box << "| " << std::setw(13) << std::left << dsp_state << " |";
    usp_box << "| " << std::setw(13) << std::left << usp_state << " |";

    std::cout << "  " << dsp_box.str();
    std::string eq_line(34 - 2, '=');
    std::cout << " " << CYAN << eq_line << RESET << " ";
    std::cout << usp_box.str() << "\n";

    std::ostringstream dsp_w, usp_w;
    dsp_w << "| Width: " << std::setw(6) << std::left << w << " |";
    usp_w << "| Width: " << std::setw(6) << std::left << w << " |";

    std::cout << "  " << dsp_w.str()
              << std::string(34 - 1, ' ')
              << usp_w.str() << "\n";
    std::cout << "  +---------------+"
              << std::string(34 - 1, ' ')
              << "+---------------+\n\n";
}

void Visualizer::port_states(const PCIePort& dsp, const PCIePort& usp) {
    std::cout << "  DSP state: " << BOLD << to_str(dsp.state()) << RESET
              << " (" << to_str(dsp.current_width()) << ")"
              << "          "
              << "USP state: " << BOLD << to_str(usp.state()) << RESET
              << " (" << to_str(usp.current_width()) << ")\n";
}

// ─────────────────────────── message arrows ─────────────────────

void Visualizer::arrow(uint64_t time_ns,
                        const std::string& from_name,
                        const std::string& to_name,
                        const Message& msg,
                        const std::string& dir) {
    std::string ts = time_str(time_ns);
    std::string label = to_str(msg.type);

    if (dir == "->") {
        // DSP → USP
        std::cout << BOLD << "  " << ts << "  "
                  << from_name << RESET;
        std::cout << "  " << YELLOW << "----[ " << label << " ]";
        // Pad arrow
        int arrow_len = WIDTH - 30 - static_cast<int>(label.size())
                        - static_cast<int>(to_name.size());
        std::cout << std::string(std::max(arrow_len, 2), '-')
                  << "->  " << RESET << BOLD << to_name << RESET << "\n";
    } else {
        // USP → DSP  (arrow points left)
        std::cout << BOLD << "  " << ts << "  "
                  << to_name << RESET;
        std::cout << "  " << YELLOW << "<---";
        int arrow_len = WIDTH - 30 - static_cast<int>(label.size())
                        - static_cast<int>(from_name.size());
        std::cout << std::string(std::max(arrow_len, 2), '-')
                  << "[ " << label << " ]---  " << RESET
                  << BOLD << from_name << RESET << "\n";
    }
    if (!msg.info.empty())
        std::cout << DIM << "              " << msg.info << RESET << "\n";
}

void Visualizer::crossed_arrows(uint64_t time_ns,
                                 const Message& dsp_msg,
                                 const Message& usp_msg) {
    std::string ts = time_str(time_ns);
    std::string dl = to_str(dsp_msg.type);
    std::string ul = to_str(usp_msg.type);

    std::cout << BOLD << "  " << ts << RESET
              << "  " << YELLOW << "SIMULTANEOUS CROSSING:" << RESET << "\n";
    std::cout << "              "
              << CYAN  << "DSP ---[ " << dl << " ]---> USP" << RESET << "\n";
    std::cout << "              "
              << GREEN << "USP ---[ " << ul << " ]---> DSP" << RESET << "\n";
    if (!dsp_msg.info.empty())
        std::cout << DIM << "              DSP: " << dsp_msg.info
                  << RESET << "\n";
    if (!usp_msg.info.empty())
        std::cout << DIM << "              USP: " << usp_msg.info
                  << RESET << "\n";
}

// ─────────────────────────── lane bar ───────────────────────────

void Visualizer::lane_bar(const std::vector<Lane>& lanes,
                           const std::string& label) {
    std::cout << "  Lanes: [";
    for (const auto& l : lanes) {
        switch (l.state()) {
            case LaneState::Active:       std::cout << GREEN << "#"; break;
            case LaneState::Parked:       std::cout << DIM   << "."; break;
            case LaneState::Activating:   std::cout << YELLOW<< "+"; break;
            case LaneState::Deactivating: std::cout << RED   << "~"; break;
        }
        std::cout << RESET;
    }
    std::cout << "]";
    if (!label.empty())
        std::cout << "  " << DIM << label << RESET;
    std::cout << "\n";

    // Lane numbers
    std::cout << "           ";
    for (const auto& l : lanes)
        std::cout << std::hex << (l.id() % 16);
    std::cout << std::dec << "\n";

    // Legend (compact)
    std::cout << DIM
              << "           # = active  . = parked  + = waking  ~ = parking"
              << RESET << "\n";
}

// ─────────────────────────── event log ──────────────────────────

void Visualizer::event(uint64_t time_ns, const std::string& actor,
                        const std::string& description) {
    std::cout << "  " << BOLD << time_str(time_ns) << RESET
              << "  " << CYAN << std::setw(12) << std::left << actor << RESET
              << "  " << description << "\n";
}

// ─────────────────────────── footer ─────────────────────────────

void Visualizer::footer() {
    std::cout << "\n" << BOLD << CYAN
              << std::string(WIDTH, '=') << RESET << "\n\n";
}

} // namespace pcie
