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
