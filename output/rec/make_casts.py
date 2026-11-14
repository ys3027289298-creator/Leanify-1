#!/usr/bin/env python3
"""Render the four demo shell transcripts into asciicast v2 files.

Runs each segment script inside a pseudo-terminal via `script`, captures the
exact output, then wraps it in an asciicast with a fixed 120x36 grid and
deterministic event timing so the resulting GIFs stay legible and concise.
"""
import json
import os
import re
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REC = os.path.join(ROOT, "output", "rec")
COLS, ROWS = 120, 36

SEGMENTS = [
    ("seg1_build.sh", "WebP lossless support - segment 1/4 - build"),
    ("seg2_lossless.sh", "WebP lossless support - segment 2/4 - lossless recompression"),
    ("seg3_failure.sh", "WebP lossless support - segment 3/4 - failure safety"),
    ("seg4_regression.sh", "WebP lossless support - segment 4/4 - acceptance & zero regression"),
]


def capture(script):
    proc = subprocess.run(
        ["script", "-qfec", f"bash {os.path.join(REC, script)}", "/dev/null"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=dict(os.environ, TERM="xterm-256color", COLUMNS=str(COLS), LINES=str(ROWS)),
        check=True,
    )
    data = proc.stdout.decode("utf-8", "replace")
    data = data.replace("\r\n", "\n").replace("\r", "")
    return data


def to_events(text):
    events = []
    for line in text.split("\n"):
        if line.strip() in ("exit", "logout", "Script done."):
            continue
        events.append(line + "\r\n")
    merged = []
    for line in events:
        if merged and line == "\r\n" and merged[-1].endswith("\r\n"):
            merged[-1] += line
        else:
            merged.append(line)
    return merged


def write_cast(path, title, lines):
    header = {
        "version": 2,
        "width": COLS,
        "height": ROWS,
        "timestamp": int(time.time()),
        "idle_time_limit": 1.5,
        "title": title,
        "env": {"SHELL": "/bin/bash", "TERM": "xterm-256color"},
    }
    out = [json.dumps(header)]
    clock = 0.4
    for line in lines:
        clock += 0.18 if line.strip() else 0.1
        out.append(json.dumps([round(clock, 3), "o", line]))
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def main():
    os.makedirs(os.path.join(REC, "casts"), exist_ok=True)
    for index, (script, title) in enumerate(SEGMENTS, start=1):
        raw = capture(script)
        marker = "SEGMENT"
        pos = raw.find(marker)
        raw = raw[max(0, pos - 20):] if pos != -1 else raw
        raw = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]",
                     lambda m: "" if m.group(0)[-1] != "m" else m.group(0), raw)
        raw = re.sub(r"(?<!\x1b)\[[0-9]*[HJ]", "", raw)
        raw = raw.replace("\x1b7", "").replace("\x1b8", "")
        lines = to_events(raw)
        path = os.path.join(REC, "casts", f"seg{index}.cast")
        write_cast(path, title, lines)
        print(f"{path}: {len(lines)} events")


if __name__ == "__main__":
    main()
