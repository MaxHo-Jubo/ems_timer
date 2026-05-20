#include "app_globals.h"


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
void drawOhcaCountdownTimeOnly(uint8_t ohcaStateForTime) {
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


/**
 * 子選單通用底部 hint（左：上下選擇／中：主鍵確認／右：返回取消）
 * 三段分別貼左右邊與置中，避免單行 + 全形空格時溢出 320px。
 */
void drawSubmenuNavHint() {
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

    // STEP 02.01: 同步狀態列（SoT §11.1「同步狀態：App未同步」/§16.6 「同步時間」）
    //   歷史模式從 historyCases mirror 取；現場模式從 g_ohca_live_synced_at_ms 取。
    //   時間顯示策略：synced_at_ms 需為真實 epoch（time_sync 對時後）才顯示 HH:MM；
    //   對時前用 millis() uptime 寫入會被當「日時」誤判，故只顯示「App已同步」不帶時間。
    {
        constexpr int16_t SYNC_STATUS_Y         = OHCA_BADGE_Y + 14;  // 標題下方一行
        constexpr int16_t SYNC_STATUS_LINE_H    = 14;                 // vlw 0.85x 行高
        const uint64_t synced_at = historySummaryMode
            ? (historyCursor < historyCount ? historyCases[historyCursor].synced_at_ms : 0)
            : g_ohca_live_synced_at_ms;
        const bool is_synced = (synced_at >= SYNCED_AT_EPOCH_FLOOR_MS);

        display.setTextSize(0.85f, 0.85f);
        display.setTextDatum(textdatum_t::top_center);
        if (!is_synced) {
            display.setTextColor(COLOR_TEXT_MUTED);
            display.drawString("同步狀態：App未同步", SCREEN_W / 2, SYNC_STATUS_Y);
        } else {
            display.setTextColor(COLOR_ACCENT_OK);
            display.drawString("同步狀態：App已同步", SCREEN_W / 2, SYNC_STATUS_Y);
            // is_synced 保證 synced_at >= EPOCH_FLOOR（有效 epoch），可直接算 HH:MM
            const uint32_t total_min = (uint32_t)((synced_at / 1000) / 60);
            const uint8_t  hh = (uint8_t)((total_min / 60) % 24);
            const uint8_t  mm = (uint8_t)(total_min % 60);
            char buf[24];
            snprintf(buf, sizeof(buf), "同步時間：%02u:%02u", hh, mm);
            display.setTextColor(COLOR_TEXT_MUTED);
            display.drawString(buf, SCREEN_W / 2, SYNC_STATUS_Y + SYNC_STATUS_LINE_H);
        }
    }

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
