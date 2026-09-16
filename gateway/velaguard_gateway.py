"""VelaGuard JSON Lines gateway. Mock is offline; MiMo is explicit opt-in."""
import argparse
import json
import os
import pathlib
import subprocess
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

def strict_json(text):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    def invalid(_):
        raise ValueError("non-finite JSON number")
    return json.loads(text, object_pairs_hook=pairs, parse_constant=invalid)

def validate_task(task, now=None):
    if type(task) is not dict or set(task) not in ({"title", "due_epoch", "priority"},
                                                      {"title", "delay_seconds", "priority"}):
        raise ValueError("task must contain title, priority and exactly one of due_epoch or delay_seconds")
    title = task["title"]
    if (type(title) is not str or not title.strip() or len(title.encode("utf-8")) > 192
            or any(ord(char) < 32 for char in title)):
        raise ValueError("title must be printable UTF-8, 1..192 bytes")
    if "delay_seconds" in task:
        if type(task["delay_seconds"]) is not int or not 1 <= task["delay_seconds"] <= 86400:
            raise ValueError("delay_seconds must be an integer 1..86400")
    elif (type(now) is not int or type(task["due_epoch"]) is not int
          or not now < task["due_epoch"] <= min(now+86400*366, 2147483647)):
        raise ValueError("due_epoch must be a future integer within one year and device time range")
    if type(task["priority"]) is not int or task["priority"] not in (0, 1, 2):
        raise ValueError("priority must be an integer 0..2")
    return task

def parse_mock(text, now=None):
    match = re.fullmatch(r"\s*([0-9]+)\s*(秒|分钟|小时)后提醒我\s*(.+?)\s*", text)
    if not match:
        raise ValueError("Mock accepts: 1分钟后提醒我喝水 (digits; seconds/minutes/hours)")
    seconds = int(match[1]) * {"秒": 1, "分钟": 60, "小时": 3600}[match[2]]
    if not 1 <= seconds <= 86400:
        raise ValueError("Mock delay must be 1..86400 seconds")
    return validate_task({"title": match[3], "delay_seconds": seconds, "priority": 1})

def parse_mimo(text, now):
    key = os.environ.get("MIMO_API_KEY")
    base = os.environ.get("MIMO_BASE_URL", "").rstrip("/")
    model = os.environ.get("MIMO_MODEL")
    if not key or not model or urllib.parse.urlparse(base).scheme != "https":
        raise ValueError("MiMo needs local MIMO_API_KEY, HTTPS MIMO_BASE_URL and MIMO_MODEL")
    # The operator supplies an endpoint supporting the chat/completions contract.
    # Never auto-fallback after an ambiguous device write, and never log the key.
    body = {"model": model, "temperature": 0, "messages": [
        {"role": "system", "content":
         f"Return only JSON with title (UTF-8 <=192 bytes), priority (integer 0..2), and exactly one time field. "
         f"For relative requests use delay_seconds (integer 1..86400); do not convert it to a date. "
         f"For explicit dates use due_epoch (future integer, at most 366 days ahead and <=2147483647). "
         f"Current Unix epoch is {now}. Never include both time fields. Do not add fields."},
        {"role": "user", "content": text}]}
    request = urllib.request.Request(base+"/chat/completions",
                                    json.dumps(body).encode(), method="POST",
                                    headers={"Authorization": "Bearer "+key, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            raw = response.read(65537)
        if len(raw) > 65536:
            raise ValueError("MiMo response exceeds limit")
        content = strict_json(raw.decode("utf-8"))["choices"][0]["message"]["content"]
        return validate_task(strict_json(content), now)
    except (urllib.error.URLError, KeyError, IndexError, TypeError, UnicodeError) as exc:
        raise ValueError("MiMo request or structured response failed") from None

def envelope(kind, payload, request_id=None):
    return {"version": 1, "request_id": request_id or uuid.uuid4().hex,
            "type": kind, "payload": payload}

def open_serial(name, baud):
    import serial
    # Configure modem lines before opening the CH343; opening is an explicit CLI action.
    port = serial.Serial(port=None, baudrate=baud, timeout=0.2, write_timeout=5)
    port.rts = False
    port.dtr = False
    port.port = name
    port.open()
    return port

def exchange(port, message, timeout=5, retries=2):
    raw = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(raw) > 1024:
        raise ValueError("command exceeds 1024 UTF-8 bytes")
    pending = bytearray()
    discard = False
    for _ in range(retries+1):
        port.write(raw+b"\n")
        port.flush()
        deadline = time.monotonic()+timeout
        while time.monotonic() < deadline:
            chunk = port.readline(2049)
            for byte in chunk:
                if byte != 10:
                    if not discard:
                        if len(pending) >= 2048:
                            pending.clear()
                            discard = True
                        else:
                            pending.append(byte)
                    continue
                line = bytes(pending)
                pending.clear()
                if discard:
                    discard = False
                    continue
                try:
                    # Official cron logs may leave an SGR reset before the JSON.
                    # Strip only leading whitespace/SGR, never arbitrary log text.
                    line = re.sub(br"^(?:[ \t\r]|\x1b\[[0-9;]*m)*", b"", line)
                    result = strict_json(line.decode("utf-8"))
                except (ValueError, UnicodeError):
                    continue
                if (isinstance(result, dict) and type(result.get("version")) is int
                        and result["version"] == 1 and result.get("request_id") == message["request_id"]
                        and result.get("type") in ("response.ok", "response.error")):
                    return result
    raise TimeoutError("No matching response; outcome unknown. Retry the saved envelope or query first.")

def revision_string(value):
    if not re.fullmatch(r"[1-9][0-9]{0,19}", value) or int(value) > 18446744073709551615:
        raise argparse.ArgumentTypeError("expected revision must be a canonical string integer 1..18446744073709551615")
    return value

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="User-selected serial port, e.g. COM7; omitted means print JSON only")
    parser.add_argument("--baud", type=int, default=1000000)
    parser.add_argument("--parser", choices=["mock", "mimo"], default="mock")
    parser.add_argument("--sync-time", action="store_true", help="Explicitly set device RTC from this PC")
    parser.add_argument("--text", help="Example: 1分钟后提醒我喝水")
    parser.add_argument("--command", choices=["device.status","device.reload","task.list","event.sync","task.ack","task.snooze","task.rearm"], default="device.status")
    parser.add_argument("--task-id")
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--seconds", type=int, default=60)
    expected = parser.add_mutually_exclusive_group()
    expected.add_argument("--expected-due-epoch", type=int)
    expected.add_argument("--expected-revision", type=revision_string)
    parser.add_argument("--request-id", help="Creation ID; retries must reuse the complete saved envelope")
    parser.add_argument("--save-request", help="Write envelope to a new file for exact retries")
    parser.add_argument("--request-file", help="Resend an existing saved envelope, preserving ID and time/revision fields")
    args = parser.parse_args()
    if args.request_file and (args.text or args.request_id):
        parser.error("--request-file cannot be combined with --text or --request-id")
    if args.request_file:
        with open(args.request_file, encoding="utf-8") as source:
            saved = source.read(2049)
        if len(saved.encode("utf-8")) > 2048:
            raise ValueError("saved request too large")
        message = strict_json(saved)
        if (type(message) is not dict or set(message) != {"version","request_id","type","payload"}
                or type(message["version"]) is not int or message["version"] != 1
                or type(message["payload"]) is not dict or type(message["type"]) is not str
                or type(message["request_id"]) is not str
                or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", message["request_id"])):
            raise ValueError("invalid saved envelope")
    else:
        if args.text:
            payload = parse_mock(args.text) if args.parser == "mock" else parse_mimo(args.text, int(time.time()))
            kind = "task.create"
        else:
            kind = args.command
            payload = {"offset": args.offset} if kind in ("task.list","event.sync") else {}
            if kind in ("task.ack","task.snooze","task.rearm"):
                if not args.task_id:
                    parser.error("--task-id is required")
                payload = {"task_id": args.task_id}
            if kind == "task.snooze":
                if args.expected_due_epoch is None and args.expected_revision is None:
                    parser.error("one expected due epoch or revision from task.list is required")
                if not 1 <= args.seconds <= 86400:
                    parser.error("--seconds must be 1..86400")
                payload["seconds"] = args.seconds
                if args.expected_revision is not None:
                    payload["expected_revision"] = args.expected_revision
                else:
                    payload["expected_due_epoch"] = args.expected_due_epoch
            elif kind == "task.rearm":
                if args.expected_revision is None:
                    parser.error("--expected-revision from NEEDS_RESET task.list is required")
                payload["expected_revision"] = args.expected_revision
        message = envelope(kind, payload, args.request_id)
    # Validate the final wire size for print/native/pyserial and replay alike.
    if len(json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) > 1024:
        raise ValueError("command exceeds 1024 UTF-8 bytes")
    if args.save_request:
        with open(args.save_request, "x", encoding="utf-8") as destination:
            json.dump(message, destination, ensure_ascii=False, separators=(",", ":"))
    if not args.port:
        if args.sync_time:
            print(json.dumps(envelope("device.time", {"epoch": int(time.time())}), ensure_ascii=False, separators=(",", ":")))
        print(json.dumps(message, ensure_ascii=False, separators=(",", ":")))
        return
    try:
        import serial
    except ImportError:
        if os.name != "nt":
            parser.error("Serial mode requires pyserial in the current Python environment")
        messages = [message]
        if args.sync_time:
            messages.insert(0, envelope("device.time", {"epoch": int(time.time())}))
        lines = "".join(json.dumps(item, ensure_ascii=False, separators=(",", ":"))+"\n" for item in messages)
        result = subprocess.run(["pwsh", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                 "-File", str(pathlib.Path(__file__).with_name("serial_transport.ps1")),
                                 "-Port", args.port, "-Baud", str(args.baud)],
                                input=lines, text=True, encoding="utf-8",
                                creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise ValueError("Native serial transport failed; query before creating a new request ID")
        return
    with open_serial(args.port, args.baud) as port:
        if args.sync_time:
            synced = exchange(port,envelope("device.time",{"epoch":int(time.time())}))
            print(json.dumps(synced, ensure_ascii=False, separators=(",", ":")))
            if synced["type"] != "response.ok":
                raise ValueError("Device time/reload failed; create was not sent")
        print(json.dumps(exchange(port,message), ensure_ascii=False, separators=(",", ":")))

if __name__ == "__main__":
    # JSON Lines is UTF-8 even when Windows redirects stdout using an ANSI locale.
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    try:
        main()
    except (ValueError, TimeoutError, OSError) as exc:
        print(f"VelaGuard: {exc}", file=sys.stderr)
        sys.exit(1)
