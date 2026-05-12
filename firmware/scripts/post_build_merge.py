"""
PlatformIO post-build hook：用 esptool merge_bin 把 bootloader + partitions + app
合成 firmware-merged.bin，落到 firmware/ 根目錄供 release-template/flash.sh|.bat
直接從 0x0 燒（純 app firmware.bin 燒 0x0 會蓋掉 bootloader 區）。

檔名規則：
  - 主 env (esp32-s3-devkitc-1)：firmware-merged.bin
  - 其他 env (tft-smoke-test 等)：firmware-merged-{envname}.bin

驗證（Phase E bootloader bug 後加上）：
  - bootloader / partitions / firmware.bin 缺一即 fail（不再 silent skip）
  - merged.bin 不可超過 flash_size（避免燒不進去）
  - app.bin 不可超過 ota_0 partition 容量（partitions.csv 解析）
  - partition layout 整體驗證（#6）：
      * max(offset+size) ≤ flash_size（防止 partition 超界，Phase E bootloader bug 根因）
      * partitions 之間無 offset 重疊
      * 相鄰 partition 有 gap 印 warning（不 fail）
  - 印出 App / OTA 使用率百分比，預算超 90% 警告

啟用方式：在 platformio.ini 的目標 env 加：
    extra_scripts = post:scripts/post_build_merge.py
"""
import os
import re
import subprocess
import sys

Import("env")  # type: ignore  # PlatformIO 注入的全域

MAIN_ENV = "esp32-s3-devkitc-1"
BUDGET_WARN_RATIO = 0.90
DEFAULT_FLASH_SIZE = "16MB"


def flash_size_to_bytes(s):
    """轉 '16MB' / '4MB' 字串為 bytes。失敗回 None。"""
    if not s:
        return None
    m = re.match(r"^\s*(\d+)\s*([KMG]?)B\s*$", s, re.IGNORECASE)
    if not m:
        return None
    n = int(m.group(1))
    unit = m.group(2).upper()
    mult = {"": 1, "K": 1024, "M": 1024 * 1024, "G": 1024 * 1024 * 1024}[unit]
    return n * mult


def parse_partitions(partitions_csv):
    """讀 partitions.csv 取所有 entry，回傳 list of dict。

    格式：Name, Type, SubType, Offset, Size, Flags
    每筆：{name, type, subtype, offset, size, end}
    解析失敗或檔案不存在回 []。
    """
    if not os.path.exists(partitions_csv):
        return []
    entries = []
    with open(partitions_csv, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5:
                continue
            try:
                offset = int(parts[3], 0)
                size = int(parts[4], 0)
            except ValueError:
                continue
            entries.append({
                "name": parts[0],
                "type": parts[1],
                "subtype": parts[2],
                "offset": offset,
                "size": size,
                "end": offset + size,
            })
    return entries


def find_app_partition_size(entries):
    """從 parse_partitions() 結果找 ota_0 size。找不到回 None。"""
    for e in entries:
        if e["type"] == "app" and e["subtype"] in ("ota_0", "0x10"):
            return e["size"]
    return None


def verify_partition_layout(entries, flash_size_bytes):
    """檢查 partition 邊界、重疊與 gap。任一 critical 失敗 sys.exit(1)。

    Critical（fail build）：
      - max(offset+size) > flash_size （Phase E bootloader bug 根因）
      - 兩個 partition offset 範圍重疊
    Warning（不 fail）：
      - 相鄰 partition 之間有 gap（非首尾的留白）
    """
    if not entries:
        print("[verify] WARN: partitions.csv 解析不到 entry，跳過 layout 檢查")
        return

    # STEP 01: 算最大邊界
    max_end = max(e["end"] for e in entries)
    if max_end > flash_size_bytes:
        print(
            f"[verify] FAIL: partition 邊界 0x{max_end:X} ({max_end:,} bytes) "
            f"超過 flash_size 0x{flash_size_bytes:X} ({flash_size_bytes:,} bytes)"
        )
        print("[verify]   原因：platformio.ini 的 board_upload.flash_size 與 partitions.csv 不一致")
        sys.exit(1)

    # STEP 02: 依 offset 排序檢查重疊與 gap
    sorted_entries = sorted(entries, key=lambda e: e["offset"])
    for i in range(1, len(sorted_entries)):
        prev = sorted_entries[i - 1]
        cur = sorted_entries[i]
        if cur["offset"] < prev["end"]:
            print(
                f"[verify] FAIL: partition 重疊 {prev['name']} (0x{prev['offset']:X}~0x{prev['end']:X}) "
                f"與 {cur['name']} (0x{cur['offset']:X}~0x{cur['end']:X})"
            )
            sys.exit(1)
        if cur["offset"] > prev["end"]:
            gap = cur["offset"] - prev["end"]
            print(
                f"[verify] WARN: partition gap {gap:,} bytes between "
                f"{prev['name']} (end 0x{prev['end']:X}) 與 {cur['name']} (start 0x{cur['offset']:X})"
            )

    print(
        f"[verify] OK: partition layout — max boundary 0x{max_end:X} "
        f"({max_end/flash_size_bytes:.1%} of flash {flash_size_bytes:,} bytes)"
    )


def verify_merged_bin(merged_path, app_bin_path, app_partition_size, flash_size_bytes):
    """產物驗證；任一項失敗 sys.exit(1) 中斷 build。"""
    # STEP 01: merged.bin 存在且非空
    if not os.path.exists(merged_path) or os.path.getsize(merged_path) == 0:
        print(f"[verify] FAIL: merged.bin 不存在或為空 → {merged_path}")
        sys.exit(1)

    merged_size = os.path.getsize(merged_path)
    app_size = os.path.getsize(app_bin_path)

    # STEP 02: merged.bin 不可超過 flash 容量
    if merged_size > flash_size_bytes:
        print(f"[verify] FAIL: merged.bin {merged_size:,} bytes 超過 flash 容量 {flash_size_bytes:,}")
        sys.exit(1)

    # STEP 03: app.bin 不可超過 ota_0 partition 容量
    if app_partition_size is None:
        print("[verify] WARN: 無法解析 partitions.csv 的 app0 size，跳過 app 預算檢查")
    elif app_size > app_partition_size:
        print(f"[verify] FAIL: app.bin {app_size:,} bytes 超過 ota_0 partition {app_partition_size:,}")
        sys.exit(1)
    else:
        ratio = app_size / app_partition_size
        warn = " ⚠️ 超 90% 預算" if ratio >= BUDGET_WARN_RATIO else ""
        print(f"[verify] App: {app_size:,} / {app_partition_size:,} bytes ({ratio:.1%}){warn}")

    print(f"[verify] OK: merged {merged_size:,} bytes (flash 容量 {flash_size_bytes:,})")


def merge_bin(source, target, env):
    # STEP 01: 解 lazy variable
    # env["BUILD_DIR"] 在 SCons 為 lazy variable（"$PROJECT_BUILD_DIR/$PIOENV"），
    # 直接用會找不到路徑 → 顯式 subst 展開
    project_dir = env.subst("$PROJECT_DIR")
    build_dir   = env.subst("$BUILD_DIR")
    env_name    = env["PIOENV"]
    suffix      = "" if env_name == MAIN_ENV else f"-{env_name}"

    # STEP 02: 取 flash_size（從 BoardConfig，fallback default）
    flash_size_str = env.BoardConfig().get("upload.flash_size", DEFAULT_FLASH_SIZE)
    flash_size_bytes = flash_size_to_bytes(flash_size_str)
    if flash_size_bytes is None:
        print(f"[post-build] FAIL: 無法解析 flash_size '{flash_size_str}'")
        sys.exit(1)

    # STEP 03: 確認 build artifacts 都在
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    app_bin    = os.path.join(build_dir, "firmware.bin")
    missing = [p for p in (bootloader, partitions, app_bin) if not os.path.exists(p)]
    if missing:
        print(f"[post-build] FAIL: 缺少 build artifacts → {missing}")
        sys.exit(1)

    # STEP 04: 先驗 partition layout（在 merge 之前驗，省一次 esptool 跑）
    partitions_csv = os.path.join(project_dir, "partitions.csv")
    partition_entries = parse_partitions(partitions_csv)
    verify_partition_layout(partition_entries, flash_size_bytes)

    # STEP 05: 取得 esptool.py 路徑
    merged_name = f"firmware-merged{suffix}.bin"
    merged_path = os.path.join(project_dir, merged_name)
    esptool_py = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy") or "", "esptool.py")
    if not os.path.exists(esptool_py):
        print(f"[post-build] FAIL: 找不到 esptool.py → {esptool_py}")
        sys.exit(1)

    # STEP 06: 跑 merge_bin
    cmd = [
        sys.executable, esptool_py, "--chip", "esp32s3", "merge_bin",
        "-o", merged_path,
        "--flash_mode", "dio", "--flash_size", flash_size_str,
        "0x0",     bootloader,
        "0x8000",  partitions,
        "0x10000", app_bin,
    ]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except Exception as e:
        print(f"[post-build] FAIL: merge 例外 → {e}")
        sys.exit(1)

    if r.returncode != 0 or not os.path.exists(merged_path):
        print(f"[post-build] FAIL: merge 失敗 → {r.stderr[:300]}")
        sys.exit(1)

    size_kb = os.path.getsize(merged_path) // 1024
    print(f"[post-build] merged ({size_kb} KB) → firmware/{merged_name}")

    # STEP 07: 跑產物驗證
    app_partition_size = find_app_partition_size(partition_entries)
    verify_merged_bin(merged_path, app_bin, app_partition_size, flash_size_bytes)


# 綁在 buildprog 確保所有 bin（含 bootloader / partitions）都產出後才執行
env.AddPostAction("buildprog", merge_bin)
