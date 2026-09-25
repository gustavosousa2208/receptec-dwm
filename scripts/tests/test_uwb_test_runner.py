"""Unit tests for scripts/uwb-test-runner.py: parser and suite logic, no hardware.

Run:  python3 -m unittest discover -s scripts/tests   (from the repository root)
"""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "uwb_test_runner", Path(__file__).resolve().parent.parent / "uwb-test-runner.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)

PASS_LINE = "UWB_TEST result=PASS test=bad_dt slot=0 offset-us=60000"
FAIL_LINE = "UWB_TEST result=FAIL test=bad_dt slot=0 reason=unexpected-response"


class Fake:
    """Transport whose replies are a dict: command -> text (or an exception to raise)."""

    def __init__(self, replies, clock=None, advance=0.0, drain=""):
        self.replies, self.sent, self.clock, self.advance, self._drain = replies, [], clock, advance, drain

    def run(self, command, timeout, stop_re=None):
        self.sent.append(command)
        if self.clock is not None:
            self.clock[0] += self.advance if command.startswith("uwb test") else 0.0
        reply = self.replies.get(command, "")
        if isinstance(reply, Exception):
            raise reply
        return reply

    def drain(self, seconds):
        return self._drain

    def close(self):
        pass


class ParseTest(unittest.TestCase):
    def test_pass_line(self):
        f = runner.parse_result_line(PASS_LINE)
        self.assertEqual((f["result"], f["test"], f["slot"], f["offset-us"]), ("PASS", "bad_dt", "0", "60000"))

    def test_fail_line_needs_a_reason(self):
        self.assertEqual(runner.parse_result_line(FAIL_LINE)["reason"], "unexpected-response")
        with self.assertRaises(runner.ParseError):
            runner.parse_result_line("UWB_TEST result=FAIL test=bad_dt slot=0")

    def test_malformed_lines(self):
        for bad in ("UWB_TEST result=MAYBE test=x slot=0", "UWB_TEST result=PASS test=x",
                    "UWB_TEST result=PASS test=x slot=9", "UWB_TEST result=PASS test=x slot=0 oops",
                    "UWB_TEST result=PASS test=X slot=0", "UWB_TEST result=PASS test=x slot=0 a=1 a=2",
                    "hello"):
            with self.assertRaises(runner.ParseError, msg=bad):
                runner.parse_result_line(bad)

    def test_shell_messages_that_mention_the_name_are_not_results(self):
        armed = "test bad_dt armed on slot 0: warm-up, test, verify; the result is one UWB_TEST line\n"
        self.assertEqual(runner.find_results(armed), [])
        self.assertEqual(runner.judge(armed + PASS_LINE, "bad_dt", 0)[:2], ("PASS", "ok"))

    def test_a_line_split_across_two_reads_is_one_result(self):
        first, second = "UWB_TEST result=PASS test=bad_length slot=0 leng", "th=21 response=none"
        self.assertEqual(runner.judge(first + second, "bad_length", 0)[:2], ("PASS", "ok"))

    def test_two_results_without_a_newline_between_them_are_two(self):
        self.assertEqual(len(runner.find_results(PASS_LINE + PASS_LINE)), 2)

    def test_ansi_and_prompt_noise_is_ignored(self):
        text = "\x1b[1;32mrtt:~$ \x1b[m" + PASS_LINE + "\r\n"
        self.assertEqual(len(runner.find_results(text)), 1)


class JudgeTest(unittest.TestCase):
    def test_pass(self):
        self.assertEqual(runner.judge(PASS_LINE, "bad_dt", 0)[:2], ("PASS", "ok"))

    def test_fail_keeps_the_firmware_reason(self):
        self.assertEqual(runner.judge(FAIL_LINE, "bad_dt", 0)[:2], ("FAIL", "unexpected-response"))

    def test_no_result(self):
        self.assertEqual(runner.judge("some log only", "bad_dt", 0)[:2], ("FAIL", "no-result"))

    def test_duplicate_result_is_a_failure_even_when_both_pass(self):
        self.assertEqual(runner.judge(PASS_LINE + "\n" + PASS_LINE, "bad_dt", 0)[:2], ("FAIL", "duplicate-result"))

    def test_wrong_test_or_slot(self):
        self.assertEqual(runner.judge(PASS_LINE, "duplicate", 0)[:2], ("FAIL", "wrong-test"))
        self.assertEqual(runner.judge(PASS_LINE, "bad_dt", 1)[:2], ("FAIL", "wrong-test"))

    def test_malformed_result(self):
        self.assertEqual(runner.judge("UWB_TEST result=PASS test=bad_dt", "bad_dt", 0)[:2], ("FAIL", "malformed-result"))


class SkipAndShellErrorTest(unittest.TestCase):
    def test_not_implemented_is_a_skip_and_never_a_pass(self):
        text = "test rx_boundary is not implemented (see docs/uwb-runtime-tests.md)"
        self.assertEqual(runner.judge(text, "rx_boundary", 0)[:2], ("SKIP", "not-implemented"))
        s = runner.run_suite(Fake({"uwb test rx_boundary 0": text}), 0, [("rx_boundary", [])])
        self.assertFalse(s["ok"])
        self.assertEqual((s["passed"], s["skipped"]), (0, 1))
        self.assertIn("INCOMPLETE", runner.render(s))

    def test_shell_refusals_are_named_failures(self):
        self.assertEqual(runner.judge("slot 0 has no registered peer", "bad_dt", 0)[:2], ("FAIL", "no-peer"))
        self.assertEqual(runner.judge("a poll or a test is running: uwb test abort", "bad_dt", 0)[:2],
                         ("FAIL", "busy"))

    def test_one_skip_among_passes_keeps_the_suite_incomplete(self):
        replies = {"uwb test bad_dt 0": PASS_LINE, "uwb test discovery 0": "test discovery is not implemented"}
        s = runner.run_suite(Fake(replies), 0, [("bad_dt", []), ("discovery", [])])
        self.assertEqual((s["passed"], s["skipped"], s["ok"]), (1, 1, False))


class EvidenceTest(unittest.TestCase):
    STATUS = "I:   UWB: state=3 rx=%d err/to=0/133 tx=%d/0/0 late=0 irq=1/2us delay=0us phase=0/0/0/0us"

    def test_parse_counters(self):
        c = runner.parse_coletec_counters(self.STATUS % (100, 90))
        self.assertEqual(c, {"rx": 100, "err": 0, "to": 133, "tx": 90})
        self.assertIsNone(runner.parse_coletec_counters("nothing here"))

    def test_delta(self):
        d = runner.evidence_delta({"rx": 10, "err": 0, "to": 1, "tx": 10}, {"rx": 14, "err": 0, "to": 3, "tx": 13})
        self.assertEqual((d["rx"], d["tx"], d["unanswered"], d["to"]), (4, 3, 1, 2))
        self.assertIsNone(runner.evidence_delta(None, {"rx": 1, "err": 0, "to": 0, "tx": 1}))

    def suite(self, name, snaps):
        class T(Fake):
            def fresh_snapshot(self, timeout=9.0):
                return snaps.pop(0)
        line = PASS_LINE.replace("bad_dt", name)
        return runner.run_suite(T({f"uwb test {name} 0": line}), 0, [(name, [])])

    def test_mutation_with_an_unanswered_frame_passes_and_records_it(self):
        s = self.suite("bad_dt", [{"rx": 10, "err": 0, "to": 0, "tx": 10}, {"rx": 14, "err": 0, "to": 0, "tx": 13}])
        self.assertTrue(s["ok"])
        self.assertEqual(s["results"][0]["coletec"]["unanswered"], 1)

    def test_mutation_silence_without_a_received_frame_is_a_failure(self):
        s = self.suite("bad_dt", [{"rx": 10, "err": 0, "to": 0, "tx": 10}, {"rx": 13, "err": 0, "to": 0, "tx": 13}])
        self.assertFalse(s["ok"])
        self.assertEqual(s["results"][0]["reason"], "no-evidence-frame-received")

    def test_non_mutation_tests_only_record_the_evidence(self):
        s = self.suite("duplicate", [{"rx": 10, "err": 0, "to": 0, "tx": 10}, {"rx": 12, "err": 0, "to": 0, "tx": 12}])
        self.assertTrue(s["ok"])
        self.assertEqual(s["results"][0]["coletec"]["unanswered"], 0)

    def test_missing_evidence_does_not_block_a_pass(self):
        s = self.suite("bad_dt", [None, None])
        self.assertTrue(s["ok"])
        self.assertNotIn("coletec", s["results"][0])


class DiscoveryEvidenceTest(unittest.TestCase):
    LINE = "UWB_TEST result=PASS test=discovery slot=0 silence-cycles=6 probes=3 transition=inferred"

    def suite(self, coletec_text):
        class T(Fake):
            def mark(self):
                return 0

            def text_since(self, mark):
                return coletec_text
        return runner.run_suite(T({"uwb test discovery 0": self.LINE}), 0, [("discovery", [])])

    def test_count_events(self):
        text = "UWB synchronization lost: entering discovery x\nUWB discovery OFF: cycle=1\nUWB discovery reentry: sequence=9\n"
        self.assertEqual(runner.count_coletec_events(text),
                         {"entering-discovery": 1, "discovery-off": 1, "reentry": 1, "reentry-after-entry": 1})

    def test_pass_needs_the_coletec_to_report_entry_and_reentry(self):
        both = "UWB synchronization lost: entering discovery\nUWB discovery reentry: sequence=9\n"
        self.assertTrue(self.suite(both)["ok"])

    def test_a_reentry_before_the_entry_does_not_count(self):
        wrong_order = ("UWB discovery reentry: sequence=1\n"
                       "UWB synchronization lost: entering discovery\n")
        s = self.suite(wrong_order)
        self.assertFalse(s["ok"])
        self.assertEqual(s["results"][0]["reason"], "no-evidence-discovery")
        warm_up_then_real = ("UWB discovery reentry: sequence=1\n"
                             "UWB synchronization lost: entering discovery\n"
                             "UWB discovery reentry: sequence=9\n")
        self.assertTrue(self.suite(warm_up_then_real)["ok"])

    def test_silence_then_an_answer_without_coletec_lines_is_not_a_pass(self):
        s = self.suite("")
        self.assertFalse(s["ok"])
        self.assertEqual(s["results"][0]["reason"], "no-evidence-discovery")
        s = self.suite("UWB synchronization lost: entering discovery\n")
        self.assertEqual(s["results"][0]["reason"], "no-evidence-discovery")


class SuiteTest(unittest.TestCase):
    def suite_one(self, replies, **kw):
        sel = [("bad_dt", [])]
        return runner.run_suite(Fake(replies, **kw), 0, sel)

    def test_all_pass(self):
        s = self.suite_one({"uwb test bad_dt 0": PASS_LINE})
        self.assertTrue(s["ok"])
        self.assertEqual((s["passed"], s["ran"], s["cleanup"]), (1, 1, "confirmed"))

    def test_one_failure_fails_the_run(self):
        s = self.suite_one({"uwb test bad_dt 0": FAIL_LINE})
        self.assertFalse(s["ok"])
        self.assertEqual(s["results"][0]["reason"], "unexpected-response")

    def test_timeout_is_reported_as_runner_timeout(self):
        clock = [0.0]
        s = runner.run_suite(Fake({"uwb test bad_dt 0": "no line"}, clock=clock, advance=20.0), 0,
                             [("bad_dt", [])], clock=lambda: clock[0])
        self.assertEqual(s["results"][0]["reason"], "runner-timeout")
        self.assertFalse(s["ok"])

    def test_second_result_after_the_first_is_caught(self):
        s = runner.run_suite(Fake({"uwb test bad_dt 0": PASS_LINE}, drain=PASS_LINE), 0, [("bad_dt", [])])
        self.assertEqual(s["results"][0]["reason"], "duplicate-result")

    def test_disconnect_aborts_and_leaves_cleanup_unconfirmed(self):
        replies = {"uwb test bad_dt 0": runner.Disconnected("cable"), "uwb poll stop": runner.Disconnected("cable")}
        s = runner.run_suite(Fake(replies), 0, [("bad_dt", []), ("duplicate", [])])
        self.assertIn("disconnected", s["aborted"])
        self.assertEqual(s["cleanup"], "unconfirmed")
        self.assertFalse(s["ok"])
        self.assertEqual(s["ran"], 1)  # the second test never started

    def test_poll_is_stopped_before_each_test_and_at_the_end(self):
        fake = Fake({"uwb test bad_dt 0": PASS_LINE, "uwb test duplicate 0": PASS_LINE.replace("bad_dt", "duplicate")})
        runner.run_suite(fake, 0, [("bad_dt", []), ("duplicate", [])])
        self.assertEqual(fake.sent, ["uwb poll stop", "uwb test bad_dt 0", "uwb poll stop",
                                     "uwb test duplicate 0", "uwb poll stop"])

    def test_never_sends_anything_dangerous(self):
        fake = Fake({})
        runner.run_suite(fake, 0, list(runner.SUITE))
        for command in fake.sent:
            self.assertTrue(command.startswith(("uwb test ", "uwb poll stop")), command)
            self.assertNotIn("stop_peer", command)
            self.assertNotIn("reset", command)

    def test_arguments_are_part_of_the_command(self):
        fake = Fake({"uwb test retry 2 1": PASS_LINE.replace("bad_dt", "retry")})
        s = runner.run_suite(fake, 1, [("retry", ["2"])])
        self.assertIn("uwb test retry 2 1", fake.sent)
        self.assertEqual(s["results"][0]["reason"], "wrong-test")  # slot in the reply was 0, asked for 1


class SelectionAndFixtureTest(unittest.TestCase):
    def test_suite_order_matches_the_plan(self):
        names = [n + (" " + " ".join(a) if a else "") for n, a in runner.SUITE]
        self.assertEqual(names[:4], ["data_payload", "retry 1", "retry 2", "wrong_source"])
        self.assertEqual(names[-3:], ["sequence_wrap", "rx_boundary", "discovery"])

    def test_select_only_and_unknown(self):
        self.assertEqual([n for n, _ in runner.select("duplicate,bad_dt")], ["bad_dt", "duplicate"])
        with self.assertRaises(SystemExit):
            runner.select("nope")

    def test_dry_run_from_a_fixture(self):
        with tempfile.TemporaryDirectory() as d:
            fx = Path(d) / "t.json"
            fx.write_text(json.dumps({"commands": {"uwb poll stop": "", "uwb test bad_dt 0": PASS_LINE}}))
            s = runner.run_suite(runner.FixtureTransport(fx), 0, [("bad_dt", [])])
        self.assertTrue(s["ok"])

    def test_render_mentions_cleanup(self):
        s = runner.run_suite(Fake({"uwb test bad_dt 0": PASS_LINE}), 0, [("bad_dt", [])])
        self.assertIn("Cleanup", runner.render(s))


if __name__ == "__main__":
    unittest.main()
