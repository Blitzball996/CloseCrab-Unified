#!/usr/bin/env python3
"""Capture a real Claude Code request (headers AND body) for fingerprint parity.

Why this exists
---------------
extract_claude_headers.bat only greps beta-header strings out of claude.exe.
That misses BODY-level fingerprinting, which is what actually broke us on
2026-08-02: proxies reject any request whose JSON has no top-level `metadata`
with HTTP 400 "我们检测到您的客户端存在异常" (client anomaly detected). No amount
of header syncing fixes that.

This script stands up a throwaway HTTP server, points a real `claude -p` run at
it, and dumps the exact request the official client sends — headers plus a
structural summary of the body. Diff that against RemoteAPIClient.cpp
(performCurlSSE headers + buildRequestBody) after every Claude Code upgrade.

Usage
-----
    python scripts/capture_claude_request.py            # capture + summarize
    python scripts/capture_claude_request.py --keep     # also keep the raw dump

Requires: python 3.8+, `claude` on PATH.
"""

import argparse
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading

PORT = 8899
SSE = (
    'event: message_start\n'
    'data: {"type":"message_start","message":{"id":"msg_capture","type":"message",'
    '"role":"assistant","model":"m","content":[],"stop_reason":null,'
    '"stop_sequence":null,"usage":{"input_tokens":1,"output_tokens":1}}}\n\n'
    'event: content_block_start\n'
    'data: {"type":"content_block_start","index":0,'
    '"content_block":{"type":"text","text":""}}\n\n'
    'event: content_block_delta\n'
    'data: {"type":"content_block_delta","index":0,'
    '"delta":{"type":"text_delta","text":"ok"}}\n\n'
    'event: content_block_stop\ndata: {"type":"content_block_stop","index":0}\n\n'
    'event: message_delta\n'
    'data: {"type":"message_delta","delta":{"stop_reason":"end_turn",'
    '"stop_sequence":null},"usage":{"output_tokens":1}}\n\n'
    'event: message_stop\ndata: {"type":"message_stop"}\n\n'
).encode()

captured = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _record(self):
        n = int(self.headers.get("content-length") or 0)
        raw = self.rfile.read(n) if n else b""
        captured.append({
            "path": self.path,
            "headers": dict(self.headers.items()),
            "body": raw,
        })

    def do_POST(self):
        self._record()
        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.send_header("content-length", str(len(SSE)))
        self.end_headers()
        self.wfile.write(SSE)

    def do_GET(self):
        self._record()
        payload = b'{"data":[]}'
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_args):
        pass


def run_claude(settings_path):
    claude = shutil.which("claude") or shutil.which("claude.exe")
    if not claude:
        sys.exit("[ERROR] `claude` not found on PATH — install @anthropic-ai/claude-code first.")
    # --settings overrides the user's ~/.claude/settings.json env block, which is
    # what pins ANTHROPIC_BASE_URL for most users. Without it the run escapes to
    # the real endpoint and captures nothing.
    proc = subprocess.run(
        [claude, "--settings", settings_path, "-p", "hi"],
        capture_output=True, text=True, timeout=300,
    )
    if proc.returncode != 0:
        print("[WARN] claude exited %d: %s" % (proc.returncode, (proc.stdout or proc.stderr)[:300]))


def summarize(req):
    print("=" * 72)
    print("POST %s" % req["path"])
    print("-" * 72)
    print("HEADERS (sync with performCurlSSE in src/api/RemoteAPIClient.cpp):")
    for k in sorted(req["headers"]):
        v = req["headers"][k]
        if k.lower() in ("authorization", "x-api-key"):
            v = "<redacted>"
        if k.lower() == "anthropic-beta":
            print("  %s:" % k)
            for beta in v.split(","):
                print("      %s" % beta.strip())
            continue
        print("  %s: %s" % (k, v))

    try:
        body = json.loads(req["body"])
    except Exception:
        print("\n[WARN] body is not JSON (%d bytes)" % len(req["body"]))
        return

    print("-" * 72)
    print("BODY TOP-LEVEL KEYS (sync with buildRequestBody):")
    for k in body:
        val = body[k]
        if k == "messages":
            note = "%d message(s)" % len(val)
        elif k == "tools":
            note = "%d tool(s)" % len(val)
        elif k == "system":
            note = "%d block(s)" % len(val)
        else:
            note = json.dumps(val, ensure_ascii=False)[:200]
        print("  %-20s %s" % (k, note))

    print("-" * 72)
    print("FINGERPRINT-CRITICAL FIELDS:")
    md = body.get("metadata")
    print("  metadata          : %s" % (json.dumps(md, ensure_ascii=False)[:300] if md is not None else "MISSING"))
    if md is None:
        print("    !! the real client always sends this; proxies 400 without it")
    for i, blk in enumerate(body.get("system", [])):
        print("  system[%d]         : %r" % (i, (blk.get("text") or "")[:90]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true", help="write the raw body to claude_request_raw.json")
    args = ap.parse_args()

    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump({"env": {
            "ANTHROPIC_BASE_URL": "http://127.0.0.1:%d" % PORT,
            "ANTHROPIC_AUTH_TOKEN": "sk-capture-dummy",
            "ANTHROPIC_API_KEY": "sk-capture-dummy",
            "NO_PROXY": "127.0.0.1,localhost",
            "no_proxy": "127.0.0.1,localhost",
            "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
        }}, f)
        settings_path = f.name

    try:
        run_claude(settings_path)
    finally:
        srv.shutdown()
        os.unlink(settings_path)

    posts = [r for r in captured if r["body"]]
    if not posts:
        sys.exit("[ERROR] captured nothing — claude never reached 127.0.0.1:%d "
                 "(check that --settings overrides your ANTHROPIC_BASE_URL)." % PORT)

    summarize(posts[0])
    if args.keep:
        out = os.path.join(os.getcwd(), "claude_request_raw.json")
        with open(out, "wb") as f:
            f.write(posts[0]["body"])
        print("\n[INFO] raw body written to %s" % out)


if __name__ == "__main__":
    main()
