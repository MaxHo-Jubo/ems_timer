# 如何打包 release 給其他工程師

> **此文件給專案開發者看，不放進對方的 release 包內。**

## 打包流程

### Step 1：編譯韌體

```bash
cd firmware
pio run -e esp32-s3-devkitc-1
```

編譯產物路徑：
```
.pio/build/esp32-s3-devkitc-1/firmware.bin
```

### Step 2：複製 binary 到 release-template

```bash
cp .pio/build/esp32-s3-devkitc-1/firmware.bin release-template/
```

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

## release-template 檔案說明

| 檔案 | 用途 | 給誰看 |
|------|------|-------|
| `firmware.bin` | 韌體 binary（**每次建置覆蓋**） | 對方 |
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
cp .pio/build/esp32-s3-devkitc-1/firmware.bin release-template/

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

- `release-template/firmware.bin` 加入 `.gitignore`（建置產物不入版控）
- `release-template/VERSION.txt` 加入 `.gitignore`（每次重新產生）
- 保留 `flash.bat` / `flash.sh` / `README.txt` / `HOW_TO_BUILD_RELEASE.md` 在版控
