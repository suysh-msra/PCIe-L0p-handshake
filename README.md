# PCIe L0p Handshake Simulator

A C++17 simulation of the PCIe Gen6 **L0p** (Low-Power sub-state) width-change protocol between a **Root Port (DSP)** and an **Endpoint (USP)** on a x16 link. The simulator models the full handshake for reducing a link from x16 to x4 and back, with colored console visualization of every message exchange, lane transition, and state change.

## What is L0p?

L0p is a power-saving sub-state introduced in PCIe 6.0 that allows a link to **dynamically reduce its active width** (e.g. x16 → x4) while remaining in the L0 (active) state. Unlike L1, the link stays operational — it just parks unused lanes to save power. The handshake uses Flit-level signaling:

```
  Root Port (DSP)                              Endpoint (USP)
  ┌──────────────┐         x16 Link           ┌──────────────┐
  │  L0 Active   │ ══════════════════════════  │  L0 Active   │
  └──────────────┘                             └──────────────┘
        │                                            │
        │──── L0p.Entry.Req (target: x4) ──────────→ │
        │                                            │ (evaluate)
        │←─────────────── L0p.Entry.Ack ─────────────│
        │                                            │
        │  [deactivate lanes 4-15]    [deactivate lanes 4-15]
        │                                            │
  ┌──────────────┐          x4 Link            ┌──────────────┐
  │  L0p Active  │ ══════════════════════════   │  L0p Active  │
  └──────────────┘                              └──────────────┘
```

## Scenarios

### 1. Vanilla L0p Entry & Exit (happy path)

DSP requests x16 → x4 width reduction. USP accepts. Both sides deactivate lanes 4–15. Then the reverse: DSP requests L0p exit, lanes reactivate, link returns to x16. Demonstrates the complete round-trip.

### 2. Simultaneous L0p.Exit

Both DSP and USP independently decide to widen the link at the same instant. Their `L0p.Exit.Req` messages cross in flight. Per spec (section 8.2.6.4.3), the **DSP wins arbitration** — the USP defers its own pending exit and ACKs the DSP's request. Both proceed with a unified lane reactivation.

### 3. L0p Entry During Pending Retry

The Endpoint receives a NAK DLLP indicating TLP replay is needed. While the replay buffer is draining, the Root Port (unaware) sends an `L0p.Entry.Req`. The USP **NAKs** the request — reducing bandwidth during replay would violate ordering guarantees. After replay completes, the DSP retries and succeeds.

### 4. PM DLLP (L1 Request) During L0p

An ASPM L1 entry request arrives while the link is in L0p at x4. The spec (5.3.3.2.1) requires the link to **exit L0p first** before transitioning to L1. The USP NAKs the initial PM request. The DSP exits L0p, restores x16, then retries L1 entry successfully.

### 5. Lane-to-Lane Skew on Wake-up

When parked lanes reactivate, each lane achieves bit/symbol lock at a slightly different time. Two sub-cases:

- **Case A**: Random skew (0–6 ns) stays within the 8 ns tolerance → link operational at x16.
- **Case B**: Large skew (0–20 ns) exceeds tolerance → **skew violation** forces the link into Recovery to retrain all lanes.

### 6. Back-Pressure — USP NAKs L0p Entry

The Endpoint has significant queued traffic (12 posted writes in flight) and cannot afford the bandwidth reduction. It NAKs the `L0p.Entry.Req`. The DSP backs off. After the transmit queue drains, the DSP retries and the entry succeeds.

## Building

Requires a C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+).

**Using Make:**

```bash
make
```

**Using CMake:**

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./l0p_sim              # Interactive menu
./l0p_sim 1            # Run a specific scenario (1-6)
./l0p_sim --all        # Run all scenarios sequentially
```

### Interactive Menu

```
╔═══════════════════════════════════════════════════════╗
║   PCIe L0p Handshake Simulator                       ║
╠═══════════════════════════════════════════════════════╣
║  1. Vanilla L0p Entry & Exit  (x16 -> x4 -> x16)    ║
║  2. Simultaneous L0p.Exit     (both ports widen)     ║
║  3. L0p Entry + Pending Retry (NAK during replay)    ║
║  4. PM DLLP During L0p        (L1 vs L0p conflict)   ║
║  5. Lane Skew on Wake-up      (alignment check)      ║
║  6. Back-Pressure NAK         (USP queued traffic)    ║
║  A. Run ALL scenarios                                 ║
║  Q. Quit                                              ║
╚═══════════════════════════════════════════════════════╝
```

## Console Visualization

The simulator uses ANSI colors to render:

- **Arrow diagrams** showing message flow between DSP and USP with timestamps
- **Lane status bars** using glyphs: `#` active, `.` parked, `+` waking, `~` parking
- **Port state boxes** with current link width
- **PASS / FAIL** results with explanations

Example lane bar during deactivation:

```
  Lanes: [####~~~~~~~~~~~~]  Lanes 4-15 transitioning to parked
          0123456789abcdef
```

After settling:

```
  Lanes: [####............]  Lanes 0-3 active, 4-15 parked
          0123456789abcdef
```

## Project Structure

```
L0p_handshake_scenarios/
├── CMakeLists.txt
├── Makefile
├── README.md
├── include/
│   ├── types.h           # Enums, Lane, Message, SimClock, timing constants
│   ├── pcie_port.h       # PCIePort base class, RootPort (DSP), Endpoint (USP)
│   ├── pcie_link.h       # PCIeLink connecting two ports
│   ├── visualizer.h      # Console visualization engine
│   └── scenarios.h       # Scenario function declarations
└── src/
    ├── pcie_port.cpp     # Port state machine and L0p message handling
    ├── pcie_link.cpp     # Link width management and message transport
    ├── visualizer.cpp    # ANSI-colored output rendering
    ├── scenarios.cpp     # All 6 scenario implementations
    └── main.cpp          # Entry point with interactive menu and CLI args
```

## Key Design Decisions

| Aspect | Choice | Rationale |
|--------|--------|-----------|
| Simulation model | Step-based / scripted | Clearer than full event-driven for educational purposes |
| Port hierarchy | `PCIePort` base → `RootPort`, `Endpoint` | Maps directly to DSP/USP roles in the spec |
| Lane modeling | Per-lane state + skew offset | Needed for scenario 5 (skew verification) |
| Arbitration | DSP priority on simultaneous exit | Per PCIe 6.0 spec section 8.2.6.4.3 |
| L1 during L0p | NAK then exit-then-retry | Per spec 5.3.3.2.1: must restore width before L1 |

## Timing Constants

Defined in `include/types.h` and tunable:

| Constant | Default | Description |
|----------|---------|-------------|
| `LANE_ACTIVATION_NS` | 20 ns | Time to reactivate a parked lane |
| `LANE_DEACTIVATION_NS` | 10 ns | Time to park an active lane |
| `FLIT_LATENCY_NS` | 4 ns | Flit transit time across the link |
| `DLLP_LATENCY_NS` | 2 ns | DLLP transit time |
| `SKEW_TOLERANCE_NS` | 8 ns | Maximum allowed lane-to-lane skew |
| `ALIGNMENT_TIMEOUT_NS` | 100 ns | Timeout for lane alignment |
| `RETRY_BUFFER_DRAIN_NS` | 30 ns | Time to drain the replay buffer |

## Spec References

- **PCIe 6.0 Base Specification**, Section 8.2.6.4 — L0p sub-state
- **Section 8.2.6.4.3** — Arbitration rules for simultaneous L0p.Exit
- **Section 5.3.3.2.1** — PM state interaction with L0p
- **Section 4.2.7** — Lane-to-lane skew requirements
