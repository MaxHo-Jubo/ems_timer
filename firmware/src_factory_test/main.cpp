// ============================================================
// Factory Test（產測）— 廠商焊接驗收韌體
// ------------------------------------------------------------
// 用途：燒進廠商焊好的原型，一次檢查 TFT／I2C（DS3231、附掛 EEPROM、電量計排針）／
//       RTC 走時與備援電池／8 顆按鍵依序驗收／蜂鳴器，結果直接畫在 TFT 上，廠商不需要 serial monitor。
//       對外判讀說明：docs/vendor-assembly-brief.html §7；PASS／FAIL 規則在 lib/ems_factory_test/
// 流程：上電 → 彩條 → 掃 I2C → RTC 第一次上電失憶時 seed 並要求「斷電再上電」驗證 CR2032 →
//       依序按 8 顆按鍵 → PASS。任何 FAIL 都鎖住到按 RST 重測。
// RTC：不用 RTClib，直接用 Wire 讀寫 DS3231 暫存器，每次讀寫都拿得到成功／失敗，
//       讀值再經 lib 的 BCD／範圍驗證，亂資料不會被當成走時（codex review CRITICAL）
// 斷電偵測：兩個獨立證據都成立才承認「板子真的斷過電」，缺一都退回未驗證（不會誤判 OK）：
//       (1) DS3231 自己的時鐘：執行中每 2 秒把 RTC 秒數寫進 NVS，開機時「RTC 讀值 − NVS 最後記錄 −
//           ESP 開機至今秒數」= 板子離線秒數，≥ 5 秒才算；按一下 RST 只會量到 ≤ 2 秒
//       (2) ESP32 RTC slow memory 的 magic（RTC_NOINIT_ATTR）：真斷電後內容隨機；按住 RST 超過 5 秒
//           時 3V3 仍在、RTC 仍由 VCC 走時，靠這個擋。RST 是否保留 RTC memory 未實機驗證，但兩種
//           情況都安全：保留 → 擋住按住 RST；不保留 → 退化成只剩證據 (1)，最壞是多要求一次真斷電
//       OSF 亮永遠優先（新的失憶證據）。NVS 走 ESP-IDF nvs API 直接拿 esp_err_t，讀寫失敗與 key 不存在
//       分開處理，失敗或讀到越界值即 FAIL: NVS ERROR
// 畫面：只用 LovyanGFX 內建 ASCII 字型，刻意不碰 vlw 中文字集——新增中文字串要重生字集，
//       而 scripts/regen_vlw.sh 的 SRC_FILES 是 allowlist，本工具不在裡面（feedback_vlw_header_sync）
// 接線：docs/gpio-allocation.md 速查表。本檔的腳位常數是它的副本（與其他 src_* 工具同一慣例），
//       改腳位時主韌體 app_globals.h 與本檔都要改
// 開發板重複測試：換了 RTC 模組後 NVS 的「已 seed」旗標會誤判成電池失效，
//       開機時按住主按鍵（GPIO 4）可清掉 NVS 三個 key（已 seed／最後時間／備援結論）
// 燒錄：pio run -e factory-test -t upload
// 交付：post-build 產出 firmware-merged-factory-test.bin，複製成 release-template/firmware-merged.bin
//       打包（見 release-template/HOW_TO_BUILD_RELEASE.md）
// 監看（選用）：pio device monitor -e factory-test
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <nvs.h>
#include <cstring>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "factory_test_logic.h"

// ── 腳位（docs/gpio-allocation.md 速查表副本）─────────────────────
static constexpr int8_t  PIN_TFT_SCLK = 3;   // TFT SPI 時脈
static constexpr int8_t  PIN_TFT_MOSI = 2;   // TFT SPI 資料
static constexpr int8_t  PIN_TFT_DC   = 1;   // TFT 資料／指令
static constexpr int8_t  PIN_TFT_CS   = 21;  // TFT 晶片選擇
static constexpr int8_t  PIN_TFT_RST  = 47;  // TFT 硬體重置
static constexpr int8_t  PIN_NONE     = -1;  // LovyanGFX「此腳不接」
static constexpr int     PIN_I2C_SDA  = 42;  // DS3231 + 電量計排針共用
static constexpr int     PIN_I2C_SCL  = 41;  // 同上
static constexpr uint8_t PIN_BUZZER   = 14;  // 主動蜂鳴器直驅
static constexpr uint8_t PIN_WS2812   = 48;  // 開發板自帶 RGB LED，只用來關掉

// 8 顆按鍵 GPIO，索引對齊 ems::ft_button_label()（PRIMARY, UP, DOWN, POWER, RECORD, BACK, EPI, SHOCK）
static constexpr uint8_t BTN_PINS[ems::FT_BUTTON_COUNT] = {4, 5, 6, 7, 15, 16, 17, 18};

// 開機時按住哪顆按鍵可清 NVS「已 seed」旗標（索引 0 = 主按鍵 GPIO 4）
static constexpr uint8_t BTN_CLEAR_SEED_INDEX = 0;

// ── I2C ─────────────────────────────────────────────────────────
static constexpr uint32_t I2C_CLOCK_HZ = 100000;  // 掃描階段用標準 100kHz 求穩（與 i2c-scan 一致）
static constexpr uint8_t  ADDR_GAUGE   = 0x36;    // 電量計（MAX17043／MAX17048 同位址），本次留排針
static constexpr uint8_t  ADDR_EEPROM  = 0x57;    // AT24C32，DS3231 模組附掛
static constexpr uint8_t  ADDR_RTC     = 0x68;    // DS3231

// ── DS3231 暫存器（datasheet Figure 1）────────────────────────────
static constexpr uint8_t DS3231_REG_TIME     = 0x00;  // 秒/分/時/星期/日/月/年，7 bytes
static constexpr uint8_t DS3231_REG_CONTROL  = 0x0E;  // 控制：bit7 EOSC（1 = 電池供電時停振）
static constexpr uint8_t DS3231_REG_STATUS   = 0x0F;  // 狀態：bit7 OSF（1 = 曾停振／失憶）
static constexpr uint8_t DS3231_CONTROL_EOSC = 0x80;
static constexpr uint8_t DS3231_STATUS_OSF   = 0x80;

// RTC 失憶時 seed 的時間：2026-01-01（四）00:00:00，BCD，24 小時制。
// 絕對值不影響判定（§7.4 明寫時間值不列入），只要是合法時間讓走時判定有東西比
static constexpr uint8_t RTC_SEED_REGS[ems::FT_RTC_RAW_LEN] = {0x00, 0x00, 0x00, 0x04, 0x01, 0x01, 0x26};

// ── NVS（ESP-IDF nvs API；不用 Preferences，它把讀取失敗與 key 不存在都回預設值）──
static constexpr const char* NVS_NAMESPACE             = "ftest";       // 命名空間
static constexpr const char* NVS_KEY_RTC_SEEDED        = "rtc_seeded";  // u8：本機曾對 RTC seed 過時間
static constexpr const char* NVS_KEY_LAST_SEEN         = "last_seen";   // u32：執行中最後記錄的 RTC 秒數（ft_rtc_time_to_seconds）
static constexpr const char* NVS_KEY_BACKUP            = "backup";      // u8：上次的 FtRtcBackup 結論
static constexpr uint32_t    NVS_LAST_SEEN_INTERVAL_MS = 2000;          // last_seen 寫入週期；10 分鐘產測約 300 次寫入，NVS 磨損可忽略
static constexpr uint32_t    MS_PER_SECOND             = 1000;          // millis → 秒

// ── ESP32 RTC slow memory：斷電證據 (2) ──────────────────────────
static constexpr uint32_t RTC_MEM_MAGIC = 0x46545332;  // "FTS2"：上次開機寫入，真斷電後內容隨機

// ── 時序 ────────────────────────────────────────────────────────
static constexpr uint32_t SERIAL_BAUD         = 115200;  // USB-CDC 速率（monitor_speed 同值）
static constexpr uint32_t SERIAL_WAIT_MS      = 3000;    // 等 USB-CDC 列舉的上限，沒接電腦也不卡
static constexpr uint32_t SERIAL_WAIT_STEP_MS = 10;      // 等列舉時的輪詢間隔
static constexpr uint32_t COLOR_BAR_MS        = 1500;    // 開機彩條停留時間
static constexpr uint32_t POLL_INTERVAL_MS    = 1000;    // I2C／RTC 輪詢與重繪週期
static constexpr uint32_t DEBOUNCE_MS         = 50;      // 按鍵去彈跳，press 與 release 共用同一門檻
static constexpr uint32_t LOOP_IDLE_MS        = 5;       // 主迴圈每圈讓出的時間（按鍵取樣間隔）
static constexpr uint32_t BEEP_SHORT_MS       = 60;      // 開機與按鍵確認音
static constexpr uint32_t BEEP_LONG_MS        = 300;     // PASS 提示音（兩長聲）
static constexpr uint32_t BEEP_GAP_MS         = 150;     // 兩長聲之間的間隔

// ── 畫面 ────────────────────────────────────────────────────────
static constexpr int16_t  SCREEN_W          = 320;       // rotation=3 橫向後的邏輯寬
static constexpr int16_t  SCREEN_H          = 240;
static constexpr uint16_t TFT_PANEL_W       = 240;       // 面板原生寬（直向）
static constexpr uint16_t TFT_PANEL_H       = 320;       // 面板原生高（直向）
static constexpr uint8_t  TFT_ROTATION      = 3;         // 橫向 320x240（與主韌體一致）
static constexpr uint32_t TFT_SPI_WRITE_HZ  = 80000000;  // 與主韌體 LGFX 設定同步
static constexpr uint32_t TFT_SPI_READ_HZ   = 16000000;
static constexpr uint8_t  FRAME_COLOR_DEPTH = 16;        // sprite 色深（RGB565）

// RGB565。型別必須是 uint16_t：LovyanGFX 依實參型別判斷色彩空間，uint32_t 會被當 RGB888
// （feedback_lovyangfx_color_type_deduction）
static constexpr uint16_t COLOR_BLACK = 0x0000;
static constexpr uint16_t COLOR_WHITE = 0xFFFF;
static constexpr uint16_t COLOR_RED   = 0xF800;
static constexpr uint16_t COLOR_GREEN = 0x07E0;
static constexpr uint16_t COLOR_BLUE  = 0x001F;
static constexpr uint16_t COLOR_AMBER = 0xFD20;
static constexpr uint16_t COLOR_GRAY  = 0x8410;

// 版面：左邊界、各區塊的 y 起點、行距（Font2 高 16px）
static constexpr int16_t LAYOUT_X_LEFT        = 8;
static constexpr int16_t LAYOUT_X_INDENT      = 48;   // I2C／RTC／BTN 標籤後的內容欄
static constexpr int16_t LAYOUT_X_BTN_COL2    = 176;  // 按鍵表第二欄
static constexpr int16_t LAYOUT_X_RTC_TICK    = 208;  // RTC 走時狀態欄（接在時間字串右側）
static constexpr int16_t LAYOUT_X_VERSION     = 280;  // 右上角版本字
static constexpr int16_t LAYOUT_Y_TITLE       = 4;
static constexpr int16_t LAYOUT_Y_I2C         = 30;
static constexpr int16_t LAYOUT_Y_RTC         = 90;
static constexpr int16_t LAYOUT_Y_RTC_BACKUP  = 108;  // RTC 區第二行：備援電池狀態
static constexpr int16_t LAYOUT_Y_BTN         = 130;
static constexpr int16_t LAYOUT_Y_RESULT      = 206;
static constexpr int16_t LAYOUT_LINE_H        = 18;
static constexpr int16_t FAIL_REASON_Y_OFFSET = 6;    // Font2（16px）原因字在 Font4（26px）行內約略垂直置中

// 畫面文字 buffer 長度（最長是 FAIL 原因 + 細節，例如 "BUTTON ORDER exp 3 got 5"）
static constexpr size_t TEXT_BUF_LEN = 48;

// ── LovyanGFX 硬體配置（與 app_globals.h 的 class LGFX 同步）────
/** ST7789 240x320 走 ESP32-S3 GP-SPI2 + DMA 的面板物件；欄位值與主韌體一致，改動要兩邊同步 */
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;  // 面板驅動
    lgfx::Bus_SPI      _bus;    // SPI bus
public:
    /** 建構時一次填好 bus 與 panel 設定（無參數、無回傳） */
    LGFX(void) {
        {
            auto cfg = _bus.config();  // SPI bus 設定副本，填完寫回
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = TFT_SPI_WRITE_HZ;
            cfg.freq_read   = TFT_SPI_READ_HZ;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = PIN_TFT_SCLK;
            cfg.pin_mosi    = PIN_TFT_MOSI;
            cfg.pin_miso    = PIN_NONE;
            cfg.pin_dc      = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();  // 面板設定副本，填完寫回
            cfg.pin_cs          = PIN_TFT_CS;
            cfg.pin_rst         = PIN_TFT_RST;
            cfg.pin_busy        = PIN_NONE;
            cfg.panel_width     = TFT_PANEL_W;
            cfg.panel_height    = TFT_PANEL_H;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;  // ST7789 讀取前的 dummy bits（本檔不讀面板，沿用主韌體值）
            cfg.dummy_read_bits  = 1;
            cfg.readable   = false;
            cfg.invert     = false;
            cfg.rgb_order  = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

// ── 全域物件與狀態 ───────────────────────────────────────────────
static LGFX              tft;                 // 實體面板
static lgfx::LGFX_Sprite frame(&tft);         // 全頁 sprite，一次 DMA 推上去避免閃爍
static bool              frameReady = false;  // createSprite 成功才走 sprite，失敗退直接畫
static nvs_handle_t      nvsHandle = 0;       // ESP-IDF NVS handle（loadNvsState 成功後有效）
static bool              nvsReady  = false;   // NVS 已開啟且沒出過錯，才允許讀寫

RTC_NOINIT_ATTR static uint32_t g_rtcMemMagic;  // RTC slow memory：== RTC_MEM_MAGIC 代表上次開機以來沒斷過電
static bool espPoweredOff = false;              // 本次開機 magic 不在 → ESP 真的斷過電（斷電證據 (2)）

static ems::FactoryTestState g_state;  // 判定輸入，只由純函式產生的新值整體替換

/** 按鍵去彈跳狀態，每顆一份；press 事件在 stableLevel 由 HIGH→LOW 時發 */
struct BtnDebounce {
    uint8_t  rawLevel;     // 最近一次 digitalRead 的原始準位
    uint8_t  stableLevel;  // 通過 DEBOUNCE_MS 門檻後才承認的準位
    uint32_t rawChangeMs;  // rawLevel 上次變動的 millis()
};
static BtnDebounce btnState[ems::FT_BUTTON_COUNT];

// RTC 走時追蹤：上次讀到的秒數與它上次變化的時刻；hasRtcSample 避免用 0 當「從未取樣」哨兵
static bool     hasRtcSample    = false;
static uint32_t lastRtcSeconds  = 0;
static uint32_t lastRtcChangeMs = 0;
static char     rtcTimeText[20] = "--:--:--";  // 顯示用 "YYYY-MM-DD HH:MM:SS"

static bool     rtcInitialised  = false;  // 本次 RTC 出現後已讀過 OSF／必要時 seed 完成
static bool     rtcSeededBefore = false;  // NVS：本機曾對 RTC seed 過時間
static bool     hasLastSeen     = false;  // NVS：有上次記錄的 RTC 秒數
static uint32_t lastSeenSeconds = 0;      // NVS：上次記錄的 RTC 秒數
static ems::FtRtcBackup storedBackup = ems::FtRtcBackup::Unverified;  // NVS：上次的備援結論
static bool     hasNvsWritten   = false;  // 與 lastNvsWriteMs 分開，0 不當哨兵
static uint32_t lastNvsWriteMs  = 0;      // 上次寫 last_seen 的時刻
static uint32_t lastPollMs      = 0;      // 上次輪詢時刻
static bool     hasPolled       = false;  // 與 lastPollMs 分開，0 不當哨兵
static bool     passAnnounced   = false;  // PASS 兩長聲只響一次
static bool     dirty           = true;   // 有狀態變化才重繪與印 serial

// ── 基本 I/O ────────────────────────────────────────────────────

/**
 * 蜂鳴器響一聲（主動式，digitalWrite 直驅；阻塞 ms 毫秒）
 * @param ms 響的長度
 */
static void beep(uint32_t ms) {
    // STEP 01: 拉高、等、拉低
    digitalWrite(PIN_BUZZER, HIGH);
    delay(ms);
    digitalWrite(PIN_BUZZER, LOW);
}

/**
 * 從 I2C 裝置連續讀暫存器，任何一段失敗都回 false（不拿半筆資料）
 * @param addr 7-bit 位址
 * @param reg  起始暫存器
 * @param buf  輸出 buffer
 * @param len  要讀的 bytes
 * @return true = 位址 ACK、requestFrom 恰好拿到 len bytes
 */
static bool i2cReadRegs(uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
    // STEP 01: 送暫存器位址，repeated START 不放開 bus
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != ems::FT_I2C_CODE_ACK) {
        return false;
    }

    // STEP 02: 要求 len bytes，數量不足就是讀取未完成
    if (Wire.requestFrom(addr, len) != len) {
        return false;
    }
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}

/**
 * 對 I2C 裝置連續寫暫存器
 * @param addr 7-bit 位址
 * @param reg  起始暫存器
 * @param buf  要寫的資料
 * @param len  bytes 數
 * @return true = 整段 ACK
 */
static bool i2cWriteRegs(uint8_t addr, uint8_t reg, const uint8_t* buf, uint8_t len) {
    // STEP 01: 位址 + 暫存器 + 資料一次送完
    Wire.beginTransmission(addr);
    Wire.write(reg);
    for (uint8_t i = 0; i < len; i++) {
        Wire.write(buf[i]);
    }
    return Wire.endTransmission() == ems::FT_I2C_CODE_ACK;
}

/**
 * 探測單一 I2C 位址（只送位址不送資料）
 * @param addr 7-bit 位址
 * @return Wire.endTransmission() 回傳碼，分類交給 lib
 */
static uint8_t probeI2c(uint8_t addr) {
    // STEP 01: START + ADDR + STOP
    Wire.beginTransmission(addr);
    return Wire.endTransmission();
}

// ── NVS ─────────────────────────────────────────────────────────

/** NVS 讀取結果：把「key 不存在」（合法的初始狀態）與「讀取失敗」（必須 FAIL）分開 */
enum class NvsRead : uint8_t { Ok, NotFound, Error };

/**
 * 把 nvs_get_* 的 esp_err_t 分成三類並在失敗時印原因
 * @param key 讀的 key（log 用）
 * @param err IDF 回傳碼
 * @return Ok / NotFound / Error
 */
static NvsRead classifyNvsRead(const char* key, esp_err_t err) {
    // STEP 01: 三類
    if (err == ESP_OK) {
        return NvsRead::Ok;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return NvsRead::NotFound;
    }
    Serial.printf("[NVS] get %s failed: %s\n", key, esp_err_to_name(err));
    return NvsRead::Error;
}

/**
 * 讀 u8
 * @param key key
 * @param out 讀值
 * @return 見 NvsRead
 */
static NvsRead nvsGetU8(const char* key, uint8_t& out) {
    // STEP 01: 未就緒直接算失敗
    if (!nvsReady) {
        return NvsRead::Error;
    }
    return classifyNvsRead(key, nvs_get_u8(nvsHandle, key, &out));
}

/**
 * 讀 u32
 * @param key key
 * @param out 讀值
 * @return 見 NvsRead
 */
static NvsRead nvsGetU32(const char* key, uint32_t& out) {
    // STEP 01: 未就緒直接算失敗
    if (!nvsReady) {
        return NvsRead::Error;
    }
    return classifyNvsRead(key, nvs_get_u32(nvsHandle, key, &out));
}

/**
 * 寫入後 commit 的共用收尾
 * @param key     key（log 用）
 * @param setErr  nvs_set_* 的回傳碼
 * @return true = set 與 commit 都成功
 */
static bool finishNvsWrite(const char* key, esp_err_t setErr) {
    // STEP 01: set 成功才 commit
    esp_err_t err = setErr;  // 最終回傳碼
    if (err == ESP_OK) {
        err = nvs_commit(nvsHandle);
    }
    if (err != ESP_OK) {
        Serial.printf("[NVS] write %s failed: %s\n", key, esp_err_to_name(err));
        return false;
    }
    return true;
}

/**
 * 寫 u8 並 commit
 * @param key   key
 * @param value 值
 * @return true = 成功
 */
static bool nvsSetU8(const char* key, uint8_t value) {
    // STEP 01: 未就緒直接算失敗
    if (!nvsReady) {
        return false;
    }
    return finishNvsWrite(key, nvs_set_u8(nvsHandle, key, value));
}

/**
 * 寫 u32 並 commit
 * @param key   key
 * @param value 值
 * @return true = 成功
 */
static bool nvsSetU32(const char* key, uint32_t value) {
    // STEP 01: 未就緒直接算失敗
    if (!nvsReady) {
        return false;
    }
    return finishNvsWrite(key, nvs_set_u32(nvsHandle, key, value));
}

/**
 * 刪 key 並 commit；key 本來就不存在視為成功
 * @param key key
 * @return true = 成功
 */
static bool nvsEraseKey(const char* key) {
    // STEP 01: 未就緒直接算失敗
    if (!nvsReady) {
        return false;
    }

    // STEP 02: 不存在 = 已達目的
    const esp_err_t err = nvs_erase_key(nvsHandle, key);  // IDF 回傳碼
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    return finishNvsWrite(key, err);
}

// ── 畫面 ────────────────────────────────────────────────────────

/**
 * 開機彩條：紅／綠／藍／白四條直條直接畫到面板，讓廠商目視 TFT 五條 SPI 線與電源是否正常
 */
static void drawColorBars() {
    // STEP 01: 依條數等分寬度，依序填色（增減彩條只要改這個陣列）
    const uint16_t bars[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_WHITE};
    const int barCount = sizeof(bars) / sizeof(bars[0]);  // 彩條數
    const int16_t barW = SCREEN_W / barCount;             // 每條寬度
    for (int i = 0; i < barCount; i++) {
        tft.fillRect(barW * i, 0, barW, SCREEN_H, bars[i]);
    }

    // STEP 02: 停留讓人看清楚
    delay(COLOR_BAR_MS);
}

/**
 * 本輪要畫的目標：sprite 建得起來就畫 sprite（整頁一次 DMA 推送不閃爍），失敗則直接畫面板。
 * 所有繪圖函式都經過這裡，避免各處各自寫一份 frameReady 三元判斷而分歧。
 * @return frame 或 tft 的 LovyanGFX 基底參考
 */
static lgfx::LovyanGFX& canvas() {
    // STEP 01: 依 sprite 是否可用選目標
    if (frameReady) {
        return frame;
    }
    return tft;
}

/**
 * 在指定座標畫一段文字（背景填黑，蓋掉同位置舊字）
 * @param x     文字左緣 x
 * @param y     文字上緣 y
 * @param text  內容
 * @param color 文字顏色
 */
static void drawText(int16_t x, int16_t y, const char* text, uint16_t color) {
    // STEP 01: 設色後畫
    lgfx::LovyanGFX& g = canvas();  // 本輪繪圖目標
    g.setTextColor(color, COLOR_BLACK);
    g.drawString(text, x, y);
}

/**
 * 畫 I2C 三行：RTC 必要、EEPROM 與電量計選配
 */
static void drawI2cBlock() {
    char line[TEXT_BUF_LEN];  // 每行組字 buffer
    drawText(LAYOUT_X_LEFT, LAYOUT_Y_I2C, "I2C", COLOR_GRAY);

    // STEP 01: RTC 必要件
    snprintf(line, sizeof(line), "RTC    0x68  %s", g_state.rtc_present ? "OK" : "MISSING");
    drawText(LAYOUT_X_INDENT, LAYOUT_Y_I2C, line, g_state.rtc_present ? COLOR_GREEN : COLOR_RED);

    // STEP 02: EEPROM 選配
    snprintf(line, sizeof(line), "EEPROM 0x57  %s", g_state.eeprom_present ? "OK" : "--");
    drawText(LAYOUT_X_INDENT, LAYOUT_Y_I2C + LAYOUT_LINE_H, line, g_state.eeprom_present ? COLOR_GREEN : COLOR_GRAY);

    // STEP 03: 電量計選配，本次留排針應為 NOT FITTED
    snprintf(line, sizeof(line), "GAUGE  0x36  %s", g_state.gauge_present ? "PRESENT" : "NOT FITTED");
    drawText(LAYOUT_X_INDENT, LAYOUT_Y_I2C + LAYOUT_LINE_H * 2, line, g_state.gauge_present ? COLOR_AMBER : COLOR_GRAY);
}

/**
 * 畫 RTC 兩行：時間 + 走時狀態、備援電池狀態
 */
static void drawRtcBlock() {
    drawText(LAYOUT_X_LEFT, LAYOUT_Y_RTC, "RTC", COLOR_GRAY);

    // STEP 01: 依走時狀態選字與顏色（I/O 錯誤時時間字串沒意義，直接標 READ ERR）
    const char* tickText  = "TICK ?";     // 走時狀態字
    uint16_t    tickColor = COLOR_GRAY;   // 走時狀態色
    if (g_state.rtc_io_error) {
        tickText  = "READ ERR";
        tickColor = COLOR_RED;
    } else if (g_state.rtc_tick == ems::FtRtcTick::Ok) {
        tickText  = "TICK OK";
        tickColor = COLOR_GREEN;
    } else if (g_state.rtc_tick == ems::FtRtcTick::Stuck) {
        tickText  = "STUCK";
        tickColor = COLOR_RED;
    }
    drawText(LAYOUT_X_INDENT, LAYOUT_Y_RTC, rtcTimeText, COLOR_WHITE);
    drawText(LAYOUT_X_RTC_TICK, LAYOUT_Y_RTC, tickText, tickColor);

    // STEP 02: 備援電池狀態（RTC 不在線時不畫，避免誤導）
    if (!g_state.rtc_present) {
        return;
    }
    uint16_t backupColor = COLOR_AMBER;  // Unverified 的顏色，其他狀態下面覆寫
    if (g_state.rtc_backup == ems::FtRtcBackup::Ok) {
        backupColor = COLOR_GREEN;
    } else if (g_state.rtc_backup == ems::FtRtcBackup::Lost) {
        backupColor = COLOR_RED;
    }
    drawText(LAYOUT_X_INDENT, LAYOUT_Y_RTC_BACKUP, ems::ft_rtc_backup_label(g_state.rtc_backup), backupColor);
}

/**
 * 該顆按鍵是否為錯接／短路事件裡的當事鍵（畫紅字用）
 * @param index 按鍵索引
 * @return true = 在 button_err_a／b 之列
 */
static bool isButtonInError(uint8_t index) {
    // STEP 01: 沒錯誤就沒有當事鍵
    if (g_state.button_error == ems::FtButtonError::None) {
        return false;
    }

    // STEP 02: WrongOrder 只標實際按到的那顆；Multiple 兩顆都標
    if (g_state.button_error == ems::FtButtonError::Multiple) {
        return index == g_state.button_err_a || index == g_state.button_err_b;
    }
    return index == g_state.button_err_b;
}

/**
 * 畫 8 顆按鍵兩欄四列：已依序驗收的打勾綠字，錯接／短路的當事鍵紅字
 */
static void drawButtonBlock() {
    char line[TEXT_BUF_LEN];  // 每格組字 buffer
    drawText(LAYOUT_X_LEFT, LAYOUT_Y_BTN, "BTN", COLOR_GRAY);

    // STEP 01: 逐顆畫；偶數索引左欄、奇數索引右欄，每兩顆一列
    for (uint8_t i = 0; i < ems::FT_BUTTON_COUNT; i++) {
        // STEP 01.01: 狀態 → 記號與顏色
        const bool done  = (i < g_state.buttons_done);          // 已驗收
        const bool isErr = isButtonInError(i);                  // 出錯的當事鍵
        const char mark  = isErr ? '!' : (done ? 'x' : ' ');    // 格內記號
        uint16_t   color = COLOR_WHITE;                         // 該格顏色
        if (isErr) {
            color = COLOR_RED;
        } else if (done) {
            color = COLOR_GREEN;
        }

        // STEP 01.02: 座標與繪製
        const int16_t x = (i % 2 == 0) ? LAYOUT_X_INDENT : LAYOUT_X_BTN_COL2;  // 欄
        const int16_t y = LAYOUT_Y_BTN + LAYOUT_LINE_H * (i / 2);              // 列
        snprintf(line, sizeof(line), "[%c]%u %s", mark, i + 1, ems::ft_button_label(i));
        drawText(x, y, line, color);
    }
}

/**
 * 組 FAIL 原因後面的細節字（按鍵顯示 1-based 編號，對齊廠商文件 §4.3）
 * 分派依據是 lib 選出的同一個失敗種類，本檔不另維護一份優先序
 * @param kind ft_fail_kind() 的結果
 * @param buf  輸出 buffer
 * @param len  buffer 長度
 */
static void formatFailDetail(ems::FtFailKind kind, char* buf, size_t len) {
    // STEP 01: 只有三種失敗有細節，其餘空字串
    buf[0] = '\0';
    switch (kind) {
        case ems::FtFailKind::BusError:
            snprintf(buf, len, " code %u", g_state.i2c_error_code);
            break;
        case ems::FtFailKind::ButtonOrder:
            snprintf(buf, len, " exp %u got %u", g_state.button_err_a + 1, g_state.button_err_b + 1);
            break;
        case ems::FtFailKind::ButtonShort:
            snprintf(buf, len, " %u+%u", g_state.button_err_a + 1, g_state.button_err_b + 1);
            break;
        default:
            break;
    }
}

/**
 * 畫最後一列判定。
 * PASS／WAITING 整行用 Font4（26px）；FAIL 只有「FAIL:」用 Font4，原因與細節接在後面用 Font2，
 * 否則整行 Font4 會超出 320px 寬被切掉。
 * @param verdict 本輪判定
 */
static void drawResultLine(ems::FtVerdict verdict) {
    char line[TEXT_BUF_LEN];        // 組字 buffer
    lgfx::LovyanGFX& g = canvas();  // 本輪繪圖目標
    g.setFont(&fonts::Font4);

    // STEP 01: PASS 綠色整行
    if (verdict == ems::FtVerdict::Pass) {
        drawText(LAYOUT_X_LEFT, LAYOUT_Y_RESULT, ems::ft_verdict_label(verdict), COLOR_GREEN);
        g.setFont(&fonts::Font2);
        return;
    }

    // STEP 02: FAIL 大字 + 原因小字；原因的 x 由大字實際寬度決定，不寫死
    if (verdict == ems::FtVerdict::Fail) {
        snprintf(line, sizeof(line), "%s: ", ems::ft_verdict_label(verdict));
        drawText(LAYOUT_X_LEFT, LAYOUT_Y_RESULT, line, COLOR_RED);
        const int16_t reasonX = LAYOUT_X_LEFT + static_cast<int16_t>(g.textWidth(line));  // 原因起點
        const ems::FtFailKind kind = ems::ft_fail_kind(g_state);  // 種類、字串、細節同源
        char detail[TEXT_BUF_LEN];  // 原因後的細節
        formatFailDetail(kind, detail, sizeof(detail));
        snprintf(line, sizeof(line), "%s%s", ems::ft_fail_reason_for(kind), detail);
        g.setFont(&fonts::Font2);
        drawText(reasonX, LAYOUT_Y_RESULT + FAIL_REASON_Y_OFFSET, line, COLOR_RED);
        return;
    }

    // STEP 03: Pending 卻沒有等待原因 = 判定與原因規則分歧，是程式錯誤，明講而不是畫一個空的 WAITING
    const char* pending = ems::ft_pending_reason(g_state);  // 等待原因
    if (pending == nullptr) {
        drawText(LAYOUT_X_LEFT, LAYOUT_Y_RESULT, "INTERNAL ERROR", COLOR_RED);
        g.setFont(&fonts::Font2);
        return;
    }

    // STEP 04: WAITING 琥珀色：等按鍵時附進度，其他情況附等待原因
    if (strcmp(pending, "BUTTONS") == 0) {
        snprintf(line, sizeof(line), "%s %u/%u", ems::ft_verdict_label(verdict), g_state.buttons_done, ems::FT_BUTTON_COUNT);
    } else {
        snprintf(line, sizeof(line), "%s %s", ems::ft_verdict_label(verdict), pending);
    }
    drawText(LAYOUT_X_LEFT, LAYOUT_Y_RESULT, line, COLOR_AMBER);
    g.setFont(&fonts::Font2);
}

/**
 * 重繪整頁並推到面板
 * @param verdict 本輪判定
 */
static void render(ems::FtVerdict verdict) {
    // STEP 01: 清畫布、設字型
    lgfx::LovyanGFX& g = canvas();  // 本輪繪圖目標
    g.fillScreen(COLOR_BLACK);
    g.setFont(&fonts::Font2);

    // STEP 02: 各區塊
    drawText(LAYOUT_X_LEFT, LAYOUT_Y_TITLE, "EMS FACTORY TEST", COLOR_WHITE);
    drawText(LAYOUT_X_VERSION, LAYOUT_Y_TITLE, "v2", COLOR_GRAY);
    drawI2cBlock();
    drawRtcBlock();
    drawButtonBlock();
    drawResultLine(verdict);

    // STEP 03: sprite 模式一次推上去
    if (frameReady) {
        frame.pushSprite(0, 0);
    }
}

// ── I2C 掃描與 RTC ───────────────────────────────────────────────

/**
 * 掃描三個已知位址，交給純函式產生新狀態（在線旗標每輪重算，錯誤黏性）
 * @param s 目前狀態
 * @return 更新後狀態
 */
static ems::FactoryTestState scanI2c(const ems::FactoryTestState& s) {
    // STEP 01: 三個位址各探測一次
    const ems::FtI2cScan scan(probeI2c(ADDR_RTC), probeI2c(ADDR_EEPROM), probeI2c(ADDR_GAUGE));

    // STEP 02: 純函式套用
    return ems::ft_apply_i2c_scan(s, scan);
}

/**
 * 標記 RTC I/O 錯誤（黏性）並印出上下文
 * @param s       目前狀態
 * @param context 哪一步失敗（serial 用）
 * @return 帶 rtc_io_error 的新狀態
 */
static ems::FactoryTestState markRtcIoError(const ems::FactoryTestState& s, const char* context) {
    // STEP 01: 第一次出現才印，避免每秒刷
    if (!s.rtc_io_error) {
        Serial.printf("[RTC] I/O error: %s\n", context);
    }

    // STEP 02: 新狀態帶錯誤旗標，取樣歸零
    ems::FactoryTestState next = s;  // 新狀態
    next.rtc_io_error = true;
    hasRtcSample = false;
    return next;
}

/**
 * RTC 失憶時 seed 一個合法時間並清 OSF／EOSC，讓走時判定有東西比、電池供電時晶振不停
 * @return true = 三段讀寫都 ACK
 */
static bool rtcSeedTime() {
    // STEP 01: 寫 7 個時間暫存器
    if (!i2cWriteRegs(ADDR_RTC, DS3231_REG_TIME, RTC_SEED_REGS, ems::FT_RTC_RAW_LEN)) {
        return false;
    }

    // STEP 02: 清 EOSC（確保電池供電時晶振照跑），其餘控制位元不動
    uint8_t control = 0;  // 控制暫存器讀值
    if (!i2cReadRegs(ADDR_RTC, DS3231_REG_CONTROL, &control, 1)) {
        return false;
    }
    control = static_cast<uint8_t>(control & ~DS3231_CONTROL_EOSC);
    if (!i2cWriteRegs(ADDR_RTC, DS3231_REG_CONTROL, &control, 1)) {
        return false;
    }

    // STEP 03: 清 OSF，下次上電若又亮就是電池沒撐住
    uint8_t status = 0;  // 狀態暫存器讀值
    if (!i2cReadRegs(ADDR_RTC, DS3231_REG_STATUS, &status, 1)) {
        return false;
    }
    status = static_cast<uint8_t>(status & ~DS3231_STATUS_OSF);
    return i2cWriteRegs(ADDR_RTC, DS3231_REG_STATUS, &status, 1);
}

/**
 * RTC 本次出現後的第一次處理：讀 OSF 判定備援電池，失憶就 seed；結果寫回 RTC memory 供軟重置沿用
 * @param s 目前狀態
 * @return 更新後狀態（rtc_backup／rtc_io_error）
 */
static ems::FactoryTestState initRtcIfNeeded(const ems::FactoryTestState& s) {
    // STEP 01: 已初始化、不在線、或已有 I/O 錯誤都不重做
    if (rtcInitialised || !s.rtc_present || s.rtc_io_error) {
        return s;
    }

    // STEP 02: 讀 OSF
    uint8_t status = 0;  // 狀態暫存器讀值
    if (!i2cReadRegs(ADDR_RTC, DS3231_REG_STATUS, &status, 1)) {
        return markRtcIoError(s, "read status");
    }
    const bool osf = (status & DS3231_STATUS_OSF) != 0;  // 曾停振／失憶

    // STEP 03: OSF 沒亮時讀一次時間算離線秒數（RTC 讀值 − NVS 最後記錄 − ESP 開機至今）
    int32_t offSeconds = 0;  // 板子離線秒數；OSF 亮時純函式不看它
    if (!osf) {
        uint8_t raw[ems::FT_RTC_RAW_LEN];  // 時間暫存器
        ems::FtRtcTime t;                  // 解碼結果
        if (!i2cReadRegs(ADDR_RTC, DS3231_REG_TIME, raw, ems::FT_RTC_RAW_LEN) || !ems::ft_rtc_decode(raw, t)) {
            return markRtcIoError(s, "read time at init");
        }
        const uint32_t nowSeconds = ems::ft_rtc_time_to_seconds(t);                      // 開機時 RTC 秒數
        const int32_t  uptimeS    = static_cast<int32_t>(millis() / MS_PER_SECOND);      // ESP 開機至今
        offSeconds = static_cast<int32_t>(nowSeconds - lastSeenSeconds) - uptimeS;
    }

    // STEP 04: 純函式判定備援狀態（兩個斷電證據一起交）
    ems::FactoryTestState next = s;  // 新狀態
    next.rtc_backup = ems::ft_classify_rtc_backup(osf, rtcSeededBefore, hasLastSeen, offSeconds,
                                                  espPoweredOff, storedBackup);
    Serial.printf("[RTC] OSF=%d seededBefore=%d hasLastSeen=%d off=%lds espPoweredOff=%d -> %s\n",
                  osf, rtcSeededBefore, hasLastSeen, static_cast<long>(offSeconds), espPoweredOff,
                  ems::ft_rtc_backup_label(next.rtc_backup));

    // STEP 05: 失憶就 seed，並把 last_seen 對齊 seed 時間（否則舊記錄比新時間大，離線秒數會是負的）；
    //          NVS 寫失敗就是主控問題，證據存不下來一律 FAIL
    if (osf) {
        if (!rtcSeedTime()) {
            return markRtcIoError(next, "seed time");
        }
        Serial.println("[RTC] seeded 2026-01-01 00:00:00, OSF cleared");
        ems::FtRtcTime seed;  // seed 常數解碼，取秒數
        if (!ems::ft_rtc_decode(RTC_SEED_REGS, seed)) {
            return markRtcIoError(next, "seed constant invalid");
        }
        lastSeenSeconds = ems::ft_rtc_time_to_seconds(seed);
        hasLastSeen     = true;
        if (!nvsSetU8(NVS_KEY_RTC_SEEDED, 1) || !nvsSetU32(NVS_KEY_LAST_SEEN, lastSeenSeconds)) {
            return ems::ft_mark_nvs_error(next);
        }
        rtcSeededBefore = true;
    }

    // STEP 06: 結論存進 NVS（RST 後沿用），有變才寫
    rtcInitialised = true;
    if (next.rtc_backup != storedBackup) {
        if (!nvsSetU8(NVS_KEY_BACKUP, static_cast<uint8_t>(next.rtc_backup))) {
            return ems::ft_mark_nvs_error(next);
        }
        storedBackup = next.rtc_backup;
    }
    return next;
}

/**
 * 讀 RTC 一次：暫存器讀取 + BCD 驗證都過才更新走時狀態與顯示字串
 * @param s     目前狀態
 * @param nowMs 目前 millis()
 * @return 更新後狀態（rtc_tick／rtc_io_error／rtc_backup）
 */
static ems::FactoryTestState pollRtc(const ems::FactoryTestState& s, uint32_t nowMs) {
    // STEP 01: RTC 不在線就歸零追蹤，等它出現再重來（走時 Stuck 黏性由 ft_apply_rtc_tick 維持）
    ems::FactoryTestState next = s;  // 新狀態
    if (!s.rtc_present) {
        hasRtcSample   = false;
        rtcInitialised = false;
        snprintf(rtcTimeText, sizeof(rtcTimeText), "--:--:--");
        return ems::ft_apply_rtc_tick(next, ems::FtRtcTick::Unknown);
    }

    // STEP 02: 首次出現的初始化；I/O 錯誤黏性，之後不再讀
    next = initRtcIfNeeded(next);
    if (!rtcInitialised || next.rtc_io_error) {
        return ems::ft_apply_rtc_tick(next, ems::FtRtcTick::Unknown);
    }

    // STEP 03: 讀 7 bytes 並驗證，失敗即 I/O 錯誤（印 raw 供排查）
    uint8_t raw[ems::FT_RTC_RAW_LEN];  // 時間暫存器原始值
    if (!i2cReadRegs(ADDR_RTC, DS3231_REG_TIME, raw, ems::FT_RTC_RAW_LEN)) {
        return markRtcIoError(next, "read time");
    }
    ems::FtRtcTime t;  // 解碼結果
    if (!ems::ft_rtc_decode(raw, t)) {
        Serial.printf("[RTC] invalid registers: %02X %02X %02X %02X %02X %02X %02X\n",
                      raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6]);
        return markRtcIoError(next, "decode");
    }
    snprintf(rtcTimeText, sizeof(rtcTimeText), "20%02u-%02u-%02u %02u:%02u:%02u",
             t.year, t.month, t.day, t.hour, t.minute, t.second);
    const uint32_t secs = ems::ft_rtc_time_to_seconds(t);  // 走時比較用

    // STEP 04: 第一筆只記錄，不下結論
    if (!hasRtcSample) {
        hasRtcSample    = true;
        lastRtcSeconds  = secs;
        lastRtcChangeMs = nowMs;
        return ems::ft_apply_rtc_tick(next, ems::FtRtcTick::Unknown);
    }

    // STEP 05: 純函式分類（Stuck 黏性在 ft_apply_rtc_tick 內），再更新「上次變化」基準
    const ems::FtRtcTick observed = ems::ft_classify_rtc_tick(lastRtcSeconds, secs, nowMs - lastRtcChangeMs);  // 本輪觀測
    if (secs != lastRtcSeconds) {
        lastRtcSeconds  = secs;
        lastRtcChangeMs = nowMs;
    }
    return ems::ft_apply_rtc_tick(next, observed);
}

// ── 按鍵 ────────────────────────────────────────────────────────

/**
 * 按鍵輪詢：去彈跳後收集本輪所有下降緣，交給純函式依序驗收；每個下降緣響一短聲
 * press 與 release 共用 DEBOUNCE_MS，否則 release 邊緣的彈跳會穿透成第二次 press
 * （feedback_button_debounce_press_release）
 * @param s     目前狀態
 * @param nowMs 目前 millis()
 * @return 更新後狀態
 */
static ems::FactoryTestState pollButtons(const ems::FactoryTestState& s, uint32_t nowMs) {
    uint8_t pressed[ems::FT_BUTTON_COUNT];  // 本輪下降緣的按鍵索引
    uint8_t pressedCount = 0;               // 下降緣數

    // STEP 01: 逐顆去彈跳
    for (uint8_t i = 0; i < ems::FT_BUTTON_COUNT; i++) {
        BtnDebounce& btn = btnState[i];  // 該顆的去彈跳狀態

        // STEP 01.01: raw 讀值一變就重置計時
        const uint8_t raw = digitalRead(BTN_PINS[i]);  // 當下準位
        if (raw != btn.rawLevel) {
            btn.rawLevel    = raw;
            btn.rawChangeMs = nowMs;
            continue;
        }

        // STEP 01.02: raw 穩定達門檻才承認為新狀態
        if (raw == btn.stableLevel || (nowMs - btn.rawChangeMs) < DEBOUNCE_MS) {
            continue;
        }
        btn.stableLevel = raw;

        // STEP 01.03: INPUT_PULLUP 下 LOW = 按下，收進本輪清單
        if (raw == LOW) {
            pressed[pressedCount++] = i;
            Serial.printf("[BTN] %u %s (GPIO %u)\n", i + 1, ems::ft_button_label(i), BTN_PINS[i]);
        }
    }

    // STEP 02: 沒事件直接回；有事件每個響一聲、交純函式判定
    if (pressedCount == 0) {
        return s;
    }
    for (uint8_t i = 0; i < pressedCount; i++) {
        beep(BEEP_SHORT_MS);
    }
    dirty = true;
    return ems::ft_apply_button_presses(s, pressed, pressedCount);
}

// ── 回報 ────────────────────────────────────────────────────────

/**
 * 印一行 serial 狀態摘要（給有接電腦的開發者，廠商不需要）
 * @param verdict 本輪判定
 */
static void printStatus(ems::FtVerdict verdict) {
    // STEP 01: 觀測值 + 判定
    Serial.printf("[STATUS] rtc=%d missSeen=%d eeprom=%d gauge=%d stuck=%d busErr=%d nvs=%d io=%d tick=%d backup=%d btn=%u/%u -> %s",
                  g_state.rtc_present, g_state.rtc_missing_seen, g_state.eeprom_present, g_state.gauge_present,
                  g_state.i2c_bus_stuck, g_state.i2c_bus_error, g_state.nvs_error, g_state.rtc_io_error,
                  static_cast<int>(g_state.rtc_tick), static_cast<int>(g_state.rtc_backup),
                  g_state.buttons_done, ems::FT_BUTTON_COUNT, ems::ft_verdict_label(verdict));

    // STEP 02: 原因
    if (verdict == ems::FtVerdict::Fail) {
        Serial.printf(" (%s)", ems::ft_fail_reason(g_state));
    } else if (verdict == ems::FtVerdict::Pending) {
        Serial.printf(" (%s)", ems::ft_pending_reason(g_state));
    }
    Serial.printf("  rtc=%s\n", rtcTimeText);
}

/**
 * PASS 第一次出現時響兩長聲；離開 PASS 後允許再響一次
 * @param verdict 本輪判定
 */
static void announcePassOnce(ems::FtVerdict verdict) {
    // STEP 01: 非 PASS 就重置旗標
    if (verdict != ems::FtVerdict::Pass) {
        passAnnounced = false;
        return;
    }

    // STEP 02: 已響過就不再響
    if (passAnnounced) {
        return;
    }
    passAnnounced = true;
    Serial.println("[FACTORY] PASS");
    beep(BEEP_LONG_MS);
    delay(BEEP_GAP_MS);
    beep(BEEP_LONG_MS);
}

// ── 開機 ────────────────────────────────────────────────────────

/**
 * 開 NVS 並讀三個 key；開機時按住主按鍵則先清掉（開發板換 RTC 模組用）
 * key 不存在是合法的初始狀態（沒 seed 過／沒記錄），讀寫錯誤才回 false
 * @return true = NVS 可用；false = 主控 NVS 有問題，呼叫端要標 nvs_error
 */
static bool loadNvsState() {
    // STEP 01: 開，開不起來就不能信任任何旗標
    const esp_err_t openErr = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);  // IDF 回傳碼
    if (openErr != ESP_OK) {
        Serial.printf("[NVS] open failed: %s\n", esp_err_to_name(openErr));
        return false;
    }
    nvsReady = true;

    // STEP 02: 按住主按鍵開機 → 清三個 key；清不掉也算失敗（否則換過 RTC 的開發板會被誤判 RTC BATTERY）
    if (digitalRead(BTN_PINS[BTN_CLEAR_SEED_INDEX]) == LOW) {
        if (!nvsEraseKey(NVS_KEY_RTC_SEEDED) || !nvsEraseKey(NVS_KEY_LAST_SEEN) || !nvsEraseKey(NVS_KEY_BACKUP)) {
            nvsReady = false;
            return false;
        }
        Serial.println("[NVS] cleared (PRIMARY held at boot)");
    }

    // STEP 03: 讀三個 key，NotFound 各有預設值；Error 或讀到越界值（儲存損壞／schema 不合）都算失敗，不套預設
    uint8_t seeded = 0;  // rtc_seeded 讀值
    NvsRead r = nvsGetU8(NVS_KEY_RTC_SEEDED, seeded);  // 各 key 的讀取結果
    if (r == NvsRead::Error || (r == NvsRead::Ok && seeded > 1)) {
        Serial.printf("[NVS] rtc_seeded unusable (r=%d v=%u)\n", static_cast<int>(r), seeded);
        nvsReady = false;
        return false;
    }
    rtcSeededBefore = (r == NvsRead::Ok && seeded == 1);

    r = nvsGetU32(NVS_KEY_LAST_SEEN, lastSeenSeconds);
    if (r == NvsRead::Error) {
        nvsReady = false;
        return false;
    }
    hasLastSeen = (r == NvsRead::Ok);

    uint8_t backup = 0;  // backup 讀值
    r = nvsGetU8(NVS_KEY_BACKUP, backup);
    if (r == NvsRead::Error || (r == NvsRead::Ok && backup > static_cast<uint8_t>(ems::FtRtcBackup::Lost))) {
        Serial.printf("[NVS] backup unusable (r=%d v=%u)\n", static_cast<int>(r), backup);
        nvsReady = false;
        return false;
    }
    storedBackup = (r == NvsRead::Ok) ? static_cast<ems::FtRtcBackup>(backup) : ems::FtRtcBackup::Unverified;

    Serial.printf("[NVS] seeded=%d lastSeen=%d(%lu) backup=%s\n",
                  rtcSeededBefore, hasLastSeen, static_cast<unsigned long>(lastSeenSeconds),
                  ems::ft_rtc_backup_label(storedBackup));
    return true;
}

/** Arduino 進入點：初始化 serial、GPIO、斷電偵測、NVS、TFT、I2C（無參數、無回傳） */
void setup() {
    // STEP 01: 啟動 USB-CDC 並等列舉；沒接電腦時 SERIAL_WAIT_MS 後照常往下跑
    Serial.begin(SERIAL_BAUD);
    const uint32_t waitStart = millis();  // 等列舉起點
    while (!Serial && millis() - waitStart < SERIAL_WAIT_MS) {
        delay(SERIAL_WAIT_STEP_MS);
    }
    Serial.println();
    Serial.println("============================================");
    Serial.println("EMS DoseSync Pro — Factory Test v2");
    Serial.println("============================================");

    // STEP 02: 關掉板上 WS2812（bootloader 可能 latch 成白色），蜂鳴器與按鍵 GPIO 初始化
    neopixelWrite(PIN_WS2812, 0, 0, 0);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    for (uint8_t i = 0; i < ems::FT_BUTTON_COUNT; i++) {
        pinMode(BTN_PINS[i], INPUT_PULLUP);
        btnState[i].rawLevel    = digitalRead(BTN_PINS[i]);
        btnState[i].stableLevel = btnState[i].rawLevel;
        btnState[i].rawChangeMs = millis();
    }

    // STEP 03: 斷電證據 (2)：RTC slow memory magic 不在 = 真斷過電；讀完立刻寫回給下次開機用
    espPoweredOff = (g_rtcMemMagic != RTC_MEM_MAGIC);
    g_rtcMemMagic = RTC_MEM_MAGIC;
    Serial.printf("[BOOT] espPoweredOff=%d\n", espPoweredOff);

    // STEP 04: NVS（要在按鍵 pinMode 之後，清 key 靠讀主按鍵）；NVS 失敗黏性 FAIL
    if (!loadNvsState()) {
        g_state = ems::ft_mark_nvs_error(g_state);
    }

    // STEP 05: TFT 初始化、彩條、開機一短聲
    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(COLOR_BLACK);
    frame.setColorDepth(FRAME_COLOR_DEPTH);
    frame.setPsram(true);
    frameReady = frame.createSprite(SCREEN_W, SCREEN_H);
    if (!frameReady) {
        Serial.println("[WARN] createSprite failed, drawing directly (may flicker)");
    }
    drawColorBars();
    beep(BEEP_SHORT_MS);

    // STEP 06: I2C bus
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
    Serial.printf("[I2C] SDA=%d SCL=%d @ %lu Hz\n", PIN_I2C_SDA, PIN_I2C_SCL, (unsigned long)I2C_CLOCK_HZ);
    Serial.println("[READY] power-cycle to verify RTC battery, then press buttons 1..8 in order");
}

/** Arduino 主迴圈：按鍵每圈掃、I2C／RTC 每秒掃、有變化才重繪（無參數、無回傳） */
void loop() {
    const uint32_t nowMs = millis();  // 本圈時刻

    // STEP 01: 按鍵每圈都掃（去彈跳需要高頻取樣）
    g_state = pollButtons(g_state, nowMs);

    // STEP 02: I2C／RTC 每 POLL_INTERVAL_MS 一次；走時狀態每輪都可能變，所以每輪都重繪
    if (!hasPolled || (nowMs - lastPollMs) >= POLL_INTERVAL_MS) {
        hasPolled  = true;
        lastPollMs = nowMs;
        g_state = scanI2c(g_state);
        g_state = pollRtc(g_state, nowMs);
        dirty = true;
    }

    // STEP 03: 每 NVS_LAST_SEEN_INTERVAL_MS 把最新 RTC 秒數記進 NVS（下次開機算離線秒數的基準）；寫失敗黏性 FAIL
    if (hasRtcSample && !g_state.nvs_error &&
        (!hasNvsWritten || (nowMs - lastNvsWriteMs) >= NVS_LAST_SEEN_INTERVAL_MS)) {
        hasNvsWritten  = true;
        lastNvsWriteMs = nowMs;
        if (nvsSetU32(NVS_KEY_LAST_SEEN, lastRtcSeconds)) {
            lastSeenSeconds = lastRtcSeconds;
            hasLastSeen     = true;
        } else {
            g_state = ems::ft_mark_nvs_error(g_state);
            dirty = true;
        }
    }

    // STEP 04: 有變化才重繪、印 serial、處理 PASS 提示音
    if (dirty) {
        dirty = false;
        const ems::FtVerdict verdict = ems::ft_evaluate(g_state);  // 本輪判定
        render(verdict);
        printStatus(verdict);
        announcePassOnce(verdict);
    }

    delay(LOOP_IDLE_MS);
}
