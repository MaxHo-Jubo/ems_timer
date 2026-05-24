---
title: DS3231 RTC 整合計畫（選項 A：runtime 偵測 + 雙模式 backend）
audience: 工程
last_updated: 2026-05-24
status: 規劃中（尚未動工）
---

# DS3231 RTC 整合計畫

> 對應 Dev-Phase 3 升級項目。**選項 A**：runtime 偵測，同一份 binary 支援接 / 不接 DS3231 兩種硬體；無 RTC 時降級為純 BLE 軟體對時（現況行為）。
>
> **前置依賴**：Phase F BLE 鏈路已完成（commit 8409ed1，2026-05-24）。
> **硬體狀態**：DS3231 模組已採購並接線（SDA=GPIO42, SCL=GPIO41）；`src_rtc_demo/main.cpp` demo 已驗證讀寫與電池備援。
> **產出**：~250 行新 code + ~40 native tests，**估 1.5 ~ 2 個工作天**。

---

## 1. 背景與動機

### 1.1 現況問題

裝置目前**無硬體時鐘**，開機後只有 `millis()` 從 0 起算的相對時間。Boot 後不知道現在是幾點，要等 BLE 連 App 時透過 `time_sync` 收到 epoch（軟體對時）。

但 case 通常**在 BLE 連線之前**就開始（救護現場 → 結束 → 才連 App）：
- `ohca_logic.cpp:47-52` `case_start_ms / case_end_ms` **hard-code 0**（明確標 TODO 等 DS3231）
- `input_handler.cpp:110` `caseStartEpochMs` 同樣 = 0（對時前）
- 同步到 web 後 `started_at_ms = 0`、`ended_at_ms = 0`、`events[*].timestamp_ms = 0`（spec §4.1 設計）
- App 端用 `elapsed_ms`（相對毫秒）重建時間軸

### 1.2 為何 DS3231

| 維度 | 純軟體對時 | + DS3231 |
|------|-----------|---------|
| Boot 後即有 epoch | ❌ 必須等 BLE | ✅ 立即 |
| 斷電後保留時間 | ❌ 重啟歸零 | ✅ 電池備援（~10 年 CR2032）|
| 救護現場可信時戳 | ❌ 案件時戳全 0 | ✅ 真實絕對時間 |
| 晶振精度 | N/A（取自手機 NTP） | ±2ppm（醫療紀錄級）|
| 成本 | $0 | ~$50 NTD（模組）|

**結論**：救護紀錄需可信時戳支撐後續法律 / 健保稽核，DS3231 是必要硬體。

### 1.3 為何雙模式

- **開發機**：未必每台都焊 RTC（pair programming 機、demo 機、PM 試燒機）
- **量產線**：單一 binary 對齊產線燒錄 SOP，不需多套 binary 管理
- **fault tolerance**：DS3231 故障（電池死、I2C 斷線、IC 燒）不該讓整個裝置 boot panic
- **TDD 友善**：抽象 backend → native test 可 mock，不依賴硬體

---

## 2. 設計選項對比

| 選項 | 機制 | 優 | 缺 | 採用？ |
|------|------|----|----|-------|
| **A. Runtime 偵測** | setup() I2C probe 0x68 → 掛 DS3231 / Null backend | 單一 binary；fault tolerance；TDD 友善 | flash 多帶 RTClib（~5KB） | ✅ |
| B. Compile-time flag | `#define ENABLE_DS3231` 雙 binary | flash 省一點 | 兩套 binary 維護負擔 | ❌ |
| C. 強制必須 RTC | probe 失敗 boot panic | 量產定型強制簡單 | 開發機 / fault 場景 brittle | ❌（量產定型後可考慮） |

**採 A**，理由：fault tolerance 與 TDD 友善遠勝 ~5KB flash 開銷（目前 Flash 67.3%，餘量充足）。

---

## 3. 介面設計

### 3.1 抽象 backend 介面

新增 `firmware/lib/ems_rtc/ems_rtc.h`：

```cpp
#pragma once
#include <cstdint>

namespace ems {

/**
 * RTC 抽象介面。同一份韌體支援：
 *   - DS3231Backend：實體 DS3231 I2C 模組（Dev-Phase 3+）
 *   - NullBackend  ：無 RTC 硬體時的降級實作（is_present=false / now=0）
 *
 * 上層 caller 不分支處理，下游邏輯遇 now_epoch_ms()==0 自動 fallback
 * 到既有「未對時 timestamp = 0」慣例（spec §4.1）。
 */
class RtcBackend {
public:
    virtual ~RtcBackend() = default;

    /** 是否實體 RTC 在線（NullBackend 永遠 false） */
    virtual bool is_present() const = 0;

    /**
     * 取目前 epoch milliseconds。NullBackend 或 DS3231 尚未設過時回 0。
     * 下游 caller 看到 0 應 fallback 到既有「未對時」邏輯（不要當合法時戳）。
     */
    virtual uint64_t now_epoch_ms() const = 0;

    /**
     * 設定 RTC 時間（用於 BLE time_sync 後反向寫回 DS3231）。
     * NullBackend 為 no-op 回 false。
     * @return true 成功寫回；false 表 backend 不支援或寫入失敗
     */
    virtual bool set_epoch_ms(uint64_t epoch) = 0;
};

}  // namespace ems
```

### 3.2 NullBackend（無 RTC 降級）

```cpp
// firmware/lib/ems_rtc/null_backend.h
namespace ems {
class NullRtcBackend : public RtcBackend {
public:
    bool     is_present() const override { return false; }
    uint64_t now_epoch_ms() const override { return 0; }
    bool     set_epoch_ms(uint64_t) override { return false; }
};
}  // namespace ems
```

### 3.3 DS3231Backend（RTClib 包裝）

```cpp
// firmware/lib/ems_rtc/ds3231_backend.h
#include <RTClib.h>
namespace ems {
class DS3231Backend : public RtcBackend {
public:
    bool begin(TwoWire& wire);  // 失敗回 false（caller 改掛 NullBackend）

    bool is_present() const override { return present_; }
    uint64_t now_epoch_ms() const override;
    bool set_epoch_ms(uint64_t epoch) override;

private:
    RTC_DS3231 rtc_;
    bool present_ = false;
};
}  // namespace ems
```

**注意**：DS3231Backend.cpp **不能進 native test**（硬體相依）；純邏輯需要的 boundary case 在 NullBackend / mock backend 覆蓋。

### 3.4 Mock backend（native test 用）

```cpp
// firmware/test/test_rtc_integration/mock_rtc_backend.h
namespace ems {
class MockRtcBackend : public RtcBackend {
public:
    bool     is_present() const override { return present_; }
    uint64_t now_epoch_ms() const override { return now_ms_; }
    bool     set_epoch_ms(uint64_t epoch) override { now_ms_ = epoch; return present_; }

    // test fixture：
    void set_present(bool p) { present_ = p; }
    void advance_ms(uint64_t d) { now_ms_ += d; }

private:
    bool     present_ = true;
    uint64_t now_ms_  = 0;
};
}  // namespace ems
```

---

## 4. Initialize sequence

### 4.1 main.cpp setup() 新增段落

```cpp
// STEP 06.5 (新): RTC 初始化 — runtime 偵測 I2C 0x68
//   - 成功掛 DS3231Backend + 若 RTC 已有時間，立即 seed g_ts_state
//     讓 boot 後第一筆 case 也有真實時戳
//   - 失敗掛 NullBackend，行為與 Phase F 一致（等 BLE time_sync）
Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
static ems::DS3231Backend ds3231_be;
static ems::NullRtcBackend null_be;
if (ds3231_be.begin(Wire)) {
    g_rtc = &ds3231_be;
    Serial.println("[RTC] DS3231 detected at 0x68");
    const uint64_t rtc_epoch = g_rtc->now_epoch_ms();
    if (rtc_epoch > SYNCED_AT_EPOCH_FLOOR_MS) {
        ems::time_sync_seed_from_rtc(&g_ts_state, rtc_epoch, millis());
        Serial.printf("[RTC] seeded software clock from RTC: %llu\n",
                      (unsigned long long)rtc_epoch);
    } else {
        Serial.println("[RTC] DS3231 present but time not set, 等 BLE time_sync");
    }
} else {
    g_rtc = &null_be;
    Serial.println("[RTC] not present, fallback to BLE time_sync only");
}
```

### 4.2 全域變數新增

`app_globals.h`:
```cpp
extern ems::RtcBackend* g_rtc;  // 永不為 nullptr（至少掛 NullBackend）
```

`main.cpp`:
```cpp
ems::RtcBackend* g_rtc = nullptr;  // setup() 內賦值
```

### 4.3 I2C 腳位常數

新增到 `app_globals.h`（與既有 GPIO 區段並列）：
```cpp
// I2C bus（DS3231 RTC + 未來 CO 感測器電化學型共用）
constexpr uint8_t I2C_SDA_PIN = 42;
constexpr uint8_t I2C_SCL_PIN = 41;
constexpr uint8_t I2C_ADDR_DS3231 = 0x68;
```

---

## 5. time_sync 雙向同步

### 5.1 BLE 對時時反向寫回 DS3231

App 變主時鐘源（user 開 App 等於對時），DS3231 永遠最新：

`main.cpp:236-260` time_sync handler 內，`time_sync_handle()` 回 `Applied` 後：

```cpp
if (r == ems::TimeSyncResult::Applied && g_rtc != nullptr && g_rtc->is_present()) {
    const uint64_t app_epoch = ems::time_sync_current_epoch_ms(&g_ts_state, millis());
    if (g_rtc->set_epoch_ms(app_epoch)) {
        Serial.printf("[RTC] write-back from BLE time_sync: %llu\n",
                      (unsigned long long)app_epoch);
    } else {
        Serial.println("[RTC] WARN write-back failed");
    }
}
```

### 5.2 ems_time_sync 新 API

`firmware/lib/ems_time_sync/ems_time_sync.h` 加：

```cpp
/**
 * 從 RTC 種 seed 軟體對時 state（boot 後若 RTC 有時間，不必等 BLE 連線）。
 * 邏輯：epoch_ms_offset = rtc_epoch - now_millis，後續 current_epoch_ms()
 * 用 millis() + offset 算同等於軟體對時的行為。
 *
 * @param state       要 seed 的 TimeSyncState
 * @param rtc_epoch   從 RTC 讀到的 epoch ms（caller 已確認 > FLOOR）
 * @param now_millis  當下 millis() 值
 */
void time_sync_seed_from_rtc(TimeSyncState* state, uint64_t rtc_epoch,
                             uint64_t now_millis);
```

實作（內部 alias `time_sync_apply()` 邏輯，source 標記 `"rtc"`）：
```cpp
void time_sync_seed_from_rtc(TimeSyncState* state, uint64_t rtc_epoch,
                             uint64_t now_millis) {
    state->synced = true;
    state->epoch_ms_offset = rtc_epoch - now_millis;
    // tz_offset_min 保持原值（DS3231 不存 tz，由 BLE 對時帶）
}
```

---

## 6. case start/end 與 events 改動

### 6.1 ohca_logic.cpp:47-52

```cpp
// 原：
bool ok = storage_save_case(&g_storage_be, EMS_CASE_TYPE_OHCA,
                            events, eventCount,
                            /*case_start_ms*/ 0,
                            /*case_end_ms*/   0);

// 改：
const uint64_t case_start_epoch = caseStartEpochMs;  // input_handler 在 case 開始時取
const uint64_t case_end_epoch   = ems::time_sync_current_epoch_ms(
    &g_ts_state, millis());
bool ok = storage_save_case(&g_storage_be, EMS_CASE_TYPE_OHCA,
                            events, eventCount,
                            case_start_epoch,
                            case_end_epoch);
```

`caseStartEpochMs` 已存在於 `input_handler.cpp:110`（對時前 = 0），DS3231 上機後該值會是真實 epoch。

### 6.2 events[*].timestamp_ms 自動受益

`ohca_logic.cpp:133, 152` 已用 `time_sync_current_epoch_ms()` 算每個 event 的 timestamp。RTC seed 進 `g_ts_state` 後此 API 自動回真實 epoch，**不需動 event 紀錄路徑**。

### 6.3 既有 sessions 不回填

LittleFS 內既存 case 的 `start_ms = 0` 保留不動（**不寫 migration**）：
- 量產上線前資料無實用價值
- 若要寫 sweep 需處理「哪些案件是 RTC 上機前」邊界，複雜度 vs 價值不划算

---

## 7. 改動範圍清單

| 檔案 | 動作 | 估算行數 |
|------|------|---------|
| `firmware/lib/ems_rtc/ems_rtc.h` | **新** RtcBackend 抽象 + NullRtcBackend | ~50 |
| `firmware/lib/ems_rtc/ds3231_backend.h` | **新** DS3231Backend 宣告 | ~30 |
| `firmware/lib/ems_rtc/ds3231_backend.cpp` | **新** RTClib 包裝實作 | ~60 |
| `firmware/lib/ems_time_sync/ems_time_sync.h` | 加 `time_sync_seed_from_rtc()` 宣告 | ~15 |
| `firmware/lib/ems_time_sync/ems_time_sync.cpp` | 實作 seed 函式 | ~10 |
| `firmware/src/app_globals.h` | I2C 腳位常數 + `g_rtc` extern | ~10 |
| `firmware/src/main.cpp` | setup() RTC 初始化 + time_sync 寫回 | ~30 |
| `firmware/src/ohca_logic.cpp` | `case_start/end_ms` 改取真實 epoch | ~10 |
| `firmware/platformio.ini` | 主 env `lib_deps += adafruit/RTClib @ ^2.1.4` | 1 |
| `firmware/test/test_rtc_integration/` | **新** test：mock backend / seed / write-back / NullBackend 降級 | ~150 + 12 cases |
| `firmware/test/test_time_sync/` | 加 `time_sync_seed_from_rtc()` cases | ~80 + 8 cases |
| `docs/gpio-allocation.md §5.4` | 標 DS3231 從「計畫」改「已上機」 | ~5 |
| `tasks/todo.md` 或新 `tasks/ds3231-integration-todo.md` | 任務追蹤 | ~100 |
| **總計** | | **~550 行 + ~20 tests** |

> 起初估 ~250 行偏低；含 test 後實際 ~550。lib + test 是大頭，主韌體改動很小（main.cpp ~30 行 + ohca_logic ~10 行）。

---

## 8. 實作順序（TDD wave）

對齊 `feedback_tdd_alignment_gate_workflow` 慣例：每 wave 先寫 test RED → impl GREEN。

### Wave 1: 抽象 + Null backend（純邏輯，無硬體）
- [ ] `ems_rtc.h` RtcBackend 介面
- [ ] `NullRtcBackend` 實作
- [ ] native test: NullBackend 全 method 行為（is_present=false / now=0 / set 回 false）
- [ ] 預期 ~3 test cases

### Wave 2: time_sync_seed_from_rtc API
- [ ] `test_time_sync` 加 RED test: seed 後 `current_epoch_ms()` 回 rtc+elapsed
- [ ] impl `time_sync_seed_from_rtc()`
- [ ] 邊界 test: rtc=0 不 seed / rtc < FLOOR 不 seed / seed 後再被 BLE time_sync 覆蓋
- [ ] 預期 ~5 test cases

### Wave 3: integration（mock backend）
- [ ] `MockRtcBackend` for test
- [ ] integration test: setup 偵測順序 → seed → case start/end 取真 epoch
- [ ] 預期 ~4 test cases

### Wave 4: DS3231Backend（硬體相依，無 native test）
- [ ] `DS3231Backend` 實作 + begin/now/set
- [ ] platformio.ini 主 env 加 RTClib
- [ ] 編譯驗證 + flash 確認 `[RTC] DS3231 detected` Serial log
- [ ] 拔 RTC 確認 fallback `[RTC] not present`

### Wave 5: time_sync 寫回 + ohca_logic case start/end
- [ ] main.cpp time_sync handler 加寫回邏輯
- [ ] ohca_logic.cpp:47-52 改用真實 epoch
- [ ] 燒實機驗證：boot 立即同步一筆 case，web 端看 started_at_ms 非 0
- [ ] 拔 RTC 重燒：started_at_ms 退回 0（fallback 行為對齊 spec §4.1）

### Wave 6: 文件
- [ ] `docs/gpio-allocation.md §5.4` 從「計畫」→「已上機」
- [ ] `docs/progress.md` 新增進度 7：DS3231 RTC 整合完成
- [ ] 移除 `ohca_logic.cpp:47-48` 的 TODO 註解

---

## 9. 驗收標準

### 9.1 純邏輯（native test）
- [ ] `pio test -e native -f test_rtc_integration` 全綠
- [ ] `pio test -e native -f test_time_sync` 全綠（含新 seed cases）
- [ ] 全 native test suite pass count 增加 ≥ 12

### 9.2 實機（接 DS3231）
- [ ] Serial boot log 印 `[RTC] DS3231 detected at 0x68`
- [ ] 若 RTC 已有時間 → 印 `[RTC] seeded software clock from RTC: NNNN`
- [ ] Boot 後立刻新建 OHCA case → 同步到 web → cases.html 詳細頁 `started_at_ms` 為真實 epoch（非 0）
- [ ] BLE time_sync 後 Serial 印 `[RTC] write-back from BLE time_sync: NNNN`
- [ ] 拔 USB → 等 30 秒 → 重接 USB → 看 RTC 時間沒歸零（電池備援驗證）

### 9.3 實機（不接 DS3231 / fallback）
- [ ] 拔 RTC 重燒 → Serial 印 `[RTC] not present, fallback to BLE time_sync only`
- [ ] 韌體不 boot panic，所有功能正常
- [ ] Boot 後新建 case 未連 BLE → `started_at_ms = 0`（fallback 對齊現況）
- [ ] BLE 連線後新建 case → `started_at_ms` 真實 epoch（軟體對時路徑仍 work）

### 9.4 雙模式互換
- [ ] 同一份 `firmware-merged.bin` 燒進「有 RTC」與「無 RTC」兩台 ESP32 都跑得起來

---

## 10. 風險與已知限制

### 10.1 風險

| 風險 | 機率 | 影響 | 緩解 |
|------|------|------|------|
| RTClib 與 ESP32 Arduino core 版本衝突 | 低 | 編譯失敗 | `src_rtc_demo` 已驗證 RTClib 2.1.4 + 主 env 同 core 版本 |
| I2C bus 與未來 CO 感測器衝突 | 中 | 多裝置同 bus 需協調 | DS3231 地址 0x68 固定；CO 感測器選不同地址即可。`docs/gpio-allocation.md §5.3` 已標可共用 |
| DS3231 電池死導致時間歸零 | 中 | RTC 變垃圾值 | `is_present()` true 但 `now_epoch_ms() < FLOOR` 視為「未對時」，等 BLE seed 後再寫回 |
| boot 時 `Wire.begin()` 失敗導致 hang | 低 | 韌體無法啟動 | RTClib `begin()` 內部有 timeout，且我們有 try-fail-fallback 路徑 |

### 10.2 已知限制

- **tz_offset_min 不寫 DS3231**：DS3231 不存 timezone，只存 epoch。tz 仍由 BLE `time_sync` 帶（重新 boot 後若沒 BLE 連線，tz 退回 0 即 UTC）。可接受：使用者語境下 epoch 是本機時間或 UTC 皆能由 App 端轉換顯示。
- **既有 sessions 不回填 epoch**：LittleFS 內 RTC 上機前的 case meta `start_ms = 0` 保留不動。
- **雙裝置時間 drift**：跨車兩台 ESP32 各自 DS3231 ±2ppm，一年最多差 ~63 秒。實務上每次 BLE 連 App 都會 write-back 對齊，不會累積。

---

## 11. 後續延伸（不在此 plan 內）

- **CO 感測器 I2C** 同 bus 共存（`docs/gpio-allocation.md §5.3`）
- **RTC alarm 用作 deep sleep wake**（量產省電考量，Prod-Phase 評估）
- **NTP over BLE App**：App 端定時 NTP → 透過 time_sync 推給韌體 → 寫 DS3231，整鏈路 ms 級準確

---

## 12. cold-start 接續指引（給未來的 session）

下個 session 接此計畫時：

1. **讀本檔**（整段戰略 + 介面 + 改動 + wave）
2. **看 git log**：`git log --oneline | grep -iE "ds3231|rtc"` 確認上次 commit 軌跡
3. **看 `src_rtc_demo/main.cpp`**：理解 RTClib API 用法（DS3231Backend 實作可直接借鑑）
4. **跑 `~/.platformio/penv/bin/pio run -e rtc-demo -t upload`** 確認硬體仍在線（避免硬體先壞）
5. **從 Wave 1 開工**：先建 `firmware/lib/ems_rtc/` + test，純邏輯不依賴硬體可快速 GREEN
6. **每 wave 完成跑 `pio test -e native`** 驗證全綠不退步
7. **Wave 4 燒實機前** 先 `pio run -e esp32-s3-devkitc-1` 確認 lib_deps 加 RTClib 後主 env 仍編得過

---

## 附錄：相關文件索引

| 文件 | 關係 |
|------|------|
| `docs/gpio-allocation.md §5.4` | DS3231 GPIO 配置與 I2C bus 共用規劃 |
| `docs/EMS_DoseSync_Pro_Prototype_V1.md` §4.1 | 未對時 timestamp = 0 spec 規範 |
| `docs/progress.md` 進度 6 | Phase F BLE 鏈路完成（DS3231 為下一里程碑） |
| `firmware/src_rtc_demo/main.cpp` | DS3231 硬體 demo（驗證讀寫 + 電池備援） |
| `firmware/lib/ems_time_sync/` | 既有軟體對時 lib（DS3231 + 它互補） |
| `firmware/src/ohca_logic.cpp:47-52` | case start/end hard-code 0 的 TODO 註解 |
| CLAUDE.md `## Dev-Phase 2 設計決策` | Dev-Phase 3 升級 DS3231 的決策理由 |
