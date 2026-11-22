"""Uncapped presentation: a new game frame is presented as drawn the moment it arrives, and a repeated
present being recorded when it arrives is cut short. The cut is only taken where nothing keeps
per-present history, its clears still run, and the lag test ledger stays off unless asked for."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(name):
    return (ROOT / name).read_text(encoding="utf-8-sig")


class ArrivalPresentWiringTest(unittest.TestCase):
    def test_backend_interface_defaults_to_never_cancel(self):
        core = source("port/runtime/gx/gx_core.h")
        self.assertIn("virtual void set_submit_cancel(const std::function<bool()>*) {}", core)
        self.assertIn("virtual bool submit_cancelled() const { return false; }", core)

    def test_cut_only_on_the_plain_path_and_clears_still_run(self):
        d3d12 = source("port/runtime/gx/gx_d3d12.cpp")
        guard = d3d12[d3d12.index("bool cancellable = submit_cancel_"):d3d12.index("for (size_t command_index = 0; command_index < frame.commands.size(); ++command_index) {")]
        for needed in ("!skip_present_", "screen && screen->clear", "!mvec_out()", "!opts_.path_tracing", "!rtx_wanted()",
                       "!remix_layer_", "!remix_mode_", "fg_applied_ <= 0", "!fsr_active()", "!xess_active()",
                       "opts_.capture_path.empty()", "!opts_.flicker_scan"):
            self.assertIn(needed, guard)
        self.assertIn("if (cmd.kind != FrameCommand::Draw && frame.copies[cmd.index].clear) clear_efb(frame.copies[cmd.index]);", d3d12)

    def test_only_repeated_presents_are_cut(self):
        backend = source("port/runtime/gx/threaded_backend.cpp")
        self.assertIn("const bool cancellable = repeat && submit_cancel;", backend)
        self.assertIn("if (cancellable) renderer->set_submit_cancel(&newer);", backend)
        self.assertIn("renderer->set_submit_cancel(nullptr);", backend)

    def test_arrival_frame_is_predict_only_uncapped_or_variable_refresh_and_pairs_after(self):
        backend = source("port/runtime/gx/threaded_backend.cpp")
        self.assertIn("const bool may_wait = arrival_exact_on && authored && !interpolate && (cap_period <= 0.0 || (handover_present && vrr == 1)) &&", backend)
        self.assertIn("if (pair_pending && current.sequence != rendered_sequence && !interpolate && t <= kArrivalPhase) {", backend)
        self.assertIn("constexpr double kArrivalPhase = 0.0015 / SIM_PERIOD;", backend)
        # Every path that does not present the frame as drawn pairs before it uses the solver.
        self.assertIn("if (!arrival_exact) pair_now(true);", backend)
        drain = backend[backend.index("if (frames[cur].sequence != rendered_sequence && queue.size() > 0) {"):]
        self.assertTrue(drain.lstrip().split("\n")[1].strip().startswith("pair_now(true);"))

    def test_ledger_is_off_without_its_switch(self):
        host = source("port/runtime/host/host.cpp")
        self.assertIn('const char* v = std::getenv("MELEE_LAGTEST_FLASH");', host)
        self.assertIn('const int m = !v ? 0 : !std::strcmp(v, "report") ? 1 : !std::strcmp(v, "tick") ? 2 : 0;', host)
        adapter = source("port/runtime/host/gc_adapter.cpp")
        self.assertIn("if (lagtest_flash_mode() == 1) {", adapter)


if __name__ == "__main__":
    unittest.main()
