#include "pcie_port.h"

namespace pcie {

PCIePort::PCIePort(PortType type, const std::string& name, LinkWidth max_width)
    : type_(type)
    , name_(name)
    , current_width_(max_width)
    , max_width_(max_width)
{
    lanes_.reserve(MAX_LANES);
    for (int i = 0; i < static_cast<int>(max_width); ++i)
        lanes_.emplace_back(i);
}

// ─────────────────────────── L0p Entry ──────────────────────────

Message PCIePort::initiate_l0p_entry(LinkWidth target, uint64_t now) {
    l0p_entry_pending_ = true;
    state_ = LinkState::L0p_Entry;

    Message msg(MsgType::L0p_Entry_Req, now);
    msg.target_width = target;
    msg.seq_num = next_seq_++;
    msg.info = "Requesting width reduction to " + to_str(target);
    return msg;
}

// ─────────────────────────── L0p Exit ───────────────────────────

Message PCIePort::initiate_l0p_exit(uint64_t now) {
    l0p_exit_pending_ = true;
    state_ = LinkState::L0p_Exit;

    Message msg(MsgType::L0p_Exit_Req, now);
    msg.target_width = max_width_;
    msg.seq_num = next_seq_++;
    msg.info = "Requesting width restoration to " + to_str(max_width_);
    return msg;
}

// ─────────────────────────── Message handling ───────────────────

std::optional<Message> PCIePort::handle_message(const Message& msg,
                                                 uint64_t now) {
    switch (msg.type) {

    case MsgType::L0p_Entry_Req: {
        // Responder logic: can we accept the width reduction?
        if (pending_retry_) {
            Message nak(MsgType::L0p_Entry_Nak, now,
                        "NAK: pending Retry — replay in progress");
            return nak;
        }
        if (queued_traffic_ && type_ == PortType::USP) {
            Message nak(MsgType::L0p_Entry_Nak, now,
                        "NAK: back-pressure — queued traffic pending");
            return nak;
        }
        state_ = LinkState::L0p_Entry;
        Message ack(MsgType::L0p_Entry_Ack, now,
                     "ACK: accepting width reduction to "
                     + to_str(msg.target_width));
        ack.target_width = msg.target_width;
        return ack;
    }

    case MsgType::L0p_Entry_Ack: {
        l0p_entry_pending_ = false;
        // Both sides will now deactivate lanes; caller orchestrates.
        return std::nullopt;
    }

    case MsgType::L0p_Entry_Nak: {
        l0p_entry_pending_ = false;
        state_ = LinkState::L0_Active; // Revert
        return std::nullopt;
    }

    case MsgType::L0p_Exit_Req: {
        // If we also have a pending exit, DSP wins arbitration.
        if (l0p_exit_pending_ && type_ == PortType::USP) {
            // USP defers to DSP's exit request
            l0p_exit_pending_ = false;
        }
        state_ = LinkState::L0p_Exit;
        Message ack(MsgType::L0p_Exit_Ack, now,
                     "ACK: proceeding with width restoration");
        ack.target_width = msg.target_width;
        return ack;
    }

    case MsgType::L0p_Exit_Ack: {
        l0p_exit_pending_ = false;
        return std::nullopt;
    }

    case MsgType::Nak_DLLP: {
        pending_retry_ = true;
        return std::nullopt;
    }

    case MsgType::PM_Enter_L1_Req: {
        if (state_ == LinkState::L0p_Active) {
            // Must exit L0p before entering L1
            Message nak(MsgType::PM_Enter_L1_Nak, now,
                        "NAK: must exit L0p before L1 entry");
            return nak;
        }
        state_ = LinkState::L1_Entry;
        Message ack(MsgType::PM_Enter_L1_Ack, now,
                     "ACK: proceeding to L1");
        return ack;
    }

    default:
        return std::nullopt;
    }
}

// ─────────────────────────── Lane operations ────────────────────

void PCIePort::deactivate_lanes_above(int keep_active, uint64_t now) {
    for (auto& lane : lanes_) {
        if (lane.id() >= keep_active)
            lane.begin_deactivation(now);
    }
}

void PCIePort::activate_all_lanes(uint64_t now,
                                   const std::vector<int>& skews) {
    for (auto& lane : lanes_) {
        if (lane.state() == LaneState::Parked ||
            lane.state() == LaneState::Deactivating) {
            int skew = 0;
            if (lane.id() < static_cast<int>(skews.size()))
                skew = skews[lane.id()];
            lane.begin_activation(now, skew);
        }
    }
}

bool PCIePort::all_lanes_settled(uint64_t now) const {
    return std::all_of(lanes_.begin(), lanes_.end(),
        [now](const Lane& l) { return l.is_transition_done(now); });
}

bool PCIePort::check_skew_within_tolerance(uint64_t /*now*/) const {
    uint64_t earliest = UINT64_MAX, latest = 0;
    bool found = false;
    for (const auto& lane : lanes_) {
        if (lane.state() == LaneState::Activating) {
            uint64_t done = lane.activation_done_time();
            earliest = std::min(earliest, done);
            latest   = std::max(latest, done);
            found = true;
        }
    }
    if (!found) return true;
    return (latest - earliest) <= SKEW_TOLERANCE_NS;
}

void PCIePort::finalize_lane_transitions() {
    for (auto& lane : lanes_) {
        if (lane.state() == LaneState::Deactivating)
            lane.set_parked();
        else if (lane.state() == LaneState::Activating)
            lane.set_active();
    }
}

int PCIePort::active_lane_count() const {
    return static_cast<int>(std::count_if(lanes_.begin(), lanes_.end(),
        [](const Lane& l) {
            return l.state() == LaneState::Active ||
                   l.state() == LaneState::Activating;
        }));
}

void PCIePort::reset_to_full_width() {
    state_ = LinkState::L0_Active;
    current_width_ = max_width_;
    pending_retry_ = false;
    queued_traffic_ = false;
    l0p_entry_pending_ = false;
    l0p_exit_pending_ = false;
    for (auto& lane : lanes_)
        lane.set_active();
}

} // namespace pcie
