#!/usr/bin/env python3
"""EMS Timer — Boot smoke test.

主韌體燒板後跑此 script，自動讀 serial 一段時間並判斷 cold boot 是否健康。

判定規則（任一 FAIL 則整體 FAIL，exit code 1）：
  1. 必要 banner：`[READY] MainMenu`（setup() 跑完的最終訊號）
     - optional：`[BOOT] EMS Timer Phase A`（native USB host attach 延遲常會錯過開頭，
       不 fail，僅在報告 note）
  2. 不得出現 panic 字串：Guru Meditation / Backtrace: / [FATAL] / Brownout /
     assert failed / abort() / CORRUPT HEAP / IllegalInstruction / LoadProhibited /
     StoreProhibited / panic'd
  3. `rst:` reset cause 不得出現 ≥ 2 次（boot loop 偵測；第一次是 RESET 觸發，預期）
     - 註：native USB 抓不到 ROM bootloader 印的 `rst:`（USB CDC 列舉前印的會掉），
       這層退化為靠 banner missing 抓 boot loop

跑法（GOOUUU ESP32-S3 native USB 無 RTS/DTR，需手動操作）：
  1. 燒主韌體：
       cd firmware
       # 進 download mode：按住 BOOT → 短按 RESET → 放開 BOOT
       pio run -e esp32-s3-devkitc-1 -t upload --upload-port /dev/cu.usbmodemXXXX
  2. 板子離開 download mode：純按 RESET
  3. 等 ~5 秒讓 USB CDC 重新列舉，找當下 port name：
       ls /dev/cu.usbmodem*
  4. 立即按 RESET 再跑 smoke：
       ~/.platformio/penv/bin/python3 scripts/smoke_boot_test.py \\
           --port /dev/cu.usbmodemXXXX --duration 8

自測 / regression（讀現成 log 不開 serial）：
       python3 scripts/smoke_boot_test.py --log-file /tmp/boot.log

退出碼：
  0 = 通過
  1 = smoke FAIL（panic / 缺 banner / boot loop）
  2 = 用法錯誤 / serial 連不上
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path
from typing import List, Optional, Tuple

REQUIRED_BANNERS = [
    "[READY] MainMenu",
]
OPTIONAL_BANNERS = [
    "[BOOT] EMS Timer Phase A",
]

PANIC_PATTERNS = [
    "Guru Meditation",
    "Backtrace:",
    "[FATAL]",
    "Brownout",
    "assert failed",
    "abort()",
    "CORRUPT HEAP",
    "IllegalInstruction",
    "LoadProhibited",
    "StoreProhibited",
    "panic'd",
]

RST_LINE_RE = re.compile(r"\brst:0x[0-9a-fA-F]+")


def read_from_serial(port: str, baud: int, duration: float) -> List[str]:
    try:
        import serial  # type: ignore
    except ImportError:
        sys.stderr.write(
            "ERROR: pyserial 未安裝。請用 PlatformIO 內建 python：\n"
            "  ~/.platformio/penv/bin/python3 "
            f"{Path(__file__).name} --port {port}\n"
        )
        sys.exit(2)

    try:
        ser = serial.Serial(port, baud, timeout=0.2)
    except Exception as exc:
        sys.stderr.write(f"ERROR: 開啟 {port} 失敗：{exc}\n")
        sys.exit(2)

    print(f"[smoke] listening on {port} @ {baud} for {duration:.1f}s ...", flush=True)
    lines: List[str] = []
    buffer = bytearray()
    deadline = time.monotonic() + duration
    try:
        while time.monotonic() < deadline:
            chunk = ser.read(256)
            if chunk:
                buffer.extend(chunk)
                while b"\n" in buffer:
                    line, _, rest = buffer.partition(b"\n")
                    decoded = line.decode("utf-8", errors="replace").rstrip("\r")
                    lines.append(decoded)
                    print(f"  | {decoded}", flush=True)
                    buffer = bytearray(rest)
        if buffer:
            decoded = bytes(buffer).decode("utf-8", errors="replace").rstrip("\r")
            if decoded:
                lines.append(decoded)
                print(f"  | {decoded}", flush=True)
    finally:
        ser.close()
    return lines


def read_from_logfile(path: Path) -> List[str]:
    if not path.is_file():
        sys.stderr.write(f"ERROR: log file not found: {path}\n")
        sys.exit(2)
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def analyse(lines: List[str]) -> Tuple[bool, List[str]]:
    """Return (passed, report_lines)."""
    report: List[str] = []

    # STEP 01: banner check (required + optional)
    missing_required = [b for b in REQUIRED_BANNERS if not any(b in ln for ln in lines)]
    missing_optional = [b for b in OPTIONAL_BANNERS if not any(b in ln for ln in lines)]
    banner_ok = not missing_required
    if banner_ok:
        if missing_optional:
            report.append(
                "[OK] banner: required markers present; optional missing "
                + ", ".join(repr(b) for b in missing_optional)
                + " (native USB attach delay — expected)"
            )
        else:
            report.append("[OK] banner: all markers present")
    else:
        report.append("[FAIL] banner: missing required " + ", ".join(repr(b) for b in missing_required))

    # STEP 02: panic scan
    panic_hits: List[Tuple[int, str, str]] = []
    for idx, ln in enumerate(lines, start=1):
        for pat in PANIC_PATTERNS:
            if pat in ln:
                panic_hits.append((idx, pat, ln))
                break
    panic_ok = not panic_hits
    if panic_ok:
        report.append("[OK] panic: no crash / fatal markers")
    else:
        report.append(f"[FAIL] panic: {len(panic_hits)} hit(s)")
        for idx, pat, ln in panic_hits[:10]:
            report.append(f"       line {idx}: matched {pat!r} -> {ln}")

    # STEP 03: boot loop (multiple rst: reset causes within capture window)
    rst_hits = [(idx, ln) for idx, ln in enumerate(lines, start=1) if RST_LINE_RE.search(ln)]
    loop_ok = len(rst_hits) <= 1
    if loop_ok:
        report.append(f"[OK] boot-loop: {len(rst_hits)} reset banner (≤1 expected)")
    else:
        report.append(f"[FAIL] boot-loop: {len(rst_hits)} reset banners — looks like reboot loop")
        for idx, ln in rst_hits[:5]:
            report.append(f"       line {idx}: {ln}")

    passed = banner_ok and panic_ok and loop_ok
    return passed, report


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="EMS Timer boot smoke test")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="Serial port (e.g. /dev/cu.usbmodem1101)")
    src.add_argument("--log-file", type=Path, help="Read serial log from file instead of opening port")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200)")
    ap.add_argument("--duration", type=float, default=8.0, help="Listen seconds (default 8)")
    return ap.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    if args.log_file:
        lines = read_from_logfile(args.log_file)
        print(f"[smoke] loaded {len(lines)} lines from {args.log_file}")
    else:
        lines = read_from_serial(args.port, args.baud, args.duration)
        print(f"[smoke] captured {len(lines)} lines")

    passed, report = analyse(lines)
    print()
    print("=" * 60)
    print("EMS Timer Boot Smoke Report")
    print("=" * 60)
    for ln in report:
        print(ln)
    print("=" * 60)
    print("RESULT:", "PASS" if passed else "FAIL")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
