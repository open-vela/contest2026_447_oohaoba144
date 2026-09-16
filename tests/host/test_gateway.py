import io
import contextlib
import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "gateway"))
import velaguard_gateway as gateway

class GatewayTests(unittest.TestCase):
    def test_mock(self):
        task = gateway.parse_mock("1分钟后提醒我喝水", 1000)
        self.assertEqual(task, {"title": "喝水", "delay_seconds": 60, "priority": 1})
        for value in ["明天提醒我喝水", "0秒后提醒我喝水", "1秒后提醒我", "999小时后提醒我走动"]:
            with self.assertRaises(ValueError):
                gateway.parse_mock(value, 1000)

    def test_validation(self):
        good = {"title": "喝水", "due_epoch": 1060, "priority": 1}
        self.assertEqual(gateway.validate_task(good, 1000), good)
        for changes in [{"due_epoch": True}, {"priority": 1.0}, {"title": "x\0y"},
                        {"extra": 1}, {"due_epoch": 999}, {"title": "水"*65}]:
            with self.assertRaises(ValueError):
                gateway.validate_task(dict(good, **changes), 1000)

    def test_exchange_fragments(self):
        class Port:
            def __init__(self):
                self.chunks = [b"boot diagnostic\n", b'{"version":1,"request_',
                               b'id":"r1","type":"response.ok","payload":{}}\n']
                self.sent = []
            def write(self, raw): self.sent.append(raw)
            def flush(self): pass
            def readline(self, limit): return self.chunks.pop(0) if self.chunks else b""
        port = Port()
        response = gateway.exchange(port, gateway.envelope("device.status", {}, "r1"), timeout=0.02, retries=0)
        self.assertEqual(response["type"], "response.ok")
        self.assertEqual(len(port.sent), 1)

    def test_exchange_leading_sgr(self):
        class Port:
            def __init__(self, chunks): self.chunks = list(chunks)
            def write(self, raw): pass
            def flush(self): pass
            def readline(self, limit): return self.chunks.pop(0) if self.chunks else b""
        message = gateway.envelope("task.list", {"offset": 0}, "sgr-test")
        response = {"version": 1, "request_id": "sgr-test", "type": "response.ok",
                    "payload": {"title": "喝水", "literal": r"\u001b[0m", "escape": "\x1b[0m"}}
        encoded = json.dumps(response, ensure_ascii=False).encode("utf-8")
        cut = encoded.index("喝".encode("utf-8")) + 1
        port = Port([b"[cron] diagnostic\r\n\x1b[", b"0m \t\x1b[2m", encoded[:cut], encoded[cut:]+b"\r\n"])
        self.assertEqual(gateway.exchange(port, message, timeout=0.02, retries=0), response)
        fallback = dict(response, type="response.error")
        for bad_prefix in (b"log ", b"\x1b[2J", b"\x1b]title\x07", b"\x1b[0m[cron] "):
            with self.subTest(prefix=bad_prefix):
                port = Port([bad_prefix+encoded+b"\n", json.dumps(fallback).encode()+b"\n"])
                self.assertEqual(gateway.exchange(port, message, timeout=0.02, retries=0), fallback)
    def test_serial_open_without_reset_lines(self):
        class Serial:
            def __init__(self, **kwargs):
                self.kwargs = kwargs
                self.rts = self.dtr = True
                self.port = kwargs.get("port")
            def open(self):
                self.open_state = self.rts, self.dtr, self.port
        import types
        with patch.dict("sys.modules", {"serial": types.SimpleNamespace(Serial=Serial)}):
            port = gateway.open_serial("COM-test", 1000000)
        self.assertEqual(port.open_state, (False, False, "COM-test"))
        self.assertIsNone(port.kwargs["port"])

    @unittest.skipUnless(sys.platform == "win32", "Windows native fallback")
    def test_native_fallback_preserves_sync_order(self):
        import types
        with patch.dict("sys.modules", {"serial": None}), patch.object(sys, "argv",
             ["gateway", "--port", "COM-test", "--sync-time"]), patch("subprocess.run",
             return_value=types.SimpleNamespace(returncode=0)) as run:
            gateway.main()
        args, kwargs = run.call_args
        self.assertIn("COM-test", args[0])
        messages = [json.loads(line) for line in kwargs["input"].splitlines()]
        self.assertEqual([item["type"] for item in messages], ["device.time", "device.status"])
        self.assertEqual(kwargs["creationflags"], subprocess.CREATE_NO_WINDOW)

    def test_cli_always_emits_utf8(self):
        result = subprocess.run([sys.executable, "-B", str(ROOT / "gateway" / "velaguard_gateway.py"),
                                 "--text", "1分钟后提醒我喝水"], capture_output=True,
                                env=dict(os.environ, PYTHONIOENCODING="cp936"), timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout.decode("utf-8"))["payload"]["title"], "喝水")

    def test_mimo_fixture(self):
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, limit):
                return json.dumps({"choices": [{"message": {"content":
                    '{"title":"喝水","due_epoch":1060,"priority":1}'}}]}).encode()
        env = {"MIMO_API_KEY": "test-fixture-only", "MIMO_BASE_URL": "https://example.invalid/v1", "MIMO_MODEL": "fixture"}
        with patch.dict("os.environ", env), patch("urllib.request.urlopen", return_value=Response()):
            self.assertEqual(gateway.parse_mimo("提醒", 1000)["due_epoch"], 1060)
        with self.assertRaises(ValueError):
            gateway.strict_json('{"title":"a","title":"b"}')

    def test_file_process_loop(self):
        exe = pathlib.Path(os.environ.get("VELAGUARD_GATEWAY_DEVICE", str(pathlib.Path(tempfile.gettempdir()) / "velaguard-host-tests" / "gateway_device.exe")))
        directory = tempfile.mkdtemp(prefix="velaguard-gateway-")
        print("process evidence preserved:", directory)
        def run(now, messages):
            data = "".join(json.dumps(m, ensure_ascii=False)+"\n" for m in messages)
            p = subprocess.run([str(exe), directory, str(now)], input=data, capture_output=True,
                               encoding="utf-8", timeout=20)
            self.assertEqual(p.returncode, 0, p.stderr)
            return [json.loads(line) for line in p.stdout.splitlines()]
        def cmd(kind, rid, payload):
            return gateway.envelope(kind, payload, rid)
        create = cmd("task.create", "water", {"title":"喝水","due_epoch":1010,"priority":1})
        snooze = cmd("task.snooze","s1",{"task_id":"water","seconds":60,"expected_due_epoch":1010})
        responses = run(1000, [
            create, create, cmd("test.advance","t1",{"epoch":1010}),
            snooze, snooze, cmd("test.advance","t2",{"epoch":1070}),
            cmd("task.ack","a1",{"task_id":"water"}),
            cmd("event.sync","list",{"offset":0})])
        self.assertTrue(all(r["type"]=="response.ok" for r in responses),responses)
        self.assertEqual(responses[0],responses[1])
        self.assertEqual(responses[3],responses[4])
        task = responses[-1]["payload"]["task"]
        self.assertEqual((task["state"],task["snooze_count"]),("ACKNOWLEDGED",1))
        reboot = run(1070,[create,cmd("device.status","status",{}),cmd("event.sync","list2",{"offset":0})])
        self.assertEqual(reboot[0],responses[0])
        self.assertEqual(reboot[1]["payload"]["active_count"],0)
        self.assertEqual(reboot[1]["payload"]["history_count"],1)
        self.assertEqual(reboot[2]["payload"]["task"],task)


    def cli(self, *args):
        output = io.StringIO()
        with patch.object(sys, "argv", ["gateway", *args]), contextlib.redirect_stdout(output):
            gateway.main()
        return [json.loads(line) for line in output.getvalue().splitlines()]

    def test_relative_validation_and_mock_no_clock(self):
        good = {"title": "water", "delay_seconds": 60, "priority": 1}
        self.assertEqual(gateway.validate_task(good, None), good)
        for changes in ({"delay_seconds": True}, {"delay_seconds": 1.0},
                        {"delay_seconds": 0}, {"delay_seconds": 86401},
                        {"due_epoch": 1060}, {"extra": 0}, {"priority": False}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                gateway.validate_task(dict(good, **changes), 1000)
        for now in (None, 0, 2147483647):
            self.assertEqual(gateway.parse_mock("1分钟后提醒我water", now), good)
        with patch.object(gateway.time, "time", side_effect=AssertionError("wall clock used")), \
             patch.object(gateway.urllib.request, "urlopen", side_effect=AssertionError("network used")):
            messages = self.cli("--text", "1分钟后提醒我water", "--request-id", "offline")
        self.assertEqual(messages, [gateway.envelope("task.create", good, "offline")])

    def test_revision_cli_and_saved_envelope(self):
        for value in ("1", "9007199254740993", "18446744073709551615"):
            for kind in ("task.snooze", "task.rearm"):
                message = self.cli("--command", kind, "--task-id", "water",
                                   "--expected-revision", value)[0]
                self.assertEqual(message["payload"]["expected_revision"], value)
                self.assertNotIn("expected_due_epoch", message["payload"])
        bad = ("0", "01", "+1", "-1", "1.0", "1e1", " 1", "18446744073709551616")
        for value in bad:
            with self.subTest(value=value), contextlib.redirect_stderr(io.StringIO()), self.assertRaises((ValueError,SystemExit)):
                self.cli("--command", "task.rearm", "--task-id", "water", "--expected-revision", value)
        for args in (("--command", "task.snooze", "--task-id", "water"),
                     ("--command", "task.snooze", "--task-id", "water", "--expected-revision", "1", "--expected-due-epoch", "1000"),
                     ("--command", "task.rearm", "--task-id", "water", "--expected-due-epoch", "1000")):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises((ValueError,SystemExit)):
                self.cli(*args)
        directory = pathlib.Path(tempfile.mkdtemp(prefix="velaguard-gateway-envelope-"))
        saved = directory / "rearm.json"
        original = self.cli("--command", "task.rearm", "--task-id", "water", "--expected-revision", "1",
                            "--request-id", "rearm-once", "--save-request", str(saved))[0]
        with patch.object(gateway.time, "time", side_effect=AssertionError("replay clock used")):
            self.assertEqual(self.cli("--request-file", str(saved), "--command", "task.rearm"), [original])
        with self.assertRaises(FileExistsError):
            self.cli("--request-file", str(saved), "--save-request", str(saved))

    def test_mimo_relative_fixture(self):
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, limit):
                return json.dumps({"choices": [{"message": {"content":
                    '{"title":"water","delay_seconds":60,"priority":1}'}}]}).encode()
        env = {"MIMO_API_KEY":"test-fixture-only", "MIMO_BASE_URL":"https://example.invalid/v1", "MIMO_MODEL":"fixture"}
        with patch.dict(os.environ, env), patch("urllib.request.urlopen", return_value=Response()) as request:
            self.assertEqual(gateway.parse_mimo("remind in a minute", 1000)["delay_seconds"], 60)
        prompt = json.loads(request.call_args.args[0].data)["messages"][0]["content"]
        self.assertIn("delay_seconds", prompt)
        self.assertIn("due_epoch", prompt)

    def test_relative_process_recovery(self):
        exe = pathlib.Path(os.environ.get("VELAGUARD_GATEWAY_DEVICE", str(pathlib.Path(tempfile.gettempdir()) / "velaguard-host-tests" / "gateway_device.exe")))
        directory = tempfile.mkdtemp(prefix="velaguard-gateway-relative-")
        print("relative process evidence preserved:", directory)
        run_number = 0
        def run(mono, messages):
            nonlocal run_number
            run_number += 1
            data = "".join(json.dumps(m)+"\n" for m in messages)
            p = subprocess.run([str(exe), directory, "0", str(mono), "0"], input=data,
                               capture_output=True, encoding="utf-8", timeout=20)
            evidence = pathlib.Path(directory) / ("process-"+str(run_number))
            evidence.with_suffix(".input.jsonl").write_text(data, encoding="utf-8")
            evidence.with_suffix(".output.jsonl").write_text(p.stdout, encoding="utf-8")
            self.assertEqual(p.returncode, 0, p.stderr)
            return [json.loads(line) for line in p.stdout.splitlines()]
        def cmd(kind, rid, **payload): return gateway.envelope(kind, payload, rid)
        def task(response): return response["payload"]["task"]
        create = cmd("task.create", "relative", title="water", delay_seconds=60, priority=1)
        query = cmd("task.list", "list", offset=0)
        rearm1 = cmd("task.rearm", "rearm1", task_id="relative", expected_revision="1")
        a = run(1000, [create, create, query, cmd("device.status", "status")])
        self.assertEqual(a[0], a[1]); self.assertEqual(task(a[2])["mono_deadline_ms"], "61000")
        self.assertEqual(task(a[2])["timer_revision"], "1")
        boot_a = int(a[3]["payload"]["active_boot_id"])
        b = run(500, [query, rearm1, cmd("test.advance", "set", mono_ms="1500", fire=False), rearm1,
                      query, cmd("device.reload", "reload"), cmd("device.time", "time", epoch=1000),
                      cmd("device.status", "status2"), query,
                      cmd("test.advance", "count-before", fire=False), query,
                      cmd("device.status", "readonly-status"), cmd("event.sync", "readonly-history", offset=0),
                      cmd("test.advance", "count-after", fire=False)])
        self.assertEqual(task(b[0])["state"], "NEEDS_RESET")
        self.assertEqual(b[1], b[3]); self.assertEqual(task(b[4]), task(b[8]))
        self.assertEqual((task(b[4])["mono_deadline_ms"],task(b[4])["timer_revision"]), ("60500","2"))
        self.assertEqual(int(b[7]["payload"]["active_boot_id"]), boot_a+1)
        self.assertEqual(b[9]["payload"]["scheduler_calls"], b[13]["payload"]["scheduler_calls"])
        self.assertEqual(task(b[8]), task(b[10]))
        rearm2 = cmd("task.rearm", "rearm2", task_id="relative", expected_revision="2")
        snooze = cmd("task.snooze", "snooze", task_id="relative", seconds=30, expected_revision="3")
        c = run(100, [rearm1, create, query, rearm2,
                      cmd("test.advance", "set-due", mono_ms="60100", fire=False), query,
                      cmd("test.advance", "fire", fire=True), query, snooze,
                      cmd("test.advance", "set-later", mono_ms="61100", fire=False), snooze, query,
                      cmd("task.snooze", "stale", task_id="relative", seconds=30, expected_revision="3"),
                      cmd("test.advance", "fire2", mono_ms="90100", fire=True),
                      cmd("task.ack", "ack", task_id="relative"), cmd("event.sync", "events", offset=0)])
        self.assertEqual(c[0]["type"], "response.error")
        self.assertEqual(task(c[2])["state"], "NEEDS_RESET")
        self.assertEqual(task(c[5])["state"], "SCHEDULED") # query does not advance due task
        self.assertEqual(task(c[7])["state"], "ALERTING")
        self.assertEqual(c[8], c[10]); self.assertEqual(c[12]["type"], "response.error")
        self.assertEqual((task(c[11])["timer_revision"],task(c[11])["mono_deadline_ms"],task(c[11])["delay_seconds"]), ("4","90100",60))
        history = task(c[-1])
        self.assertEqual((history["state"],history["snooze_count"],history["acknowledged_epoch"]), ("ACKNOWLEDGED",1,0))
        d = run(50, [create, cmd("task.ack", "ack-again", task_id="relative"),
                     cmd("device.status", "status3"), cmd("event.sync", "events2", offset=0)])
        self.assertTrue(all(r["type"] == "response.ok" for r in d), d)
        self.assertEqual((d[2]["payload"]["active_count"],d[2]["payload"]["history_count"]),(0,1))
        self.assertEqual(task(d[3]), history)


    def test_retry_keeps_envelope_and_uncertain(self):
        message = gateway.envelope("task.snooze", {"task_id":"water", "seconds":30,
                                                   "expected_revision":"9007199254740993"}, "retry-once")
        uncertain = gateway.envelope("response.error", {"code":"STORAGE_ERROR", "uncertain":True}, "retry-once")
        class Port:
            def __init__(self): self.sent = []; self.returned = False
            def write(self, raw): self.sent.append(raw)
            def flush(self): pass
            def readline(self, limit):
                if len(self.sent) == 2 and not self.returned:
                    self.returned = True
                    return json.dumps(uncertain).encode()+b"\n"
                return b""
        port = Port()
        self.assertEqual(gateway.exchange(port, message, timeout=0.005, retries=1), uncertain)
        self.assertEqual(len(port.sent), 2)
        self.assertEqual(port.sent[0], port.sent[1])
        self.assertEqual(json.loads(port.sent[0]), message)

    def test_old_boot_alerting_controls(self):
        exe = pathlib.Path(os.environ.get("VELAGUARD_GATEWAY_DEVICE", str(pathlib.Path(tempfile.gettempdir()) / "velaguard-host-tests" / "gateway_device.exe")))
        for action in ("ack", "snooze"):
            with self.subTest(action=action):
                directory = pathlib.Path(tempfile.mkdtemp(prefix="velaguard-gateway-old-alert-"))
                print("old boot alert evidence preserved:", directory)
                run_number = 0
                def run(mono, messages):
                    nonlocal run_number
                    run_number += 1
                    data = "".join(json.dumps(m)+"\n" for m in messages)
                    result = subprocess.run([str(exe), str(directory), "0", str(mono), "0"],
                                            input=data, capture_output=True, encoding="utf-8", timeout=20)
                    (directory / (str(run_number)+".input.jsonl")).write_text(data, encoding="utf-8")
                    (directory / (str(run_number)+".output.jsonl")).write_text(result.stdout, encoding="utf-8")
                    self.assertEqual(result.returncode, 0, result.stderr)
                    responses = [json.loads(line) for line in result.stdout.splitlines()]
                    self.assertTrue(all(r["type"] == "response.ok" for r in responses), responses)
                    return responses
                def cmd(kind, rid, **payload): return gateway.envelope(kind, payload, rid)
                query = cmd("task.list", "query", offset=0)
                first = run(1000, [cmd("task.create", "oldalert", title="water", delay_seconds=60, priority=1),
                                   cmd("test.advance", "fire", mono_ms="61000"), query])
                before = first[-1]["payload"]["task"]
                self.assertEqual(before["state"], "ALERTING")
                control = cmd("task.ack", "ack", task_id="oldalert") if action == "ack" else cmd(
                    "task.snooze", "snooze", task_id="oldalert", seconds=30, expected_revision="1")
                messages = [query, cmd("test.advance", "still-alert", mono_ms="500", fire=True), query,
                            control, cmd("event.sync" if action == "ack" else "task.list", "after", offset=0)]
                second = run(100, messages)
                self.assertEqual(second[0]["payload"]["task"], before)
                self.assertEqual(second[2]["payload"]["task"], before)
                after = second[-1]["payload"]["task"]
                if action == "ack":
                    self.assertEqual((after["state"],after["acknowledged_epoch"]), ("ACKNOWLEDGED",0))
                else:
                    self.assertEqual((after["state"],after["timer_revision"],after["mono_deadline_ms"],
                                      after["delay_seconds"],after["snooze_count"]), ("SCHEDULED","2","30500",60,1))
                    self.assertGreater(int(after["timer_boot_id"]), int(before["timer_boot_id"]))


    def test_actual_print_and_native_wire_limit(self):
        directory = pathlib.Path(tempfile.mkdtemp(prefix="velaguard-gateway-wire-"))
        saved = directory / "boundary.json"
        message = gateway.envelope("task.create", {"title":"", "delay_seconds":60,"priority":1}, "wire")
        base = len(json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode())
        message["payload"]["title"] = "x"*(1024-base)
        saved.write_text(json.dumps(message), encoding="utf-8")
        output = io.StringIO()
        with patch.object(sys, "argv", ["gateway", "--request-file", str(saved)]), contextlib.redirect_stdout(output):
            gateway.main()
        raw = output.getvalue().rstrip("\n").encode("utf-8")
        self.assertEqual(len(raw), 1024)
        self.assertEqual(json.loads(raw), message)
        if sys.platform == "win32":
            import types
            with patch.dict("sys.modules", {"serial":None}), patch.object(sys,"argv",
                 ["gateway", "--request-file", str(saved), "--port", "COM-test"]), patch("subprocess.run",
                 return_value=types.SimpleNamespace(returncode=0)) as run:
                gateway.main()
            self.assertEqual(run.call_args.kwargs["input"].rstrip("\n").encode("utf-8"), raw)
        message["payload"]["title"] += "x"
        saved.write_text(json.dumps(message), encoding="utf-8")
        with self.assertRaises(ValueError): self.cli("--request-file", str(saved))

if __name__ == "__main__":
    unittest.main(verbosity=2)
