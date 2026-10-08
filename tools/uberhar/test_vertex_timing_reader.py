#!/usr/bin/env python3
"""CodexAstraLocal: Independent malformed/partial accounting regressions for reports."""
import copy
import json
import unittest

import vertex_timing as vt


def fixture():
    # CodexAstraLocal: Deliberately author this small accounting witness separately
    # from the producer. Three inputs include one hit and two different load routes.
    return {
        "schema": 1, "diagnostic_id": "synthetic-01", "mode": "detailed",
        "title_id": "0000000000000001", "run": 1, "owner_tid": 2,
        "version": "synthetic", "revision": "synthetic", "engine": "synthetic",
        "selection": "periodic_first_eligible_cpu_draw_hashed_contiguous_input_offset",
        "scope": "actual_observed_no_gs_loop_only_no_extrapolation",
        "clock_bias": "raw_instrumented_cost_no_calibration_subtraction", "stop": "budget_exhausted",
        "config": {"delay_ms": 0, "duration_ms": 10000, "period_ms": 100,
                   "chunk_vertices": 64, "max_chunks": 64, "max_vertices": 4096,
                   "seed": 1, "max_output_bytes": 65536, "max_draw_inputs": 4096},
        "clock": {"cpu": "CLOCK_THREAD_CPUTIME_ID", "wall": "std::chrono::steady_clock",
                  "wall_resolution_ns": 1, "cpu_resolution_ns": 1, "wall_reads": 40, "cpu_reads": 18},
        "calibration": {"pairs": 8, "wall_pairs": 8, "cpu_ns": 40, "outer_wall_ns": 90,
                        "wall_pair_ns": 15, "maximum_wall_pair_ns": 3},
        "summary": {"cpu_batches": 10, "eligible_batches": 10, "waiting_batches": 3,
                    "in_window_batches": 7, "period_filtered": 6, "empty_batches": 0,
                    "unsupported_batches": 0, "oversized_batches": 0, "budget_dropped": 0,
                    "clock_failures": 0, "identity_failures": 0, "phase_failures": 0,
                    "sparse_suppressed": 0, "reserved_vertices": 3, "selected_draw_inputs": 100,
                    "records": 1, "armed_at_ns": 100, "window_begin_ns": 100,
                    "window_end_ns": 10_000_000_100, "last_poll_ns": 200, "phases": [0, 0, 0, 10, 0]},
        "stage_order": list(vt.STAGES),
        "records": [{"draw_ordinal": 10, "program_hash": "0000000000000002",
                     "swizzle_hash": "0000000000000003", "entry": 7, "draw_count": 100,
                     "vs_input_attributes": 3, "topology": 0, "indexed": True, "fused_plan": True,
                     "first_input": 12, "input_count": 3, "observed_inputs": 3,
                     "hits": 1, "misses": 2, "fused_misses": 1, "legacy_misses": 1,
                     "start_offset_ns": 100, "token_before": 11, "token_after": 11,
                     "begun": True, "completed": True, "clocks_valid": True,
                     "phase_stable": True, "overhang": False,
                     "begin": [200, 100, 210], "end": [310, 170, 320],
                     "stage_calls": [3, 2, 2, 2, 3, 3], "stage_wall_ns": [10, 20, 30, 15, 5, 10]}],
    }


# CodexAstraLocal: Separate accepted measurements from malformed or
# censored rows; empty/boundary data must not become invented stage times.
class ReportTests(unittest.TestCase):
    def accepted(self, report):
        parsed, digest = vt.decode(json.dumps(report).encode())
        return vt.summarize(parsed, digest)

    def rejected(self, change):
        report = fixture()
        change(report)
        with self.assertRaises(vt.TimingError):
            vt.decode(json.dumps(report).encode())

    def test_measured_population_and_brackets(self):
        result = self.accepted(fixture())
        self.assertEqual((result["accepted_chunks"], result["observed_inputs"]), (1, 3))
        self.assertEqual(result["retained_chunk_cpu_ns"], 70)
        self.assertEqual(result["retained_chunk_wall_lower_ns"], 100)
        self.assertEqual(result["retained_chunk_wall_upper_ns"], 120)
        self.assertEqual(result["stages"]["shader_engine_run"]["instrumented_wall_ns"], 30)
        self.assertEqual((result["fused_misses"], result["legacy_misses"]), (1, 1))

    def test_boundary_has_counts_but_no_interior_time(self):
        report = fixture()
        report["mode"] = "boundary"
        report["records"][0]["stage_wall_ns"] = [0] * 6
        result = self.accepted(report)
        self.assertEqual(result["accepted_chunks"], 1)
        self.assertIsNone(result["stages"]["shader_engine_run"]["instrumented_wall_ns"])
        self.rejected(lambda r: r.update(mode="boundary"))

    def test_empty_is_unknown_cost(self):
        report = fixture()
        report["records"] = []
        report["summary"].update(records=0, reserved_vertices=0, selected_draw_inputs=0)
        result = self.accepted(report)
        self.assertIsNone(result["retained_chunk_cpu_ns"])
        self.assertIsNone(result["retained_chunk_wall_lower_ns"])

    def test_clock_phase_and_window_exclusions(self):
        for key in ("begun", "completed", "clocks_valid", "phase_stable"):
            with self.subTest(key=key):
                report = fixture()
                report["records"][0][key] = False
                if key == "begun":
                    report["records"][0]["clocks_valid"] = False
                self.assertEqual(self.accepted(report)["accepted_chunks"], 0)
        report = fixture()
        report["records"][0]["overhang"] = True
        report["records"][0]["end"] = [10_000_000_100, 170, 10_000_000_101]
        self.assertEqual(self.accepted(report)["exclusion_reasons_may_overlap"], {"overhang": 1})

    def test_impossible_cpu_has_no_speed_claim(self):
        report = fixture()
        report["records"][0]["end"][1] = 221
        result = self.accepted(report)
        self.assertEqual(result["accepted_chunks"], 0)
        self.assertEqual(result["exclusion_reasons_may_overlap"], {"cpu_exceeds_outer_wall": 1})

    def test_counts_bounds_routes_and_tokens(self):
        changes = [
            lambda r: r["summary"].update(records=2),
            lambda r: r["summary"].update(reserved_vertices=4),
            lambda r: r["summary"].update(selected_draw_inputs=101),
            lambda r: r["summary"].update(sparse_suppressed=2),
            lambda r: r["summary"].update(window_end_ns=2000),
            lambda r: r.update(owner_tid=0),
            lambda r: r["records"][0].update(first_input=99),
            lambda r: r["records"][0].update(input_count=65),
            lambda r: r["records"][0].update(draw_count=4097),
            lambda r: r["records"][0].update(hits=2),
            lambda r: r["records"][0].update(fused_misses=2),
            lambda r: r["records"][0].update(fused_plan=False),
            lambda r: r["records"][0].update(token_after=19),
            lambda r: r["records"][0].update(start_offset_ns=99),
            lambda r: r["records"][0].update(stage_calls=[3, 2, 1, 2, 3, 3]),
            lambda r: r["records"][0].update(stage_wall_ns=[10, 20, 300, 15, 5, 10]),
            lambda r: r["records"][0].update(end=[310, 99, 320]),
            lambda r: r["records"][0].update(indexed=1),
            lambda r: r["config"].update(max_chunks=True),
            lambda r: r["clock"].update(cpu="CLOCK_PROCESS_CPUTIME_ID"),
            lambda r: r.update(stage_order=list(reversed(vt.STAGES))),
            lambda r: r.update(schema=2),
            lambda r: r.update(diagnostic_id="../replace"),
        ]
        for index, change in enumerate(changes):
            with self.subTest(index=index):
                self.rejected(change)

    def test_bad_json_and_size(self):
        valid = json.dumps(fixture()).encode()
        for data in (b"", b" " * (vt.MAX_BYTES + 1), valid[:-1],
                     b'{"schema":1,"schema":1}', valid.replace(b'"run": 1', b'"run": NaN'),
                     b"[" * 1200 + b"0" + b"]" * 1200):
            with self.subTest(length=len(data)):
                with self.assertRaises(vt.TimingError):
                    vt.decode(data)

    def test_no_mutation(self):
        report = fixture()
        before = copy.deepcopy(report)
        self.accepted(report)
        self.assertEqual(report, before)


if __name__ == "__main__":
    unittest.main()
