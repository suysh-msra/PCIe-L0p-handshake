#pragma once

namespace pcie { namespace scenarios {

void vanilla_l0p_entry();
void simultaneous_l0p_exit();
void retry_during_l0p_entry();
void pm_dllp_during_l0p();
void lane_skew_on_wakeup();
void backpressure_nak();

void run_all();

}} // namespace pcie::scenarios
