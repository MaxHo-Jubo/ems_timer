"""
PlatformIO post-build hook：用 esptool merge_bin 把 bootloader + partitions + app
合成 firmware-merged.bin，落到 firmware/ 根目錄供 release-template/flash.sh|.bat
直接從 0x0 燒（純 app firmware.bin 燒 0x0 會蓋掉 bootloader 區）。

檔名規則：
  - 主 env (esp32-s3-devkitc-1)：firmware-merged.bin
  - 其他 env (tft-smoke-test 等)：firmware-merged-{envname}.bin

啟用方式：在 platformio.ini 的目標 env 加：
    extra_scripts = post:scripts/post_build_merge.py
"""
import os
import subprocess
import sys

Import("env")  # type: ignore  # PlatformIO 注入的全域

MAIN_ENV = "esp32-s3-devkitc-1"


def merge_bin(source, target, env):
    # env["BUILD_DIR"] 在 SCons 為 lazy variable（"$PROJECT_BUILD_DIR/$PIOENV"），
    # 直接用會找不到路徑 → 顯式 subst 展開
    project_dir = env.subst("$PROJECT_DIR")
    build_dir   = env.subst("$BUILD_DIR")
    env_name    = env["PIOENV"]
    suffix      = "" if env_name == MAIN_ENV else f"-{env_name}"

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    app_bin    = os.path.join(build_dir, "firmware.bin")
    missing = [p for p in (bootloader, partitions, app_bin) if not os.path.exists(p)]
    if missing:
        print(f"[post-build] skip merge — missing={missing}")
        return

    merged_name = f"firmware-merged{suffix}.bin"
    merged_path = os.path.join(project_dir, merged_name)
    esptool_py = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy") or "", "esptool.py")
    if not os.path.exists(esptool_py):
        print("[post-build] skip merge（找不到 esptool.py）")
        return

    cmd = [
        sys.executable, esptool_py, "--chip", "esp32s3", "merge_bin",
        "-o", merged_path,
        "--flash_mode", "dio", "--flash_size", "16MB",
        "0x0",     bootloader,
        "0x8000",  partitions,
        "0x10000", app_bin,
    ]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if r.returncode == 0 and os.path.exists(merged_path):
            size_kb = os.path.getsize(merged_path) // 1024
            print(f"[post-build] merged ({size_kb} KB) → firmware/{merged_name}")
        else:
            print(f"[post-build] merge 失敗：{r.stderr[:200]}")
    except Exception as e:
        print(f"[post-build] merge 例外：{e}")


# 綁在 buildprog 確保所有 bin（含 bootloader / partitions）都產出後才執行
env.AddPostAction("buildprog", merge_bin)
