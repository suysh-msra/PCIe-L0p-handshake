#pragma once

#include "types.h"

namespace pcie {

// ─────────────────────────────────────────────────────────────────
// PCIePort — base class modeling either end of a PCIe link.
//
// State machine for L0p:
//
//   L0_Active ──(entry req)──► L0p_Entry ──(lanes parked)──► L0p_Active
//   L0p_Active ─(exit req)──► L0p_Exit  ──(lanes active)──► L0_Active
//   L0p_Active ─(PM L1 req)─► must exit L0p first
//
// The DSP (Root Port) is the arbiter for simultaneous requests.
// ─────────────────────────────────────────────────────────────────

class PCIePort {
public:
    PCIePort(PortType type, const std::string& name, LinkWidth max_width);
    virtual ~PCIePort() = default;

    // ── Accessors ──────────────────────────────────────────────
    PortType           port_type()     const { return type_; }
    const std::string& name()          const { return name_; }
    LinkState          state()         const { return state_; }
    LinkWidth          current_width() const { return current_width_; }
    LinkWidth          max_width()     const { return max_width_; }
    const std::vector<Lane>& lanes()   const { return lanes_; }
    std::vector<Lane>&       lanes()         { return lanes_; }

    bool pending_retry()     const { return pending_retry_; }
    bool queued_traffic()    const { return queued_traffic_; }
    bool l0p_entry_pending() const { return l0p_entry_pending_; }
    bool l0p_exit_pending()  const { return l0p_exit_pending_; }

    void set_pending_retry(bool v)  { pending_retry_  = v; }
    void set_queued_traffic(bool v) { queued_traffic_  = v; }
    void set_state(LinkState s)     { state_ = s; }
    void set_width(LinkWidth w)     { current_width_ = w; }

    // ── L0p Entry / Exit ───────────────────────────────────────
    // Returns the message to send (caller dispatches it).
    Message initiate_l0p_entry(LinkWidth target, uint64_t now);
    Message initiate_l0p_exit(uint64_t now);

    // Process an inbound message; returns an optional response.
    std::optional<Message> handle_message(const Message& msg, uint64_t now);

    // ── Lane operations ────────────────────────────────────────
    void deactivate_lanes_above(int keep_active, uint64_t now);
    void activate_all_lanes(uint64_t now,
                            const std::vector<int>& skews = {});
    bool all_lanes_settled(uint64_t now) const;
    bool check_skew_within_tolerance(uint64_t now) const;
    void finalize_lane_transitions();

    int  active_lane_count() const;
    void reset_to_full_width();

protected:
    PortType    type_;
    std::string name_;
    LinkState   state_          = LinkState::L0_Active;
    LinkWidth   current_width_;
    LinkWidth   max_width_;
    std::vector<Lane> lanes_;

    bool pending_retry_      = false;
    bool queued_traffic_     = false;
    bool l0p_entry_pending_  = false;
    bool l0p_exit_pending_   = false;
    uint64_t next_seq_       = 0;
};

// ─────────────────────────── Concrete port types ─────────────────

class RootPort : public PCIePort {
public:
    explicit RootPort(LinkWidth max_w = LinkWidth::x16)
        : PCIePort(PortType::DSP, "Root Port", max_w) {}
};

class Endpoint : public PCIePort {
public:
    explicit Endpoint(LinkWidth max_w = LinkWidth::x16)
        : PCIePort(PortType::USP, "Endpoint", max_w) {}
};

} // namespace pcie
