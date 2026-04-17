#include "pcie_link.h"

namespace pcie {

PCIeLink::PCIeLink(RootPort& dsp, Endpoint& usp)
    : dsp_(dsp), usp_(usp)
    , active_width_(dsp.max_width())
{}

uint64_t PCIeLink::transmit(const Message& /*msg*/, const PCIePort& src,
                             uint64_t now, uint64_t latency) {
    (void)src; // direction is handled by the scenario orchestrator
    return now + latency;
}

void PCIeLink::apply_width(LinkWidth w) {
    active_width_ = w;
    dsp_.set_width(w);
    usp_.set_width(w);
}

void PCIeLink::reset() {
    active_width_ = dsp_.max_width();
    dsp_.reset_to_full_width();
    usp_.reset_to_full_width();
}

} // namespace pcie
