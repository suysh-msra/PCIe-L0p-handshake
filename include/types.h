#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <queue>
#include <deque>
#include <optional>
#include <functional>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <cassert>

namespace pcie {

// ─────────────────────────── Constants ───────────────────────────

constexpr int    MAX_LANES                = 16;
constexpr uint64_t LANE_ACTIVATION_NS     = 20;   // Time to reactivate a parked lane
constexpr uint64_t LANE_DEACTIVATION_NS   = 10;   // Time to park an active lane
constexpr uint64_t FLIT_LATENCY_NS        = 4;    // Flit transit across link
constexpr uint64_t DLLP_LATENCY_NS        = 2;    // DLLP transit across link
constexpr uint64_t SKEW_TOLERANCE_NS      = 8;    // Max allowed lane-to-lane skew
constexpr uint64_t ALIGNMENT_TIMEOUT_NS   = 100;  // Timeout for lane alignment
constexpr uint64_t RETRY_BUFFER_DRAIN_NS  = 30;   // Time to drain retry buffer

// ─────────────────────────── Enumerations ────────────────────────

enum class PortType { USP, DSP };

enum class LinkState {
    L0_Active,
    L0p_Entry,
    L0p_Active,
    L0p_Exit,
    L1_Entry,
    L1,
    Recovery,
    Disabled
};

enum class LaneState {
    Active,
    Parked,
    Activating,
    Deactivating
};

enum class LinkWidth : int {
    x1 = 1, x2 = 2, x4 = 4, x8 = 8, x16 = 16
};

enum class MsgType {
    L0p_Entry_Req,
    L0p_Entry_Ack,
    L0p_Entry_Nak,
    L0p_Exit_Req,
    L0p_Exit_Ack,
    Ack_DLLP,
    Nak_DLLP,
    PM_Enter_L1_Req,
    PM_Enter_L1_Ack,
    PM_Enter_L1_Nak,
    TLP_Data
};

// ─────────────────────────── String helpers ──────────────────────

inline const char* to_str(PortType t) {
    return t == PortType::USP ? "USP" : "DSP";
}

inline const char* to_str(LinkState s) {
    switch (s) {
        case LinkState::L0_Active:  return "L0 Active";
        case LinkState::L0p_Entry:  return "L0p Entry";
        case LinkState::L0p_Active: return "L0p Active";
        case LinkState::L0p_Exit:   return "L0p Exit";
        case LinkState::L1_Entry:   return "L1 Entry";
        case LinkState::L1:         return "L1";
        case LinkState::Recovery:   return "Recovery";
        case LinkState::Disabled:   return "Disabled";
    }
    return "?";
}

inline const char* to_str(LaneState s) {
    switch (s) {
        case LaneState::Active:       return "Active";
        case LaneState::Parked:       return "Parked";
        case LaneState::Activating:   return "Activating";
        case LaneState::Deactivating: return "Deactivating";
    }
    return "?";
}

inline std::string to_str(LinkWidth w) {
    return "x" + std::to_string(static_cast<int>(w));
}

inline const char* to_str(MsgType t) {
    switch (t) {
        case MsgType::L0p_Entry_Req:   return "L0p.Entry.Req";
        case MsgType::L0p_Entry_Ack:   return "L0p.Entry.Ack";
        case MsgType::L0p_Entry_Nak:   return "L0p.Entry.Nak";
        case MsgType::L0p_Exit_Req:    return "L0p.Exit.Req";
        case MsgType::L0p_Exit_Ack:    return "L0p.Exit.Ack";
        case MsgType::Ack_DLLP:        return "ACK DLLP";
        case MsgType::Nak_DLLP:        return "NAK DLLP";
        case MsgType::PM_Enter_L1_Req: return "PM_Enter_L1";
        case MsgType::PM_Enter_L1_Ack: return "PM_L1_Ack";
        case MsgType::PM_Enter_L1_Nak: return "PM_L1_Nak";
        case MsgType::TLP_Data:        return "TLP Data";
    }
    return "?";
}

// ─────────────────────────── Lane ────────────────────────────────

class Lane {
    int       id_;
    LaneState state_              = LaneState::Active;
    uint64_t  transition_start_   = 0;
    int       skew_offset_ns_     = 0;

public:
    explicit Lane(int id) : id_(id) {}

    int       id()    const { return id_; }
    LaneState state() const { return state_; }
    int       skew()  const { return skew_offset_ns_; }

    void begin_deactivation(uint64_t now) {
        state_ = LaneState::Deactivating;
        transition_start_ = now;
    }

    void begin_activation(uint64_t now, int skew = 0) {
        state_ = LaneState::Activating;
        transition_start_ = now;
        skew_offset_ns_ = skew;
    }

    void set_active()  { state_ = LaneState::Active; skew_offset_ns_ = 0; }
    void set_parked()  { state_ = LaneState::Parked; }

    uint64_t activation_done_time() const {
        return transition_start_ + LANE_ACTIVATION_NS + skew_offset_ns_;
    }

    uint64_t deactivation_done_time() const {
        return transition_start_ + LANE_DEACTIVATION_NS;
    }

    bool is_transition_done(uint64_t now) const {
        if (state_ == LaneState::Activating)
            return now >= activation_done_time();
        if (state_ == LaneState::Deactivating)
            return now >= deactivation_done_time();
        return true;
    }
};

// ─────────────────────────── Message ─────────────────────────────

struct Message {
    MsgType   type;
    LinkWidth target_width = LinkWidth::x16;
    uint64_t  timestamp    = 0;
    uint64_t  seq_num      = 0;
    std::string info;

    Message() : type(MsgType::TLP_Data) {}
    Message(MsgType t, uint64_t ts, const std::string& i = "")
        : type(t), timestamp(ts), info(i) {}
};

// ─────────────────────────── SimClock ────────────────────────────

class SimClock {
    uint64_t ns_ = 0;
public:
    uint64_t now()                  const { return ns_; }
    void     advance(uint64_t delta)      { ns_ += delta; }
    void     set(uint64_t t)              { ns_ = t; }
    void     reset()                      { ns_ = 0; }
};

} // namespace pcie
