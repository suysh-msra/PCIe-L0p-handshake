#include "scenarios.h"
#include "pcie_link.h"
#include "visualizer.h"
#include <random>
#include <thread>
#include <chrono>

namespace pcie { namespace scenarios {

// ═══════════════════════════════════════════════════════════════════
//  Utility: small pause between scenarios so output is readable
// ═══════════════════════════════════════════════════════════════════

static void pause_between() {
    std::cout << "\n  Press ENTER to continue to the next scenario...";
    std::cin.ignore(1000, '\n');
}

// ═══════════════════════════════════════════════════════════════════
//  SCENARIO 1 — Vanilla L0p Entry  (x16 → x4)
//
//  Happy-path: DSP requests width reduction, USP accepts, both
//  deactivate lanes 4-15, link settles at x4.  Then exit back to
//  x16 to show the full round-trip.
// ═══════════════════════════════════════════════════════════════════

void vanilla_l0p_entry() {
    SimClock  clk;
    Visualizer vis;
    RootPort  rp;
    Endpoint  ep;
    PCIeLink  link(rp, ep);

    vis.banner("SCENARIO 1: Vanilla L0p Entry & Exit",
               "x16 -> x4 -> x16  (happy path)");

    // ── Initial state ──
    vis.section("Initial link state — L0 Active, x16");
    vis.link_status(link);
    vis.lane_bar(rp.lanes(), "All 16 lanes active");
    vis.separator();

    // ── Step 1: DSP initiates L0p entry ──
    vis.section("DSP initiates L0p entry — requesting x4");
    Message entry_req = rp.initiate_l0p_entry(LinkWidth::x4, clk.now());
    vis.arrow(clk.now(), "Root Port", "Endpoint", entry_req, "->");
    vis.blank();

    // ── Step 2: flit crosses the link ──
    clk.advance(FLIT_LATENCY_NS);
    vis.event(clk.now(), "Link", "Flit carrying L0p.Entry.Req arrives at USP");

    // ── Step 3: USP evaluates and ACKs ──
    auto resp = ep.handle_message(entry_req, clk.now());
    assert(resp && resp->type == MsgType::L0p_Entry_Ack);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *resp, "<-");
    vis.blank();

    // ── Step 4: ACK travels back ──
    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*resp, clk.now());
    vis.event(clk.now(), "Link", "L0p.Entry.Ack arrives at DSP");
    vis.separator();

    // ── Step 5: Both sides deactivate lanes 4-15 ──
    vis.section("Both sides deactivate lanes 4-15");
    rp.deactivate_lanes_above(4, clk.now());
    ep.deactivate_lanes_above(4, clk.now());
    vis.lane_bar(rp.lanes(), "Lanes 4-15 transitioning to parked");
    vis.blank();

    clk.advance(LANE_DEACTIVATION_NS);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    rp.set_state(LinkState::L0p_Active);
    ep.set_state(LinkState::L0p_Active);
    link.apply_width(LinkWidth::x4);

    vis.event(clk.now(), "Both", "Lane deactivation complete");
    vis.lane_bar(rp.lanes(), "Lanes 0-3 active, 4-15 parked");
    vis.separator();

    // ── Settled in L0p ──
    vis.section("Link settled in L0p — operating at x4");
    vis.link_status(link);
    vis.result(true, "L0p entry completed successfully");
    vis.separator();

    // ═══════ Now demonstrate L0p Exit (x4 → x16) ═══════

    vis.section("DSP initiates L0p exit — restoring x16");
    Message exit_req = rp.initiate_l0p_exit(clk.now());
    vis.arrow(clk.now(), "Root Port", "Endpoint", exit_req, "->");
    vis.blank();

    clk.advance(FLIT_LATENCY_NS);
    auto exit_resp = ep.handle_message(exit_req, clk.now());
    assert(exit_resp && exit_resp->type == MsgType::L0p_Exit_Ack);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *exit_resp, "<-");
    vis.blank();

    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*exit_resp, clk.now());

    // ── Reactivate lanes ──
    vis.section("Both sides reactivate lanes 4-15");
    rp.activate_all_lanes(clk.now());
    ep.activate_all_lanes(clk.now());
    vis.lane_bar(rp.lanes(), "Lanes 4-15 waking up");

    clk.advance(LANE_ACTIVATION_NS);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    rp.set_state(LinkState::L0_Active);
    ep.set_state(LinkState::L0_Active);
    link.apply_width(LinkWidth::x16);

    vis.event(clk.now(), "Both", "Lane reactivation complete");
    vis.lane_bar(rp.lanes(), "All 16 lanes active");
    vis.separator();

    vis.section("Link restored to L0 Active — x16");
    vis.link_status(link);
    vis.result(true, "L0p exit completed successfully — full round-trip done");
    vis.footer();
}

// ═══════════════════════════════════════════════════════════════════
//  SCENARIO 2 — Simultaneous L0p.Exit from both ports
//
//  Both DSP and USP independently decide to widen at the same
//  instant.  Their L0p.Exit.Req messages cross in flight.
//  Per PCIe 6.0 spec, the DSP (Root Port) wins arbitration;
//  the USP's request is subsumed.  Both proceed with exit.
// ═══════════════════════════════════════════════════════════════════

void simultaneous_l0p_exit() {
    SimClock  clk;
    Visualizer vis;
    RootPort  rp;
    Endpoint  ep;
    PCIeLink  link(rp, ep);

    vis.banner("SCENARIO 2: Simultaneous L0p.Exit",
               "Both sides request widening at the same time");

    // Get into L0p first (abbreviated)
    vis.section("Pre-condition: Link already in L0p at x4");
    rp.set_state(LinkState::L0p_Active);
    ep.set_state(LinkState::L0p_Active);
    link.apply_width(LinkWidth::x4);
    for (auto* port : {static_cast<PCIePort*>(&rp),
                       static_cast<PCIePort*>(&ep)})
        port->deactivate_lanes_above(4, 0);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    vis.link_status(link);
    vis.lane_bar(rp.lanes());
    vis.separator();

    // ── Both initiate exit at the same time ──
    vis.section("Both ports issue L0p.Exit.Req simultaneously");
    Message dsp_exit = rp.initiate_l0p_exit(clk.now());
    Message usp_exit = ep.initiate_l0p_exit(clk.now());
    vis.crossed_arrows(clk.now(), dsp_exit, usp_exit);
    vis.blank();

    // ── Messages cross in flight ──
    clk.advance(FLIT_LATENCY_NS);
    vis.event(clk.now(), "Link",
              "Messages cross in flight — both arrive simultaneously");
    vis.blank();

    // ── DSP receives USP's exit req ──
    vis.section("Arbitration: DSP has priority (per spec sec 8.2.6.4.3)");
    auto dsp_resp = rp.handle_message(usp_exit, clk.now());
    vis.event(clk.now(), "Root Port",
              "Receives USP Exit.Req — ACKs (already exiting)");
    if (dsp_resp)
        vis.arrow(clk.now(), "Root Port", "Endpoint", *dsp_resp, "->");
    vis.blank();

    // ── USP receives DSP's exit req; defers own pending request ──
    auto usp_resp = ep.handle_message(dsp_exit, clk.now());
    vis.event(clk.now(), "Endpoint",
              "Receives DSP Exit.Req — defers own request, ACKs DSP");
    vis.note("USP's pending exit flag is cleared; DSP's request takes over.");
    if (usp_resp)
        vis.arrow(clk.now(), "Endpoint", "Root Port", *usp_resp, "<-");
    vis.separator();

    // ── Both sides reactivate lanes ──
    vis.section("Unified exit: both sides reactivate lanes");
    rp.activate_all_lanes(clk.now());
    ep.activate_all_lanes(clk.now());
    vis.lane_bar(rp.lanes(), "Lanes 4-15 waking up");

    clk.advance(LANE_ACTIVATION_NS);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    rp.set_state(LinkState::L0_Active);
    ep.set_state(LinkState::L0_Active);
    link.apply_width(LinkWidth::x16);

    vis.event(clk.now(), "Both", "Lane reactivation complete");
    vis.lane_bar(rp.lanes(), "All 16 lanes restored");
    vis.separator();

    vis.section("Result");
    vis.link_status(link);
    vis.result(true,
        "Simultaneous exit resolved — DSP arbitration, link at x16");
    vis.footer();
}

// ═══════════════════════════════════════════════════════════════════
//  SCENARIO 3 — L0p Entry during pending Retry
//
//  DSP requests L0p entry, but the USP has just received a NAK
//  DLLP indicating TLP replay is needed.  The USP must NAK the
//  L0p request because reducing width during replay would violate
//  ordering and timing guarantees.
// ═══════════════════════════════════════════════════════════════════

void retry_during_l0p_entry() {
    SimClock  clk;
    Visualizer vis;
    RootPort  rp;
    Endpoint  ep;
    PCIeLink  link(rp, ep);

    vis.banner("SCENARIO 3: L0p Entry During Pending Retry",
               "NAK DLLP arrives while width-change handshake is in progress");

    vis.section("Initial state: L0 Active x16, EP about to replay");
    vis.link_status(link);

    // ── Inject a NAK: EP has pending retry ──
    vis.section("USP receives NAK DLLP — TLP replay required");
    Message nak_dllp(MsgType::Nak_DLLP, clk.now(),
                     "Seq 42-47 need replay");
    ep.handle_message(nak_dllp, clk.now());
    vis.event(clk.now(), "Endpoint",
              "NAK DLLP received — setting pending_retry flag");
    vis.note("Replay buffer is being drained; bandwidth is critical.");
    vis.separator();

    // ── DSP (unaware of retry) initiates L0p entry ──
    clk.advance(2);
    vis.section("DSP initiates L0p entry (unaware of pending Retry)");
    Message entry_req = rp.initiate_l0p_entry(LinkWidth::x4, clk.now());
    vis.arrow(clk.now(), "Root Port", "Endpoint", entry_req, "->");
    vis.blank();

    clk.advance(FLIT_LATENCY_NS);
    vis.event(clk.now(), "Link", "L0p.Entry.Req arrives at USP");

    // ── USP NAKs the request ──
    auto resp = ep.handle_message(entry_req, clk.now());
    assert(resp && resp->type == MsgType::L0p_Entry_Nak);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *resp, "<-");
    vis.blank();

    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*resp, clk.now());
    vis.event(clk.now(), "Root Port",
              "Receives L0p.Entry.Nak — entry aborted, stays in L0");
    vis.separator();

    vis.section("Retry completes, DSP retries L0p entry");
    clk.advance(RETRY_BUFFER_DRAIN_NS);
    ep.set_pending_retry(false);
    vis.event(clk.now(), "Endpoint", "Replay complete — retry flag cleared");
    vis.blank();

    // Retry the L0p entry
    Message retry_req = rp.initiate_l0p_entry(LinkWidth::x4, clk.now());
    vis.arrow(clk.now(), "Root Port", "Endpoint", retry_req, "->");
    clk.advance(FLIT_LATENCY_NS);

    auto retry_resp = ep.handle_message(retry_req, clk.now());
    assert(retry_resp && retry_resp->type == MsgType::L0p_Entry_Ack);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *retry_resp, "<-");
    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*retry_resp, clk.now());

    rp.deactivate_lanes_above(4, clk.now());
    ep.deactivate_lanes_above(4, clk.now());
    clk.advance(LANE_DEACTIVATION_NS);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    rp.set_state(LinkState::L0p_Active);
    ep.set_state(LinkState::L0p_Active);
    link.apply_width(LinkWidth::x4);

    vis.event(clk.now(), "Both", "L0p entry succeeded on retry");
    vis.lane_bar(rp.lanes());
    vis.separator();

    vis.section("Result");
    vis.link_status(link);
    vis.result(true,
        "Retry correctly blocked L0p; entry succeeded after replay done");
    vis.footer();
}

// ═══════════════════════════════════════════════════════════════════
//  SCENARIO 4 — PM DLLP (L1 request) while in L0p
//
//  Link is in L0p at x4.  Software triggers PM L1 entry.  Per
//  spec, the link must first exit L0p (restore full width) before
//  it can transition to L1.  The initial PM request is NAK'd,
//  L0p exit is performed, then L1 entry proceeds.
// ═══════════════════════════════════════════════════════════════════

void pm_dllp_during_l0p() {
    SimClock  clk;
    Visualizer vis;
    RootPort  rp;
    Endpoint  ep;
    PCIeLink  link(rp, ep);

    vis.banner("SCENARIO 4: PM DLLP (L1 Request) During L0p",
               "L1 entry must wait for L0p exit");

    // Set up L0p at x4
    vis.section("Pre-condition: Link in L0p at x4");
    rp.set_state(LinkState::L0p_Active);
    ep.set_state(LinkState::L0p_Active);
    link.apply_width(LinkWidth::x4);
    for (auto* port : {static_cast<PCIePort*>(&rp),
                       static_cast<PCIePort*>(&ep)})
        port->deactivate_lanes_above(4, 0);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    vis.link_status(link);
    vis.lane_bar(rp.lanes());
    vis.separator();

    // ── PM L1 request arrives ──
    vis.section("Software triggers PM L1 entry on DSP");
    Message pm_req(MsgType::PM_Enter_L1_Req, clk.now(),
                   "ASPM L1 entry requested by power manager");
    vis.arrow(clk.now(), "Root Port", "Endpoint", pm_req, "->");
    clk.advance(DLLP_LATENCY_NS);

    auto pm_resp = ep.handle_message(pm_req, clk.now());
    assert(pm_resp && pm_resp->type == MsgType::PM_Enter_L1_Nak);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *pm_resp, "<-");
    vis.note("USP NAKs L1: cannot enter L1 while in L0p (spec 5.3.3.2.1)");
    vis.separator();

    // ── Must exit L0p first ──
    vis.section("DSP initiates L0p exit before retrying L1");
    clk.advance(DLLP_LATENCY_NS);
    Message exit_req = rp.initiate_l0p_exit(clk.now());
    vis.arrow(clk.now(), "Root Port", "Endpoint", exit_req, "->");

    clk.advance(FLIT_LATENCY_NS);
    auto exit_resp = ep.handle_message(exit_req, clk.now());
    assert(exit_resp && exit_resp->type == MsgType::L0p_Exit_Ack);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *exit_resp, "<-");

    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*exit_resp, clk.now());

    rp.activate_all_lanes(clk.now());
    ep.activate_all_lanes(clk.now());
    vis.lane_bar(rp.lanes(), "Lanes reactivating...");

    clk.advance(LANE_ACTIVATION_NS);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    rp.set_state(LinkState::L0_Active);
    ep.set_state(LinkState::L0_Active);
    link.apply_width(LinkWidth::x16);
    vis.event(clk.now(), "Both", "L0p exit complete — back at x16");
    vis.lane_bar(rp.lanes());
    vis.separator();

    // ── Now retry L1 entry ──
    vis.section("DSP retries PM L1 entry (now in L0 Active)");
    Message pm_retry(MsgType::PM_Enter_L1_Req, clk.now(),
                     "Retry L1 entry — link is at full width");
    vis.arrow(clk.now(), "Root Port", "Endpoint", pm_retry, "->");

    clk.advance(DLLP_LATENCY_NS);
    auto pm_ack = ep.handle_message(pm_retry, clk.now());
    assert(pm_ack && pm_ack->type == MsgType::PM_Enter_L1_Ack);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *pm_ack, "<-");

    rp.set_state(LinkState::L1);
    ep.set_state(LinkState::L1);
    vis.event(clk.now(), "Both", "Link entering L1 — low-power sleep");
    vis.separator();

    vis.section("Result");
    vis.port_states(rp, ep);
    vis.blank();
    vis.result(true,
        "L1 entry correctly deferred until L0p exit; link now in L1");
    vis.footer();
}

// ═══════════════════════════════════════════════════════════════════
//  SCENARIO 5 — Lane-to-lane skew on wake-up
//
//  During L0p exit, reactivated lanes have varying lock times
//  (random skew).  We verify that all lanes align within the
//  SKEW_TOLERANCE_NS window.  If skew exceeds tolerance the link
//  must enter Recovery — we show both the pass and fail cases.
// ═══════════════════════════════════════════════════════════════════

void lane_skew_on_wakeup() {
    SimClock  clk;
    Visualizer vis;
    RootPort  rp;
    Endpoint  ep;
    PCIeLink  link(rp, ep);

    vis.banner("SCENARIO 5: Lane-to-Lane Skew on Wake-up",
               "Verifying reactivated lanes align within spec timing");

    // Pre-condition: L0p at x4
    rp.set_state(LinkState::L0p_Active);
    ep.set_state(LinkState::L0p_Active);
    link.apply_width(LinkWidth::x4);
    for (auto* port : {static_cast<PCIePort*>(&rp),
                       static_cast<PCIePort*>(&ep)})
        port->deactivate_lanes_above(4, 0);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();

    vis.section("Pre-condition: L0p at x4");
    vis.lane_bar(rp.lanes());
    vis.separator();

    // ─── SUB-CASE A: skew within tolerance ───────────────────

    vis.section("Case A: Skew within tolerance (<= "
                + std::to_string(SKEW_TOLERANCE_NS) + " ns)");

    // Generate small random skews for lanes 4-15
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> small_skew(0, 6);

    std::vector<int> skews_a(MAX_LANES, 0);
    vis.note("Per-lane activation skew (ns):");
    std::cout << "    Lane:  ";
    for (int i = 0; i < MAX_LANES; ++i)
        std::cout << std::hex << i << " ";
    std::cout << std::dec << "\n    Skew:  ";
    for (int i = 0; i < MAX_LANES; ++i) {
        skews_a[i] = (i < 4) ? 0 : small_skew(rng);
        std::cout << skews_a[i] << " ";
    }
    std::cout << "\n\n";

    Message exit_req_a = rp.initiate_l0p_exit(clk.now());
    vis.arrow(clk.now(), "Root Port", "Endpoint", exit_req_a, "->");
    clk.advance(FLIT_LATENCY_NS);
    auto exit_ack_a = ep.handle_message(exit_req_a, clk.now());
    vis.arrow(clk.now(), "Endpoint", "Root Port", *exit_ack_a, "<-");
    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*exit_ack_a, clk.now());

    rp.activate_all_lanes(clk.now(), skews_a);
    ep.activate_all_lanes(clk.now(), skews_a);
    vis.lane_bar(rp.lanes(), "Lanes waking with per-lane skew");

    // Find max completion time
    uint64_t max_done = 0;
    for (const auto& l : rp.lanes())
        max_done = std::max(max_done, l.activation_done_time());
    clk.set(max_done);

    bool within_tol = rp.check_skew_within_tolerance(clk.now());
    vis.event(clk.now(), "Both", "All lanes completed activation");

    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    rp.set_state(LinkState::L0_Active);
    ep.set_state(LinkState::L0_Active);
    link.apply_width(LinkWidth::x16);

    vis.lane_bar(rp.lanes());
    vis.result(within_tol,
               "Skew within tolerance — link operational at x16");
    vis.separator();

    // ─── SUB-CASE B: skew EXCEEDS tolerance ─────────────────

    vis.section("Case B: Skew exceeds tolerance (> "
                + std::to_string(SKEW_TOLERANCE_NS) + " ns)");

    // Reset to L0p
    link.apply_width(LinkWidth::x4);
    rp.set_state(LinkState::L0p_Active);
    ep.set_state(LinkState::L0p_Active);
    for (auto* port : {static_cast<PCIePort*>(&rp),
                       static_cast<PCIePort*>(&ep)})
        port->deactivate_lanes_above(4, 0);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    clk.advance(10);

    // Large skews on some lanes
    std::vector<int> skews_b(MAX_LANES, 0);
    std::uniform_int_distribution<int> big_skew(0, 20);
    vis.note("Per-lane activation skew (ns):");
    std::cout << "    Lane:  ";
    for (int i = 0; i < MAX_LANES; ++i)
        std::cout << std::hex << i << " ";
    std::cout << std::dec << "\n    Skew:  ";
    for (int i = 0; i < MAX_LANES; ++i) {
        skews_b[i] = (i < 4) ? 0 : big_skew(rng);
        std::cout << skews_b[i] << " ";
    }
    std::cout << "\n\n";

    Message exit_req_b = rp.initiate_l0p_exit(clk.now());
    clk.advance(FLIT_LATENCY_NS);
    auto exit_ack_b = ep.handle_message(exit_req_b, clk.now());
    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*exit_ack_b, clk.now());

    rp.activate_all_lanes(clk.now(), skews_b);
    ep.activate_all_lanes(clk.now(), skews_b);
    vis.lane_bar(rp.lanes(), "Lanes waking with LARGE per-lane skew");

    max_done = 0;
    for (const auto& l : rp.lanes())
        max_done = std::max(max_done, l.activation_done_time());
    clk.set(max_done);

    bool within_tol_b = rp.check_skew_within_tolerance(clk.now());
    vis.event(clk.now(), "Both", "Lane activation complete — checking skew");

    if (!within_tol_b) {
        vis.event(clk.now(), "Both",
                  "SKEW VIOLATION: entering Recovery state");
        rp.set_state(LinkState::Recovery);
        ep.set_state(LinkState::Recovery);
        rp.finalize_lane_transitions();
        ep.finalize_lane_transitions();
        vis.lane_bar(rp.lanes());
        vis.result(false,
                   "Skew exceeded tolerance — link forced to Recovery");
        vis.note("Recovery will retrain all lanes to re-establish alignment.");
    } else {
        rp.finalize_lane_transitions();
        ep.finalize_lane_transitions();
        rp.set_state(LinkState::L0_Active);
        ep.set_state(LinkState::L0_Active);
        link.apply_width(LinkWidth::x16);
        vis.lane_bar(rp.lanes());
        vis.result(true, "Skew within tolerance (lucky large-skew run)");
    }

    vis.separator();
    vis.port_states(rp, ep);
    vis.footer();
}

// ═══════════════════════════════════════════════════════════════════
//  SCENARIO 6 — Back-pressure: USP NAK's entry request
//
//  DSP requests L0p entry, but the USP has significant queued
//  traffic and cannot afford the bandwidth reduction.  The USP
//  NAKs.  The DSP backs off and retries later once traffic drops.
// ═══════════════════════════════════════════════════════════════════

void backpressure_nak() {
    SimClock  clk;
    Visualizer vis;
    RootPort  rp;
    Endpoint  ep;
    PCIeLink  link(rp, ep);

    vis.banner("SCENARIO 6: Back-Pressure — USP NAKs L0p Entry",
               "USP has queued traffic, cannot reduce bandwidth");

    vis.section("Initial state: L0 Active x16, USP transmitting bulk data");
    vis.link_status(link);

    // Mark USP as having queued traffic
    ep.set_queued_traffic(true);
    vis.event(clk.now(), "Endpoint",
              "Queued TLP traffic: 12 posted writes in flight");
    vis.separator();

    // ── DSP requests L0p entry ──
    vis.section("DSP initiates L0p entry — requesting x4");
    clk.advance(5);
    Message entry_req = rp.initiate_l0p_entry(LinkWidth::x4, clk.now());
    vis.arrow(clk.now(), "Root Port", "Endpoint", entry_req, "->");

    clk.advance(FLIT_LATENCY_NS);
    auto resp = ep.handle_message(entry_req, clk.now());
    assert(resp && resp->type == MsgType::L0p_Entry_Nak);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *resp, "<-");
    vis.blank();

    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*resp, clk.now());
    vis.event(clk.now(), "Root Port",
              "Receives NAK — backs off, will retry after cooldown");
    vis.separator();

    // ── Traffic drains ──
    vis.section("Time passes... USP drains its transmit queue");
    clk.advance(80);
    ep.set_queued_traffic(false);
    vis.event(clk.now(), "Endpoint", "Transmit queue drained — ready for L0p");
    vis.separator();

    // ── Retry succeeds ──
    vis.section("DSP retries L0p entry");
    Message retry = rp.initiate_l0p_entry(LinkWidth::x4, clk.now());
    vis.arrow(clk.now(), "Root Port", "Endpoint", retry, "->");

    clk.advance(FLIT_LATENCY_NS);
    auto retry_resp = ep.handle_message(retry, clk.now());
    assert(retry_resp && retry_resp->type == MsgType::L0p_Entry_Ack);
    vis.arrow(clk.now(), "Endpoint", "Root Port", *retry_resp, "<-");

    clk.advance(FLIT_LATENCY_NS);
    rp.handle_message(*retry_resp, clk.now());

    rp.deactivate_lanes_above(4, clk.now());
    ep.deactivate_lanes_above(4, clk.now());
    vis.lane_bar(rp.lanes(), "Lanes 4-15 deactivating");

    clk.advance(LANE_DEACTIVATION_NS);
    rp.finalize_lane_transitions();
    ep.finalize_lane_transitions();
    rp.set_state(LinkState::L0p_Active);
    ep.set_state(LinkState::L0p_Active);
    link.apply_width(LinkWidth::x4);

    vis.event(clk.now(), "Both", "L0p entry complete on retry");
    vis.lane_bar(rp.lanes());
    vis.separator();

    vis.section("Result");
    vis.link_status(link);
    vis.result(true,
        "Back-pressure correctly handled — L0p entered after traffic drained");
    vis.footer();
}

// ═══════════════════════════════════════════════════════════════════
//  Run all scenarios
// ═══════════════════════════════════════════════════════════════════

void run_all() {
    vanilla_l0p_entry();
    pause_between();
    simultaneous_l0p_exit();
    pause_between();
    retry_during_l0p_entry();
    pause_between();
    pm_dllp_during_l0p();
    pause_between();
    lane_skew_on_wakeup();
    pause_between();
    backpressure_nak();
}

}} // namespace pcie::scenarios
