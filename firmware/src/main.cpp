/**
 * EMS Timer 韌體：OHCA 核心 + Phase B 已落地
 *
 * SoT 對齊：
 *   - docs/EMS_DoseSync_Pro_Prototype_V1.md §3 主功能表 / §5~§13 OHCA + Vent
 *   - docs/pm-dev-spec.md §3~§5（OHCA 子狀態機 / EPI 倒數 / 兩段確認）
 *   - docs/pm-dev-spec.md §四 Phase A~H acceptance
 *   - docs/gpio-allocation.md（GPIO 分配 SSOT）
 *
 * 實作狀態（pm-dev-spec §四，2026-05-09 對齊）：
 *
 *   Phase A — OHCA 核心
 *     ✅ 主功能表 5 項（OHCA 入口可進；Training/History/Settings 顯示 placeholder）
 *     ✅ OHCA 子狀態機（10 態，delegate 至 lib/ems_ohca）
 *     ✅ EPI 4 分鐘倒數引擎（純函式 lib，TIMER_TICK 驅動）
 *     ✅ EPI / 電擊兩段確認（5s timeout）
 *     ✅ 三模態輸出：蜂鳴 / TFT flash / 震動（佔位）
 *
 *   Phase B — 補登 + Amiodarone + 案件總覽
 *     ✅ 長按 EPI 鍵 → 藥物選單（補登 EPI / Amiodarone）
 *     ✅ 長按電擊鍵 → 電擊補登（接手前 / 純補登）
 *     ✅ 補登次數選擇 UI（接手前 1~5 / 純補登 1~3，列表式對齊 demo）
 *     ✅ Amiodarone 兩段確認（twoStepConfirm）
 *     ✅ END_CHECK 結束前檢查（V1 §10：完成並結束 / 前往補登 / 返回案件）
 *     ✅ 案件總覽（V1 §11，caseSummary_build）+ Timeline 子畫面
 *
 *   Phase C — 6 秒通氣節奏（程式碼已落地，待 PM 驗收）
 *     ❌ GLOBAL_VENT 獨立模式（從主功能表進入）
 *     ❌ 通氣音量 0~5 即時可調（音量持久化見 Phase G）
 *     ❌ OHCA 中快速功能進入（返回鍵 → SUBSTATE_QUICK_MENU）
 *     ❌ EPI 高優先打斷邏輯（OHCA_STATE_ALARMING → effectiveVol=0）
 *     ❌ EPI 完成後「返回通氣節奏？」詢問（V1 §13.13 待補）
 *
 *   ❌ Phase D — Training 模式
 *   ❌ Phase E — 持久化 / 歷史紀錄（LittleFS partition + FIFO 覆蓋）
 *   ❌ Phase F — BLE 同步（NUS + JSON 過渡 / 配對碼）
 *   ❌ Phase G — 系統設定（亮度 / 系統音量 / 通氣音量 NVS）
 *   ❌ Phase H — 電源管理（螢幕常亮 / Deep Sleep）
 *
 * 接線（完整 SSOT 見 docs/gpio-allocation.md）：
 *   主按鍵    → GPIO 4   返回鍵    → GPIO 16
 *   上鍵      → GPIO 5   EPI 鍵    → GPIO 17
 *   下鍵      → GPIO 6   電擊鍵    → GPIO 18
 *   Power 鍵  → GPIO 7   蜂鳴器    → GPIO 14
 *   錄音鍵    → GPIO 15  震動馬達  → 停用（ENABLE_VIBRATION=0，原 21 已給 TFT CS）
 *   TFT       → SCK 3 / MOSI 2 / DC 1 / CS 21 / RST 47（LovyanGFX SPI2 + DMA，BL 常亮）
 */

#include <Arduino.h>
#include <SPI.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "ems_ohca_state.h"
#include "ems_ohca_countdown.h"
#include "ems_two_step_confirm.h"
#include "ems_supp_model.h"
#include "ems_case_summary.h"
#include "ems_vent_metronome.h"
#include "ems_storage_logic.h"   // Phase E：持久化邏輯層
#include "ems_storage_fs.h"      // Phase E：LittleFS adapter
#include "ems_display_snapshot.h"// L2 regression：DisplaySnapshot 純邏輯
#include "ems_time_sync.h"       // Phase F MVP1：BLE 對時純邏輯
#include "ems_sync_dispatcher.h" // Phase F MVP2：配對碼 + 同步流程狀態機
#include <ArduinoJson.h>         // Phase F MVP2：BLE RX 訊息 type 分流

// Phase F MVP1：BLE NUS peripheral（ESP32 Arduino framework 內建）
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "ems_zh_24_vlw.h"  // Sarasa Mono TC Bold 24px vlw, 258 glyphs (95 ASCII + 163 CJK; Phase F MVP2 補 29 字)

using namespace ems;

/**
 * LovyanGFX 硬體配置：ESP32-S3 GP-SPI2 + DMA + ST7789 240×320 panel。
 * Pin 號碼與 TFT_*_PIN 常數同步（gpio-allocation.md §5.2）。
 */
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
public:
    LGFX(void) {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 3;
            cfg.pin_mosi = 2;
            cfg.pin_miso = -1;
            cfg.pin_dc   = 1;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs   = 21;
            cfg.pin_rst  = 47;
            cfg.pin_busy = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable  = false;
            cfg.invert    = false;  // 蝦皮紅板 polarity（同 Adafruit_ST7789 invertDisplay(false)）
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        setPanel(&_panel);
    }
};

/**
 * 全頁 RAM framebuffer（LGFX_Sprite + PSRAM）：updateDisplay 結尾以
 * pushSprite(&tft, 0, 0) DMA 一次推到實體 TFT，消全頁切換掃描感。
 * clearDisplay() 為 fillScreen(BLACK) 別名，updateDisplay 入口呼叫一次。
 */
class FrameSprite : public lgfx::LGFX_Sprite {
public:
    explicit FrameSprite(lgfx::LovyanGFX* parent) : LGFX_Sprite(parent) {}
    void clearDisplay() { fillScreen(0x0000); }
};

// ============================================================
// 硬體常數（gpio-allocation.md §5.2）
// ============================================================

/** TFT 解析度（native portrait） */
static const uint16_t TFT_WIDTH     = 240;
static const uint16_t TFT_HEIGHT    = 320;
/** TFT 邏輯解析度（rotation=1 橫向後使用） */
static const int16_t  SCREEN_W      = 320;
static const int16_t  SCREEN_H      = 240;
// ============================================================
// 顯示設計 tokens（對齊 docs/demo/index.html）
// 規格：黑底 + 白主字 + 灰次要 + 急救色（綠/琥珀/紅）+ Courier monospace 大時間
// 詳見 .claude/.../project_tft_ui_design_target.md
// ============================================================

/** 背景色：純黑 #000000（demo `body { background: #000 }`） */
static const uint16_t COLOR_BG          = 0x0000;
/** 主文字：白 #ffffff */
static const uint16_t COLOR_TEXT_PRIMARY = 0xFFFF;
/** 次要資訊：灰 ≈ #94a3b8（demo `.scr-title color`） */
static const uint16_t COLOR_TEXT_MUTED  = 0x9492;
/** 暗灰：#64748b（demo `.scr-counter color`） */
static const uint16_t COLOR_TEXT_DIM    = 0x6B4D;
/** 執行中綠：≈ #22c55e（demo `.scr-mode color`） */
static const uint16_t COLOR_ACCENT_OK   = 0x2604;
/** 警告琥珀：≈ #fbbf24（demo `.scr-time-amber`） */
static const uint16_t COLOR_ACCENT_WARN = 0xFDE0;
/** 警報紅：≈ #ef4444（demo `.scr-time-red`） */
static const uint16_t COLOR_ACCENT_ALERT = 0xEA44;
/** 暗紅閃爍：#5a0000（demo `flash-vent` 整片暗紅） */
static const uint16_t COLOR_FLASH_VENT   = 0x5800;

/** OHCA 倒數畫面 layout（drawOhcaCountdownCommon 使用） */
/** ALARMING 背景閃爍半週期 ms（demo flashRed 0.6s 全週期 / 2） */
static const uint32_t OHCA_FLASH_HALF_MS  = 300;
/** 舊 layout（非 vlw badge）— EndCheck/Locked/Summary/Placeholder 等尚未遷移 vlw 字型的畫面共用；
 *  vlw 系列畫面（WaitFirstEpi/Countdown/VentPre）改用 OHCA_CHINESE_BADGE_TOP_Y */
static const int16_t  OHCA_BADGE_Y        = 14;
/** vlw 1.5× badge "OHCA" 文字 top y（datum top_center；ChineseBadge 在 WaitFirstEpi/Countdown 共用） */
static const int16_t  OHCA_CHINESE_BADGE_TOP_Y = 8;
/** 大時間視覺上偏 px（middle datum 中心 y = SCREEN_H/2 - VISUAL_UP=100，時間 bbox ~52~148） */
static const int16_t  OHCA_TIME_VISUAL_UP = 20;
/** OHCA 倒數標籤 middle-center y（vlw 1.8×；理論幾何中點 174 上偏 9px 讓 descender 與計數行留間距） */
static const int16_t  OHCA_LABEL_Y        = 165;
/** WaitFirstEpi 底部 EPI/電擊 計數 top y（vlw 1.5× ≈ 36px；200 + 36 + 4 邊界 = SCREEN_H 240） */
static const int16_t  OHCA_WAIT_COUNTER_Y = 200;
/** 底部 EPI/Shock 計數行距底邊 px（用於 SCREEN_H - OHCA_COUNTER_BOTTOM - 8 算 hint 行 y） */
static const int16_t  OHCA_COUNTER_BOTTOM = 18;

/** 倒數 partial sprite erase bbox（FreeMonoBold24pt7b size 2 → 280×96 + margin） */
static const int16_t  OHCA_TIME_PUSH_X    = 14;
static const int16_t  OHCA_TIME_PUSH_Y    = 48;
static const int16_t  OHCA_TIME_PUSH_W    = 292;
static const int16_t  OHCA_TIME_PUSH_H    = 104;

/** 確認 bar 高度（TwoStepArmed / AmioConfirm「再按一次主鍵確認」橫條） */
static const int16_t  DIALOG_BAR_H        = 44;
/** Vent 結束提示 bar 高度（VentStandalone「長按 ≥ 0.75s 結束」較窄保留中央資訊區） */
static const int16_t  VENT_BAR_H          = 32;

/** TFT SPI 腳位（避開 N16R8 octal PSRAM 佔用的 GPIO 35-37 + 板上 WS2812 GPIO 48） */
static const int8_t   TFT_CS_PIN    = 21;
static const int8_t   TFT_DC_PIN    = 1;   /**< 原 48 → 1：避開板上 WS2812 RGB LED（2026-05-08 實機踩雷） */
static const int8_t   TFT_RST_PIN   = 47;
static const int8_t   TFT_MOSI_PIN  = 2;
static const int8_t   TFT_SCLK_PIN  = 3;
/** I2C bus（OLED 已移除，腳位保留給未來 DS3231 RTC / CO 感測器擴充） */
static const uint8_t  I2C_SDA_PIN   = 42;
static const uint8_t  I2C_SCL_PIN   = 41;

/** 蜂鳴器 GPIO */
static const uint8_t BUZZER_PIN     = 14;

/** 震動馬達 GPIO（gpio-allocation.md：原 GPIO 16 已封給返回鍵） */
#define ENABLE_VIBRATION 0
#if ENABLE_VIBRATION
static const uint8_t VIBRATION_PIN = 21;
#endif

// ============================================================
// 8 按鍵配置（依 SoT V1 §4.1 + gpio-allocation.md）
// ============================================================

static const uint8_t BTN_COUNT = 8;
static const uint8_t BTN_PINS[BTN_COUNT] = {
    4,   // 主按鍵
    5,   // 上鍵
    6,   // 下鍵
    7,   // Power 鍵
    15,  // 錄音鍵（noop 佔位）
    16,  // 返回鍵
    17,  // EPI 鍵（針筒圖案）
    18,  // 電擊鍵（閃電圖案）
};

/** 按鈕索引語意 */
static const uint8_t BTN_PRIMARY = 0;
static const uint8_t BTN_UP      = 1;
static const uint8_t BTN_DOWN    = 2;
static const uint8_t BTN_POWER   = 3;
static const uint8_t BTN_RECORD  = 4;
static const uint8_t BTN_BACK    = 5;
static const uint8_t BTN_EPI     = 6;
static const uint8_t BTN_SHOCK   = 7;

/** Debounce / 短按 / 長按時間（ms） */
static const uint16_t DEBOUNCE_MS         = 80;
static const uint16_t SHORT_PRESS_MAX_MS  = 1500;

/**
 * 各按鈕的長按 threshold（ms）；0 = 該按鈕不支援長按
 *   BTN_PRIMARY 3000ms：OHCA 任意進行中 phase → END_CHECK（SoT V1 §10.1）
 *   BTN_EPI     1000ms：藥物選單入口（Phase B）
 *   BTN_SHOCK   1000ms：電擊補登選單入口（Phase B）
 */
static const uint16_t LONG_PRESS_MS_PER_BTN[8] = {
    3000,  // BTN_PRIMARY
    0,     // BTN_UP
    0,     // BTN_DOWN
    0,     // BTN_POWER
    0,     // BTN_RECORD
    0,     // BTN_BACK
    1000,  // BTN_EPI
    1000,  // BTN_SHOCK
};

// ============================================================
// Phase F MVP1：BLE NUS peripheral 常數
// 對齊 docs/ble-time-sync-protocol.md §1 / docs/ble-tester/index.html
// 與 firmware/src_ble_time_sync_smoke/main.cpp 共用同一組 UUID
// ============================================================

// NUS（Nordic UART Service）UUID 與兩個 characteristics
static constexpr const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // App→Device Write
static constexpr const char* NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // Device→App Notify

// 廣播名稱（正式版裝置名，與 smoke firmware「EMS-DoseSync-Smoke」區分）
static constexpr const char* BLE_DEVICE_NAME = "EMS-DoseSync-Pro";

// BLE RX/ACK buffer 上限：time_sync JSON ~100 bytes，ACK ~150 bytes，留 ~3-5x 餘裕
static constexpr size_t BLE_RX_BUF_MAX  = 512;
static constexpr size_t BLE_ACK_BUF_MAX = 512;

// BLE advertising 偏好連線間隔（單位 × 1.25 ms），與 smoke firmware 一致以利 iOS 連線
static constexpr uint16_t BLE_CONN_INTERVAL_MIN = 0x06;  // 7.5 ms
static constexpr uint16_t BLE_CONN_INTERVAL_MAX = 0x12;  // 22.5 ms

// ============================================================
// 全域狀態機（pm-dev-spec §2）
// ============================================================

enum GlobalState : uint8_t {
    GLOBAL_MAIN_MENU            = 0,
    GLOBAL_OHCA                 = 1,
    GLOBAL_VENT                 = 2,  // Phase C 獨立 6 秒通氣節奏
    GLOBAL_TRAINING_PLACEHOLDER = 3,  // Phase D 待實作
    GLOBAL_HISTORY_PLACEHOLDER  = 4,  // Phase E 待實作
    GLOBAL_SETTINGS_PLACEHOLDER = 5,  // Phase G 待實作
    GLOBAL_SYNC                 = 6,  // Phase F MVP2：BLE 同步資料（配對碼 + dispatcher 流程）
};
static GlobalState globalState = GLOBAL_MAIN_MENU;

/** 主功能表 5 項（SoT V1 §3.1 封版）
 *
 * 同步入口位置：SoT §11.1 規定「同步至 App」在 OHCA 案件總覽 sub-menu，
 * 不在主功能表（§10.4：案件結束鎖定後才可同步，屬個案級行為非裝置級全域功能）。
 * 入口由 drawOhcaSummary 的 cursor sub-menu 觸發 GLOBAL_SYNC。
 */
static const uint8_t MAIN_MENU_COUNT = 5;
static const char* const MAIN_MENU_LABELS[MAIN_MENU_COUNT] = {
    "OHCA 案件",
    "6 秒通氣節奏",
    "訓練模式",
    "歷史紀錄",
    "系統設定",
};
static uint8_t mainMenuCursor = 0;

// ============================================================
// OHCA 子狀態（delegate 至 lib/ems_ohca）
// ============================================================

static ohca_state_t ohcaState         = OHCA_STATE_MAIN_MENU;
static uint32_t     ohcaLastEpiMs     = 0;     // 上次 EPI 確認的 millis()
static uint32_t     ohcaPrevSinceMs   = 0;     // 上一輪 since 值（給邊緣偵測）
static uint32_t     startFlashStartMs = 0;     // START_FLASH 1s 計時
static bool         alarmMuted        = false; // ALARMING/OVERTIME 是否已被消音
static uint32_t     caseStartMs       = 0;     // case 開始 millis()（uptime；給 elapsed_ms 算用）
static uint64_t     caseStartEpochMs  = 0;     // case 開始 epoch ms（time_sync 後計算；給 summary 相對時間顯示用；0 = 案件未開始 / 未對時）

/** START_FLASH 階段顯示 1s */
static const uint32_t START_FLASH_DURATION_MS = 1000;

/** 兩段確認 instances（EPI / 電擊 / Amio 各自獨立） */
static two_step_confirm_t epiConfirm;
static two_step_confirm_t shockConfirm;
static two_step_confirm_t amioConfirm;

/** 兩段確認 armed 提示視窗 */
static bool     showEpiArmedPrompt    = false;
static uint32_t epiArmedPromptStart   = 0;
static bool     showShockArmedPrompt  = false;
static uint32_t shockArmedPromptStart = 0;
static bool     showAmioArmedPrompt   = false;
static uint32_t amioArmedPromptStart  = 0;
static const uint32_t ARMED_PROMPT_MS = 5000;

// ============================================================
// 事件紀錄（升級為 ems_event_t，對齊 pm-dev-spec §6）
// Phase E 才做持久化；Phase B 仍為 in-memory
// ============================================================

static const uint16_t MAX_EVENTS = 50;
static ems_event_t events[MAX_EVENTS];
static uint16_t    eventCount   = 0;
static uint32_t    nextEventId  = 1;  // 案件內流水號，從 1 起

// ===== Phase E：持久化 =====
static IStorageBackend g_storage_be;
static bool g_storage_ready    = false;
static bool g_locked_saved     = false;  // LOCKED 防重複存

// ===== Phase F MVP1：BLE NUS peripheral state =====
//   ESP32 BLE callback 跑在 GATT task（與 main loop 不同 task，常不同核），
//   volatile 不足以擋跨 task race。用 portMUX_TYPE spinlock 保護 g_rx_buf：
//   - GATT task onWrite：memcpy + 旗標 in 臨界區
//   - main loop drain：原子取出 + 清旗標 in 臨界區
static BLEServer*         g_ble_server         = nullptr;
static BLECharacteristic* g_ble_tx_char        = nullptr;
static volatile bool      g_ble_client_connected = false;
static volatile bool      g_ble_rx_ready       = false;
static uint8_t            g_ble_rx_buf[BLE_RX_BUF_MAX];
static volatile size_t    g_ble_rx_len         = 0;
static portMUX_TYPE       g_ble_rx_mux         = portMUX_INITIALIZER_UNLOCKED;
static ems::TimeSyncState g_ts_state;  // 對時 state：只在 main loop task 讀寫

// ===== Phase F MVP2：BLE 同步流程狀態機 =====
//   pairing + dispatcher 都已 native test 覆蓋（21 + 20 cases）。
//   只在 main loop task 讀寫（dispatcher 內部 pairing_generate 用 static counter，
//   多 task 呼叫會 race；遵守 ble_callback_non_blocking 將 BLE input enqueue 到 main loop）。
static ems::SyncContext   g_sync_ctx;

// 同步結束（DONE/ERROR → IDLE）後 globalState 拉回此值（SoT §16.5「自動回案件總覽」）。
// 唯一寫入點：handleSummarySubmenuPrimary（且 re-entry guard 擋 globalState==GLOBAL_SYNC 自指）。
// 預設 GLOBAL_MAIN_MENU 保險：若任何路徑進 SYNC 沒設此值，至少回到主功能表不卡死。
static GlobalState        g_sync_return_to = GLOBAL_MAIN_MENU;

// ===== Phase E：歷史紀錄 UI =====
static case_meta_t historyCases[EMS_STORAGE_OHCA_CAP];
static uint16_t historyCount        = 0;
static uint16_t historyCursor       = 0;
static uint16_t historyScrollOffset = 0;
static bool     historySummaryMode  = false;  // true = 載入了案件、進 SUMMARY 子畫面
static const uint8_t HISTORY_VISIBLE_ROWS = 5;

// ============================================================
// Phase B 子流程狀態機（補登 / Amio / Timeline）
// ============================================================

enum OhcaSubState : uint8_t {
    SUBSTATE_NONE                  = 0,  // 無子流程，主畫面
    SUBSTATE_DRUG_MENU             = 1,  // 長按 EPI 鍵進入：補登 EPI / Amiodarone
    SUBSTATE_BACKFILL_TYPE         = 2,  // 接手前 / 純補登 二選一
    SUBSTATE_BACKFILL_COUNT        = 3,  // 次數選擇 1~5 或 1~3
    SUBSTATE_BACKFILL_CONFIRM      = 4,  // 「確認補登？... 成立後不可撤銷」
    SUBSTATE_BACKFILL_SUCCESS      = 5,  // 「補登成功」顯示 2s
    SUBSTATE_AMIO_CONFIRM          = 6,  // Amio 兩段確認
    SUBSTATE_TIMELINE              = 7,  // Timeline 子畫面（從 SUMMARY 進入）
    SUBSTATE_QUICK_MENU            = 8,  // Phase C: OHCA 中按返回鍵進入快速功能選單
};
static OhcaSubState ohcaSubState = SUBSTATE_NONE;

/** 補登流程暫存：類別（EPI / 電擊）與類型（接手前 / 純補登）與次數 */
enum BackfillCategory : uint8_t {
    BACKFILL_CAT_EPI   = 0,
    BACKFILL_CAT_SHOCK = 1,
};
static BackfillCategory backfillCategory   = BACKFILL_CAT_EPI;
static supp_type_t      backfillSuppType   = SUPP_TYPE_EPI_PRE_HANDOVER;
static uint8_t          backfillCursor     = 0;  // 子選單 cursor
static uint8_t          backfillCount      = 1;  // 1..suppCountMax
static uint32_t         backfillSuccessShownMs = 0;  // 顯示「成功」起點
static const uint32_t   BACKFILL_SUCCESS_MS = 2000;

/** OHCA 案件總覽 sub-menu（SoT V1 §11.1）
 *
 * SoT §11.1 完整定義 6 項（EPI 詳細 / 電擊詳細 / 藥物紀錄 / 事件時間軸 / 同步至 App / 傳輸資料）。
 * 當前實作 SUMMARY_SUBMENU_COUNT 項；補項時 enum 接續加，drawOhcaSummary cursor 上限同步擴。
 *
 * 歷史模式（historySummaryMode）下 Timeline 子畫面未實作 → 該項以停用色顯示，
 * 主鍵按下不轉子畫面（cursor 仍可移動以查 sub-menu 範圍）。
 */
enum SummarySubmenuItem : uint8_t {
    SUMMARY_SUBMENU_TIMELINE = 0,  // 事件時間軸
    SUMMARY_SUBMENU_SYNC     = 1,  // 同步至 App（GLOBAL_SYNC）
    SUMMARY_SUBMENU_COUNT
};
// 範圍 0..SUMMARY_SUBMENU_COUNT-1；OHCA 案件結束 SUMMARY 與歷史 SUMMARY 共用同一 cursor。
static uint8_t summarySubmenuCursor = SUMMARY_SUBMENU_TIMELINE;

// drawOhcaSummary sub-menu 視覺常數（hoist 至 file scope 讓 static_assert 可見）
constexpr int16_t SUMMARY_SUBMENU_ROW_H          = 24;
constexpr int16_t SUMMARY_SUBMENU_LEFT_PAD       = 28;
constexpr int16_t SUMMARY_SUBMENU_CURSOR_GLYPH_W = 16;  // ">" 游標符號左偏寬度
constexpr int16_t SUMMARY_SUBMENU_BOTTOM_MARGIN  = 8;   // sub-menu 末列距底部 hint anchor 像素

// 編譯期保護：補項到 SUMMARY_SUBMENU_COUNT 時 SUBMENU_Y_BASE 不可變負（會壓出畫面頂端）。
// 若觸發此 assert：縮 SUMMARY_SUBMENU_ROW_H、改 layout 或分頁顯示（vlw 24px bitmap 無法縮字）。
static_assert(
    SCREEN_H - OHCA_COUNTER_BOTTOM - SUMMARY_SUBMENU_BOTTOM_MARGIN
        - SUMMARY_SUBMENU_ROW_H * SUMMARY_SUBMENU_COUNT >= 0,
    "OHCA SUMMARY sub-menu rows overflow screen — see SoT V1 §11.1 layout note"
);

/** Timeline 子畫面 scroll offset */
static uint16_t timelineScrollOffset = 0;

// ============================================================
// Phase C: 6 秒通氣節奏狀態
// ============================================================

static uint8_t  ventVolume         = VENT_VOLUME_DEFAULT;  // 通氣音量（Phase G 才做 NVS 持久化）
static uint32_t ventStartMs        = 0;                    // 進入 vent 模式時的 millis()
static uint32_t ventPrevSinceMs    = 0;                    // 邊緣偵測用
static bool     ventEndCheckShown  = false;                // 獨立模式長按 3s 結束確認對話框
static bool     ventBackHintShown  = false;                // 「請長按主鍵」提示
static uint32_t ventBackHintStartMs = 0;
static const uint32_t VENT_BACK_HINT_MS = 2000;
static bool     ventPaused         = false;                // V1 §13.11 / §13.12 獨立 vent 暫停旗標
static bool     ventPreShown       = false;                // A8：VENT_PRE「按主鍵開始」preview 畫面（demo 對齊）

/** OHCA 內 6 秒通氣輔助區塊開關（V1 §14.9 開啟、暫停、繼續與關閉） */
static bool ohcaVentOverlayEnabled = false;
/** V1 §14.10 / §14.11：OHCA 內 6 秒通氣暫停旗標（透過快速功能切換） */
static bool ohcaVentPaused         = false;

// ============================================================
// END_CHECK 與 SUMMARY 子狀態
// ============================================================

enum EndCheckCursor : uint8_t {
    END_CHECK_CURSOR_CONFIRM  = 0,  // 完成並結束案件
    END_CHECK_CURSOR_BACKFILL = 1,  // 前往補登
    END_CHECK_CURSOR_CANCEL   = 2,  // 返回案件
};
static EndCheckCursor endCheckCursor = END_CHECK_CURSOR_CANCEL;
/** A7：END_CHECK 選「完成並結束」後彈出二次確認對話（demo OHCA_END_CONFIRM） */
static bool endConfirmShown = false;

static uint16_t summaryScrollOffset = 0;

// ============================================================
// Flash overlay（demo 對齊：1.2s 全螢幕過場提示）
// ============================================================
struct FlashState {
    bool     active;
    uint32_t startMs;
    uint16_t durationMs;
    char     title[40];
    char     subtitle[40];
    uint16_t titleColor;
    float    titleSize;     // 觸發時計算，避免 render hot-path 反覆 strcmp
    float    subtitleSize;
};
static FlashState flashState = {};
static const uint16_t FLASH_DEFAULT_MS = 1200;

// Flash overlay 字級常數（vlw 24px 為基準；選值依 320px 螢幕反推安全縮放，留 ~10px 邊距）
//   Default 2.25：~7-char 內主標 / ~6-char 內副標都不溢出 320px
//   Long    1.9 ：title 7+ char（如「案件結束並鎖定」size 2.25 會超寬）/
//                subtitle 10+ char（如「結束案件後看完整總覽」size 1.5 = 360px > 320）
//   XLong   1.4 ：英文+中文混合 11+ 視寬度單位（如「Amiodarone 已紀錄」連 size 1.9 ≈ 395px 仍超）
static constexpr float FLASH_TITLE_SIZE_DEFAULT    = 2.25f;
static constexpr float FLASH_TITLE_SIZE_LONG       = 1.9f;
static constexpr float FLASH_TITLE_SIZE_XLONG      = 1.4f;
static constexpr float FLASH_SUBTITLE_SIZE_DEFAULT = 1.5f;
static constexpr float FLASH_SUBTITLE_SIZE_LONG    = 1.2f;

// ============================================================
// 按鈕狀態
// ============================================================

static uint8_t  lastBtnState[BTN_COUNT];
static uint32_t lastPressMs[BTN_COUNT]      = { 0 };
static uint32_t btnPressStartMs[BTN_COUNT]  = { 0 };
static bool     btnLongFired[BTN_COUNT]     = { false };

// ============================================================
// OLED + 蜂鳴 / 反色 SM
// ============================================================

/** 實體 TFT 控制（init / SPI / rotation / DMA push） */
LGFX tft;
/** 全頁 RAM sprite — 所有 draw functions 寫到這裡，updateDisplay 結尾 pushSprite DMA 一次推到 tft */
FrameSprite display(&tft);

/* vlw 載入旗標 — false 時 useZhFont() 不重 load（fallback 用 default font） */
static bool g_vlw_loaded = false;

/* vlw 字型切換 helper：
 * LovyanGFX setFont() 切到內建字型時會 _runtime_font.reset() 析構 VLWfont，
 * 所以一旦 setFont(&fonts::Font0) 後再切 vlw 必須重新 loadFont（lazy reload）。
 * useZhFont() 檢查當前 font type，非 vlw 就重 load；同畫面連續用不會重 load。
 */
static inline void useZhFont() {
    if (!g_vlw_loaded) {
        return;
    }
    auto* f = display.getFont();
    if (f == nullptr || f->getType() != lgfx::v1::IFont::font_type_t::ft_vlw) {
        if (!display.loadFont(ems_zh_24_vlw)) {
            Serial.println("[FONT] lazy reload FAILED, disable VLW");
            g_vlw_loaded = false;
        }
    }
}

// 蜂鳴器：脈衝模式（pulses=255 視為連續直到 stop）
static uint8_t  beepPulsesRemaining = 0;
static uint32_t beepNextToggleMs    = 0;
static bool     beepActive          = false;
static uint16_t beepOnMs            = 0;
static uint16_t beepOffMs           = 0;
static bool     buzzContinuous      = false;  // 連續發報模式中（ALARMING）

// OLED 反色閃爍 SM
static bool     oledInverted          = false;
static uint32_t oledInvertStartMs     = 0;
static uint16_t oledInvertDurationMs  = 0;
static const uint16_t OLED_FLASH_MS   = 200;

// 顯示節流
static uint32_t lastDisplayUpdateMs = 0;
static const uint32_t DISPLAY_UPDATE_INTERVAL_MS = 200;

// ============================================================
// 函式宣告
// ============================================================

void handleButtons();
void onShortPress(uint8_t btnIdx);
void onLongPress(uint8_t btnIdx);
void dispatchOhcaEvent(ohca_event_t event, uint32_t since_ms);
void updateOhcaTick();
void recordLocalEvent(ems_event_type_t type);
void recordSuppEvent(supp_type_t supp_type, uint8_t count);
void enterDrugMenu();
void enterShockBackfillMenu();
void resetSubState();
void updateVentTick();
void applyVentOutput(const vent_output_t& out);
void drawVentPre();
void drawVentStandalone();
void drawVentEndCheck();
void drawQuickMenu();
void drawOhcaVentOverlay(int y_top);
void enterMainMenu();
void exitOhcaCase();

void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs);
void stopBeep();
void updateBeepMachine();
void triggerOledFlash(uint16_t durationMs);
void updateOledFlashMachine();
void applyOhcaOutput(const ohca_output_t& out);

void updateDisplay();
void drawMainMenu();
static void drawSyncScreen();
void drawOhcaStartFlash();
void drawOhcaWaitFirstEpi();
void drawOhcaCountdownCommon(uint32_t time_ms, uint16_t timeColor, const char* label, bool flashOn);
static void drawOhcaCountdownTimeOnly(uint8_t ohcaState);
void drawOhcaEndCheck();
void drawOhcaLocked();
void drawOhcaSummary();
void drawTwoStepArmedOverlay(const char* what);
void drawOhcaConfirmDialog(uint8_t evType);
void drawOhcaEndConfirmDialog();
void drawFlashOverlay();
void triggerFlash(const char* title, const char* subtitle, uint16_t duration_ms, uint16_t titleColor,
                  float titleSize = FLASH_TITLE_SIZE_DEFAULT,
                  float subtitleSize = FLASH_SUBTITLE_SIZE_DEFAULT);
void drawPlaceholder(const char* title, const char* phase);
void drawHistoryList();
static void handleSummarySubmenuPrimary();
void drawDrugMenu();
void drawBackfillType();
void drawBackfillCount();
void drawBackfillConfirm();
void drawBackfillSuccess();
void drawAmioConfirmPrompt();
void drawTimeline();

// ============================================================
// Phase F MVP1：BLE GATT callbacks + RX queue drain
// 設計遵守 feedback_ble_callback_non_blocking：onWrite 只 memcpy + 旗標，
// 實際 time_sync_handle 推給 main loop 跑（避免阻塞 GATT task）
// ============================================================

/**
 * BLE Server 生命週期 callback。
 * 連線斷掉後立即重啟廣播，方便 web 端重連測試。
 */
class BleServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* /*pServer*/) override {
        // STEP 01: 標記連線狀態，main loop 印 log + 更新 BLE 圖示
        g_ble_client_connected = true;
    }
    void onDisconnect(BLEServer* /*pServer*/) override {
        // STEP 01: 標記斷線
        g_ble_client_connected = false;
        // STEP 02: 立即重啟廣播，否則裝置會「躲」起來無法被掃到
        BLEDevice::startAdvertising();
    }
};

/**
 * RX characteristic write callback。
 * 跑在 ESP32 BLE GATT task — 嚴禁阻塞操作。
 * 只做 memcpy 到 g_ble_rx_buf + 旗標 in portMUX 臨界區，
 * 實際 JSON 處理推給 main loop 的 process_pending_ble_rx()。
 */
class BleRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
        // STEP 01: 取得 callback 提供的 raw bytes（binary，未必 null-terminated）
        std::string value = pChar->getValue();
        size_t len = value.length();
        if (len == 0 || len > BLE_RX_BUF_MAX) {
            return;
        }

        // STEP 02: 拷貝 + 旗標統一在 portMUX 臨界區（擋 main loop drain race）
        portENTER_CRITICAL(&g_ble_rx_mux);
        if (g_ble_rx_ready) {
            // STEP 02.01: 已有未排空的訊息就丟新的（main loop 下個 tick 即排空）
            portEXIT_CRITICAL(&g_ble_rx_mux);
            return;
        }
        memcpy(g_ble_rx_buf, value.data(), len);
        g_ble_rx_len = len;
        g_ble_rx_ready = true;
        portEXIT_CRITICAL(&g_ble_rx_mux);
    }
};

/**
 * Main loop 排空 BLE RX：解析 time_sync JSON → 更新 g_ts_state → notify ACK。
 * 呼叫前已確認 g_ble_rx_ready=true。
 */
static void process_pending_ble_rx() {
    // STEP 01: 原子取出 buffer 後立即清旗標（portMUX 擋 GATT task 同時寫入）
    uint8_t local_buf[BLE_RX_BUF_MAX];
    size_t local_len;
    portENTER_CRITICAL(&g_ble_rx_mux);
    local_len = g_ble_rx_len;
    memcpy(local_buf, g_ble_rx_buf, local_len);
    g_ble_rx_ready = false;
    portEXIT_CRITICAL(&g_ble_rx_mux);

    // STEP 02: 解析 JSON 取 type 欄位做訊息分流
    //   time_sync_handle / sync_dispatcher_dispatch_input 各自還會 parse 一次自己
    //   關心的欄位，此處只看 type 一個 string，重複 parse 成本可忽略
    JsonDocument doc;
    DeserializationError parse_err = deserializeJson(doc, local_buf, local_len);
    if (parse_err) {
        Serial.printf("[BLE] drop malformed JSON (%u bytes)\n", (unsigned)local_len);
        return;
    }
    const char* type = doc["type"].is<const char*>() ? doc["type"].as<const char*>() : "";

    // STEP 03: type=time_sync → 既有 MVP1 對時路徑（不變）
    if (strcmp(type, "time_sync") == 0) {
        char ack_buf[BLE_ACK_BUF_MAX];
        size_t ack_len = 0;
        ems::TimeSyncResult r = ems::time_sync_handle(
            &g_ts_state,
            local_buf, local_len,
            millis(),
            /*rtc_present=*/false,
            ack_buf, sizeof(ack_buf), &ack_len);
        const char* result_str =
            (r == ems::TimeSyncResult::Applied)  ? "Applied"  :
            (r == ems::TimeSyncResult::Rejected) ? "Rejected" :
                                                   "ParseError";
        Serial.printf("[BLE] time_sync %s ack_len=%u\n", result_str, (unsigned)ack_len);
        if (ack_len > 0 && g_ble_tx_char != nullptr && g_ble_client_connected) {
            g_ble_tx_char->setValue(reinterpret_cast<uint8_t*>(ack_buf), ack_len);
            g_ble_tx_char->notify();
        }
        return;
    }

    // STEP 04: type=pair_verify → Phase F MVP2 配對碼驗證
    //   payload: {"type":"pair_verify","input":"NNNN"}
    if (strcmp(type, "pair_verify") == 0) {
        const char* input = doc["input"].is<const char*>() ? doc["input"].as<const char*>() : "";
        size_t input_len = strlen(input);
        ems::DispatchInputResult r = ems::sync_dispatcher_dispatch_input(
            &g_sync_ctx, input, input_len, millis());
        const char* result_str =
            (r == ems::DispatchInputResult::Accepted)          ? "Accepted"  :
            (r == ems::DispatchInputResult::Rejected)          ? "Rejected"  :
            (r == ems::DispatchInputResult::LockedOut)         ? "LockedOut" :
                                                                 "IgnoredWrongState";
        Serial.printf("[BLE] pair_verify %s state=%u\n",
                      result_str, (unsigned)g_sync_ctx.state);
        return;
    }

    // STEP 05: 未知 type
    Serial.printf("[BLE] unknown type='%s' drop\n", type);
}

// ============================================================
// setup() / loop()
// ============================================================

/** GOOUUU 板上 WS2812 RGB LED 接 GPIO 48；boot 階段可能被訊號 latch 成隨機顏色，主動發 (0,0,0) 關掉。 */
static const uint8_t WS2812_PIN = 48;

void setup() {
    Serial.begin(115200);
    // ESP32-S3 USB-CDC 列舉需 1~2 秒，太早 Serial.println 會被吃掉。
    // 等到 Serial 通了再印，方便除錯。
    uint32_t serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart) < 3000) {
        delay(10);
    }
    Serial.println("[BOOT] EMS Timer Phase A");

    // STEP 00: 關掉板上 WS2812（GPIO 48）。boot bootloader 可能 latch 成白色，必須主動 reset。
    //   neopixelWrite() 由 esp32-hal-rgb-led 提供（ESP32 Arduino core ≥ 2.0.7 內建）。
    neopixelWrite(WS2812_PIN, 0, 0, 0);

    // STEP 01: 蜂鳴器
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

#if ENABLE_VIBRATION
    pinMode(VIBRATION_PIN, OUTPUT);
    digitalWrite(VIBRATION_PIN, LOW);
#endif

    // STEP 02: 8 按鍵 INPUT_PULLUP
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        pinMode(BTN_PINS[i], INPUT_PULLUP);
        lastBtnState[i] = digitalRead(BTN_PINS[i]);
    }

    // STEP 03: TFT 初始化（LovyanGFX + DMA + sprite buffer）
    //   - tft.init()：SPI 80MHz @ DMA_CH_AUTO（class LGFX 內 cfg）
    //   - invert: false（蝦皮紅板 polarity，已寫死於 LGFX panel cfg）
    //   - display 為全頁 LGFX_Sprite（320×240 @ 16bpp = 153,600 bytes，
    //     優先嘗試 PSRAM；fail 退 internal RAM）
    //   - 所有 draw 寫到 display sprite，updateDisplay 結尾 pushSprite DMA 推到 tft
    tft.init();
    tft.setRotation(3);  // 3 = 橫向 320x240（LGFX 跟 Adafruit_ST7789 rotation index 差 180）
    tft.fillScreen(0x0000);

    display.setColorDepth(16);
    display.setPsram(true);  // PSRAM 優先（N16R8 8MB），fail 退 internal RAM
    if (!display.createSprite(SCREEN_W, SCREEN_H)) {
        Serial.println("[FATAL] sprite createSprite failed");
    }
    display.fillScreen(0x0000);

    // STEP 03.01: vlw 字型載入（Sarasa Mono TC Bold 24px，222 glyphs）
    //   - loadFont 後設 g_vlw_loaded 旗標；中文渲染前呼叫 useZhFont() lazy reload
    //   - LGFX setFont() 切走內建字型會 _runtime_font.reset() 析構 VLWfont，
    //     所以不能緩存 VLWfont*，每次需要 vlw 必須檢查並重 load
    //   - 首次載入失敗時做一次 retry（PSRAM 暫態），仍失敗就走 fallback
    //     Font0 並 Serial 警告（CJK 會顯示為 ASCII tofu，避免救護現場啞顯示）
    g_vlw_loaded = display.loadFont(ems_zh_24_vlw);
    if (!g_vlw_loaded) {
        delay(50);
        g_vlw_loaded = display.loadFont(ems_zh_24_vlw);
    }
    Serial.printf("[FONT] vlw %s: %u bytes\n",
                  g_vlw_loaded ? "loaded" : "FAILED (CJK will fallback to Font0)",
                  (unsigned)ems_zh_24_vlw_len);

    // STEP 04: 兩段確認 init
    twoStepConfirm_init(&epiConfirm,   TWO_STEP_DEFAULT_TIMEOUT_MS);
    twoStepConfirm_init(&shockConfirm, TWO_STEP_DEFAULT_TIMEOUT_MS);

    // STEP 04.5: Phase E 持久化 — mount LittleFS + storage_init
    //   失敗只 log warn，不擋 boot；歷史紀錄會走 "無資料" 路徑，OHCA case 跑得起來
    if (emsStorage_fs_mount(&g_storage_be)) {
        if (storage_init(&g_storage_be)) {
            g_storage_ready = true;
            Serial.println("[STORAGE] OK LittleFS mounted + storage_init");
        } else {
            Serial.println("[STORAGE] WARN storage_init failed");
        }
    } else {
        Serial.println("[STORAGE] WARN LittleFS mount failed");
    }

    // STEP 04.6: Phase F MVP1 — BLE NUS peripheral 初始化
    //   失敗只 log warn 不擋 boot（對齊 storage_init 容錯模式）。
    //   廣播後待 web 端（docs/ble-tester/）連線送 time_sync 才會有 epoch；
    //   未對時前事件 timestamp_ms = 0（spec §4.1）。
    ems::time_sync_init(&g_ts_state);
    BLEDevice::init(BLE_DEVICE_NAME);
    g_ble_server = BLEDevice::createServer();
    if (g_ble_server == nullptr) {
        Serial.println("[BLE] WARN createServer failed, time_sync 將不可用");
    } else {
        g_ble_server->setCallbacks(new BleServerCallbacks());

        BLEService* ble_service = g_ble_server->createService(NUS_SERVICE_UUID);

        BLECharacteristic* ble_rx_char = ble_service->createCharacteristic(
            NUS_RX_UUID,
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        ble_rx_char->setCallbacks(new BleRxCallbacks());

        g_ble_tx_char = ble_service->createCharacteristic(
            NUS_TX_UUID,
            BLECharacteristic::PROPERTY_NOTIFY);
        g_ble_tx_char->addDescriptor(new BLE2902());  // CCCD：讓 client 訂閱 notify

        ble_service->start();
        BLEAdvertising* ble_advertising = BLEDevice::getAdvertising();
        ble_advertising->addServiceUUID(NUS_SERVICE_UUID);
        ble_advertising->setScanResponse(true);
        ble_advertising->setMinPreferred(BLE_CONN_INTERVAL_MIN);
        ble_advertising->setMaxPreferred(BLE_CONN_INTERVAL_MAX);
        BLEDevice::startAdvertising();
        Serial.printf("[BLE] advertising as %s\n", BLE_DEVICE_NAME);
    }

    // STEP 04.7: Phase F MVP2 — sync dispatcher 初始 IDLE
    ems::sync_dispatcher_init(&g_sync_ctx, millis());

    // STEP 05: 初始顯示
    updateDisplay();
    Serial.println("[READY] MainMenu");
}

void loop() {
    // STEP 00: Phase F MVP1 — 排空 BLE RX queue
    //   time_sync 立即生效，讓接下來 handleButtons 觸發的事件 timestamp 用真實 epoch
    if (g_ble_rx_ready) {
        process_pending_ble_rx();
    }

    // STEP 00.5: Phase F MVP2 — sync dispatcher observer（僅在 GLOBAL_SYNC 內活）
    //   - BLE 連線邊緣 → dispatch BLE_CONNECTED / BLE_DISCONNECTED
    //   - 每 tick dispatch TICK 給 timeout 判斷
    //   - SENDING 入口 stub：set_total_chunks(0) + CHUNK_ACKED → 立即 DONE
    //   - DONE/ERROR 顯示完自然回 IDLE → 退主選單
    if (globalState == GLOBAL_SYNC) {
        static bool prev_ble_conn = false;
        bool cur_ble_conn = g_ble_client_connected;
        if (cur_ble_conn != prev_ble_conn) {
            ems::sync_dispatcher_dispatch(&g_sync_ctx,
                cur_ble_conn ? ems::SyncEvent::BLE_CONNECTED : ems::SyncEvent::BLE_DISCONNECTED,
                millis());
            prev_ble_conn = cur_ble_conn;
        }
        ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::TICK, millis());

        static ems::SyncState prev_sync_state = ems::SyncState::IDLE;
        ems::SyncState cur_sync_state = g_sync_ctx.state;
        if (cur_sync_state != prev_sync_state) {
            Serial.printf("[SYNC] state %u -> %u\n",
                          (unsigned)prev_sync_state, (unsigned)cur_sync_state);
        }
        if (cur_sync_state == ems::SyncState::SENDING && prev_sync_state != ems::SyncState::SENDING) {
            // MVP2 stub：不真送資料，0 chunk + 立即 ACK → DONE
            ems::sync_dispatcher_set_total_chunks(&g_sync_ctx, 0);
            ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::CHUNK_ACKED, millis());
        }
        if (cur_sync_state == ems::SyncState::IDLE && prev_sync_state != ems::SyncState::IDLE) {
            // SoT §16.5：「同步完成 → 顯示 1 秒 → 自動回案件總覽」
            // caller globalState 由 handleSummarySubmenuPrimary 在進 SYNC 前記入 g_sync_return_to
            // （GLOBAL_OHCA 結束鎖定 SUMMARY / GLOBAL_HISTORY_PLACEHOLDER 歷史 SUMMARY）。
            // ERROR → IDLE 也走此路徑（SoT §16.9 同步失敗亦留在原案件總覽供重試）。
            globalState = g_sync_return_to;
        }
        prev_sync_state = g_sync_ctx.state;
    }

    handleButtons();
    updateOhcaTick();
    updateVentTick();

    uint32_t now = millis();
    twoStepConfirm_tick(&epiConfirm,   now);
    twoStepConfirm_tick(&shockConfirm, now);
    twoStepConfirm_tick(&amioConfirm,  now);

    // STEP 01: armed 提示 5s 自動消失
    if (showEpiArmedPrompt && now - epiArmedPromptStart >= ARMED_PROMPT_MS) {
        showEpiArmedPrompt = false;
    }
    if (showShockArmedPrompt && now - shockArmedPromptStart >= ARMED_PROMPT_MS) {
        showShockArmedPrompt = false;
    }
    if (showAmioArmedPrompt && now - amioArmedPromptStart >= ARMED_PROMPT_MS) {
        showAmioArmedPrompt = false;
    }

    // STEP 02: 補登成功提示 2s 後自動回主畫面
    if (ohcaSubState == SUBSTATE_BACKFILL_SUCCESS &&
        now - backfillSuccessShownMs >= BACKFILL_SUCCESS_MS) {
        resetSubState();
    }

    // Flash overlay timeout（demo flash() 對齊，duration 由 caller 指定）
    if (flashState.active && now - flashState.startMs >= flashState.durationMs) {
        flashState.active = false;
    }

    // STEP 03: vent 返回鍵提示 2s 自動消失
    if (ventBackHintShown && now - ventBackHintStartMs >= VENT_BACK_HINT_MS) {
        ventBackHintShown = false;
    }

    updateBeepMachine();
    updateOledFlashMachine();

    if (now - lastDisplayUpdateMs >= DISPLAY_UPDATE_INTERVAL_MS) {
        lastDisplayUpdateMs = now;
        updateDisplay();
    }
}

// ============================================================
// 按鈕處理：邊緣偵測 + debounce + 短按 / 長按 3s
// ============================================================

void handleButtons() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        uint8_t cur = digitalRead(BTN_PINS[i]);

        if (cur != lastBtnState[i]) {
            // STEP 01: 邊緣事件
            // STEP 01.00: 統一 debounce — press 與 release 邊緣共用同一個門檻，
            //   避免 TFT 慢渲染拉長時間窗時 bounce 邊緣穿過原本只擋 press 的防抖造成 double-fire
            if (now - lastPressMs[i] < DEBOUNCE_MS) {
                continue;
            }
            lastPressMs[i] = now;

            if (cur == LOW) {
                // STEP 01.01: 按下（press start）
                btnPressStartMs[i] = now;
                btnLongFired[i]    = false;
            } else {
                // STEP 01.02: 放開（release）
                if (btnPressStartMs[i] > 0 && !btnLongFired[i]) {
                    uint32_t held = now - btnPressStartMs[i];
                    if (held < SHORT_PRESS_MAX_MS) {
                        onShortPress(i);
                    }
                }
                btnPressStartMs[i] = 0;
            }
            lastBtnState[i] = cur;
        } else {
            // STEP 02: 按住中 — 依該按鈕 threshold 觸發長按（fire on threshold reach）
            uint16_t threshold = LONG_PRESS_MS_PER_BTN[i];
            if (threshold > 0 && cur == LOW &&
                btnPressStartMs[i] > 0 && !btnLongFired[i]) {
                if (now - btnPressStartMs[i] >= threshold) {
                    btnLongFired[i] = true;
                    onLongPress(i);
                }
            }
        }
    }
}

// ============================================================
// 短按 dispatcher
// ============================================================

void onShortPress(uint8_t btnIdx) {
    Serial.printf("[BTN] short %u (state=%u/%u)\n", btnIdx, globalState, ohcaState);

    // ===== 主功能表 =====
    if (globalState == GLOBAL_MAIN_MENU) {
        switch (btnIdx) {
            case BTN_UP:
                mainMenuCursor = (mainMenuCursor + MAIN_MENU_COUNT - 1) % MAIN_MENU_COUNT;
                break;
            case BTN_DOWN:
                mainMenuCursor = (mainMenuCursor + 1) % MAIN_MENU_COUNT;
                break;
            case BTN_PRIMARY:
                // STEP 01: 進入對應子模組
                switch (mainMenuCursor) {
                    case 0:  // OHCA Case
                        globalState = GLOBAL_OHCA;
                        dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_SHORT, 0);
                        startFlashStartMs = millis();
                        caseStartMs       = millis();
                        caseStartEpochMs  = ems::time_sync_current_epoch_ms(&g_ts_state, caseStartMs);  // 對時前 = 0
                        eventCount        = 0;
                        nextEventId       = 1;
                        ohcaLastEpiMs     = 0;
                        ohcaPrevSinceMs   = 0;
                        alarmMuted        = false;
                        resetSubState();
                        Serial.println("[OHCA] Case start (START_FLASH)");
                        break;
                    case 1:  // 6 秒通氣節奏（獨立模式）
                        // A8：先進 VENT_PRE「按主鍵開始」preview，主鍵按下後才正式啟動
                        globalState        = GLOBAL_VENT;
                        ventStartMs        = 0;            // 尚未啟動
                        ventPrevSinceMs    = 0;
                        ventEndCheckShown  = false;
                        ventBackHintShown  = false;
                        ventPaused         = false;
                        ventPreShown       = true;          // A8：等使用者按主鍵
                        Serial.println("[VENT] enter PRE (preview)");
                        break;
                    case 2: globalState = GLOBAL_TRAINING_PLACEHOLDER; break;
                    case 3:  // 歷史紀錄（Phase E）
                        // 進入時重抓 list（每次進都最新；最新案件在 index 0）
                        if (g_storage_ready) {
                            historyCount = storage_list(&g_storage_be,
                                                        EMS_CASE_TYPE_OHCA,
                                                        historyCases,
                                                        EMS_STORAGE_OHCA_CAP);
                        } else {
                            historyCount = 0;
                        }
                        historyCursor       = 0;
                        historyScrollOffset = 0;
                        historySummaryMode  = false;
                        globalState         = GLOBAL_HISTORY_PLACEHOLDER;
                        break;
                    case 4:  // 系統設定（Phase G placeholder）
                        globalState = GLOBAL_SETTINGS_PLACEHOLDER;
                        break;
                }
                break;
            default:
                break;
        }
        return;
    }

    // ===== Phase C: GLOBAL_VENT 獨立模式（V1 §13）=====
    if (globalState == GLOBAL_VENT) {
        uint32_t now = millis();
        // A8: VENT_PRE preview 畫面（按主鍵開始）
        if (ventPreShown) {
            if (btnIdx == BTN_UP) {
                ventVolume = clampVentVolume((int16_t)ventVolume + 1);
                return;
            }
            if (btnIdx == BTN_DOWN) {
                ventVolume = clampVentVolume((int16_t)ventVolume - 1);
                return;
            }
            if (btnIdx == BTN_BACK) {
                // 返回鍵 → 直接回主功能表（preview 還沒啟動，可直接走）
                ventPreShown = false;
                enterMainMenu();
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                // 主鍵 → 正式啟動（V1 §13.5 啟動規則：秒數從 1 開始）
                ventPreShown = false;
                ventStartMs  = now;
                ventPrevSinceMs = 0;
                Serial.println("[VENT] PRE -> running");
                return;
            }
            return;
        }
        // STEP 01: 結束確認對話框中
        if (ventEndCheckShown) {
            if (btnIdx == BTN_PRIMARY) {
                Serial.println("[VENT] standalone end confirmed");
                stopBeep();
                ventEndCheckShown = false;
                enterMainMenu();
                return;
            }
            if (btnIdx == BTN_BACK) {
                ventEndCheckShown = false;
                return;
            }
            return;
        }
        // STEP 02: 主畫面按鍵
        if (btnIdx == BTN_UP) {
            ventVolume = clampVentVolume((int16_t)ventVolume + 1);
            return;
        }
        if (btnIdx == BTN_DOWN) {
            ventVolume = clampVentVolume((int16_t)ventVolume - 1);
            return;
        }
        if (btnIdx == BTN_BACK) {
            // V1 §13.15：執行中按返回鍵不直接結束 → 提示「請長按主鍵」
            ventBackHintShown   = true;
            ventBackHintStartMs = now;
            return;
        }
        if (btnIdx == BTN_PRIMARY) {
            // V1 §13.11 / §13.12：主鍵短按 toggle 暫停/繼續
            // 繼續時 §13.12 規定秒數重新從 1 開始 → 重置 ventStartMs
            ventPaused = !ventPaused;
            if (!ventPaused) {
                ventStartMs     = now;
                ventPrevSinceMs = 0;
            } else {
                stopBeep();
            }
            Serial.printf("[VENT] paused=%d\n", ventPaused);
            return;
        }
        return;
    }

    // ===== Phase E：歷史紀錄（已實作，覆蓋共用佔位處理） =====
    if (globalState == GLOBAL_HISTORY_PLACEHOLDER) {
        // STEP 01: SUMMARY 子畫面 — sub-menu cursor + BACK 回列表（SoT §10.4「可同步至 App」涵蓋歷史）
        if (historySummaryMode) {
            switch (btnIdx) {
                case BTN_UP:
                    summarySubmenuCursor =
                        (summarySubmenuCursor + SUMMARY_SUBMENU_COUNT - 1) % SUMMARY_SUBMENU_COUNT;
                    break;
                case BTN_DOWN:
                    summarySubmenuCursor =
                        (summarySubmenuCursor + 1) % SUMMARY_SUBMENU_COUNT;
                    break;
                case BTN_PRIMARY:
                    handleSummarySubmenuPrimary();
                    break;
                case BTN_BACK:
                    historySummaryMode = false;
                    eventCount = 0;     // 清掉 history 載回來的事件，避免汙染下個 OHCA case
                    break;
                default:
                    // EPI/SHOCK/RECORD/POWER 在歷史 SUMMARY 無作用，留 trace 避免「裝置死當」誤判
                    Serial.printf("[HIST_SUMMARY] ignored btn=%u\n", btnIdx);
                    break;
            }
            return;
        }
        // STEP 02: 列表模式
        switch (btnIdx) {
            case BTN_UP:
                if (historyCursor > 0) {
                    historyCursor--;
                }
                if (historyCursor < historyScrollOffset) {
                    historyScrollOffset = historyCursor;
                }
                break;
            case BTN_DOWN:
                if (historyCursor + 1 < historyCount) {
                    historyCursor++;
                }
                if (historyCursor >= historyScrollOffset + HISTORY_VISIBLE_ROWS) {
                    historyScrollOffset = historyCursor - (HISTORY_VISIBLE_ROWS - 1);
                }
                break;
            case BTN_BACK:
                enterMainMenu();
                break;
            case BTN_PRIMARY:
                // STEP 02.01: 選定後載入該案件、跳到 SUMMARY 子畫面
                if (historyCount > 0 && g_storage_ready) {
                    uint16_t loaded = 0;
                    bool ok = storage_load_events(&g_storage_be,
                                                  EMS_CASE_TYPE_OHCA,
                                                  historyCases[historyCursor].id,
                                                  events, MAX_EVENTS, &loaded);
                    if (ok) {
                        eventCount = loaded;
                        historySummaryMode   = true;
                        summarySubmenuCursor = SUMMARY_SUBMENU_TIMELINE;
                        Serial.printf("[STORAGE] loaded case %s (%u events)\n",
                                      historyCases[historyCursor].id,
                                      loaded);
                    } else {
                        Serial.printf("[STORAGE] load failed for %s\n",
                                      historyCases[historyCursor].id);
                    }
                }
                break;
            default:
                break;
        }
        return;
    }

    // ===== Phase X 佔位畫面：返回鍵回主功能表 =====
    if (globalState >= GLOBAL_TRAINING_PLACEHOLDER && globalState <= GLOBAL_SETTINGS_PLACEHOLDER) {
        if (btnIdx == BTN_BACK) {
            enterMainMenu();
        }
        return;
    }

    // ===== Phase F MVP2：同步資料 =====
    //   - BTN_PRIMARY 在 AWAITING_MAIN_KEY → MAIN_KEY_PRESS（dispatcher 推進 SENDING）
    //   - BTN_BACK 在非 SENDING → BACK_KEY_PRESS（dispatcher 回 IDLE → 主功能表）
    if (globalState == GLOBAL_SYNC) {
        if (btnIdx == BTN_PRIMARY && g_sync_ctx.state == ems::SyncState::AWAITING_MAIN_KEY) {
            ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::MAIN_KEY_PRESS, millis());
        } else if (btnIdx == BTN_BACK && g_sync_ctx.state != ems::SyncState::SENDING) {
            ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::BACK_KEY_PRESS, millis());
        }
        return;
    }

    // ===== OHCA 模式 =====
    if (globalState != GLOBAL_OHCA) return;

    uint32_t now = millis();

    // ===== Phase B/C sub-state 子流程處理 =====
    if (ohcaSubState != SUBSTATE_NONE) {
        // STEP 01: SUCCESS 顯示中：忽略所有按鍵（自動 2s 後消失，由 loop 處理）
        if (ohcaSubState == SUBSTATE_BACKFILL_SUCCESS) return;

        // STEP 01.5: QUICK_MENU（Phase C，返回鍵入口；V1 §14.9 動態 3/4 項）
        if (ohcaSubState == SUBSTATE_QUICK_MENU) {
            // V1 §14.9 選項（B3：加「案件簡版總覽」對齊 demo）：
            //   未開啟：[0] Enable [1] Summary [2] Back
            //   已開啟：[0] Pause/Resume [1] Disable [2] Summary [3] Back
            uint8_t cnt = ohcaVentOverlayEnabled ? 4 : 3;
            if (btnIdx == BTN_UP) {
                backfillCursor = (backfillCursor + cnt - 1) % cnt;
                return;
            }
            if (btnIdx == BTN_DOWN) {
                backfillCursor = (backfillCursor + 1) % cnt;
                return;
            }
            if (btnIdx == BTN_BACK) { resetSubState(); return; }
            if (btnIdx == BTN_PRIMARY) {
                const uint8_t summaryIdx = ohcaVentOverlayEnabled ? 2 : 1;
                const uint8_t backIdx    = ohcaVentOverlayEnabled ? 3 : 2;
                if (backfillCursor == summaryIdx) {
                    // B3：案件簡版總覽 — demo 略過，flash 提示
                    triggerFlash("簡版總覽", "結束案件後看完整總覽", 2000, COLOR_TEXT_PRIMARY,
                                 FLASH_TITLE_SIZE_DEFAULT, FLASH_SUBTITLE_SIZE_LONG);
                    Serial.println("[OHCA] quick: summary placeholder");
                    resetSubState();
                    return;
                }
                if (backfillCursor == backIdx) {
                    // 返回 OHCA → resetSubState 即可
                    resetSubState();
                    return;
                }
                if (!ohcaVentOverlayEnabled) {
                    // backfillCursor == 0：Enable 6s vent
                    ohcaVentOverlayEnabled = true;
                    ohcaVentPaused         = false;
                    ventStartMs            = millis();
                    ventPrevSinceMs        = 0;
                    triggerFlash("6 秒給氣", "已開啟", 800, COLOR_ACCENT_OK);
                    Serial.println("[OHCA] vent overlay = ON");
                } else {
                    if (backfillCursor == 0) {
                        // toggle Pause / Resume（V1 §14.10 / §14.11）
                        ohcaVentPaused = !ohcaVentPaused;
                        if (!ohcaVentPaused) {
                            ventStartMs     = millis();
                            ventPrevSinceMs = 0;
                            triggerFlash("6 秒給氣", "已繼續", 800, COLOR_ACCENT_OK);
                        } else {
                            stopBeep();
                            triggerFlash("6 秒給氣", "已暫停", 800, COLOR_ACCENT_WARN);
                        }
                        Serial.printf("[OHCA] vent paused = %d\n", ohcaVentPaused);
                    } else if (backfillCursor == 1) {
                        // Disable 6s vent
                        ohcaVentOverlayEnabled = false;
                        ohcaVentPaused         = false;
                        stopBeep();
                        triggerFlash("6 秒給氣", "已關閉", 800, COLOR_TEXT_MUTED);
                        Serial.println("[OHCA] vent overlay = OFF");
                    }
                }
                resetSubState();
                return;
            }
            return;
        }

        // STEP 02: TIMELINE：上下鍵翻頁、返回鍵回 SUMMARY
        if (ohcaSubState == SUBSTATE_TIMELINE) {
            if (btnIdx == BTN_UP   && timelineScrollOffset > 0)         timelineScrollOffset--;
            if (btnIdx == BTN_DOWN && timelineScrollOffset + 4 < eventCount) timelineScrollOffset++;
            if (btnIdx == BTN_BACK)                                      ohcaSubState = SUBSTATE_NONE;
            return;
        }

        // STEP 03: AMIO_CONFIRM：BTN_PRIMARY 兩段確認；返回鍵取消
        if (ohcaSubState == SUBSTATE_AMIO_CONFIRM) {
            if (btnIdx == BTN_BACK) {
                resetSubState();
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                bool ok = twoStepConfirm_press(&amioConfirm, now);
                if (ok) {
                    showAmioArmedPrompt = false;
                    recordLocalEvent(EVT_AMIODARONE);
                    dispatchOhcaEvent(OHCA_EVT_AMIO_CONFIRMED, 0);  // 不重啟倒數
                    triggerBeep(1, 80, 0);
                    // A4：對齊 demo flash('Amiodarone 已紀錄', '')
                    triggerFlash("Amiodarone 已紀錄", "", FLASH_DEFAULT_MS, COLOR_ACCENT_OK,
                                 FLASH_TITLE_SIZE_XLONG, FLASH_SUBTITLE_SIZE_DEFAULT);
                    Serial.println("[OHCA] Amio confirmed");
                    resetSubState();
                } else {
                    showAmioArmedPrompt  = true;
                    amioArmedPromptStart = now;
                }
            }
            return;
        }

        // STEP 04: DRUG_MENU：上下鍵切換「補登 EPI / Amiodarone」；主鍵確認；返回鍵取消
        if (ohcaSubState == SUBSTATE_DRUG_MENU) {
            if (btnIdx == BTN_UP || btnIdx == BTN_DOWN) {
                backfillCursor ^= 1;  // 0 ↔ 1
                return;
            }
            if (btnIdx == BTN_BACK) { resetSubState(); return; }
            if (btnIdx == BTN_PRIMARY) {
                if (backfillCursor == 0) {
                    // STEP 04.01: 進補登 EPI 流程（先選類型）
                    backfillCategory = BACKFILL_CAT_EPI;
                    backfillSuppType = SUPP_TYPE_EPI_PRE_HANDOVER;
                    backfillCursor   = 0;
                    ohcaSubState     = SUBSTATE_BACKFILL_TYPE;
                } else {
                    // STEP 04.02: 進 Amio 兩段確認
                    twoStepConfirm_init(&amioConfirm, TWO_STEP_DEFAULT_TIMEOUT_MS);
                    showAmioArmedPrompt = false;
                    ohcaSubState        = SUBSTATE_AMIO_CONFIRM;
                }
            }
            return;
        }

        // STEP 05: BACKFILL_TYPE：上下鍵切換「接手前 / 純補登」；主鍵確認；返回鍵取消
        if (ohcaSubState == SUBSTATE_BACKFILL_TYPE) {
            if (btnIdx == BTN_UP || btnIdx == BTN_DOWN) {
                backfillCursor ^= 1;
                if (backfillCategory == BACKFILL_CAT_EPI) {
                    backfillSuppType = (backfillCursor == 0)
                                     ? SUPP_TYPE_EPI_PRE_HANDOVER
                                     : SUPP_TYPE_EPI_PURE;
                } else {
                    backfillSuppType = (backfillCursor == 0)
                                     ? SUPP_TYPE_SHOCK_PRE_HANDOVER
                                     : SUPP_TYPE_SHOCK_PURE;
                }
                return;
            }
            if (btnIdx == BTN_BACK) {
                // 從 EPI 入口進來 → 退回 DRUG_MENU；從電擊入口進來 → 退出
                if (backfillCategory == BACKFILL_CAT_EPI) {
                    ohcaSubState   = SUBSTATE_DRUG_MENU;
                    backfillCursor = 0;
                } else {
                    resetSubState();
                }
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                backfillCount = 1;  // 預設 1
                ohcaSubState  = SUBSTATE_BACKFILL_COUNT;
                return;
            }
            return;
        }

        // STEP 06: BACKFILL_COUNT：上下鍵增減次數；主鍵 → CONFIRM；返回鍵 → TYPE
        if (ohcaSubState == SUBSTATE_BACKFILL_COUNT) {
            uint8_t maxN = suppCountMax(backfillSuppType);
            if (btnIdx == BTN_UP   && backfillCount < maxN) backfillCount++;
            if (btnIdx == BTN_DOWN && backfillCount > 1)    backfillCount--;
            if (btnIdx == BTN_BACK) {
                ohcaSubState   = SUBSTATE_BACKFILL_TYPE;
                backfillCursor = 0;
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                ohcaSubState = SUBSTATE_BACKFILL_CONFIRM;
            }
            return;
        }

        // STEP 07: BACKFILL_CONFIRM：主鍵 → 寫入；返回鍵 → 取消回 COUNT
        if (ohcaSubState == SUBSTATE_BACKFILL_CONFIRM) {
            if (btnIdx == BTN_BACK) {
                ohcaSubState = SUBSTATE_BACKFILL_COUNT;
                return;
            }
            if (btnIdx == BTN_PRIMARY) {
                recordSuppEvent(backfillSuppType, backfillCount);
                triggerBeep(1, 80, 0);
                ohcaSubState           = SUBSTATE_BACKFILL_SUCCESS;
                backfillSuccessShownMs = now;
                Serial.printf("[OHCA] supp recorded: type=%u count=%u\n",
                              backfillSuppType, backfillCount);
            }
            return;
        }
        return;
    }

    // ===== OHCA 主畫面（無 sub-state） =====
    switch (btnIdx) {
        case BTN_PRIMARY:
            // STEP 01: ALARMING/OVERTIME 主鍵 — 消音（不轉 state）
            if (ohcaState == OHCA_STATE_ALARMING || ohcaState == OHCA_STATE_OVERTIME) {
                alarmMuted = true;
                stopBeep();
                Serial.println("[OHCA] alarm muted");
                return;
            }
            // STEP 02: END_CHECK 主鍵 — 依 cursor 行為
            if (ohcaState == OHCA_STATE_END_CHECK) {
                // A7：二次確認對話顯示中 → 主鍵 = 真鎖定
                if (endConfirmShown) {
                    endConfirmShown = false;
                    dispatchOhcaEvent(OHCA_EVT_END_CONFIRM, 0);
                    // A5：對齊 demo flash('案件結束並鎖定', '已存入歷史紀錄')
                    triggerFlash("案件結束並鎖定", "已存入歷史紀錄", FLASH_DEFAULT_MS, COLOR_ACCENT_ALERT,
                                 FLASH_TITLE_SIZE_LONG, FLASH_SUBTITLE_SIZE_DEFAULT);
                    Serial.println("[OHCA] case LOCKED (after END_CONFIRM dialog)");
                    return;
                }
                if (endCheckCursor == END_CHECK_CURSOR_CONFIRM) {
                    // A7：開二次確認對話（不直接鎖定）
                    endConfirmShown = true;
                    Serial.println("[OHCA] END_CONFIRM dialog opened");
                    return;
                }
                if (endCheckCursor == END_CHECK_CURSOR_BACKFILL) {
                    // B1：前往補登 — 先回到原 phase，再進 drug menu
                    dispatchOhcaEvent(OHCA_EVT_END_CANCEL, 0);
                    enterDrugMenu();
                    Serial.println("[OHCA] END_CHECK -> backfill (drug menu)");
                    return;
                }
                // CANCEL
                dispatchOhcaEvent(OHCA_EVT_END_CANCEL, 0);
                return;
            }
            // STEP 03: LOCKED 主鍵 — 翻 SUMMARY
            if (ohcaState == OHCA_STATE_LOCKED) {
                dispatchOhcaEvent(OHCA_EVT_TO_SUMMARY, 0);
                summaryScrollOffset  = 0;
                summarySubmenuCursor = SUMMARY_SUBMENU_TIMELINE;
                return;
            }
            // STEP 04: SUMMARY 主鍵 — 觸發 sub-menu 當前 cursor 對應行為（SoT V1 §11.1）
            if (ohcaState == OHCA_STATE_SUMMARY) {
                handleSummarySubmenuPrimary();
                return;
            }
            break;

        case BTN_UP:
            if (ohcaState == OHCA_STATE_END_CHECK && !endConfirmShown) {
                // 3 項循環：CONFIRM(0) ↔ BACKFILL(1) ↔ CANCEL(2)
                endCheckCursor = (EndCheckCursor)((endCheckCursor + 2) % 3);
            } else if (ohcaState == OHCA_STATE_SUMMARY) {
                summarySubmenuCursor =
                    (summarySubmenuCursor + SUMMARY_SUBMENU_COUNT - 1) % SUMMARY_SUBMENU_COUNT;
            }
            break;

        case BTN_DOWN:
            if (ohcaState == OHCA_STATE_END_CHECK && !endConfirmShown) {
                endCheckCursor = (EndCheckCursor)((endCheckCursor + 1) % 3);
            } else if (ohcaState == OHCA_STATE_SUMMARY) {
                summarySubmenuCursor = (summarySubmenuCursor + 1) % SUMMARY_SUBMENU_COUNT;
            }
            break;

        case BTN_BACK:
            // A1：兩段確認對話顯示中 → 返回鍵 = 取消對話（modal 行為）
            if (showEpiArmedPrompt || showShockArmedPrompt) {
                showEpiArmedPrompt   = false;
                showShockArmedPrompt = false;
                Serial.println("[OHCA] confirm dialog cancelled (BACK)");
                return;
            }
            // STEP 01: SUMMARY 返回主功能表
            if (ohcaState == OHCA_STATE_SUMMARY) {
                exitOhcaCase();
                return;
            }
            // STEP 02: END_CHECK 返回鍵
            if (ohcaState == OHCA_STATE_END_CHECK) {
                // A7：二次確認對話顯示中 → 返回 = 退回 END_CHECK 主畫面
                if (endConfirmShown) {
                    endConfirmShown = false;
                    return;
                }
                // 一般 = 取消（回原 phase）
                dispatchOhcaEvent(OHCA_EVT_END_CANCEL, 0);
                return;
            }
            // STEP 03: Phase C — 案件進行中（非 ALARMING）按返回鍵 → 快速功能選單
            //   ALARMING 中不允許離開警報（V1 §14.8 EPI 到期優先權 / Test Plan §5.2.2）
            if (ohcaState == OHCA_STATE_WAIT_FIRST_EPI ||
                ohcaState == OHCA_STATE_COUNTDOWN     ||
                ohcaState == OHCA_STATE_WARNING       ||
                ohcaState == OHCA_STATE_OVERTIME) {
                ohcaSubState   = SUBSTATE_QUICK_MENU;
                backfillCursor = 0;
                Serial.println("[OHCA] enter QUICK_MENU");
                return;
            }
            break;

        case BTN_EPI: {
            // STEP 01: LOCKED 拒絕
            if (ohcaState == OHCA_STATE_LOCKED || ohcaState == OHCA_STATE_SUMMARY) return;
            // STEP 02: 兩段確認
            bool confirmed = twoStepConfirm_press(&epiConfirm, now);
            if (confirmed) {
                showEpiArmedPrompt = false;
                uint32_t since = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
                dispatchOhcaEvent(OHCA_EVT_EPI_CONFIRMED, since);
                ohcaLastEpiMs   = now;
                ohcaPrevSinceMs = 0;
                alarmMuted      = false;
                stopBeep();
                recordLocalEvent(EVT_EPI_LOCAL);
                triggerBeep(1, 80, 0);  // 短確認音
                // A2：對齊 demo flash('EPI 已紀錄', '重新倒數 4 分鐘')
                triggerFlash("EPI 已紀錄", "重新倒數 4 分鐘", FLASH_DEFAULT_MS, COLOR_ACCENT_OK);
                Serial.println("[OHCA] EPI confirmed");
            } else {
                showEpiArmedPrompt   = true;
                epiArmedPromptStart  = now;
                Serial.println("[OHCA] EPI armed (please re-press)");
            }
            break;
        }

        case BTN_SHOCK: {
            if (ohcaState == OHCA_STATE_LOCKED || ohcaState == OHCA_STATE_SUMMARY) return;
            bool confirmed = twoStepConfirm_press(&shockConfirm, now);
            if (confirmed) {
                showShockArmedPrompt = false;
                dispatchOhcaEvent(OHCA_EVT_SHOCK_CONFIRMED, 0);  // 不重啟倒數
                recordLocalEvent(EVT_SHOCK_LOCAL);
                triggerBeep(1, 80, 0);
                // A3：對齊 demo flash('電擊已紀錄', '')
                triggerFlash("電擊已紀錄", "", FLASH_DEFAULT_MS, COLOR_ACCENT_OK);
                Serial.println("[OHCA] Shock confirmed");
            } else {
                showShockArmedPrompt   = true;
                shockArmedPromptStart  = now;
                Serial.println("[OHCA] Shock armed (please re-press)");
            }
            break;
        }

        case BTN_POWER:
        case BTN_RECORD:
            // Phase H / Phase 1.5 — noop
            break;
    }
}

// ============================================================
// 長按 3s dispatcher（OHCA OVERTIME 結束前檢查觸發）
// ============================================================

void onLongPress(uint8_t btnIdx) {
    Serial.printf("[BTN] long %u (state=%u/%u/sub=%u)\n",
                  btnIdx, globalState, ohcaState, ohcaSubState);

    // STEP 01: 主鍵 3s 長按
    if (btnIdx == BTN_PRIMARY) {
        // STEP 01.01: GLOBAL_VENT 獨立模式 → 結束確認對話框（V1 §13.14 結束獨立 6 秒通氣節奏）
        if (globalState == GLOBAL_VENT && !ventEndCheckShown) {
            ventEndCheckShown = true;
            stopBeep();
            Serial.println("[VENT] standalone end check");
            return;
        }
        // STEP 01.02: GLOBAL_OHCA 進行中任意 phase → END_CHECK（SoT V1 §10.1）
        //   排除 START_FLASH / END_CHECK / LOCKED / SUMMARY（A.S8 步驟 4 要求 noop）
        if (globalState == GLOBAL_OHCA && ohcaSubState == SUBSTATE_NONE &&
            isOhcaInProgress(ohcaState)) {
            dispatchOhcaEvent(OHCA_EVT_MAIN_BTN_LONG_3S, 0);
            endCheckCursor   = END_CHECK_CURSOR_CANCEL;
            endConfirmShown  = false;  // A7：reset 二次確認對話旗標
            stopBeep();
            Serial.println("[OHCA] enter END_CHECK");
            return;
        }
        // STEP 01.03: OHCA 但條件不滿足（substate / 排除清單）— field debug log
        if (globalState == GLOBAL_OHCA) {
            Serial.printf("[OHCA] long-press ignored (state=%u sub=%u)\n",
                          ohcaState, ohcaSubState);
        }
        return;
    }

    // STEP 02: EPI 鍵 1s 長按 — 進入藥物選單（Phase B）
    if (btnIdx == BTN_EPI) {
        if (globalState != GLOBAL_OHCA) return;
        if (ohcaState == OHCA_STATE_LOCKED || ohcaState == OHCA_STATE_SUMMARY) return;
        if (ohcaSubState != SUBSTATE_NONE) return;
        enterDrugMenu();
        return;
    }

    // STEP 03: 電擊鍵 1s 長按 — 進入電擊補登選單（Phase B）
    if (btnIdx == BTN_SHOCK) {
        if (globalState != GLOBAL_OHCA) return;
        if (ohcaState == OHCA_STATE_LOCKED || ohcaState == OHCA_STATE_SUMMARY) return;
        if (ohcaSubState != SUBSTATE_NONE) return;
        enterShockBackfillMenu();
        return;
    }
}

// ============================================================
// OHCA event dispatcher（包裝 nextOhcaState）
// ============================================================

void dispatchOhcaEvent(ohca_event_t event, uint32_t since_ms) {
    // 進 END_CHECK 前的 source state — END_CANCEL 取消時用此還原。
    // SoT V1 §10.1：source 不一定是 OVERTIME；於此處由 prev 自動 cache，caller 無需感知。
    static ohca_state_t end_check_source = OHCA_STATE_OVERTIME;
    ohca_state_t prev = ohcaState;

    // STEP 01: 新 OHCA case 起始 dispatch (prev = MAIN_MENU) 時 reset cache
    //   避免上次出勤殘留的 source 被本次 END_CANCEL 誤用（跨 session 隔離）
    if (prev == OHCA_STATE_MAIN_MENU) {
        end_check_source = OHCA_STATE_OVERTIME;
    }

    // STEP 02: 進 END_CHECK 前 capture 合法 source；非進行中 prev 不寫入 cache
    if (event == OHCA_EVT_MAIN_BTN_LONG_3S && isOhcaInProgress(prev)) {
        end_check_source = prev;
    }

    ohcaState = nextOhcaState(prev, event, since_ms, end_check_source);
    if (ohcaState != prev) {
        Serial.printf("[OHCA] %u -> %u (evt=%u)\n", prev, ohcaState, event);
    }

    // STEP 03: END_CANCEL 命中無效 source — lib 已 fallback OVERTIME，於此 log 供 field debug
    if (event == OHCA_EVT_END_CANCEL && !isOhcaInProgress(end_check_source)) {
        Serial.printf("[OHCA] WARN END_CANCEL invalid source=%u, fallback OVERTIME\n",
                      end_check_source);
    }

    // STEP 04: Phase E — 首次進 LOCKED 持久化案件，離開 LOCKED 時 reset 防重複旗標
    if (ohcaState == OHCA_STATE_LOCKED
        && prev != OHCA_STATE_LOCKED
        && g_storage_ready
        && !g_locked_saved) {
        // case_start_ms / case_end_ms 傳 0：caseStartMs 是 boot 後的 millis() 不是 epoch；
        // 重啟後無對齊基準，當絕對時戳會誤導。等 Phase 3 DS3231 上機後改傳 rtc.nowEpochMs()
        bool ok = storage_save_case(&g_storage_be, EMS_CASE_TYPE_OHCA,
                                    events, eventCount,
                                    /*case_start_ms*/ 0,
                                    /*case_end_ms*/   0);
        Serial.printf("[STORAGE] save case (%u events) %s\n",
                      eventCount, ok ? "OK" : "FAILED");
        // C-4 最小修：只有成功才設 true。原本 unconditional = true 導致失敗永不重試 +
        // 救護員以為紀錄齊全（silent data loss）。失敗時 g_locked_saved 留 false，
        // 下個 tick 進來條件仍成立 → 自動 retry。
        // 完整修法（紅色 UI 警告 + 蜂鳴 + 重試按鈕 + 失敗哲學 A/B/C）需 PM 對齊，
        // 見 tasks/todo.md §🔧 Group 2C C-4。
        g_locked_saved = ok;
        if (!ok) {
            Serial.println("[STORAGE] WARN save failed; will auto-retry next tick "
                           "(C-4 minimal fix; PM-aligned UI feedback TBD)");
        }
    }
    if (ohcaState != OHCA_STATE_LOCKED
        && ohcaState != OHCA_STATE_SUMMARY) {
        g_locked_saved = false;
    }
}

// ============================================================
// OHCA tick driver：START_FLASH timeout + COUNTDOWN/.../OVERTIME 推進
// ============================================================

void updateOhcaTick() {
    if (globalState != GLOBAL_OHCA) return;
    uint32_t now = millis();

    // STEP 01: START_FLASH 1s timeout
    if (ohcaState == OHCA_STATE_START_FLASH) {
        if (now - startFlashStartMs >= START_FLASH_DURATION_MS) {
            dispatchOhcaEvent(OHCA_EVT_FLASH_TIMEOUT, 0);
        }
        return;
    }

    // STEP 02: 倒數 group — 計算 since 並 dispatch TIMER_TICK + 套用輸出
    if (ohcaState == OHCA_STATE_COUNTDOWN  ||
        ohcaState == OHCA_STATE_WARNING    ||
        ohcaState == OHCA_STATE_ALARMING   ||
        ohcaState == OHCA_STATE_OVERTIME) {

        uint32_t since = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);

        // STEP 02.01: 取出當前 phase，推進 phase
        ohca_phase_t curPhase = mapStateToPhase(ohcaState);
        ohca_phase_t newPhase = advanceOhcaPhase(curPhase, since);

        // STEP 02.02: 計算輸出（含邊緣偵測）
        ohca_output_t out = decideOhcaOutput(newPhase, ohcaPrevSinceMs, since);
        applyOhcaOutput(out);

        // STEP 02.03: 同步狀態機
        ohca_state_t mapped = mapPhaseToState(newPhase);
        if (mapped != ohcaState) {
            Serial.printf("[OHCA] phase auto: %u -> %u (since=%u)\n",
                          ohcaState, mapped, since);
            ohcaState = mapped;
            // STEP 02.04: 進入新 phase 重置消音標記（讓新階段提示能再出現）
            if (mapped == OHCA_STATE_OVERTIME) {
                alarmMuted = false;  // OVERTIME 的 15s 短嗍不應被舊 mute 擋掉
            }
        }
        ohcaPrevSinceMs = since;
    }
}

// ============================================================
// 紀錄事件（Phase A：in-memory only；Phase E 才 LittleFS 持久化）
// ============================================================

/** 紀錄本機即時事件（EVT_EPI_LOCAL / EVT_SHOCK_LOCAL / EVT_AMIODARONE） */
void recordLocalEvent(ems_event_type_t type) {
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[EVT] full, drop");
        return;
    }
    // Phase F MVP1：對時後 timestamp_ms = 真實 epoch；未對時 = 0（spec §4.1）
    //   elapsed_ms 仍走 millis()，不受對時影響（spec §0）
    uint32_t now     = millis();
    uint64_t now_ts  = ems::time_sync_current_epoch_ms(&g_ts_state, now);
    uint32_t elapsed = (caseStartMs == 0) ? 0 : (now - caseStartMs);
    buildLocalEvent(&events[eventCount], nextEventId++, type,
                    /*timestamp_ms*/ now_ts, elapsed);
    eventCount++;
}

/** 紀錄補登事件（接手前 / 純補登 EPI / 電擊） */
void recordSuppEvent(supp_type_t supp_type, uint8_t count) {
    if (eventCount >= MAX_EVENTS) {
        Serial.println("[EVT] full, drop");
        return;
    }
    if (!isSuppCountValid(supp_type, count)) {
        Serial.printf("[EVT] invalid supp count: type=%u count=%u\n", supp_type, count);
        return;
    }
    // Phase F MVP1：對時後 recorded_at_ms = 真實 epoch；未對時 = 0（spec §4.1）
    uint32_t now     = millis();
    uint64_t now_ts  = ems::time_sync_current_epoch_ms(&g_ts_state, now);
    uint32_t elapsed = (caseStartMs == 0) ? 0 : (now - caseStartMs);
    buildSuppEvent(&events[eventCount], nextEventId++, supp_type, count,
                   /*recorded_at_ms*/ now_ts, elapsed);
    eventCount++;
}

/** 重置 Phase B 補登 / Amio sub-state 暫存 */
void resetSubState() {
    ohcaSubState           = SUBSTATE_NONE;
    backfillCursor         = 0;
    backfillCount          = 1;
    backfillSuccessShownMs = 0;
    timelineScrollOffset   = 0;
    showAmioArmedPrompt    = false;
}

/** 進入藥物選單（長按 EPI 鍵入口） */
void enterDrugMenu() {
    ohcaSubState   = SUBSTATE_DRUG_MENU;
    backfillCursor = 0;
    Serial.println("[OHCA] enter DRUG_MENU");
}

/** 進入電擊補登選單（長按電擊鍵入口） */
void enterShockBackfillMenu() {
    ohcaSubState     = SUBSTATE_BACKFILL_TYPE;
    backfillCategory = BACKFILL_CAT_SHOCK;
    backfillSuppType = SUPP_TYPE_SHOCK_PRE_HANDOVER;
    backfillCursor   = 0;
    Serial.println("[OHCA] enter SHOCK BACKFILL TYPE menu");
}

// ============================================================
//  Phase C: 6 秒通氣節奏 tick + output applier
// ============================================================

/** 套用 vent 輸出至蜂鳴 + 反色（不負責顯示文字，由 draw 函式畫秒數） */
void applyVentOutput(const vent_output_t& out) {
    if (out.buzz_emphasis) {
        // 第 1 拍：強提示 (250ms on)
        triggerBeep(1, 250, 0);
    } else if (out.buzz_short) {
        // 第 2~6 拍：短提示 (60ms on)
        triggerBeep(1, 60, 0);
    }
    if (out.screen_invert_red) {
        triggerOledFlash(OLED_FLASH_MS);  // 重複觸發會被 oledInverted guard 擋掉
    }
}

/**
 * Tick 驅動 vent 輸出
 * - GLOBAL_VENT 獨立模式：永遠跑；ventEndCheckShown 中暫停
 * - GLOBAL_OHCA + ohcaVentOverlayEnabled：跑；ALARMING 時靜音（vol=0 effect）
 *   pm-dev-spec §7.2 / V1 §14.8：EPI 高優先打斷 → vent 立即靜音（EPI 到期優先權）
 */
void updateVentTick() {
    bool standalone = (globalState == GLOBAL_VENT) && !ventEndCheckShown && !ventPaused;
    bool ohcaOverlay = (globalState == GLOBAL_OHCA) && ohcaVentOverlayEnabled && !ohcaVentPaused;
    if (!standalone && !ohcaOverlay) return;

    uint32_t now   = millis();
    uint32_t since = (ventStartMs == 0) ? 0 : (now - ventStartMs);

    // ALARMING 時 vent 靜音（vol=0 即可達成「buzz 全關 + 視覺保留」）
    uint8_t effectiveVol = ventVolume;
    if (ohcaOverlay && ohcaState == OHCA_STATE_ALARMING) {
        effectiveVol = 0;
    }

    vent_output_t out = decideVentOutput(ventPrevSinceMs, since, effectiveVol);
    applyVentOutput(out);
    ventPrevSinceMs = since;
}

// ============================================================
// 退出 OHCA case（從 SUMMARY 返回主功能表）
// ============================================================

void enterMainMenu() {
    globalState = GLOBAL_MAIN_MENU;
}

void exitOhcaCase() {
    enterMainMenu();
    ohcaState              = OHCA_STATE_MAIN_MENU;
    ohcaLastEpiMs          = 0;
    ohcaPrevSinceMs        = 0;
    eventCount             = 0;
    summaryScrollOffset    = 0;
    alarmMuted             = false;
    ohcaVentOverlayEnabled = false;  // V1 §14.12 案件結束 → 6 秒通氣自動停止
    ohcaVentPaused         = false;
    stopBeep();
}

// ============================================================
// 蜂鳴器非 blocking SM
// pulses = 255 視為「連續」（直到 stopBeep）
// ============================================================

void triggerBeep(uint8_t pulses, uint16_t onMs, uint16_t offMs) {
    beepPulsesRemaining = pulses;
    beepOnMs            = onMs;
    beepOffMs           = offMs;
    beepActive          = true;
    digitalWrite(BUZZER_PIN, HIGH);
    beepNextToggleMs    = millis() + onMs;
}

void stopBeep() {
    beepPulsesRemaining = 0;
    beepActive          = false;
    buzzContinuous      = false;
    digitalWrite(BUZZER_PIN, LOW);
}

void updateBeepMachine() {
    if (beepPulsesRemaining == 0 && !beepActive) return;
    uint32_t now = millis();
    if ((int32_t)(now - beepNextToggleMs) < 0) return;

    if (beepActive) {
        // STEP 01: ON → OFF（pulse 結束）
        digitalWrite(BUZZER_PIN, LOW);
        beepActive = false;
        if (beepPulsesRemaining != 255 && beepPulsesRemaining > 0) {
            beepPulsesRemaining--;
        }
        if (beepPulsesRemaining == 0) return;
        beepNextToggleMs = now + beepOffMs;
    } else {
        // STEP 02: OFF → ON（下一個 pulse 開始）
        digitalWrite(BUZZER_PIN, HIGH);
        beepActive       = true;
        beepNextToggleMs = now + beepOnMs;
    }
}

// ============================================================
// OLED 反色 SM（震動視覺替代）
// ============================================================

void triggerOledFlash(uint16_t durationMs) {
    if (oledInverted) return;  // 已在閃 — 不重複觸發
    oledInverted          = true;
    oledInvertStartMs     = millis();
    oledInvertDurationMs  = durationMs;
    tft.invertDisplay(true);
}

void updateOledFlashMachine() {
    if (!oledInverted) return;
    if (millis() - oledInvertStartMs >= oledInvertDurationMs) {
        oledInverted = false;
        tft.invertDisplay(false);
    }
}

// ============================================================
// 套用 ohca_output_t 到實際 GPIO
// ============================================================

void applyOhcaOutput(const ohca_output_t& out) {
    // STEP 01: 連續發報（ALARMING）
    if (out.buzz_alarm_continuous && !alarmMuted) {
        if (!buzzContinuous) {
            buzzContinuous = true;
            triggerBeep(255, 200, 100);  // 連續 200ms on / 100ms off
        }
    } else if (buzzContinuous) {
        buzzContinuous = false;
        stopBeep();
    }

    // STEP 02: 短嗍（WARNING / OVERTIME 邊緣觸發）
    if (out.buzz_short && !alarmMuted) {
        triggerBeep(1, 80, 0);
    }

    // STEP 03: OLED 閃紅（震動視覺替代）
    if (out.screen_flash) {
        triggerOledFlash(OLED_FLASH_MS);
    }

    // STEP 04: 震動馬達（佔位）
#if ENABLE_VIBRATION
    if (out.vibrate) {
        digitalWrite(VIBRATION_PIN, HIGH);
        delay(50);
        digitalWrite(VIBRATION_PIN, LOW);
    }
#endif
}

// ============================================================
// 顯示繪製
// ============================================================

/**
 * 顯示狀態快照：updateDisplay 每次比對與上次的差異，無變化則跳過全螢幕重畫。
 * TFT 沒有 framebuffer，每次 clearDisplay+redraw 都直寫 76,800 像素到 SPI bus，
 * 視覺上會看到掃描線。snapshot 比對只有當顯示內容真正改變時才重繪 → 解決閃爍。
 *
 * struct/純邏輯抽到 lib/ems_display_snapshot/，native 環境可寫 L2 regression test
 * （Phase E history UI 漏 historyCursor 導致無重繪那類 bug 必須在 native 階段被擋）。
 */
static DisplaySnapshot lastDisplaySnapshot = {};  // 全 0 初始 → 首次 updateDisplay 必觸發重繪

/** 當前顯示狀態 → DisplaySnapshot。 */
static DisplaySnapshot captureDisplaySnapshot() {
    // STEP 01: 算時間相關衍生值（lib 不依賴 millis() / 巨集常數）
    DisplaySnapshotInputs in;
    in.globalState     = (uint8_t)globalState;
    in.ohcaState       = (uint8_t)ohcaState;
    in.ohcaSubState    = (uint8_t)ohcaSubState;
    in.mainMenuCursor       = mainMenuCursor;
    in.backfillCursor       = backfillCursor;
    in.summarySubmenuCursor = summarySubmenuCursor;
    in.ventVolume      = ventVolume;
    in.ventPaused      = ventPaused;
    in.historyCursor       = historyCursor;
    in.historyScrollOffset = historyScrollOffset;

    // STEP 02: countdownSec — EPI cycle 倒數/超時秒數
    if (ohcaLastEpiMs != 0) {
        const uint32_t since = millis() - ohcaLastEpiMs;
        in.countdownSec = (since < EPI_CYCLE_MS)
                        ? (EPI_CYCLE_MS - since) / 1000
                        : (since - EPI_CYCLE_MS) / 1000;
    }

    // STEP 03: ventBeat — 6 秒通氣節奏目前秒
    if (ventStartMs != 0 && !ventPaused) {
        const uint32_t since = millis() - ventStartMs;
        in.ventBeat = (uint8_t)computeVentBeat(since);
    }

    // STEP 04: ALARMING 半週期閃爍 phase（讓 dedupe 在 ALARMING 期間每半週期觸發重繪）
    in.alarmingFlashOn = (globalState == GLOBAL_OHCA)
                      && (ohcaState == OHCA_STATE_ALARMING)
                      && (((millis() / OHCA_FLASH_HALF_MS) & 1) != 0);

    // STEP 05: bool flags
    in.showEpiArmedPrompt    = showEpiArmedPrompt;
    in.showShockArmedPrompt  = showShockArmedPrompt;
    in.showAmioArmedPrompt   = showAmioArmedPrompt;
    in.ohcaVentOverlayEnabled = ohcaVentOverlayEnabled;
    in.ventEndCheckShown     = ventEndCheckShown;
    in.alarmMuted            = alarmMuted;
    in.ventBackHintShown     = ventBackHintShown;
    in.endConfirmShown       = endConfirmShown;
    in.flashStateActive      = flashState.active;
    in.ventPreShown          = ventPreShown;
    in.historySummaryMode    = historySummaryMode;
    in.bleConnected          = g_ble_client_connected;  // Phase F MVP1
    in.syncState             = (uint8_t)g_sync_ctx.state;  // Phase F MVP2

    return captureSnapshot(in);
}

void updateDisplay() {
    // STEP 00: snapshot 去重 — 顯示狀態無變化即跳過，避免無謂的全螢幕重畫造成掃描線閃爍。
    DisplaySnapshot now = captureDisplaySnapshot();
    if (memcmp(&now, &lastDisplaySnapshot, sizeof(DisplaySnapshot)) == 0) {
        return;
    }

    // STEP 01: partial update — 倒數每秒只 tick 一格時，整片 fillScreen 會出掃描線閃爍。
    // 偵測「在 OHCA 倒數同 state 內，僅 countdownSec 改變」→ 只重繪時間區塊，不動 badge/label/counter。
    // 排除 ALARMING（半週期閃 phase 跟著變，必須走 full path 重畫整片紅 bg）。
    // 排除 modal overlay（confirm dialog/flash）— partial 不會重畫 overlay，會被時間區塊 fillRect 蓋掉
    constexpr uint16_t MODAL_FLAGS_MASK = 0x01    // showEpiArmedPrompt
                                        | 0x02    // showShockArmedPrompt
                                        | 0x04    // showAmioArmedPrompt
                                        | 0x100   // endConfirmShown
                                        | 0x200;  // flashState.active
    const bool inCountdownGroup = (now.globalState == GLOBAL_OHCA)
                               && (now.ohcaState == OHCA_STATE_COUNTDOWN
                                   || now.ohcaState == OHCA_STATE_WARNING
                                   || now.ohcaState == OHCA_STATE_OVERTIME)
                               && (now.ohcaSubState == 0)
                               && ((now.flags & MODAL_FLAGS_MASK) == 0);
    const bool sameStateAsLast = (now.globalState     == lastDisplaySnapshot.globalState)
                              && (now.ohcaState       == lastDisplaySnapshot.ohcaState)
                              && (now.ohcaSubState    == lastDisplaySnapshot.ohcaSubState)
                              && (now.mainMenuCursor  == lastDisplaySnapshot.mainMenuCursor)
                              && (now.backfillCursor  == lastDisplaySnapshot.backfillCursor)
                              && (now.ventBeat        == lastDisplaySnapshot.ventBeat)
                              && (now.ventVolume      == lastDisplaySnapshot.ventVolume)
                              && (now.ventPaused      == lastDisplaySnapshot.ventPaused)
                              && (now.flags           == lastDisplaySnapshot.flags);
    if (inCountdownGroup
        && sameStateAsLast
        && (now.countdownSec != lastDisplaySnapshot.countdownSec)) {
        drawOhcaCountdownTimeOnly(now.ohcaState);
        // LGFX DMA pushSprite 整片 ~4ms @80MHz；sub-region push 雖更短，
        // 但 DMA 整片視覺已近瞬完成，留簡化邏輯
        display.pushSprite(0, 0);
        lastDisplaySnapshot = now;
        return;
    }

    Serial.printf("[REDRAW] gs=%u os=%u sub=%u cur=%u cdSec=%lu vBeat=%u flags=0x%04x\n",
                  now.globalState, now.ohcaState, now.ohcaSubState, now.mainMenuCursor,
                  (unsigned long)now.countdownSec, now.ventBeat, now.flags);
    lastDisplaySnapshot = now;

    display.clearDisplay();

    if (globalState == GLOBAL_MAIN_MENU) {
        drawMainMenu();
    } else if (globalState == GLOBAL_SYNC) {
        drawSyncScreen();
    } else if (globalState == GLOBAL_OHCA) {
        // Phase B: sub-state 子流程畫面優先
        if (ohcaSubState == SUBSTATE_QUICK_MENU)        { drawQuickMenu();       display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_DRUG_MENU)         { drawDrugMenu();        display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_TYPE)     { drawBackfillType();    display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_COUNT)    { drawBackfillCount();   display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_CONFIRM)  { drawBackfillConfirm(); display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_BACKFILL_SUCCESS)  { drawBackfillSuccess(); display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_AMIO_CONFIRM)      { drawAmioConfirmPrompt(); display.pushSprite(0, 0); return; }
        if (ohcaSubState == SUBSTATE_TIMELINE)          { drawTimeline();        display.pushSprite(0, 0); return; }

        switch (ohcaState) {
            case OHCA_STATE_START_FLASH:
                drawOhcaStartFlash();
                break;
            case OHCA_STATE_WAIT_FIRST_EPI:
                drawOhcaWaitFirstEpi();
                break;
            case OHCA_STATE_COUNTDOWN:
            case OHCA_STATE_WARNING:
            case OHCA_STATE_ALARMING:
            case OHCA_STATE_OVERTIME: {
                const uint32_t nowMs  = millis();
                const uint32_t since  = (ohcaLastEpiMs == 0) ? 0 : (nowMs - ohcaLastEpiMs);
                const uint32_t remain = (since < EPI_CYCLE_MS) ? (EPI_CYCLE_MS - since) : 0;
                const uint32_t past   = (since > EPI_CYCLE_MS) ? (since - EPI_CYCLE_MS) : 0;
                // ALARMING 閃爍開關直接從 snapshot 讀，避免在 render 內二次取樣 millis()
                const bool alarmingFlashOn = (lastDisplaySnapshot.flags & SNAP_FLAG_ALARMING_FLASH) != 0;
                if (ohcaState == OHCA_STATE_COUNTDOWN) {
                    drawOhcaCountdownCommon(remain, COLOR_TEXT_PRIMARY, "下次給藥", false);
                } else if (ohcaState == OHCA_STATE_WARNING) {
                    drawOhcaCountdownCommon(remain, COLOR_ACCENT_WARN,  "請準備給藥", false);
                } else if (ohcaState == OHCA_STATE_ALARMING) {
                    drawOhcaCountdownCommon(past,   COLOR_ACCENT_ALERT, "請給藥",     alarmingFlashOn);
                } else {
                    drawOhcaCountdownCommon(past,   COLOR_ACCENT_ALERT, "請給藥",   false);
                }
                break;
            }
            case OHCA_STATE_END_CHECK:
                if (endConfirmShown) drawOhcaEndConfirmDialog();
                else                  drawOhcaEndCheck();
                break;
            case OHCA_STATE_LOCKED:
                drawOhcaLocked();
                break;
            case OHCA_STATE_SUMMARY:
                drawOhcaSummary();
                break;
            default:
                break;
        }

        // overlay：6 秒通氣輔助區塊（V1 §14；不蓋 ALARMING 訊息）
        if (ohcaVentOverlayEnabled &&
            ohcaState != OHCA_STATE_START_FLASH &&
            ohcaState != OHCA_STATE_END_CHECK   &&
            ohcaState != OHCA_STATE_LOCKED      &&
            ohcaState != OHCA_STATE_SUMMARY) {
            drawOhcaVentOverlay(/*y_top*/ 44);
        }

        // A1：兩段確認改用全螢幕對話（fillScreen 覆蓋背景，對齊 demo OHCA_CONFIRM）
        if (showEpiArmedPrompt)        drawOhcaConfirmDialog(EVT_EPI_LOCAL);
        else if (showShockArmedPrompt) drawOhcaConfirmDialog(EVT_SHOCK_LOCAL);

    } else if (globalState == GLOBAL_VENT) {
        if (ventPreShown)            drawVentPre();
        else if (ventEndCheckShown)  drawVentEndCheck();
        else                          drawVentStandalone();
    } else if (globalState == GLOBAL_TRAINING_PLACEHOLDER) {
        drawPlaceholder("訓練模式", "D 階段");
    } else if (globalState == GLOBAL_HISTORY_PLACEHOLDER) {
        // Phase E：列表 vs SUMMARY 子畫面（從歷史進入時重用既有 drawOhcaSummary）
        if (historySummaryMode) {
            drawOhcaSummary();
        } else {
            drawHistoryList();
        }
    } else if (globalState == GLOBAL_SETTINGS_PLACEHOLDER) {
        drawPlaceholder("系統設定", "G 階段");
    }

    // Flash overlay 最上層覆蓋（demo flash() 對齊 — 蓋掉所有底下畫面）
    if (flashState.active) {
        drawFlashOverlay();
    }

    // STEP 99: pushSprite DMA 一次推到實體 TFT — 消除「fillScreen → 慢慢出文字」中間態
    display.pushSprite(0, 0);
}

void drawMainMenu() {
    // 螢幕已由 updateDisplay() 的 clearDisplay() 清為黑底，這裡不重複 fillScreen 避免雙閃。

    // STEP 01: 上方標題列 — "EMS DOSESYNC PRO" 英文用 default font size 2
    display.setFont(&fonts::Font0);
    display.setTextSize(2);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setCursor(16, 12);
    display.print("EMS DOSESYNC PRO");

    // STEP 01.01: Phase F MVP1 — BLE 連線狀態圖示（標題列右側）
    //   連線中：綠色「BT」字樣；未連線：留空（不畫即可，主畫面已為黑底）
    if (g_ble_client_connected) {
        display.setTextColor(COLOR_ACCENT_OK);
        display.setCursor(SCREEN_W - 36, 12);  // 2 字元 × 10px size 2 = 20px，留 16px 右邊距
        display.print("BT");
    }

    // STEP 02: 標題下分隔線 y=36，灰色橫貫
    display.drawLine(16, 36, SCREEN_W - 16, 36, COLOR_TEXT_DIM);

    // STEP 03: 5 個選單項用 vlw 24px size 1.1（PM 反饋放大），y=58 起每 36px 一行
    //   - cursor 項：白底黑字（demo cursor highlight）
    //   - 非 cursor：黑底白字
    constexpr int16_t MENU_Y_START   = 58;
    constexpr int16_t MENU_ROW_H     = 36;
    constexpr int16_t MENU_TEXT_PAD  = 24;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 4;  // 26px 字在 36px row 內垂直置中

    useZhFont();
    display.setTextSize(1.1f, 1.1f);
    for (uint8_t i = 0; i < MAIN_MENU_COUNT; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == mainMenuCursor) {
            // STEP 03.01: cursor highlight — 白底全寬橫條，黑字
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(MAIN_MENU_LABELS[i]);
    }
}

/** 共用：水平置中文字（top_center datum，y = top 對齊） */
static void drawCenteredText(const char* text, int16_t y, uint16_t color) {
    display.setTextColor(color);
    display.setTextDatum(textdatum_t::top_center);
    display.drawString(text, SCREEN_W / 2, y);
}

/**
 * Phase F MVP2：同步資料 6 個 state 畫面渲染。
 *
 * 字級對齊既有 OHCA 螢幕：vlw 中文 size 1.2~1.8x、ASCII 配對碼用 vlw 2.5x（≈60px）。
 * ERROR reason 由 caller 從 pairing_code.failure_count 推導（dispatcher 不存原因）。
 */
static void drawSyncScreen() {
    useZhFont();

    switch (g_sync_ctx.state) {
        case ems::SyncState::AWAITING_CONNECT: {
            display.setTextSize(1.5f, 1.5f);
            drawCenteredText("等待 BLE 連線", 50, COLOR_TEXT_PRIMARY);
            display.setTextSize(1.0f, 1.0f);
            drawCenteredText("請開啟網頁端", 150, COLOR_TEXT_MUTED);
            break;
        }
        case ems::SyncState::AWAITING_INPUT: {
            display.setTextSize(1.2f, 1.2f);
            drawCenteredText("請於網頁輸入", 30, COLOR_TEXT_PRIMARY);
            // 4 位配對碼大字（vlw size 2.5 ≈ 60px 等寬，足以遠距讀取）
            display.setTextSize(2.5f, 2.5f);
            display.setTextColor(COLOR_ACCENT_OK);
            display.setTextDatum(textdatum_t::middle_center);
            display.drawString(g_sync_ctx.pairing_code.digits, SCREEN_W / 2, SCREEN_H / 2);
            display.setTextSize(1.0f, 1.0f);
            drawCenteredText("120 秒內有效", 210, COLOR_TEXT_MUTED);
            break;
        }
        case ems::SyncState::AWAITING_MAIN_KEY: {
            display.setTextSize(1.2f, 1.2f);
            drawCenteredText("驗證成功", 40, COLOR_ACCENT_OK);
            display.setTextSize(1.8f, 1.8f);
            drawCenteredText("按主鍵開始", 100, COLOR_TEXT_PRIMARY);
            display.setTextSize(1.0f, 1.0f);
            drawCenteredText("30 秒未按取消", 200, COLOR_TEXT_MUTED);
            break;
        }
        case ems::SyncState::SENDING: {
            display.setTextSize(1.5f, 1.5f);
            drawCenteredText("傳輸中…", 80, COLOR_TEXT_PRIMARY);
            display.setTextSize(1.0f, 1.0f);
            drawCenteredText("DEMO 模式", 150, COLOR_TEXT_MUTED);
            break;
        }
        case ems::SyncState::DONE: {
            display.setTextSize(1.5f, 1.5f);
            drawCenteredText("同步完成", 50, COLOR_ACCENT_OK);
            // 大字 OK（vlw 2.5x ASCII 與配對碼同字級）
            display.setTextSize(2.5f, 2.5f);
            display.setTextColor(COLOR_ACCENT_OK);
            display.setTextDatum(textdatum_t::middle_center);
            display.drawString("OK", SCREEN_W / 2, 140);
            display.setTextSize(1.0f, 1.0f);
            drawCenteredText("（DEMO）", 210, COLOR_TEXT_MUTED);
            break;
        }
        case ems::SyncState::ERROR: {
            display.setTextSize(1.5f, 1.5f);
            drawCenteredText("同步失敗", 50, COLOR_ACCENT_ALERT);
            display.setTextSize(1.2f, 1.2f);
            // dispatcher 不存原因 — 由 caller 看 failure_count 自行判定
            const char* reason = (g_sync_ctx.pairing_code.failure_count >= ems::PAIRING_MAX_FAILURES)
                               ? "輸入錯誤鎖定"
                               : "連線中斷或逾時";
            drawCenteredText(reason, 120, COLOR_TEXT_PRIMARY);
            display.setTextSize(1.0f, 1.0f);
            drawCenteredText("即將回主選單", 200, COLOR_TEXT_MUTED);
            break;
        }
        case ems::SyncState::IDLE:
        default:
            // observer 已將 globalState 設回 MAIN_MENU，理論上不會走到這
            break;
    }
}

void drawOhcaStartFlash() {
    // OHCA 案件啟動 1 秒提示：對齊 demo flash('案件開始', 'OHCA')
    // PM 反饋全部 1.5x：主 size 1.5→2.25（~54px）、副 Font0 size 3 → vlw 1.5（~36px）
    useZhFont();

    // 主：「案件開始」綠色（vlw size 2.25 ≈ 54px）
    display.setTextSize(2.25f, 2.25f);
    display.setTextColor(COLOR_ACCENT_OK);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString("案件開始", SCREEN_W / 2, SCREEN_H / 2 - 30);

    // 副：「OHCA」灰色（vlw size 1.5 ≈ 36px，與 OHCA badge 同樣式）
    display.setTextSize(1.5f, 1.5f);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.drawString("OHCA", SCREEN_W / 2, SCREEN_H / 2 + 36);
}

void drawOhcaWaitFirstEpi() {
    // 對齊 docs/demo/index.html 第二螢幕「待本機 EPI」layout（PM 反饋全部 1.5x）
    // 前置：caller 已 clearDisplay 為黑底
    // Layout（vlw 24px 為基準，row 高 ≈ font_size × scale）：
    //   - 頂部 OHCA badge：top y=8, size 1.5 → ~36px tall, bottom y≈44
    //   - 中央大字「待本機 EPI」：middle-center datum, y=120, size 2.25 → ~54px tall, top≈93 / bottom≈147
    //   - 底部 EPI/電擊 計數：top y=200, size 1.5 → ~36px tall, bottom y≈236（240 上限內）
    useZhFont();

    // STEP 01: 頂部 OHCA 綠 badge（vlw size 1.5 ≈ 36px）
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText("OHCA", OHCA_CHINESE_BADGE_TOP_Y, COLOR_ACCENT_OK);

    // STEP 02: 中央大字「待本機 EPI」（vlw size 2.25 ≈ 54px）
    // demo OHCA 螢幕無副標，故移除「(按兩次 EPI 確認)」hint
    display.setTextSize(2.25f, 2.25f);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString("待本機 EPI", SCREEN_W / 2, SCREEN_H / 2);

    // STEP 03: 底部 EPI/電擊 計數（vlw size 1.5 ≈ 36px）
    uint16_t epiN = 0, shockN = 0;
    for (uint16_t i = 0; i < eventCount; i++) {
        if      (isEpiEvent(&events[i]))   epiN   += events[i].count;
        else if (isShockEvent(&events[i])) shockN += events[i].count;
    }
    char counter[32];
    snprintf(counter, sizeof(counter), "EPI %u｜電擊 %u", epiN, shockN);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText(counter, OHCA_WAIT_COUNTER_Y, COLOR_TEXT_DIM);
}

/**
 * OHCA 倒數共用畫面（對齊 docs/demo/index.html OHCA 第二螢幕）。
 *
 * 320×240 layout：
 *   - 頂部 mode badge "OHCA"（綠），baseline y = OHCA_BADGE_Y
 *   - 中央大時間 mm:ss（FreeMonoBold24pt7b），顏色由 caller 指定
 *   - 時間下方標籤（default font size 2，灰色）
 *   - 底部 "EPI N  Shock N"（default font size 1，dim 灰）
 *   - flashOn=true 時整片背景填 COLOR_FLASH_VENT；flashOn 由 snapshot phase bit 決定（單一真相）
 *
 * 前置條件：caller（updateDisplay）已執行 display.clearDisplay() 把畫面填黑。
 */
void drawOhcaCountdownCommon(uint32_t time_ms, uint16_t timeColor, const char* label, bool flashOn) {
    // STEP 01: 背景 — flashOn 時填深紅，否則保持 caller clear 的黑底
    if (flashOn) {
        display.fillScreen(COLOR_FLASH_VENT);
    }

    // STEP 02: 頂部 mode badge "OHCA"（vlw size 1.5 ≈ 36px，對齊 WaitFirstEpi）
    useZhFont();
    display.setTextSize(1.5f, 1.5f);
    display.setTextColor(COLOR_ACCENT_OK);
    display.setTextDatum(textdatum_t::top_center);
    display.drawString("OHCA", SCREEN_W / 2, OHCA_CHINESE_BADGE_TOP_Y);

    // STEP 03: 中央大時間（FreeMonoBold24pt7b，middle-center datum 自動置中）
    char timeStr[8];
    const uint32_t total_sec = time_ms / 1000;
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu",
             (unsigned long)(total_sec / 60),
             (unsigned long)(total_sec % 60));

    display.setFont(&fonts::FreeMonoBold24pt7b);
    display.setTextSize(2);  // 96px 高（PM 反饋大時間放大；middle_center 自動居中）
    display.setTextColor(timeColor);
    display.setTextDatum(textdatum_t::middle_center);
    const int16_t time_y = SCREEN_H / 2 - OHCA_TIME_VISUAL_UP;
    display.drawString(timeStr, SCREEN_W / 2, time_y);

    // STEP 04: 時間下方標籤「下次給藥」等（vlw size 1.8 ≈ 43px）
    //   時間 bbox bottom ≈ 148、counter row top ≈ 200，
    //   理論幾何中點為 174；y=165 上偏 9px 讓 descender 與 counter 留間距。
    useZhFont();
    display.setTextSize(1.8f, 1.8f);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(label, SCREEN_W / 2, OHCA_LABEL_Y);

    // STEP 05: 累加 EPI / Shock 事件次數（含補登 count）
    uint16_t epiN = 0, shockN = 0;
    for (uint16_t i = 0; i < eventCount; i++) {
        if      (isEpiEvent(&events[i]))   epiN   += events[i].count;
        else if (isShockEvent(&events[i])) shockN += events[i].count;
    }

    // STEP 06: 底部計數行（vlw size 1.5 ≈ 36px，bottom-center y=236 留 4px 邊界）
    char counter[32];     // UTF-8「電擊」6 byte，buf 從 24 加大避免飽和
    snprintf(counter, sizeof(counter), "EPI %u｜電擊 %u", epiN, shockN);
    display.setTextSize(1.5f, 1.5f);
    display.setTextColor(COLOR_TEXT_DIM);
    display.setTextDatum(textdatum_t::bottom_center);
    display.drawString(counter, SCREEN_W / 2, SCREEN_H - 4);
}

/**
 * OHCA 倒數 partial update — 每秒 tick 只重畫時間區塊，badge / label / counter 不動。
 *
 * 解閃爍：避免每秒 fillScreen 整片 320×240 → 掃描線可見。只 fillRect 時間 bbox。
 * 限定條件由 caller 確保（同 state、僅 countdownSec 變化、非 ALARMING）。
 */
static void drawOhcaCountdownTimeOnly(uint8_t ohcaStateForTime) {
    const uint32_t now    = millis();
    const uint32_t since  = (ohcaLastEpiMs == 0) ? 0 : (now - ohcaLastEpiMs);
    const uint32_t remain = (since < EPI_CYCLE_MS) ? (EPI_CYCLE_MS - since) : 0;
    const uint32_t past   = (since > EPI_CYCLE_MS) ? (since - EPI_CYCLE_MS) : 0;

    uint32_t time_ms;
    uint16_t timeColor;
    if (ohcaStateForTime == OHCA_STATE_COUNTDOWN) {
        time_ms = remain; timeColor = COLOR_TEXT_PRIMARY;
    } else if (ohcaStateForTime == OHCA_STATE_WARNING) {
        time_ms = remain; timeColor = COLOR_ACCENT_WARN;
    } else {
        time_ms = past;   timeColor = COLOR_ACCENT_ALERT;  // OVERTIME
    }

    char timeStr[8];
    const uint32_t total_sec = time_ms / 1000;
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu",
             (unsigned long)(total_sec / 60),
             (unsigned long)(total_sec % 60));

    // 先把 partial push 區域的 canvas 全黑（解 advance gap 殘留 — glyph bbox 之外
    // 的像素 setTextColor(fg,bg) 不會清，每秒寫一次會累積成白塊）
    display.fillRect(OHCA_TIME_PUSH_X, OHCA_TIME_PUSH_Y,
                     OHCA_TIME_PUSH_W, OHCA_TIME_PUSH_H, COLOR_BG);

    display.setFont(&fonts::FreeMonoBold24pt7b);
    display.setTextSize(2);  // 對齊 drawOhcaCountdownCommon STEP 03
    display.setTextColor(timeColor);
    display.setTextDatum(textdatum_t::middle_center);
    const int16_t time_y = SCREEN_H / 2 - OHCA_TIME_VISUAL_UP;
    display.drawString(timeStr, SCREEN_W / 2, time_y);

    // 也重畫 label — partial bbox 涵蓋 label 上半，每秒被 fillRect 清掉會看起來頂部被切掉
    const char* label =
        (ohcaStateForTime == OHCA_STATE_COUNTDOWN) ? "下次給藥" :
        (ohcaStateForTime == OHCA_STATE_WARNING)   ? "請準備給藥" :
                                                     "請給藥";
    useZhFont();
    display.setTextSize(1.8f, 1.8f);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(label, SCREEN_W / 2, OHCA_LABEL_Y);
}

void drawOhcaEndCheck() {
    // 標題（efontTW_24 size 1.2 ≈ 29px）
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("結束前檢查", OHCA_BADGE_Y, COLOR_ACCENT_WARN);

    // 三個選項：完成並結束案件 / 前往補登 / 返回案件（demo OHCA_END_CHECK）
    const int16_t y0     = 70;
    const int16_t row_h  = 40;

    auto drawOption = [&](const char* text, int16_t y, bool selected) {
        if (selected) {
            display.fillRect(20, y, SCREEN_W - 40, row_h, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        useZhFont();
        display.setTextSize(1.2f, 1.2f);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString(text, SCREEN_W / 2, y + row_h / 2);
    };

    drawOption("完成並結束案件", y0,            endCheckCursor == END_CHECK_CURSOR_CONFIRM);
    drawOption("前往補登",       y0 + row_h,    endCheckCursor == END_CHECK_CURSOR_BACKFILL);
    drawOption("返回案件",       y0 + row_h*2,  endCheckCursor == END_CHECK_CURSOR_CANCEL);

    // 底部 hint
    useZhFont();
    display.setTextSize(1);
    drawCenteredText("上下選擇　主鍵確認",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/**
 * 觸發 flash 過場提示（對齊 demo flash() helper）。
 * @param title         主標題（非 NULL）
 * @param subtitle      副標題（NULL 或 "" 略過）
 * @param duration_ms   顯示毫秒（典型 FLASH_DEFAULT_MS=1200）
 * @param titleColor    主標題色（典型 COLOR_ACCENT_OK / COLOR_TEXT_PRIMARY）
 * @param titleSize     主標 vlw multiplier（預設 FLASH_TITLE_SIZE_DEFAULT=2.25；
 *                      ~7-char 以上易超寬，傳 FLASH_TITLE_SIZE_LONG=1.9）
 * @param subtitleSize  副標 vlw multiplier（預設 FLASH_SUBTITLE_SIZE_DEFAULT=1.5；
 *                      ~10-char 以上易超寬，傳 FLASH_SUBTITLE_SIZE_LONG=1.2）
 */
void triggerFlash(const char* title, const char* subtitle, uint16_t duration_ms, uint16_t titleColor,
                  float titleSize, float subtitleSize) {
    // STEP 01: 寫入 lifecycle 與 visual 欄位（render 由 drawFlashOverlay 讀取）
    flashState.active       = true;
    flashState.startMs      = millis();
    flashState.durationMs   = duration_ms;
    flashState.titleColor   = titleColor;
    flashState.titleSize    = titleSize;
    flashState.subtitleSize = subtitleSize;

    // STEP 02: 文字欄位 bounded copy（NULL 視為 ""，超出 buf 截斷不 overflow）
    strncpy(flashState.title,    title    ? title    : "", sizeof(flashState.title)    - 1);
    strncpy(flashState.subtitle, subtitle ? subtitle : "", sizeof(flashState.subtitle) - 1);
    flashState.title[sizeof(flashState.title)       - 1] = '\0';
    flashState.subtitle[sizeof(flashState.subtitle) - 1] = '\0';

    // STEP 03: serial trace（debug 用，可看到 flash 觸發順序）
    Serial.printf("[FLASH] %s | %s\n", flashState.title, flashState.subtitle);
}

/** Flash overlay render — 全螢幕黑底（覆蓋背景），主副標居中 */
void drawFlashOverlay() {
    if (flashState.title[0] == '\0') {
        return;  // 空 title 不繪 → 不要黑屏 duration_ms 卻什麼都沒顯示
    }
    display.fillScreen(COLOR_BG);
    useZhFont();
    const bool hasSub = (flashState.subtitle[0] != '\0');
    display.setTextDatum(textdatum_t::middle_center);

    if (hasSub) {
        display.setTextSize(flashState.titleSize, flashState.titleSize);
        display.setTextColor(flashState.titleColor);
        display.drawString(flashState.title, SCREEN_W / 2, SCREEN_H / 2 - 30);
        display.setTextSize(flashState.subtitleSize, flashState.subtitleSize);
        display.setTextColor(COLOR_TEXT_MUTED);
        display.drawString(flashState.subtitle, SCREEN_W / 2, SCREEN_H / 2 + 36);
    } else {
        display.setTextSize(flashState.titleSize, flashState.titleSize);
        display.setTextColor(flashState.titleColor);
        display.drawString(flashState.title, SCREEN_W / 2, SCREEN_H / 2);
    }
}

/** 對話框共用框架：8/8 margin → 304×224 大框（避免文字觸碰邊框） */
static void drawDialogFrame(uint16_t borderColor) {
    display.fillScreen(COLOR_BG);
    const int16_t margin = 8;
    const int16_t x = margin;
    const int16_t y = margin;
    const int16_t w = SCREEN_W - 2 * margin;   // 304
    const int16_t h = SCREEN_H - 2 * margin;   // 224
    display.drawRect(x,     y,     w,     h,     borderColor);
    display.drawRect(x + 1, y + 1, w - 2, h - 2, borderColor);
}

/**
 * A7：OHCA_END_CONFIRM 二次確認對話（END_CHECK 選「完成並結束」後彈出）
 * 對齊 demo OHCA_END_CONFIRM render
 */
void drawOhcaEndConfirmDialog() {
    drawDialogFrame(COLOR_ACCENT_ALERT);

    // 標題（紅色，efontTW_24 × 1.5 ≈ 36px）
    useZhFont();
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText("確認結束案件？", 36, COLOR_ACCENT_ALERT);

    // 內文（efontTW_24 × 1.2 ≈ 29px）
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("結束後不可修改", 116, COLOR_TEXT_MUTED);

    // 底部分隔線（在 hint 上方）
    display.drawLine(20, 184, SCREEN_W - 20, 184, COLOR_TEXT_DIM);

    // hint
    display.setTextSize(1);
    drawCenteredText("主鍵確認　返回取消", 196, COLOR_TEXT_DIM);
}

/**
 * A1：OHCA 兩段確認全螢幕對話（取代 44px bar overlay）
 * 對齊 demo OHCA_CONFIRM render
 *
 * @param evType EVT_EPI_LOCAL / EVT_SHOCK_LOCAL（Amio 走 SUBSTATE_AMIO_CONFIRM）
 */
void drawOhcaConfirmDialog(uint8_t evType) {
    // 框架（demo overlay rgba(0,0,0,0.92) + 2px amber）
    drawDialogFrame(COLOR_ACCENT_WARN);

    // 標題（efontTW_24 × 1.5 ≈ 36px）
    const char* title = (evType == EVT_EPI_LOCAL)   ? "確認已給 EPI？"
                      : (evType == EVT_SHOCK_LOCAL) ? "確認已電擊？"
                      :                                "確認操作？";
    useZhFont();
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText(title, 28, COLOR_TEXT_PRIMARY);

    // 內文兩行（efontTW_24 × 1.2 ≈ 29px）
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("確認後將建立時間戳", 92, COLOR_TEXT_MUTED);
    const char* body2 = (evType == EVT_EPI_LOCAL) ? "並重啟 4 分鐘倒數" : "不影響 EPI 倒數";
    drawCenteredText(body2, 128, COLOR_TEXT_MUTED);

    // 底部分隔線
    display.drawLine(20, 184, SCREEN_W - 20, 184, COLOR_TEXT_DIM);

    // hint
    display.setTextSize(1);
    const char* hint = (evType == EVT_EPI_LOCAL)   ? "再按 EPI 鍵確認　返回取消"
                     : (evType == EVT_SHOCK_LOCAL) ? "再按電擊鍵確認　返回取消"
                     :                                "主鍵確認　返回取消";
    drawCenteredText(hint, 196, COLOR_TEXT_DIM);
}

void drawOhcaLocked() {
    useZhFont();

    // 標題：紅「已鎖定」
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("已鎖定", OHCA_BADGE_Y, COLOR_ACCENT_ALERT);

    // 中央大字「案件結束」（size 2 ≈ 48px）
    display.setTextSize(2);
    drawCenteredText("案件結束", SCREEN_H / 2 - 24, COLOR_TEXT_PRIMARY);

    // 事件總數
    char buf[32];
    snprintf(buf, sizeof(buf), "事件 %u 筆", eventCount);
    display.setTextSize(1);
    drawCenteredText(buf, SCREEN_H / 2 + 40, COLOR_TEXT_MUTED);

    // 底部 hint
    drawCenteredText("主鍵　總覽",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

void drawHistoryList() {
    useZhFont();
    char buf[64];

    // STEP 01: 標題（對齊 demo V1 §12：總數附在標題後）
    //   vlw 未收錄全形括號（`（）`），用半形避字型缺字風險
    snprintf(buf, sizeof(buf), "OHCA 案件 (%u)", (unsigned)historyCount);
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText(buf, OHCA_BADGE_Y, COLOR_ACCENT_OK);

    // STEP 02: 無資料 — 提示 + early exit
    //   副標「結束案件後紀錄」對齊 demo「完成案件後會自動列入」語意，
    //   全字已在韌體既有字串中驗證 vlw 可顯示
    if (historyCount == 0) {
        display.setTextSize(1);
        drawCenteredText("尚無案件紀錄", SCREEN_H / 2 - 8, COLOR_TEXT_MUTED);
        drawCenteredText("結束案件後紀錄",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
        return;
    }

    // STEP 03: 列出 HISTORY_VISIBLE_ROWS（5）筆，以 historyScrollOffset 為起點
    //   row 高度 28px 大於 summary 22px → 列表強調觸控可辨識性，summary 內聚資訊密度
    constexpr int16_t HISTORY_LIST_START_Y      = 60;
    constexpr int16_t HISTORY_ROW_HEIGHT_PX     = 28;
    constexpr int16_t HISTORY_ROW_SIDE_MARGIN   = 8;   // 高亮 fillRect 左右留白
    constexpr int16_t HISTORY_ROW_TOP_INSET     = 4;   // 高亮 fillRect 上緣往上延伸
    constexpr int16_t HISTORY_ROW_HIGHLIGHT_H   = 26;  // 高亮 fillRect 高度（< row 高度避免疊出）
    constexpr int16_t HISTORY_ROW_TEXT_VOFFSET  = 8;   // 文字 baseline 微調對齊視覺中心
    constexpr size_t  HISTORY_ID_DISPLAY_TAIL   = 5;   // 10 位 zero-padded id 顯示後 5 位（前面通常為 0）
    uint16_t shown = 0;
    for (uint16_t i = historyScrollOffset;
         i < historyCount && shown < HISTORY_VISIBLE_ROWS;
         ++i, ++shown) {
        const case_meta_t& m = historyCases[i];
        int y = HISTORY_LIST_START_Y + (int)shown * HISTORY_ROW_HEIGHT_PX;

        // cursor 高亮列：填底色
        if (i == historyCursor) {
            display.fillRect(HISTORY_ROW_SIDE_MARGIN,
                             y - HISTORY_ROW_TOP_INSET,
                             SCREEN_W - HISTORY_ROW_SIDE_MARGIN * 2,
                             HISTORY_ROW_HIGHLIGHT_H,
                             COLOR_ACCENT_WARN);
        }
        uint16_t fg = (i == historyCursor) ? COLOR_BG : COLOR_TEXT_PRIMARY;

        // 顯示「id 後 5 位 ｜ EPI N ｜ 電擊 N」
        snprintf(buf, sizeof(buf), "#%s  EPI %u  電擊 %u",
                 m.id + (EMS_STORAGE_ID_LEN - 1 - HISTORY_ID_DISPLAY_TAIL),
                 (unsigned)case_meta_epi_total(m),
                 (unsigned)case_meta_shock_total(m));
        display.setTextSize(1);
        display.setTextColor(fg);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString(buf, SCREEN_W / 2, y + HISTORY_ROW_TEXT_VOFFSET);
    }

    // STEP 04: 底部操作提示（demo V1 §12 無提示，韌體保留實機可發現性）
    //   「詳/情」不在 ems_zh_24_vlw.h 222 glyphs 字表內 → 改用 SUMMARY 畫面已驗證的「總覽」
    drawCenteredText("主鍵 總覽　返回 主功能表",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/**
 * SUMMARY sub-menu 主鍵分派（SoT V1 §11.1）。
 *
 * 共用於 OHCA 結束鎖定後 SUMMARY（globalState=GLOBAL_OHCA）與歷史模式 SUMMARY
 * （globalState=GLOBAL_HISTORY_PLACEHOLDER + historySummaryMode）。
 *
 * 同步返回語意（SoT §16.5「自動回案件總覽」、§16.9 失敗亦留原處）：
 *   進 GLOBAL_SYNC 前記下 caller globalState 到 g_sync_return_to，
 *   loop observer 於 DONE/ERROR → IDLE 邊緣讀此值拉回原案件總覽。
 */
static void handleSummarySubmenuPrimary() {
    switch (summarySubmenuCursor) {
        // STEP 01: 事件時間軸（歷史模式子畫面未實作 → noop + trace）
        case SUMMARY_SUBMENU_TIMELINE:
            if (historySummaryMode) {
                Serial.println("[SUMMARY] timeline disabled in history mode (noop)");
                return;
            }
            ohcaSubState         = SUBSTATE_TIMELINE;
            timelineScrollOffset = 0;
            return;
        // STEP 02: 同步至 App（SoT §10.4 / §11.1 入口；§16.5 結束自動回案件總覽）
        case SUMMARY_SUBMENU_SYNC:
            // STEP 02.01: re-entry guard — 已在 GLOBAL_SYNC 期間別覆寫 return_to，
            //   否則 loop observer DONE/ERROR → IDLE 邊緣會把 caller SUMMARY 永久遺失。
            if (globalState != GLOBAL_SYNC) {
                g_sync_return_to = globalState;
            } else {
                Serial.println("[SYNC] re-entry detected, keep g_sync_return_to");
            }
            globalState = GLOBAL_SYNC;
            ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::START, millis());
            if (g_ble_client_connected) {
                ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::BLE_CONNECTED, millis());
            }
            // STEP 02.02: dispatch rollback — dispatcher 為 void 介面，若 START 被狀態機拒絕
            //   state 仍是 IDLE → 畫面已切 GLOBAL_SYNC 但流程沒走 = 卡死。回退到 return_to。
            if (g_sync_ctx.state == ems::SyncState::IDLE) {
                Serial.printf("[SYNC] dispatcher rejected START, rollback to %u\n",
                              (unsigned)g_sync_return_to);
                globalState = g_sync_return_to;
                return;
            }
            Serial.printf("[SYNC] enter state=%u (return_to=%u)\n",
                          (unsigned)g_sync_ctx.state, (unsigned)g_sync_return_to);
            return;
        // STEP 03: 未知 cursor 值（enum 擴充忘加 case 防呆）
        default:
            Serial.printf("[SUMMARY] unhandled cursor=%u\n", summarySubmenuCursor);
            return;
    }
}

void drawOhcaSummary() {
    // STEP 01: 用 caseSummary 聚合（V1 §11）
    //   - 現場案件結束流程：caseStartEpochMs 為對時後的案件起點 epoch；未對時則 0
    //   - 歷史進入（historySummaryMode）：caseStartEpochMs=0，timestamp 來自過去 boot，
    //     無法換算相對時間 → 時間 row 隱藏（spec §4.1 未對時行為，by-design）
    //   - Phase F MVP1 前用 caseStartMs (uptime)，與 epoch timestamp_ms 混算會爆掉
    //     （顯示天文數字）；本 fix 切到 caseStartEpochMs 統一為 epoch 基準
    const uint64_t startForRel = historySummaryMode ? 0 : caseStartEpochMs;
    ohca_case_summary_t s;
    caseSummary_build(&s, events, eventCount, startForRel, /*case_end*/ 0);

    useZhFont();
    char buf[64];

    // STEP 02: 標題（對齊 demo V1 §11.1）
    //   「｜OHCA」全形 vertical bar 已在 EPI/電擊細分字串驗證可顯示
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("案件總覽｜OHCA", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    // STEP 03: 兩欄式 key|value layout（對齊 demo dense summary 風格）
    const int16_t COL_KEY_X = 12;             // key 左對齊
    const int16_t COL_VAL_X = SCREEN_W - 12;  // value 右對齊
    int16_t y = 50;
    const int16_t LINE_H     = 22;
    const int16_t SECTION_GAP = 4;

    display.setTextSize(1);

    // ===== EPI 區段 =====
    // line 1: 區段標題 + 總數（demo: `EPI` 區段 + `總數 N` 兩列；韌體預算緊縮為單列 `EPI 總 N`）
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::middle_left);
    display.drawString("EPI", COL_KEY_X, y);
    display.setTextDatum(textdatum_t::middle_right);
    snprintf(buf, sizeof(buf), "總 %u", s.epi_total);
    display.drawString(buf, COL_VAL_X, y);
    y += LINE_H;

    // line 2: 細分（本機/接手前/補登 對齊 demo「本機 / 接手前 / 純補登」）
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setTextDatum(textdatum_t::middle_left);
    display.drawString("本機/接手前/補登", COL_KEY_X, y);
    display.setTextDatum(textdatum_t::middle_right);
    snprintf(buf, sizeof(buf), "%u/%u/%u",
             s.epi_local, s.epi_pre_handover, s.epi_pure_supp);
    display.drawString(buf, COL_VAL_X, y);
    y += LINE_H;

    // line 3: 本機 EPI 相對時間 m:ss（無 case_start → 隱藏）
    //   demo 拆「第一次本機」「最後本機」兩列，此處合一列 first / last 節省垂直空間
    if (startForRel > 0 && s.first_epi_local_ms > 0) {
        const uint32_t f = (uint32_t)((s.first_epi_local_ms - startForRel) / 1000);
        const uint32_t l = (uint32_t)((s.last_epi_local_ms  - startForRel) / 1000);
        display.setTextDatum(textdatum_t::middle_left);
        display.drawString("本機 m:ss", COL_KEY_X, y);
        if (s.first_epi_local_ms == s.last_epi_local_ms) {
            snprintf(buf, sizeof(buf), "%lu:%02lu",
                     (unsigned long)(f / 60), (unsigned long)(f % 60));
        } else {
            snprintf(buf, sizeof(buf), "%lu:%02lu / %lu:%02lu",
                     (unsigned long)(f / 60), (unsigned long)(f % 60),
                     (unsigned long)(l / 60), (unsigned long)(l % 60));
        }
        display.setTextDatum(textdatum_t::middle_right);
        display.drawString(buf, COL_VAL_X, y);
        y += LINE_H;
    }
    y += SECTION_GAP;

    // ===== 電擊 區段 =====
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::middle_left);
    display.drawString("電擊", COL_KEY_X, y);
    display.setTextDatum(textdatum_t::middle_right);
    snprintf(buf, sizeof(buf), "總 %u", s.shock_total);
    display.drawString(buf, COL_VAL_X, y);
    y += LINE_H;

    display.setTextColor(COLOR_TEXT_MUTED);
    display.setTextDatum(textdatum_t::middle_left);
    display.drawString("本機/接手前/補登", COL_KEY_X, y);
    display.setTextDatum(textdatum_t::middle_right);
    snprintf(buf, sizeof(buf), "%u/%u/%u",
             s.shock_local, s.shock_pre_handover, s.shock_pure_supp);
    display.drawString(buf, COL_VAL_X, y);
    y += LINE_H;

    // 電擊 demo V1 §11.3 不顯示「第一次本機」，只顯示 last
    if (startForRel > 0 && s.last_shock_local_ms > 0) {
        const uint32_t l = (uint32_t)((s.last_shock_local_ms - startForRel) / 1000);
        display.setTextDatum(textdatum_t::middle_left);
        display.drawString("本機 m:ss", COL_KEY_X, y);
        display.setTextDatum(textdatum_t::middle_right);
        snprintf(buf, sizeof(buf), "%lu:%02lu",
                 (unsigned long)(l / 60), (unsigned long)(l % 60));
        display.drawString(buf, COL_VAL_X, y);
        y += LINE_H;
    }
    y += SECTION_GAP;

    // ===== Amio =====
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::middle_left);
    display.drawString("Amio", COL_KEY_X, y);
    display.setTextDatum(textdatum_t::middle_right);
    if (startForRel > 0 && s.last_amio_ms > 0) {
        const uint32_t l = (uint32_t)((s.last_amio_ms - startForRel) / 1000);
        snprintf(buf, sizeof(buf), "總 %u  %lu:%02lu",
                 s.amio_total,
                 (unsigned long)(l / 60), (unsigned long)(l % 60));
    } else {
        snprintf(buf, sizeof(buf), "總 %u", s.amio_total);
    }
    display.drawString(buf, COL_VAL_X, y);

    // STEP 04: sub-menu cursor 列（SoT V1 §11.1，視覺常數見 file scope SUMMARY_SUBMENU_*）
    //   歷史模式（historySummaryMode）下 Timeline 子畫面未實作 → Timeline 項顯示停用色，
    //   按主鍵 noop（handleSummarySubmenuPrimary 直接 return）；同步項仍可用。
    const int16_t SUBMENU_Y_BASE = SCREEN_H - OHCA_COUNTER_BOTTOM
                                   - SUMMARY_SUBMENU_BOTTOM_MARGIN
                                   - SUMMARY_SUBMENU_ROW_H * SUMMARY_SUBMENU_COUNT;
    const char* const SUBMENU_LABELS[SUMMARY_SUBMENU_COUNT] = {
        "事件時間軸",  // SUMMARY_SUBMENU_TIMELINE
        "同步至 App",  // SUMMARY_SUBMENU_SYNC
    };

    display.setTextSize(1);
    for (uint8_t i = 0; i < SUMMARY_SUBMENU_COUNT; ++i) {
        const int16_t rowY    = SUBMENU_Y_BASE + i * SUMMARY_SUBMENU_ROW_H;
        const bool    cursor  = (i == summarySubmenuCursor);
        const bool    disabled = (i == SUMMARY_SUBMENU_TIMELINE && historySummaryMode);

        uint16_t color;
        if (disabled) {
            color = cursor ? COLOR_TEXT_MUTED : COLOR_TEXT_DIM;
        } else {
            color = cursor ? COLOR_ACCENT_OK : COLOR_TEXT_PRIMARY;
        }

        display.setTextColor(color);
        display.setTextDatum(textdatum_t::middle_left);
        display.drawString(cursor ? ">" : " ",
                           SUMMARY_SUBMENU_LEFT_PAD - SUMMARY_SUBMENU_CURSOR_GLYPH_W,
                           rowY + SUMMARY_SUBMENU_ROW_H / 2);
        display.drawString(SUBMENU_LABELS[i],
                           SUMMARY_SUBMENU_LEFT_PAD, rowY + SUMMARY_SUBMENU_ROW_H / 2);
    }
}

void drawTwoStepArmedOverlay(const char* what) {
    // 底部全寬反色提示條（琥珀警示色，efontTW_24 × 1.2 ≈ 29px 黑字）
    display.fillRect(0, SCREEN_H - DIALOG_BAR_H, SCREEN_W, DIALOG_BAR_H, COLOR_ACCENT_WARN);
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    display.setTextColor(COLOR_BG);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(what, SCREEN_W / 2, SCREEN_H - DIALOG_BAR_H / 2);
}

void drawPlaceholder(const char* title, const char* phase) {
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText(title, OHCA_BADGE_Y, COLOR_TEXT_PRIMARY);

    display.setTextSize(2);
    drawCenteredText(phase, SCREEN_H / 2 - 40, COLOR_ACCENT_OK);

    display.setTextSize(1);
    drawCenteredText("尚未實作", SCREEN_H / 2 + 24, COLOR_TEXT_MUTED);
    drawCenteredText("返回　主功能表",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

// ============================================================
//  Phase B 顯示函式
// ============================================================

/**
 * 子選單通用底部 hint（左：上下選擇／中：主鍵確認／右：返回取消）
 * 三段分別貼左右邊與置中，避免單行 + 全形空格時溢出 320px。
 */
static void drawSubmenuNavHint() {
    constexpr int16_t HINT_SIDE_INSET = 10;     // 左右段距螢幕邊
    constexpr int16_t HINT_BOTTOM_GAP = 8;      // 貼底基準偏移（沿用 OHCA 主畫面 counter 行底邊 anchor）
    constexpr float   HINT_TEXT_SIZE  = 0.85f;  // vlw 24px 縮放，0.85× 讓 3 段水平互不貼邊
    useZhFont();
    display.setTextSize(HINT_TEXT_SIZE);
    display.setTextColor(COLOR_TEXT_DIM);
    const int16_t hintY = SCREEN_H - OHCA_COUNTER_BOTTOM - HINT_BOTTOM_GAP;
    display.setTextDatum(textdatum_t::top_left);
    display.drawString("上下選擇", HINT_SIDE_INSET, hintY);
    display.setTextDatum(textdatum_t::top_center);
    display.drawString("主鍵確認", SCREEN_W / 2, hintY);
    display.setTextDatum(textdatum_t::top_right);
    display.drawString("返回取消", SCREEN_W - HINT_SIDE_INSET, hintY);
}

/** 藥物選單（V1 §9.2） */
void drawDrugMenu() {
    // 標題
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("EPI / 藥物選單", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    // 2 項目
    const char* labels[2] = { "補登 EPI", "Amiodarone" };
    constexpr int16_t MENU_Y_START       = 78;
    constexpr int16_t MENU_ROW_H         = 36;
    constexpr int16_t MENU_TEXT_PAD      = 32;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 6;
    useZhFont();
    display.setTextSize(1);
    for (uint8_t i = 0; i < 2; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == backfillCursor) {
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(labels[i]);
    }

    drawSubmenuNavHint();
}

/** 補登類型選擇（接手前 / 純補登） */
void drawBackfillType() {
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    const char* header = (backfillCategory == BACKFILL_CAT_EPI) ? "補登 EPI" : "電擊補登";
    drawCenteredText(header, 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    const char* labels[2];
    if (backfillCategory == BACKFILL_CAT_EPI) {
        labels[0] = "接手前 EPI"; labels[1] = "純補登 EPI";
    } else {
        labels[0] = "接手前電擊"; labels[1] = "純補登電擊";
    }

    constexpr int16_t MENU_Y_START       = 78;
    constexpr int16_t MENU_ROW_H         = 36;
    constexpr int16_t MENU_TEXT_PAD      = 32;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 6;
    useZhFont();
    display.setTextSize(1);
    for (uint8_t i = 0; i < 2; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == backfillCursor) {
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(labels[i]);
    }

    drawSubmenuNavHint();
}

/** 補登次數選擇（V1 §9.6；對齊 demo OHCA_SUPP_COUNT 列表式：每 row "<typeLabel> ×<n>"） */
void drawBackfillCount() {
    // STEP 01: header「選擇次數」（對齊 demo menu-header）
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("選擇次數", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    // STEP 02: 取 typeLabel + 範圍上限（PRE_HANDOVER=5 / PURE=3，不分 EPI/Shock）
    const char* typeLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "接手前 EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "純補登 EPI" :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "接手前電擊" :
                                                             "純補登電擊";
    const uint8_t maxN = suppCountMax(backfillSuppType);

    // STEP 03: 列表 row（對齊 demo `${typeLabel} ×${i+1}` 與 drawBackfillConfirm/Success 同字符）
    //   row_h=30 + start=60 → 5 row 結束 y=210，hint y=214 留 4px 緩衝
    constexpr int16_t MENU_Y_START       = 60;
    constexpr int16_t MENU_ROW_H         = 30;
    constexpr int16_t MENU_TEXT_PAD      = 32;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 4;
    display.setTextSize(1);
    char rowBuf[32];
    for (uint8_t i = 0; i < maxN; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        // STEP 03.01: backfillCount 是 1-indexed，i 從 0 起算 → 比對 i+1
        const bool selected = ((i + 1) == backfillCount);
        if (selected) {
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        snprintf(rowBuf, sizeof(rowBuf), "%s ×%u", typeLabel, (unsigned)(i + 1));
        display.print(rowBuf);
    }

    // STEP 04: 底部 hint
    drawSubmenuNavHint();
}

/** 補登確認對話框（V1 §9.4） */
void drawBackfillConfirm() {
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("確認補登？", 20, COLOR_ACCENT_WARN);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    const char* shortLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "接手前 EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "純補登 EPI" :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "接手前電擊" :
                                                             "純補登電擊";
    char buf[32];
    snprintf(buf, sizeof(buf), "%s ×%u", shortLabel, backfillCount);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText(buf, 90, COLOR_TEXT_PRIMARY);

    display.setTextSize(1);
    drawCenteredText("成立後不可撤銷", 150, COLOR_TEXT_MUTED);

    drawCenteredText("主鍵確認　返回取消",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** 補登成功提示（2s 後自動消失，V1 §9.5） */
void drawBackfillSuccess() {
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("補登成功", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    const char* shortLabel =
        (backfillSuppType == SUPP_TYPE_EPI_PRE_HANDOVER)   ? "接手前 EPI" :
        (backfillSuppType == SUPP_TYPE_EPI_PURE)           ? "純補登 EPI" :
        (backfillSuppType == SUPP_TYPE_SHOCK_PRE_HANDOVER) ? "接手前電擊" :
                                                             "純補登電擊";
    char buf[32];
    snprintf(buf, sizeof(buf), "%s ×%u", shortLabel, backfillCount);
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText(buf, 90, COLOR_TEXT_PRIMARY);

    display.setTextSize(1);
    drawCenteredText("已紀錄", 150, COLOR_TEXT_MUTED);
}

/** Amiodarone 兩段確認 */
void drawAmioConfirmPrompt() {
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("確認 Amiodarone？", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    display.setTextSize(1);
    drawCenteredText("確認後將建立時間戳", 90, COLOR_TEXT_PRIMARY);
    drawCenteredText("不影響 EPI 倒數",   124, COLOR_TEXT_MUTED);

    if (showAmioArmedPrompt) {
        // 底部琥珀 bar overlay：再按一次主鍵確認
        display.fillRect(0, SCREEN_H - DIALOG_BAR_H, SCREEN_W, DIALOG_BAR_H, COLOR_ACCENT_WARN);
        useZhFont();
        display.setTextSize(1.2f, 1.2f);
        display.setTextColor(COLOR_BG);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString("再按一次主鍵確認", SCREEN_W / 2, SCREEN_H - DIALOG_BAR_H / 2);
    } else {
        drawCenteredText("主鍵確認　返回取消",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
    }
}

/** Timeline 子畫面（V1 §11.5） */
void drawTimeline() {
    // 標題
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("事件時間軸", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    if (eventCount == 0) {
        display.setTextSize(1);
        drawCenteredText("（無事件）", SCREEN_H / 2 - 12, COLOR_TEXT_MUTED);
        drawCenteredText("返回　總覽",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
        return;
    }

    // 排序索引（純函式 lib）
    static uint16_t idx[MAX_EVENTS];
    caseSummary_buildTimeline(idx, events, eventCount);

    // 顯示最多 5 筆，row_h=28
    constexpr int16_t ROW_Y0  = 70;
    constexpr int16_t ROW_H   = 28;
    constexpr int16_t TIME_X  = 24;
    constexpr int16_t LABEL_X = 110;

    useZhFont();
    display.setTextSize(1);
    uint16_t shown = 0;
    for (uint16_t i = timelineScrollOffset; i < eventCount && shown < 5; i++, shown++) {
        const int16_t y = ROW_Y0 + shown * ROW_H;
        const ems_event_t& e = events[idx[i]];

        // 時間欄
        char timeBuf[16];
        const bool isSupp = isBackfillEvent(&e);
        if (isSupp) {
            snprintf(timeBuf, sizeof(timeBuf), "—");
            display.setTextColor(COLOR_TEXT_DIM);
        } else {
            const uint32_t sec = e.elapsed_ms / 1000;
            snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu",
                     (unsigned long)(sec / 60), (unsigned long)(sec % 60));
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(TIME_X, y);
        display.print(timeBuf);

        // 類型欄
        const char* lbl =
            e.type == EVT_EPI_LOCAL          ? "EPI" :
            e.type == EVT_SHOCK_LOCAL        ? "電擊" :
            e.type == EVT_AMIODARONE         ? "Amiodarone" :
            e.type == EVT_EPI_PRE_HANDOVER   ? "接手前 EPI" :
            e.type == EVT_EPI_PURE_SUPP      ? "純補登 EPI" :
            e.type == EVT_SHOCK_PRE_HANDOVER ? "接手前電擊" :
            e.type == EVT_SHOCK_PURE_SUPP    ? "純補登電擊" : "?";
        display.setTextColor(isSupp ? COLOR_ACCENT_WARN : COLOR_TEXT_PRIMARY);
        display.setCursor(LABEL_X, y);
        display.print(lbl);
        if (e.count > 1) {
            display.print(" ×");
            display.print(e.count);
        }
    }

    drawCenteredText("返回　總覽",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

// ============================================================
//  Phase C 顯示函式
// ============================================================

/** 獨立 6 秒通氣節奏主畫面（V1 §13.6 執行中畫面 / §13.12 暫停畫面） */
/**
 * A8：VENT_PRE 預備畫面 — 進入 Vent 模式但尚未按主鍵開始
 * 對齊 demo VENT_PRE render
 */
void drawVentPre() {
    useZhFont();

    // 頂部標題
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("6 秒通氣節奏", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    // 中央主訊息「按主鍵開始」（efontTW × 1.5 ≈ 36px）
    display.setTextSize(1.5f, 1.5f);
    drawCenteredText("按主鍵開始", SCREEN_H / 2 - 24, COLOR_TEXT_PRIMARY);

    // 副訊息兩行
    display.setTextSize(1);
    drawCenteredText("通氣音量可由 上/下 調整", SCREEN_H / 2 + 32, COLOR_TEXT_MUTED);
    drawCenteredText("主鍵暫停／繼續　長按 3 秒結束", SCREEN_H / 2 + 60, COLOR_TEXT_DIM);

    // 底部音量顯示
    char volBuf[32];
    snprintf(volBuf, sizeof(volBuf), "音量 %u/%u", ventVolume, VENT_VOLUME_MAX);
    drawCenteredText(volBuf, SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

void drawVentStandalone() {
    useZhFont();

    // 頂部標題 + 音量
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("6 秒通氣節奏", OHCA_BADGE_Y, COLOR_ACCENT_OK);

    char volBuf[32];
    snprintf(volBuf, sizeof(volBuf), "音量 %u/%u", ventVolume, VENT_VOLUME_MAX);
    display.setTextSize(1);
    drawCenteredText(volBuf, 56, COLOR_TEXT_MUTED);

    if (ventPaused) {
        display.setTextSize(2);
        drawCenteredText("已暫停", SCREEN_H / 2 - 24, COLOR_ACCENT_WARN);
        display.setTextSize(1);
        drawCenteredText("主鍵繼續　返回結束",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
        return;
    }

    // 中央大字單秒數（中心 y 略下移，避開頂部 Vol 行）
    uint32_t since = (ventStartMs == 0) ? 0 : (millis() - ventStartMs);
    vent_beat_t beat = computeVentBeat(since);
    uint8_t num = (uint8_t)beat + 1;

    char numBuf[4];
    snprintf(numBuf, sizeof(numBuf), "%u", num);
    display.setFont(&fonts::FreeMonoBold24pt7b);
    display.setTextSize(2);
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::middle_center);
    display.drawString(numBuf, SCREEN_W / 2, SCREEN_H / 2 + 16);

    // 底部提示（橫條反色顯示結束 hint 或基本提示）
    useZhFont();
    if (ventBackHintShown) {
        display.fillRect(0, SCREEN_H - VENT_BAR_H, SCREEN_W, VENT_BAR_H, COLOR_ACCENT_WARN);
        display.setTextSize(1);
        display.setTextColor(COLOR_BG);
        display.setTextDatum(textdatum_t::middle_center);
        display.drawString("如要結束　請長按主鍵",
                           SCREEN_W / 2, SCREEN_H - VENT_BAR_H / 2);
    } else {
        display.setTextSize(1);
        drawCenteredText("主鍵暫停　長按 3 秒結束",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
    }
}

/** 獨立 vent 結束確認對話框（V1 §13.14 結束獨立 6 秒通氣節奏） */
void drawVentEndCheck() {
    useZhFont();

    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("結束通氣節奏？", OHCA_BADGE_Y, COLOR_ACCENT_WARN);

    display.setTextSize(2);
    drawCenteredText("確認結束？", SCREEN_H / 2 - 24, COLOR_TEXT_PRIMARY);

    display.setTextSize(1);
    drawCenteredText("主鍵確認　返回取消",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** 快速功能選單（V1 §14.9 開啟、暫停、繼續與關閉 + §9 OHCA 中按返回鍵） */
void drawQuickMenu() {
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText("快速功能", 20, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    // V1 §14.9：動態 3 / 4 項（B3：加「案件簡版總覽」對齊 demo）
    const char* labels[4];
    uint8_t count;
    if (!ohcaVentOverlayEnabled) {
        labels[0] = "開啟 6 秒給氣提示";
        labels[1] = "案件簡版總覽";
        labels[2] = "返回 OHCA";
        count = 3;
    } else {
        labels[0] = ohcaVentPaused ? "繼續 6 秒給氣" : "暫停 6 秒給氣";
        labels[1] = "關閉 6 秒給氣提示";
        labels[2] = "案件簡版總覽";
        labels[3] = "返回 OHCA";
        count = 4;
    }

    constexpr int16_t MENU_Y_START       = 68;  // 往上 10px 避免 4 列時撞到底部 hint
    constexpr int16_t MENU_ROW_H         = 36;
    constexpr int16_t MENU_TEXT_PAD      = 32;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 4;  // size 1.1 → 26px 字在 36px row 內垂直置中
    display.setTextSize(1.1f, 1.1f);
    for (uint8_t i = 0; i < count; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == backfillCursor) {
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(labels[i]);
    }

    // 底部 hint 14 字在 size 1 下 ~336px 超出 320，縮 size 0.85 ≈ 286px 完整顯示
    display.setTextSize(0.85f, 0.85f);
    drawCenteredText("上下選擇　主鍵確認　返回關閉",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** OHCA 內 6 秒通氣輔助區塊（V1 §14.4 單秒數視窗 / §14.10 暫停狀態）
 *  右上角小 overlay：放在 OHCA badge 同 y 的右側，不撞中央大時間 / 標籤 / 計數
 *  y_top 參數保留 API 相容但忽略（新 layout 自決定位置）
 */
void drawOhcaVentOverlay(int /*y_top*/) {
    useZhFont();

    if (ohcaVentPaused) {
        // 暫停狀態：右上角「通氣暫停」
        display.setTextSize(1);
        display.setTextColor(COLOR_ACCENT_WARN);
        display.setTextDatum(textdatum_t::top_right);
        display.drawString("通氣暫停", SCREEN_W - 8, OHCA_BADGE_Y + 2);
        return;
    }

    uint32_t since = (ventStartMs == 0) ? 0 : (millis() - ventStartMs);
    vent_beat_t beat = computeVentBeat(since);
    uint8_t num = (uint8_t)beat + 1;

    // 右上角「通氣 N」
    display.setTextSize(1);
    display.setTextColor(COLOR_TEXT_MUTED);
    display.setTextDatum(textdatum_t::top_right);
    char buf[16];
    snprintf(buf, sizeof(buf), "通氣 %u", num);
    display.drawString(buf, SCREEN_W - 8, OHCA_BADGE_Y + 2);
}
