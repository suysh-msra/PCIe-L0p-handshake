#pragma once

#include "pcie_port.h"

namespace pcie {

// ─────────────────────────────────────────────────────────────────
// PCIeLink — models the physical link between a Root Port (DSP)
// and an Endpoint (USP).  Carries messages with configurable
// latency and tracks the active link width.
// ─────────────────────────────────────────────────────────────────

class PCIeLink {
public:
    PCIeLink(RootPort& dsp, Endpoint& usp);

    RootPort& dsp()             { return dsp_; }
    Endpoint& usp()             { return usp_; }
    const RootPort& dsp() const { return dsp_; }
    const Endpoint& usp() const { return usp_; }

    LinkWidth active_width() const { return active_width_; }
    void set_active_width(LinkWidth w) { active_width_ = w; }

    // Deliver a message from src toward dst with the given latency.
    // Returns the arrival time.
    uint64_t transmit(const Message& msg, const PCIePort& src,
                      uint64_t now, uint64_t latency = FLIT_LATENCY_NS);

    // Apply a completed width change on both sides.
    void apply_width(LinkWidth w);

    // Reset to negotiated max.
    void reset();

private:
    RootPort& dsp_;
    Endpoint& usp_;
    LinkWidth active_width_;
};

} // namespace pcie
