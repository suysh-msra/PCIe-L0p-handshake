#include "scenarios.h"
#include <iostream>
#include <string>

static void print_menu() {
    std::cout << "\n";
    std::cout << "\033[1m\033[36m"
              << "╔═══════════════════════════════════════════════════════╗\n"
              << "║   PCIe L0p Handshake Simulator                      ║\n"
              << "║   Models L0p width-change protocol between           ║\n"
              << "║   Root Port (DSP) and Endpoint (USP) on x16 link    ║\n"
              << "╠═══════════════════════════════════════════════════════╣\n"
              << "║                                                       ║\n"
              << "║  1. Vanilla L0p Entry & Exit  (x16 -> x4 -> x16)    ║\n"
              << "║  2. Simultaneous L0p.Exit     (both ports widen)     ║\n"
              << "║  3. L0p Entry + Pending Retry (NAK during replay)    ║\n"
              << "║  4. PM DLLP During L0p        (L1 vs L0p conflict)  ║\n"
              << "║  5. Lane Skew on Wake-up      (alignment check)     ║\n"
              << "║  6. Back-Pressure NAK          (USP queued traffic)  ║\n"
              << "║                                                       ║\n"
              << "║  A. Run ALL scenarios                                ║\n"
              << "║  Q. Quit                                             ║\n"
              << "║                                                       ║\n"
              << "╚═══════════════════════════════════════════════════════╝\n"
              << "\033[0m\n"
              << "  Select scenario: ";
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--all" || arg == "-a") {
            pcie::scenarios::run_all();
            return 0;
        }
        int choice = std::stoi(arg);
        switch (choice) {
            case 1: pcie::scenarios::vanilla_l0p_entry();       break;
            case 2: pcie::scenarios::simultaneous_l0p_exit();   break;
            case 3: pcie::scenarios::retry_during_l0p_entry();  break;
            case 4: pcie::scenarios::pm_dllp_during_l0p();      break;
            case 5: pcie::scenarios::lane_skew_on_wakeup();     break;
            case 6: pcie::scenarios::backpressure_nak();        break;
            default:
                std::cerr << "Unknown scenario: " << choice << "\n";
                return 1;
        }
        return 0;
    }

    while (true) {
        print_menu();

        std::string input;
        std::getline(std::cin, input);
        if (input.empty()) continue;

        char c = input[0];
        if (c == 'q' || c == 'Q') break;

        if (c == 'a' || c == 'A') {
            pcie::scenarios::run_all();
            continue;
        }

        int choice = c - '0';
        switch (choice) {
            case 1: pcie::scenarios::vanilla_l0p_entry();       break;
            case 2: pcie::scenarios::simultaneous_l0p_exit();   break;
            case 3: pcie::scenarios::retry_during_l0p_entry();  break;
            case 4: pcie::scenarios::pm_dllp_during_l0p();      break;
            case 5: pcie::scenarios::lane_skew_on_wakeup();     break;
            case 6: pcie::scenarios::backpressure_nak();        break;
            default:
                std::cout << "  Invalid choice. Try again.\n";
        }
    }

    std::cout << "\n  Goodbye.\n\n";
    return 0;
}
