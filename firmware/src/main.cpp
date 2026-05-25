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
 *   🟡 Phase F — BLE 同步（NUS + JSON 過渡 / 配對碼）：MVP1 對時 + MVP2 dispatcher + Followup §11.1/§16.5 入口完成；MVP3 chunked data 真送待實機
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

#include "app_globals.h"

// Dev-Phase 3 RTC：DS3231 包裝（#ifdef ARDUINO 守護的具體實作）
#include <Wire.h>
#include "ds3231_backend.h"
#include "null_backend.h"


// ════════════════════════════════════════════════════════════════
// 全域變數定義（extern 宣告在 app_globals.h）
// ════════════════════════════════════════════════════════════════

GlobalState globalState = GLOBAL_MAIN_MENU;
uint8_t mainMenuCursor = 0;

// OHCA 子狀態
ohca_state_t ohcaState         = OHCA_STATE_MAIN_MENU;
uint32_t     ohcaLastEpiMs     = 0;
uint32_t     ohcaPrevSinceMs   = 0;
uint32_t     startFlashStartMs = 0;
bool         alarmMuted        = false;
uint32_t     caseStartMs       = 0;
uint64_t     caseStartEpochMs  = 0;

// 兩段確認
two_step_confirm_t epiConfirm;
two_step_confirm_t shockConfirm;
two_step_confirm_t amioConfirm;

bool     showEpiArmedPrompt    = false;
uint32_t epiArmedPromptStart   = 0;
bool     showShockArmedPrompt  = false;
uint32_t shockArmedPromptStart = 0;
bool     showAmioArmedPrompt   = false;
uint32_t amioArmedPromptStart  = 0;

// 事件紀錄
ems_event_t events[MAX_EVENTS];
uint16_t    eventCount   = 0;
uint32_t    nextEventId  = 1;

// Phase E 持久化
IStorageBackend g_storage_be;
bool g_storage_ready    = false;
bool g_locked_saved     = false;

// Phase F MVP1 BLE
ems::BleNus g_ble;
ems::TimeSyncState g_ts_state;

// Dev-Phase 3 RTC — setup() 內依 DS3231 偵測結果指向 DS3231Backend 或 NullRtcBackend
// 永不為 nullptr（setup 後）
ems::RtcBackend* g_rtc = nullptr;

// Phase F MVP2 同步
ems::SyncContext   g_sync_ctx;
ems::SyncReturnTo  g_sync_return_to = ems::SyncReturnTo::MAIN;
SyncTarget g_sync_target = {};
uint64_t g_ohca_live_synced_at_ms = 0;

// 歷史紀錄 UI
case_meta_t historyCases[EMS_STORAGE_OHCA_CAP];
uint16_t historyCount        = 0;
uint16_t historyCursor       = 0;
uint16_t historyScrollOffset = 0;
bool     historySummaryMode  = false;

// Phase B 子流程
OhcaSubState ohcaSubState = SUBSTATE_NONE;
BackfillCategory backfillCategory   = BACKFILL_CAT_EPI;
supp_type_t      backfillSuppType   = SUPP_TYPE_EPI_PRE_HANDOVER;
uint8_t          backfillCursor     = 0;
uint8_t          backfillCount      = 1;
uint32_t         backfillSuccessShownMs = 0;
uint8_t summarySubmenuCursor = SUMMARY_SUBMENU_TIMELINE;
uint16_t timelineScrollOffset = 0;

// Phase C
uint8_t  ventVolume         = VENT_VOLUME_DEFAULT;
uint32_t ventStartMs        = 0;
uint32_t ventPrevSinceMs    = 0;
bool     ventEndCheckShown  = false;
bool     ventBackHintShown  = false;
uint32_t ventBackHintStartMs = 0;
bool     ventPaused         = false;
bool     ventPreShown       = false;
bool     ohcaVentOverlayEnabled = false;
bool     ohcaVentPaused         = false;

// END_CHECK / SUMMARY
EndCheckCursor endCheckCursor = END_CHECK_CURSOR_CANCEL;
bool endConfirmShown = false;
bool resyncConfirmShown = false;  // Phase F §16.7：已同步案件再次同步的確認 dialog
uint16_t summaryScrollOffset = 0;

// Flash overlay
FlashState flashState = {};

// 按鈕狀態
uint8_t  lastBtnState[BTN_COUNT];
uint32_t lastPressMs[BTN_COUNT]      = { 0 };
uint32_t btnPressStartMs[BTN_COUNT]  = { 0 };
bool     btnLongFired[BTN_COUNT]     = { false };

// 顯示
LGFX tft;
FrameSprite display(&tft);
bool g_vlw_loaded = false;

// 蜂鳴器
uint8_t  beepPulsesRemaining = 0;
uint32_t beepNextToggleMs    = 0;
bool     beepActive          = false;
uint16_t beepOnMs            = 0;
uint16_t beepOffMs           = 0;
bool     buzzContinuous      = false;

// OLED 反色
bool     oledInverted          = false;
uint32_t oledInvertStartMs     = 0;
uint16_t oledInvertDurationMs  = 0;

// 顯示節流
uint32_t lastDisplayUpdateMs = 0;

// DisplaySnapshot
DisplaySnapshot lastDisplaySnapshot = {};


// ============================================================
// Phase F MVP1：BLE RX 訊息分流（由 g_ble.poll() 觸發）
// ============================================================

/**
 * 依配對碼驗證結果回送 pair_status TX 訊息給網頁端（對齊 ble-client.js 切句、
 * connect.js handlePairStatus 分流）。
 *
 * accepted / locked_out 為終局結果；rejected 帶 remaining_attempts 供網頁顯示
 * 剩餘可重試次數。IgnoredWrongState（非 AWAITING_INPUT 收到輸入）不回訊息 ——
 * 網頁端此時並未等待此回覆。
 *
 * @param r  sync_dispatcher_dispatch_input() 的回傳結果
 */
static void send_pair_status(ems::DispatchInputResult r) {
    // STEP 01: 非預期狀態的輸入不回覆（網頁端未在等待）
    if (r == ems::DispatchInputResult::IgnoredWrongState) {
        return;
    }

    // STEP 02: 依結果組 pair_status JSON（行結尾 '\n' 對齊網頁端切句慣例）。
    //   schema 固定 ≤3 欄、無使用者輸入字串，直接 snprintf 組字面 JSON 即可，
    //   毋須比照 case_sync / time_sync_ack 走 ArduinoJson（省 JsonDocument 開銷）。
    char buf[96];
    int n = 0;
    if (r == ems::DispatchInputResult::Accepted) {
        n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"pair_status\",\"result\":\"accepted\"}\n");
    } else if (r == ems::DispatchInputResult::LockedOut) {
        n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"pair_status\",\"result\":\"locked_out\"}\n");
    } else {
        // STEP 02.01: Rejected — 剩餘重試次數由 ems_pairing 統一計算（與 lockout 門檻同源）
        const unsigned remaining =
            ems::pairing_remaining_attempts(g_sync_ctx.pairing_code);
        n = snprintf(buf, sizeof(buf),
                     "{\"type\":\"pair_status\",\"result\":\"rejected\","
                     "\"remaining_attempts\":%u}\n",
                     remaining);
    }

    // STEP 03: notify 送出。snprintf 失敗 / 截斷不送（避免送半截 JSON）；送出失敗只
    //   WARN log —— pair_status 為 best-effort 通知，連線已斷時整個同步流程自會轉 ERROR。
    if (n <= 0 || (size_t)n >= sizeof(buf)) {
        Serial.printf("[BLE] WARN pair_status snprintf anomaly n=%d\n", n);
        return;
    }
    if (!g_ble.send(reinterpret_cast<const uint8_t*>(buf), (size_t)n)) {
        Serial.println("[BLE] WARN pair_status send failed (client 已斷線?)");
    }
}

/**
 * BLE RX callback：解析 JSON type 欄位做訊息分流。
 * 由 g_ble.poll() 在 main loop 呼叫（非 GATT task）。
 */
static void on_ble_rx(const uint8_t* data, size_t len) {
    // STEP 01: 解析 JSON 取 type 欄位
    JsonDocument doc;
    DeserializationError parse_err = deserializeJson(doc, data, len);
    if (parse_err) {
        Serial.printf("[BLE] drop malformed JSON (%u bytes)\n", (unsigned)len);
        return;
    }
    const char* type = doc["type"].is<const char*>() ? doc["type"].as<const char*>() : "";

    // STEP 02: type=time_sync → MVP1 對時
    if (strcmp(type, "time_sync") == 0) {
        char ack_buf[BLE_ACK_BUF_MAX];
        size_t ack_len = 0;
        const bool rtc_present = (g_rtc != nullptr) && g_rtc->is_present();
        ems::TimeSyncResult r = ems::time_sync_handle(
            &g_ts_state,
            data, len,
            millis(),
            rtc_present,
            ack_buf, sizeof(ack_buf), &ack_len);
        const char* result_str =
            (r == ems::TimeSyncResult::Applied)  ? "Applied"  :
            (r == ems::TimeSyncResult::Rejected) ? "Rejected" :
                                                   "ParseError";
        Serial.printf("[BLE] time_sync %s ack_len=%u\n", result_str, (unsigned)ack_len);

        // STEP 02.01: Applied → 反向寫回 DS3231（App 變主時鐘源）
        //   對齊 docs/ds3231-integration-plan.md §5.1：g_rtc 不在線時 set_epoch_ms
        //   為 no-op 回 false，無需額外分支判斷
        if (r == ems::TimeSyncResult::Applied && rtc_present) {
            const uint64_t app_epoch =
                ems::time_sync_current_epoch_ms(&g_ts_state, millis());
            if (g_rtc->set_epoch_ms(app_epoch)) {
                Serial.printf("[RTC] write-back from BLE time_sync: %llu\n",
                              (unsigned long long)app_epoch);
            } else {
                Serial.println("[RTC] WARN write-back failed");
            }
        }

        if (ack_len > 0) {
            // ems_time_sync serializeJson 不附 '\n'。ble-client 以 '\n' 切句，缺結尾符
            // 會讓 ack 與後續訊息（如 pair_status）黏在同一行 → JSON.parse 失敗 →
            // 兩則訊息都丟掉。caller 在 send 前補上分隔符，與 case_sync_serializer
            // 慣例一致。
            if (ack_len < sizeof(ack_buf) - 1) {
                ack_buf[ack_len++] = '\n';
            } else {
                // buf 滿到無法塞 '\n'：送出去仍會跟下一則訊息黏行（同類 bug 再演）。
                // 實務上 BLE_ACK_BUF_MAX = 512 而 ack JSON ~120 bytes，永遠不該命中；
                // 若觸發代表 ack schema 暴增或 buf 縮小，需立即查 ems_time_sync schema。
                Serial.printf("[BLE] WARN time_sync_ack 已塞滿 buf(%u) 無法附 '\\n'，"
                              "送出後 web 端 JSON.parse 會失敗\n",
                              (unsigned)sizeof(ack_buf));
            }
            g_ble.send(reinterpret_cast<uint8_t*>(ack_buf), ack_len);
        }
        return;
    }

    // STEP 03: type=pair_verify → MVP2 配對碼驗證
    if (strcmp(type, "pair_verify") == 0) {
        const char* input = doc["input"].is<const char*>() ? doc["input"].as<const char*>() : "";
        size_t input_len = strlen(input);
        Serial.printf("[BLE] pair_verify input='%s' len=%u vs code='%s'\n",
                      input, (unsigned)input_len, g_sync_ctx.pairing_code.digits);
        ems::DispatchInputResult r = ems::sync_dispatcher_dispatch_input(
            &g_sync_ctx, input, input_len, millis());
        const char* result_str =
            (r == ems::DispatchInputResult::Accepted)          ? "Accepted"  :
            (r == ems::DispatchInputResult::Rejected)          ? "Rejected"  :
            (r == ems::DispatchInputResult::LockedOut)         ? "LockedOut" :
                                                                 "IgnoredWrongState";
        Serial.printf("[BLE] pair_verify %s state=%u\n",
                      result_str, (unsigned)g_sync_ctx.state);
        // 回送 pair_status TX，網頁端據此決定進主鍵等待 / 重輸 / 鎖定收尾
        send_pair_status(r);
        return;
    }

    // STEP 04: 未知 type
    Serial.printf("[BLE] unknown type='%s' drop\n", type);
}

// ============================================================
// setup() / loop()
// ============================================================

/**
 * Arduino 啟動入口：初始化 Serial、周邊（蜂鳴器/按鍵/TFT）、字型、
 * 兩段確認、LittleFS 持久化、BLE NUS 與 sync dispatcher，最後繪出初始畫面。
 */
void setup() {
    Serial.begin(115200);
    // ESP32-S3 USB-CDC 列舉需 1~2 秒，太早 Serial.println 會被吃掉。
    // 等到 Serial 通了再印，方便除錯。
    uint32_t serialWaitStart = millis();
    while (!Serial && (millis() - serialWaitStart) < 3000) {
        delay(10);
    }
    Serial.println("[BOOT] EMS Timer Phase A");

    // STEP 01: 關掉板上 WS2812（GPIO 48）。boot bootloader 可能 latch 成白色，必須主動 reset。
    //   neopixelWrite() 由 esp32-hal-rgb-led 提供（ESP32 Arduino core ≥ 2.0.7 內建）。
    neopixelWrite(WS2812_PIN, 0, 0, 0);

    // STEP 02: 蜂鳴器
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

#if ENABLE_VIBRATION
    pinMode(VIBRATION_PIN, OUTPUT);
    digitalWrite(VIBRATION_PIN, LOW);
#endif

    // STEP 03: 8 按鍵 INPUT_PULLUP
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        pinMode(BTN_PINS[i], INPUT_PULLUP);
        lastBtnState[i] = digitalRead(BTN_PINS[i]);
    }

    // STEP 04: TFT 初始化（LovyanGFX + DMA + sprite buffer）
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

    // STEP 04.01: vlw 字型載入（Sarasa Mono TC Bold 24px，222 glyphs）
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

    // STEP 05: 兩段確認 init
    twoStepConfirm_init(&epiConfirm,   TWO_STEP_DEFAULT_TIMEOUT_MS);
    twoStepConfirm_init(&shockConfirm, TWO_STEP_DEFAULT_TIMEOUT_MS);

    // STEP 06: Phase E 持久化 — mount LittleFS + storage_init
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

    // STEP 06.5: Dev-Phase 3 — RTC 初始化（runtime 偵測 I2C 0x68）
    //   不變式：time_sync_init 必須先於 RTC seed 呼叫，否則 init 會清空 seed 結果
    //   對齊 docs/ds3231-integration-plan.md §4.1（三分支：在線+有時間 / 在線+未設 / 不在線）
    ems::time_sync_init(&g_ts_state);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    static ems::DS3231Backend ds3231_be;
    static ems::NullRtcBackend null_be;
    if (ds3231_be.begin(Wire)) {
        g_rtc = &ds3231_be;
        Serial.println("[RTC] DS3231 detected at 0x68");
        const uint64_t rtc_epoch = g_rtc->now_epoch_ms();
        // 對齊 spec §2.3 BLE time_sync 拒絕條件，雙向 floor / ceiling 一致；
        // 避免 DS3231 寄存器爛資料（如年份溢位回到 2099）漏網
        if (rtc_epoch >= ems::TIME_SYNC_MIN_EPOCH_MS &&
            rtc_epoch <= ems::TIME_SYNC_MAX_EPOCH_MS) {
            ems::time_sync_seed_from_rtc(&g_ts_state, rtc_epoch, millis());
            Serial.printf("[RTC] seeded software clock from RTC: %llu\n",
                          (unsigned long long)rtc_epoch);
        } else if (rtc_epoch == 0) {
            Serial.println("[RTC] DS3231 present but time not set, 等 BLE time_sync");
        } else {
            Serial.printf("[RTC] WARN DS3231 epoch out of range (%llu), 拒絕 seed\n",
                          (unsigned long long)rtc_epoch);
        }
    } else {
        g_rtc = &null_be;
        Serial.println("[RTC] not present, fallback to BLE time_sync only");
    }

    // STEP 07: Phase F MVP1 — BLE NUS peripheral 初始化
    //   失敗只 log warn 不擋 boot（對齊 storage_init 容錯模式）。
    //   廣播後待 web 端（docs/ble-tester/）連線送 time_sync 才會有 epoch；
    //   未對時前事件 timestamp_ms = 0（spec §4.1）。
    if (!g_ble.begin(BLE_DEVICE_NAME)) {
        Serial.println("[BLE] WARN init failed, time_sync 將不可用");
    }

    // STEP 08: Phase F MVP2 — sync dispatcher 初始 IDLE
    ems::sync_dispatcher_init(&g_sync_ctx, millis());

    // STEP 09: 初始顯示
    updateDisplay();
    Serial.println("[READY] MainMenu");
}

/**
 * Arduino 主迴圈：每輪排空 BLE RX、推進 sync dispatcher、處理按鍵與
 * OHCA/通氣/兩段確認等 tick，到期清提示，最後依固定間隔刷新顯示。
 */
void loop() {
    // STEP 01: Phase F MVP1 — 排空 BLE RX queue
    //   time_sync 立即生效，讓接下來 handleButtons 觸發的事件 timestamp 用真實 epoch
    g_ble.poll(on_ble_rx);

    // STEP 02: Phase F — sync dispatcher observer（僅在 GLOBAL_SYNC 內活）
    //   - BLE 連線邊緣 → dispatch BLE_CONNECTED / BLE_DISCONNECTED
    //   - 每 tick dispatch TICK 給 timeout 判斷
    //   - SENDING 入口 syncSendingPrepare()、期間 syncSendingPump() 定速真送 chunked payload
    //   - DONE/ERROR 顯示完自然回 IDLE → 退主選單
    if (globalState == GLOBAL_SYNC) {
        static bool prev_ble_conn = false;
        bool cur_ble_conn = g_ble.connected();
        if (cur_ble_conn != prev_ble_conn) {
            ems::sync_dispatcher_dispatch(&g_sync_ctx,
                cur_ble_conn ? ems::SyncEvent::BLE_CONNECTED : ems::SyncEvent::BLE_DISCONNECTED,
                millis());
            prev_ble_conn = cur_ble_conn;
        }
        ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::TICK, millis());

        static ems::SyncState prev_sync_state = ems::SyncState::IDLE;

        // STEP 02.01: SENDING 入口（一次性）準備 payload；SENDING 期間每 tick 定速推 chunk。
        //   syncSendingPrepare 失敗會轉 ERROR、syncSendingPump 送完最後一個 chunk 會轉
        //   DONE，故入口/期間判斷與下方邊緣偵測一律讀 g_sync_ctx.state 即時值。
        if (g_sync_ctx.state == ems::SyncState::SENDING
            && prev_sync_state != ems::SyncState::SENDING) {
            syncSendingPrepare();
        }
        if (g_sync_ctx.state == ems::SyncState::SENDING) {
            syncSendingPump();
        }

        // STEP 02.02: 取 prepare/pump 後的即時 state 做邊緣偵測 —— SENDING → DONE 的轉換
        //   發生在 pump 內（迴圈中段），若沿用迴圈頂端 stale 值會漏掉持久化邊緣。
        const ems::SyncState cur_sync_state = g_sync_ctx.state;
        if (cur_sync_state != prev_sync_state) {
            Serial.printf("[SYNC] state %u -> %u\n",
                          (unsigned)prev_sync_state, (unsigned)cur_sync_state);
            // 進入 AWAITING_INPUT 印出韌體實際 generate 的配對碼，供 BLE/web 端比對 debug
            if (cur_sync_state == ems::SyncState::AWAITING_INPUT) {
                Serial.printf("[SYNC] pairing_code='%s' expires_at_ms=%llu\n",
                              g_sync_ctx.pairing_code.digits,
                              (unsigned long long)g_sync_ctx.pairing_code.expires_at_ms);
            }
        }
        if (cur_sync_state == ems::SyncState::DONE
            && prev_sync_state == ems::SyncState::SENDING) {
            // SoT §16.6 寫回同步狀態：嚴格 SENDING → DONE 邊緣（forward-safety：避免未來
            // dispatcher 新增 AWAITING_INPUT→DONE 等非正常路徑誤觸發持久化）
            //   time_sync 對時後回真實 epoch ms；未對時則回 0（drawOhcaSummary
            //   以 SYNCED_AT_EPOCH_FLOOR_MS 下界判定：低於此值顯示「App未同步」）
            const uint64_t synced_at_ms = ems::time_sync_current_epoch_ms(
                &g_ts_state, millis());
            bool persisted = false;
            if (g_storage_ready && !g_sync_target.empty()) {
                persisted = storage_set_case_synced_at(&g_storage_be,
                                                      g_sync_target.type,
                                                      g_sync_target.id,
                                                      synced_at_ms);
                Serial.printf("[SYNC] persist synced_at id=%s %s\n",
                              g_sync_target.id, persisted ? "OK" : "FAILED");
            } else {
                Serial.printf("[SYNC] WARN no persist target (ready=%d id=%s)\n",
                              (int)g_storage_ready,
                              g_sync_target.empty() ? "<empty>" : g_sync_target.id);
            }
            // 同步狀態 UI：persist 成功時更新 mirror（SoT §11.1/§16.6 持久化承諾：UI 與 disk 一致）
            if (persisted) {
                g_ohca_live_synced_at_ms = synced_at_ms;
            }
            if (persisted && historySummaryMode) {
                bool mirror_hit = false;
                for (uint16_t i = 0; i < historyCount; ++i) {
                    if (strncmp(historyCases[i].id, g_sync_target.id,
                                sizeof(historyCases[i].id)) == 0) {
                        historyCases[i].synced_at_ms = synced_at_ms;
                        mirror_hit = true;
                        break;
                    }
                }
                if (!mirror_hit) {
                    Serial.printf("[SYNC] WARN history mirror miss id=%s count=%u\n",
                                  g_sync_target.id, (unsigned)historyCount);
                }
            }
        }
        if (cur_sync_state == ems::SyncState::IDLE && prev_sync_state != ems::SyncState::IDLE) {
            // SoT §16.5：「同步完成 → 顯示 1 秒 → 自動回案件總覽」
            // caller globalState 由 handleSummarySubmenuPrimary 在進 SYNC 前記入 g_sync_return_to
            // （OHCA 結束鎖定 SUMMARY / HISTORY 歷史 SUMMARY / MAIN 保險 fallback）。
            // ERROR → IDLE 也走此路徑（SoT §16.9 同步失敗亦留在原案件總覽供重試）。
            globalState = sync_return_to_global(g_sync_return_to);
        }
        prev_sync_state = cur_sync_state;
    }

    handleButtons();
    updateOhcaTick();
    updateVentTick();

    uint32_t now = millis();
    twoStepConfirm_tick(&epiConfirm,   now);
    twoStepConfirm_tick(&shockConfirm, now);
    twoStepConfirm_tick(&amioConfirm,  now);

    // STEP 03: armed 提示 5s 自動消失
    if (showEpiArmedPrompt && now - epiArmedPromptStart >= ARMED_PROMPT_MS) {
        showEpiArmedPrompt = false;
    }
    if (showShockArmedPrompt && now - shockArmedPromptStart >= ARMED_PROMPT_MS) {
        showShockArmedPrompt = false;
    }
    if (showAmioArmedPrompt && now - amioArmedPromptStart >= ARMED_PROMPT_MS) {
        showAmioArmedPrompt = false;
    }

    // STEP 04: 補登成功提示 2s 後自動回主畫面
    if (ohcaSubState == SUBSTATE_BACKFILL_SUCCESS &&
        now - backfillSuccessShownMs >= BACKFILL_SUCCESS_MS) {
        resetSubState();
    }

    // Flash overlay timeout（demo flash() 對齊，duration 由 caller 指定）
    if (flashState.active && now - flashState.startMs >= flashState.durationMs) {
        flashState.active = false;
    }

    // STEP 05: vent 返回鍵提示 2s 自動消失
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

// ════════════════════════════════════════════════════════════════
// 顯示繪製
// ════════════════════════════════════════════════════════════════

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
    in.endCheckCursor  = (uint8_t)endCheckCursor;
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
    // STEP 02.02: GLOBAL_SYNC 期間覆寫 countdownSec 觸發 snapshot dedup miss：
    //   AWAITING_INPUT 帶配對碼剩餘秒（每秒重繪）、SENDING 帶已送 chunk 數（進度推進重繪）。
    //   STEP 02 為獨立 if：OHCA case 結束後 ohcaLastEpiMs 不會被清零仍會寫入，進 SYNC
    //   後此處以「後寫勝」覆蓋。drawSyncScreen 自行算渲染值（不讀 snapshot.countdownSec），
    //   此處純為觸發 dedup miss。
    if (globalState == GLOBAL_SYNC) {
        if (g_sync_ctx.state == ems::SyncState::AWAITING_INPUT) {
            in.countdownSec = ems::pairing_remaining_sec(
                g_sync_ctx.pairing_code, (uint64_t)millis());
        } else if (g_sync_ctx.state == ems::SyncState::SENDING) {
            in.countdownSec = g_sync_ctx.sent_chunks_count;
        }
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
    in.resyncConfirmShown    = resyncConfirmShown;  // Phase F §16.7
    in.flashStateActive      = flashState.active;
    in.ventPreShown          = ventPreShown;
    in.historySummaryMode    = historySummaryMode;
    in.bleConnected          = g_ble.connected();  // Phase F MVP1
    in.syncState             = (uint8_t)g_sync_ctx.state;  // Phase F MVP2

    return captureSnapshot(in);
}

/**
 * 顯示狀態快照：updateDisplay 每次比對與上次的差異，無變化則跳過全螢幕重畫。
 * TFT 沒有 framebuffer，每次 clearDisplay+redraw 都直寫 76,800 像素到 SPI bus，
 * 視覺上會看到掃描線。snapshot 比對只有當顯示內容真正改變時才重繪 → 解決閃爍。
 *
 * struct/純邏輯抽到 lib/ems_display_snapshot/，native 環境可寫 L2 regression test
 * （Phase E history UI 漏 historyCursor 導致無重繪那類 bug 必須在 native 階段被擋）。
 */
void updateDisplay() {
    // STEP 01: snapshot 去重 — 顯示狀態無變化即跳過，避免無謂的全螢幕重畫造成掃描線閃爍。
    DisplaySnapshot now = captureDisplaySnapshot();
    if (memcmp(&now, &lastDisplaySnapshot, sizeof(DisplaySnapshot)) == 0) {
        return;
    }

    // STEP 02: partial update — 倒數每秒只 tick 一格時，整片 fillScreen 會出掃描線閃爍。
    // 偵測「在 OHCA 倒數同 state 內，僅 countdownSec 改變」→ 只重繪時間區塊，不動 badge/label/counter。
    // 排除 ALARMING（半週期閃 phase 跟著變，必須走 full path 重畫整片紅 bg）。
    // 排除 modal overlay（confirm dialog/flash）— partial 不會重畫 overlay，會被時間區塊 fillRect 蓋掉
    constexpr uint16_t MODAL_FLAGS_MASK = 0x01    // showEpiArmedPrompt
                                        | 0x02    // showShockArmedPrompt
                                        | 0x04    // showAmioArmedPrompt
                                        | 0x100   // endConfirmShown
                                        | 0x200   // flashState.active
                                        | 0x2000; // resyncConfirmShown
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
                              && (now.endCheckCursor  == lastDisplaySnapshot.endCheckCursor)
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

    // SoT §16.7：已同步案件再次同步的確認 modal — 全螢幕取代總覽畫面。
    //   resyncConfirmShown 僅在 SUMMARY 內由 handleSummarySubmenuPrimary 設起，進
    //   SUMMARY 時亦會重置，故此處早退安全（不會蓋掉非 SUMMARY 畫面）。
    if (resyncConfirmShown) {
        drawResyncConfirmDialog();
        display.pushSprite(0, 0);
        return;
    }

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

    // STEP 03: pushSprite DMA 一次推到實體 TFT — 消除「fillScreen → 慢慢出文字」中間態
    display.pushSprite(0, 0);
}

