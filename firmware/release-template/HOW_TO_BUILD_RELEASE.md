# 如何打包 release 給其他工程師

> **此文件給專案開發者看，不放進對方的 release 包內。**

## 打包流程

### Step 1：編譯韌體

```bash
cd firmware
pio run -e esp32-s3-devkitc-1
```

編譯產物路徑（post-build hook 自動 merge 落到 `firmware/` 根目錄）：
```
firmware/firmware-merged.bin   # bootloader + partition + app 三合一（給 release）
.pio/build/esp32-s3-devkitc-1/firmware.bin   # 純 app 段（不可從 0x0 燒，會蓋 bootloader）
```

### Step 2：複製 merged binary 到 release-template

```bash
# 從 firmware/ 根目錄執行；用 merged bin，內含 bootloader/partition/app
cp firmware-merged.bin release-template/
```

> ⚠️ 一定要用 `firmware-merged.bin`，**不可**用純 app 的 `firmware.bin`。
> esptool `write_flash 0x0 firmware.bin` 會蓋掉 bootloader 區，造成無限重置。

### Step 3：建立 VERSION.txt（記錄版本資訊）

```bash
cd release-template

# STEP 01: 從 git 取得版本資訊
COMMIT=$(git rev-parse --short HEAD)
DATE=$(date +%Y-%m-%d)
BRANCH=$(git branch --show-current)

# STEP 02: 寫入 VERSION.txt
cat > VERSION.txt <<EOF
EMS Timer 韌體
建置日期：${DATE}
Git commit：${COMMIT}
Git branch：${BRANCH}
編譯環境：PlatformIO + ESP-IDF (Arduino framework)
目標硬體：ESP32-S3 GOOUUU
EOF
```

### Step 4：打包 zip

```bash
# 從 firmware/ 目錄執行
cd ..
VERSION=$(date +%Y%m%d)
zip -r "ems-timer-firmware-${VERSION}.zip" release-template/ \
    -x "release-template/HOW_TO_BUILD_RELEASE.md"
```

> ⚠️ 注意 `-x` 排除 `HOW_TO_BUILD_RELEASE.md`，這份文件不給對方看。

### Step 5：交付

zip 透過以下任一管道傳給對方：
- GitLab Release Asset（推薦，有版本追蹤）
- 公司內網 / 共用磁碟
- 雲端硬碟連結

---

## 產測韌體包（給焊接廠商）

廠商驗收用的是 `factory-test` env，不是主韌體。流程與上面相同，只差來源檔名：

```bash
cd firmware
pio run -e factory-test
# flash.sh / flash.bat 只認 firmware-merged.bin 這個檔名，所以要改名複製
cp firmware-merged-factory-test.bin release-template/firmware-merged.bin
```

- `VERSION.txt` 第一行寫「EMS DoseSync Pro 產測韌體（factory-test）」，其餘欄位同 Step 3；打包同 Step 4，檔名建議 `ems-factory-test-YYYYMMDD.zip`
- 交付時連同 `docs/vendor-assembly-brief.html` 一起給廠商，§7 是判讀表與 FAIL 對照
- 產測畫面的字串（`PASS` / `RTC MISSING` 等）來自 `lib/ems_factory_test/factory_test_logic.cpp`，改字串要同步改 vendor-assembly-brief §7.4／§7.5

> ⚠️ 打包完把 `release-template/firmware-merged.bin` 刪掉或換回主韌體，避免下次打包主韌體時誤用產測版。

---

## release-template 檔案說明

| 檔案 | 用途 | 給誰看 |
|------|------|-------|
| `firmware-merged.bin` | 韌體 binary, bootloader + partition + app 三合一（**每次建置覆蓋**） | 對方 |
| `flash.bat` | Windows 燒錄腳本 | 對方 |
| `flash.sh` | macOS / Linux 燒錄腳本 | 對方 |
| `README.txt` | 對方燒錄說明 | 對方 |
| `VERSION.txt` | 版本資訊（**每次建置重新產生**） | 對方 |
| `HOW_TO_BUILD_RELEASE.md` | 本文件 | **僅開發者** |

---

## 自動化建議（未來可做）

可寫成 `scripts/build-release.sh`：

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/../firmware"

pio run -e esp32-s3-devkitc-1
cp firmware-merged.bin release-template/

# 產生 VERSION.txt
{
    echo "EMS Timer 韌體"
    echo "建置日期：$(date +%Y-%m-%d)"
    echo "Git commit：$(git rev-parse --short HEAD)"
    echo "Git branch：$(git branch --show-current)"
} > release-template/VERSION.txt

# 打包
VERSION=$(date +%Y%m%d-%H%M)
zip -r "../ems-timer-firmware-${VERSION}.zip" release-template/ \
    -x "release-template/HOW_TO_BUILD_RELEASE.md"

echo "✅ Release 打包完成：ems-timer-firmware-${VERSION}.zip"
```

執行：`bash scripts/build-release.sh`

---

## 版本控管建議

- `release-template/firmware-merged.bin` 加入 `.gitignore`（建置產物不入版控）
- `release-template/VERSION.txt` 加入 `.gitignore`（每次重新產生）
- 保留 `flash.bat` / `flash.sh` / `README.txt` / `HOW_TO_BUILD_RELEASE.md` 在版控
