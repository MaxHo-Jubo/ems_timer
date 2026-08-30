#include "app_globals.h"


/** 主功能表畫面：標題列（含 BLE 連線「BT」圖示）+ 分隔線 + 5 項可捲動選單（cursor 列白底黑字反白）。 */
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
    if (g_ble.connected()) {
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
    // top_left datum: (x, y) = 字 top-left 角，與下方 fillRect 的 raw Y 同座標系。
    //   走 setCursor + print 在 vlw 下會 baseline-relative，與 highlight 框錯開。
    display.setTextDatum(textdatum_t::top_left);
    for (uint8_t i = 0; i < MAIN_MENU_COUNT; i++) {
        const int16_t y = MENU_Y_START + i * MENU_ROW_H;
        if (i == mainMenuCursor) {
            // STEP 03.01: cursor highlight — 白底全寬橫條，黑字
            display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        display.drawString(MAIN_MENU_LABELS[i], MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
    }
}

/** 共用：水平置中文字（top_center datum，y = top 對齊） */
void drawCenteredText(const char* text, int16_t y, uint16_t color) {
    display.setTextColor(color);
    display.setTextDatum(textdatum_t::top_center);
    display.drawString(text, SCREEN_W / 2, y);
}

/** 共用垂直選單 widget：標題 + 分隔線 + 選項列 + 底部提示
 *
 * 抽取自 drawDrugMenu / drawBackfillType / drawBackfillCount /
 * drawTrainingSetup / drawTrainingSave / drawTrainingHistoryOptions / drawQuickMenu
 * 的共同結構，消除 6 處重複的選單渲染邏輯。
 *
 * @param title     標題文字
 * @param labels    選項標籤陣列（呼叫端負責宣告，大小由 count 決定）
 * @param count     選項數量（≤5）
 * @param cursor    當前游標位置
 * @param rowH      單列高度
 * @param yStart    第一列起始 Y 座標
 * @param hint      底部提示文字（nullptr 略過）
 */
void drawVerticalMenu(const char* title, const char* labels[], uint8_t count,
                      uint8_t cursor, uint16_t rowH, int16_t yStart,
                      const char* hint) {
    // STEP 01: 標題列
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    drawCenteredText(title, OHCA_BADGE_Y, COLOR_ACCENT_OK);
    display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

    // STEP 02: 選項列
    constexpr int16_t MENU_TEXT_PAD      = 32;
    constexpr int16_t MENU_TEXT_OFFSET_Y = 6;
    display.setTextSize(1);
    for (uint8_t i = 0; i < count; i++) {
        // STEP 02.01: 游標高亮列
        const int16_t y = yStart + i * rowH;
        if (i == cursor) {
            display.fillRect(0, y, SCREEN_W, rowH, COLOR_TEXT_PRIMARY);
            display.setTextColor(COLOR_BG);
        } else {
            display.setTextColor(COLOR_TEXT_PRIMARY);
        }
        // STEP 02.02: 繪製文字
        display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
        display.print(labels[i]);
    }

    // STEP 03: 底部提示
    if (hint != nullptr) {
        drawCenteredText(hint, SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
    }
}

/**
 * Phase F MVP2：同步資料 6 個 state 畫面渲染。
 *
 * 字級對齊既有 OHCA 螢幕：vlw 中文 size 1.2~1.8x、ASCII 配對碼用 vlw 2.5x（≈60px）。
 * ERROR reason 由 caller 從 pairing_code.failure_count 推導（dispatcher 不存原因）。
 */
void drawSyncScreen() {
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
            // SoT §16.4 配對碼畫面：標題 + 4 位大字 + 提示 + 剩餘秒數倒數
            display.setTextSize(1.2f, 1.2f);
            drawCenteredText("App 配對碼", 30, COLOR_TEXT_PRIMARY);
            // 4 位配對碼大字（vlw size 2.5 ≈ 60px 等寬，足以遠距讀取）
            display.setTextSize(2.5f, 2.5f);
            display.setTextColor(COLOR_ACCENT_OK);
            display.setTextDatum(textdatum_t::middle_center);
            display.drawString(g_sync_ctx.pairing_code.digits, SCREEN_W / 2, SCREEN_H / 2);
            // 提示與倒數（per-frame 由 captureDisplaySnapshot 覆寫 countdownSec 觸發每秒重繪）
            display.setTextSize(1.0f, 1.0f);
            drawCenteredText("請於 App 輸入", 180, COLOR_TEXT_MUTED);
            const uint32_t remaining_sec = ems::pairing_remaining_sec(
                g_sync_ctx.pairing_code, (uint64_t)millis());
            char buf[32];
            snprintf(buf, sizeof(buf), "剩餘 %u 秒", (unsigned)remaining_sec);
            drawCenteredText(buf, 210, COLOR_TEXT_MUTED);
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
            // chunk 進度（sent / total）；captureDisplaySnapshot 以 sent_chunks_count
            // 覆寫 countdownSec 觸發 dedup miss，故 SENDING 期間此畫面會隨進度重繪。
            display.setTextSize(1.5f, 1.5f);
            drawCenteredText("傳輸中…", 80, COLOR_TEXT_PRIMARY);
            char buf[32];
            snprintf(buf, sizeof(buf), "%u / %u",
                     (unsigned)g_sync_ctx.sent_chunks_count,
                     (unsigned)g_sync_ctx.total_chunks);
            display.setTextSize(1.2f, 1.2f);
            drawCenteredText(buf, 150, COLOR_TEXT_MUTED);
            break;
        }
        case ems::SyncState::DONE: {
            // SoT §16.5「同步完成 / 本案件已傳輸」
            display.setTextSize(1.5f, 1.5f);
            drawCenteredText("同步完成", 60, COLOR_ACCENT_OK);
            display.setTextSize(1.2f, 1.2f);
            drawCenteredText("本案件已傳輸", 130, COLOR_TEXT_PRIMARY);
            break;
        }
        case ems::SyncState::ERROR: {
            // SoT §16.4 配對碼逾時 / §16.9 同步失敗 — dispatcher 不存原因，由 caller 看
            // failure_count + expires_at_ms 兩個訊號分流（無法精確區分 30s 主鍵 timeout vs BLE 斷）
            display.setTextSize(1.5f, 1.5f);
            const uint64_t now_ms = (uint64_t)millis();
            const uint64_t exp_ms = g_sync_ctx.pairing_code.expires_at_ms;
            const bool locked_out = g_sync_ctx.pairing_code.failure_count >= ems::PAIRING_MAX_FAILURES;
            const bool expired    = (exp_ms > 0) && (exp_ms <= now_ms);
            // 第四路：START 失敗或 pre-AWAITING_INPUT 直接 ERROR → pairing_code 全 0
            //   區分「啟動失敗」與「連線中斷」避免誤導救護員
            const bool start_failed = (exp_ms == 0) && (g_sync_ctx.pairing_code.failure_count == 0);
            const char* title;
            const char* detail;
            if (locked_out) {
                title  = "輸入錯誤鎖定";
                detail = "請從案件總覽重試";
            } else if (expired) {
                title  = "配對碼已失效";
                detail = "請重新產生";
            } else if (start_failed) {
                title  = "啟動失敗";
                detail = "請確認 BLE 與儲存";
            } else {
                title  = "同步失敗";
                detail = "連線中斷";
            }
            drawCenteredText(title, 50, COLOR_ACCENT_ALERT);
            display.setTextSize(1.2f, 1.2f);
            drawCenteredText(detail, 120, COLOR_TEXT_PRIMARY);
            display.setTextSize(1.0f, 1.0f);
            drawCenteredText("即將回案件總覽", 200, COLOR_TEXT_MUTED);
            break;
        }
        case ems::SyncState::IDLE:
        default:
            // observer 已將 globalState 設回 MAIN_MENU，理論上不會走到這
            break;
    }
}


/** OHCA 歷史案件列表：標題附總數，無資料顯示提示，否則列出最多 5 筆（id 後 5 位／EPI／電擊／同步狀態），cursor 列高亮。 */
void drawHistoryList() {
    useZhFont();
    char buf[64];

    // STEP 00: W6 歷史分類層（OHCA / Training 選擇）
    if (ohcaSubState == SUBSTATE_HISTORY_CATEGORY) {
        display.setTextSize(1.2f, 1.2f);
        drawCenteredText("選擇歷史類型", OHCA_BADGE_Y, COLOR_ACCENT_OK);
        display.drawLine(16, 56, SCREEN_W - 16, 56, COLOR_TEXT_DIM);

        const char* labels[2] = { "OHCA 案件", "Training 紀錄" };
        constexpr int16_t MENU_Y_START       = 80;
        constexpr int16_t MENU_ROW_H         = 40;
        constexpr int16_t MENU_TEXT_PAD      = 32;
        constexpr int16_t MENU_TEXT_OFFSET_Y = 8;
        display.setTextSize(1);
        for (uint8_t i = 0; i < 2; i++) {
            const int16_t y = MENU_Y_START + i * MENU_ROW_H;
            if (i == historyTypeCursor) {
                display.fillRect(0, y, SCREEN_W, MENU_ROW_H, COLOR_TEXT_PRIMARY);
                display.setTextColor(COLOR_BG);
            } else {
                display.setTextColor(COLOR_TEXT_PRIMARY);
            }
            display.setCursor(MENU_TEXT_PAD, y + MENU_TEXT_OFFSET_Y);
            display.print(labels[i]);
        }
        drawCenteredText("主鍵確認  返回 主功能表",
                         SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
        return;
    }

    // STEP 01: 標題（對齊 demo V1 §12：總數附在標題後）
    //   vlw 未收錄全形括號（`（）`），用半形避字型缺字風險
    const char* type_label = (g_history_type == CASE_MODE_TRAINING) ? "Training" : "OHCA";
    snprintf(buf, sizeof(buf), "%s 案件 (%u)", type_label, (unsigned)historyCount);
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

        // 顯示「id 後 5 位 ｜ EPI N ｜ 電擊 N ｜ 同步狀態」（SoT §16.6 歷史紀錄含同步狀態）
        //   「同」字為「已同步」縮寫；vlw 字型既有，避免引入新字元擴子集
        const char* sync_mark = case_meta_is_synced(m) ? "  同" : "";
        snprintf(buf, sizeof(buf), "#%s  EPI %u  電擊 %u%s",
                 m.id + (EMS_STORAGE_ID_LEN - 1 - HISTORY_ID_DISPLAY_TAIL),
                 (unsigned)case_meta_epi_total(m),
                 (unsigned)case_meta_shock_total(m),
                 sync_mark);
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

/** 未實作功能的佔位畫面：標題 + Phase 標記 + 「尚未實作」提示。 */
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
    // 標題（OHCA / Training 依 mode 分支）
    {
        char title[48];
        snprintf(title, sizeof(title), "事件時間軸｜%s",
                 (g_case_mode == CASE_MODE_TRAINING) ? "Training" : "OHCA");
        useZhFont();
        display.setTextSize(1.2f, 1.2f);
        drawCenteredText(title, 20, COLOR_ACCENT_OK);
    }
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

/** 獨立 6 秒通氣節奏主畫面（V1 §13.6 執行中畫面 / §13.12 暫停畫面） */
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

    // V1 §14.9：動態 3 / 4 項（B3：加「案件簡版總覽」對齊 demo）；W8 Training 末項加「重置訓練」
    const bool is_training = (g_case_mode == CASE_MODE_TRAINING);
    const char* labels[5];  // 最多 5：vent 已開(4) + Training 重置(1)
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
    // W8：Training 模式末項加「重置訓練」（對齊 input_handler QUICK_MENU resetIdx = cnt-1）
    if (is_training) {
        labels[count] = "重置訓練";
        count++;
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
    drawCenteredText("上下選擇 主鍵確認 返回關閉",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/** W3：Training 倒數選擇畫面（30 秒 / 1 分鐘 / 4 分鐘） */
void drawTrainingSetup() {
    // STEP 01: 共用垂直選單 widget（3 選項，30s / 60s / 240s）
    const char* labels[3] = { "30 秒", "1 分鐘", "4 分鐘" };
    drawVerticalMenu("訓練倒數", labels, 3, trainingSetupCursor, 36, 78,
                     "上下選擇  主鍵確認  返回 主功能表");
}

/** W5：Training 保存選單（保存 / 不保存） */
void drawTrainingSave() {
    // STEP 01: 共用垂直選單 widget（2 選項）
    const char* labels[2] = { "保存", "不保存" };
    drawVerticalMenu("保存訓練紀錄？", labels, 2, trainingSaveCursor, 40, 80,
                     "上下選擇  主鍵確認  返回不保存");
}

/** W7：Training 歷史操作選單（查看總覽 / 同步 / 刪除 / 返回） */
void drawTrainingHistoryOptions() {
    // STEP 01: 共用垂直選單 widget（4 選項）
    const char* labels[4] = { "查看總覽", "同步至 App", "刪除此訓練紀錄", "返回" };
    drawVerticalMenu("訓練紀錄操作", labels, 4, trainingHistoryOptionsCursor, 36, 78,
                     "上下選擇  主鍵確認  返回");
}

/** W9：儲存失敗提示（紅字「存檔失敗」+ 重試按鈕，無聲音）
 *
 * 呼叫時機：OHCA 案件結束進入 LOCKED 但 storage_save_case 失敗時，
 *   在 SUMMARY 畫面下方顯示紅字警告，提示救護員按主鍵重試。
 *   不觸發蜂鳴器（避免救護現場噪音干擾）。
 *
 * @param type 案件類型（OHCA / Training），用於 log 輸出
 */
void drawStorageFailure(CaseMode type) {
    // STEP 01: 底部警告列（紅色背景 + 白字）
    display.fillRect(0, SCREEN_H - 56, SCREEN_W, 56, COLOR_ACCENT_ALERT);
    display.setTextSize(1.2f, 1.2f);
    display.setTextColor(COLOR_TEXT_PRIMARY);
    display.setTextDatum(textdatum_t::middle_center);

    const char* msg = "存檔失敗  按主鍵重試";
    display.drawString(msg, SCREEN_W / 2, SCREEN_H - 28);
}

/** W7 / W8：二次確認對話框（通用） */
void drawConfirmDialog(const char* title, const char* body) {
    useZhFont();
    display.setTextSize(1.2f, 1.2f);
    display.setTextColor(COLOR_ACCENT_WARN, COLOR_BG);
    display.fillRect(24, 60, SCREEN_W - 48, 120, COLOR_BG);
    display.drawRect(24, 60, SCREEN_W - 48, 120, COLOR_ACCENT_WARN);
    display.setTextColor(COLOR_TEXT_PRIMARY);
    drawCenteredText(title, 100, COLOR_ACCENT_WARN);
    display.setTextSize(1);
    // body 可能含單一 '\n' 需分兩行置中（drawCenteredText 不處理換行，直印會橫向溢框）
    const char* nl = strchr(body, '\n');
    if (nl != nullptr) {
        constexpr size_t DIALOG_LINE_MAX = 48;  // 對話框單行最大位元組數
        char line1[DIALOG_LINE_MAX];
        size_t len1 = (size_t)(nl - body);
        if (len1 >= sizeof(line1)) {
            len1 = sizeof(line1) - 1;
        }
        memcpy(line1, body, len1);
        line1[len1] = '\0';
        drawCenteredText(line1, 122, COLOR_TEXT_MUTED);   // 兩行版上行
        drawCenteredText(nl + 1, 146, COLOR_TEXT_MUTED);  // 兩行版下行
    } else {
        drawCenteredText(body, 130, COLOR_TEXT_MUTED);    // 單行版
    }
    drawCenteredText("主鍵確認  返回取消",
                     SCREEN_H - OHCA_COUNTER_BOTTOM - 8, COLOR_TEXT_DIM);
}

/**
 * 右上角電量圖示（Impl-Phase H）：四格電量條 + 低電量閃爍 + 充電中疊加閃電符號。
 * 於 presentFrame() 內對所有畫面統一繪製；燃料計不在線時完全不畫（連外框都不畫，
 * 因為畫空電池會被讀成沒電）。
 *
 * @param lowBlinkOn 低電量閃爍的目前相位（true=亮相位）。⚠️ 這**不是**「該不該畫圖示」
 *                   的開關——它只在「低電量」情境下才決定畫或不畫；非低電量時
 *                   compute_low_battery_blink_on() 恆回 false，此時圖示仍要正常
 *                   畫出來，不可拿 lowBlinkOn==false 當跳過繪製的條件（2026-08-23
 *                   controller 驗證抓到的實際回歸：曾誤寫成單獨的
 *                   `if (!lowBlinkOn) return;`，導致電量正常時圖示完全不會出現）。
 *                   真正「該不該畫」的判斷見 STEP 03 的 ems::should_draw_battery_icon()。
 *                   相位本身由呼叫端從 DisplaySnapshot 的 SNAP_FLAG_BATTERY_LOW_BLINK
 *                   bit 傳入，本函式不自行重算 millis()：snapshot 擷取時間與繪製
 *                   時間若各自算一次相位，兩者跨越 500ms 邊界時會不同步
 *                   （snapshot 記亮、畫面畫暗）。
 */
void drawBatteryIcon(bool lowBlinkOn) {
    // STEP 01: 計算外框位置（右緣對齊，往左展開），並算出含正極頭的完整佔用矩形
    const int16_t body_x = SCREEN_W - BATTERY_ICON_RIGHT_MARGIN
                         - BATTERY_ICON_TIP_W - BATTERY_ICON_BODY_W;
    const int16_t body_y = BATTERY_ICON_Y;
    const int16_t icon_w = BATTERY_ICON_BODY_W + BATTERY_ICON_TIP_W;  // 含正極頭的總寬
    const int16_t icon_h = BATTERY_ICON_BODY_H;  // 正極頭高度 < 外框高度，外框範圍已含蓋

    // STEP 02: 先清除圖示佔用區域——updateDisplay() 的 partial update 路徑
    // （inCountdownGroup && sameStateAsLast 分支）不經過外層 clearDisplay() 就推送，
    // 圖示不自己清背景會在倒數畫面上疊字；閃爍暗相位若不清除，上一次畫的圖示會殘留成常亮
    display.fillRect(body_x, body_y, icon_w, icon_h, COLOR_BG);

    // STEP 03: 決定本次是否該繼續畫內容——不在線、或低電量暗相位都不畫，只清背景
    // 就返回。判斷邏輯抽進 ems::should_draw_battery_icon()（純函式、native 測試
    // 涵蓋），不在這裡用兩個獨立 if 各自判斷：lowBlinkOn 在非低電量時恆為
    // false，先前版本寫成 `if (!lowBlinkOn) return;` 會把「電量正常」誤判成
    // 「該跳過繪製」，圖示變成只有低電量亮相位才出現，與 Task 9 的目的正好相反
    // （2026-08-23 controller 驗證時抓到）
    if (!ems::should_draw_battery_icon(g_battery_percent, g_battery_low, lowBlinkOn)) {
        return;
    }

    // STEP 04: 低電量用警示色，其餘用一般前景色
    const uint16_t color = g_battery_low ? COLOR_ACCENT_ALERT : COLOR_TEXT_MUTED;

    // STEP 05: 畫外框與正極頭
    display.drawRect(body_x, body_y, BATTERY_ICON_BODY_W, BATTERY_ICON_BODY_H, color);
    display.fillRect(body_x + BATTERY_ICON_BODY_W,
                     body_y + (BATTERY_ICON_BODY_H - BATTERY_ICON_TIP_H) / 2,
                     BATTERY_ICON_TIP_W, BATTERY_ICON_TIP_H, color);

    // STEP 06: 依電量填格（格與格之間留 BATTERY_ICON_SEGMENT_GAP_PX 間隙）。
    // 分界邏輯抽在 ems::battery_segments_for_percent()（lib/ems_fuel_gauge），
    // native 測試涵蓋每個分界兩側，UI 層本身不重算門檻。STEP 03 已保證
    // g_battery_percent 不是哨兵值（should_draw_battery_icon 為 true 時必然在線），
    // 這裡可直接傳入。可用寬度/高度的雙邊合計扣除量由單邊留白
    // BATTERY_ICON_SEGMENT_INSET_PX 推導（× 2），不另外寫一個裸數字
    const uint8_t segments = ems::battery_segments_for_percent(g_battery_percent);
    const int16_t seg_w = (BATTERY_ICON_BODY_W - BATTERY_ICON_SEGMENT_INSET_PX * 2)
                         / BATTERY_ICON_SEGMENTS;
    for (uint8_t i = 0; i < segments; i++) {
        display.fillRect(body_x + BATTERY_ICON_SEGMENT_INSET_PX + i * seg_w,
                         body_y + BATTERY_ICON_SEGMENT_INSET_PX,
                         seg_w - BATTERY_ICON_SEGMENT_GAP_PX,
                         BATTERY_ICON_BODY_H - BATTERY_ICON_SEGMENT_INSET_PX * 2,
                         color);
    }

    // STEP 07: 充電中在電量格上疊一個幾何閃電符號。不可用文字 "⚡"：字型來源
    // STHeiti Medium.ttc 未含該字元，且 vlw 字集是掃描原始碼字串 union 生成的
    // PROGMEM header，改字串不重跑 regen_vlw.sh 就會實機缺字（2026-05-25 教訓）。
    // 改用兩個三角形拼出的折線形狀，畫在電量格內部（16×6px 可用空間），
    // 顏色用 COLOR_ACCENT_OK 與電量條顏色區隔
    if (g_battery_charge_state == ems::ChargeState::Charging) {
        constexpr int16_t BOLT_W = 8;  // 閃電圖形寬度（px），小於可用空間 16px
        constexpr int16_t BOLT_H = 6;  // 閃電圖形高度（px），等於電量格內部高度（BODY_H-4）
        // STEP 07.01: 閃電外框水平置中於電量格內部：(16-8)/2=4px 內縮
        const int16_t bolt_x = body_x + 2 + (BATTERY_ICON_BODY_W - 4 - BOLT_W) / 2;
        const int16_t bolt_y = body_y + 2;

        // STEP 07.02: 上半三角形（頂點在右上，底邊落在垂直中線）+
        // 下半三角形（頂邊落在垂直中線，頂點在左下），兩者共用中線交錯出 Z 字折線，
        // 是最小可辨識的閃電輪廓
        display.fillTriangle(bolt_x + BOLT_W,         bolt_y,
                             bolt_x,                   bolt_y + BOLT_H / 2,
                             bolt_x + BOLT_W * 5 / 8,   bolt_y + BOLT_H / 2,
                             COLOR_ACCENT_OK);
        display.fillTriangle(bolt_x + BOLT_W * 3 / 8,  bolt_y + BOLT_H / 2,
                             bolt_x + BOLT_W,           bolt_y + BOLT_H / 2,
                             bolt_x,                    bolt_y + BOLT_H,
                             COLOR_ACCENT_OK);
    }
}

/** §13.16 低電量提示 panel 左右內距（px）：文字包絡與 panel 邊框之間的留白 */
constexpr int16_t NOTICE_PANEL_PADDING_X = 20;
/** §13.16 低電量提示 panel 上下內距（px），與 NOTICE_PANEL_PADDING_X 分開命名，
 *  方便未來上下/左右各自微調排版而不互相牽動 */
constexpr int16_t NOTICE_PANEL_PADDING_Y = 16;
/** §13.16 低電量提示兩行文字之間的垂直間距（px）：與量測出的 line_h 相加即兩行中心距。
 *  不把「兩行中心距」寫死成單一定值（例如 28~32px）——那個總距離取決於實際字高，
 *  字高已用 display.fontHeight() 動態量測，這裡只需再加一段最小留白，
 *  字型或縮放改變時兩行仍不會咬到（2026-08-23 fix round 1 D1 裁決） */
constexpr int16_t NOTICE_LINE_GAP_PX = 8;

/**
 * §13.16 低電量提示：首次低電量事件會被保留，並在案件進行中的下一次 tick 被消費後
 * 顯示 3 秒——不要求「跨進門檻」與「進入案件」是同一個當下：低電量事件可能在純
 * 選單瀏覽等非適用情境下先發生並被 latch 記住，之後才進 OHCA/VENT，此時會在進入
 * 後的下一輪 tick 立即消費並顯示（見 ems::low_battery_notice_tick() 的守衛說明）。
 *
 * 依規格不發聲。是否仍在顯示期間由 captureDisplaySnapshot() 經
 * ems::is_low_battery_notice_visible() 算好並存進 snapshot flag，本函式不自行呼叫
 * millis()——理由與 drawBatteryIcon() 的相位處理相同：擷取時間點與繪製時間點若各自
 * 取樣，跨越邊界時 snapshot 與畫面會不同步，且「顯示中」這個狀態不進 snapshot 就
 * 不會觸發重繪（DisplaySnapshot 漏欄位已連踩 5 次）。
 *
 * 文字前先畫不透明背景 panel，不能只用透明文字疊在既有畫面上——OHCA 的 96px 大倒數
 * （y≈100，字塊縱向約 y=52~148）與獨立通氣大數字（y=136）都與提示原本的 y=110/130
 * 重疊，不只兩層字互相覆寫讀不出來，更嚴重的是會把案件進行中的主要臨床讀數整段遮掉
 * 3 秒（2026-08-23 fix round 1 A1 裁決；嚴重度由第二輪 review 補充）。此處沿用同檔
 * drawConfirmDialog()（`:776-782`）既有的 fillRect(COLOR_BG) + drawRect(COLOR_ACCENT_WARN)
 * 背景填充與警示邊框模式，不另造一套視覺模式。panel 尺寸用
 * display.textWidth()/fontHeight() 動態量測兩行文字，不硬編字寬字高——
 * 文字內容或字型改變時 panel 仍會正確包住文字，兩行本身也不會咬到（見 NOTICE_LINE_GAP_PX
 * 註解）。panel 置中於畫面中央，與右上角電量圖示（y < BATTERY_ICON_Y +
 * BATTERY_ICON_BODY_H，遠小於畫面中央）不會重疊。
 *
 * @param visible 本次是否要畫（來自 snapshot 的 SNAP_FLAG_LOW_BATTERY_NOTICE bit，
 *                由 captureDisplaySnapshot() 經 ems::is_low_battery_notice_visible()
 *                算好；本函式不可自行判斷「該不該顯示」）
 */
void drawLowBatteryNotice(bool visible) {
    // STEP 01: 不在顯示期間就不畫
    if (!visible) {
        return;
    }

    // STEP 02: 先設字型與大小，textWidth()/fontHeight() 才能量出正確的縮放後尺寸
    useZhFont();
    display.setTextSize(1);
    display.setTextDatum(textdatum_t::middle_center);

    // STEP 03: 動態量測兩行文字寬度，取較寬者決定 panel 寬度
    const char* line1 = "低電量";
    const char* line2 = "建議接上行動電源";
    const int16_t line1_w = static_cast<int16_t>(display.textWidth(line1));
    const int16_t line2_w = static_cast<int16_t>(display.textWidth(line2));
    const int16_t line_w  = (line1_w > line2_w) ? line1_w : line2_w;
    const int16_t line_h  = static_cast<int16_t>(display.fontHeight());

    // STEP 04: panel 尺寸 = 文字包絡 + 內距，置中於畫面中央
    const int16_t panel_w = line_w + NOTICE_PANEL_PADDING_X * 2;
    const int16_t panel_h = line_h * 2 + NOTICE_LINE_GAP_PX + NOTICE_PANEL_PADDING_Y * 2;
    const int16_t panel_x = (SCREEN_W - panel_w) / 2;
    const int16_t panel_y = (SCREEN_H - panel_h) / 2;

    // STEP 05: 不透明背景 + 警示色外框，蓋住畫面上原本可能重疊的大字
    display.fillRect(panel_x, panel_y, panel_w, panel_h, COLOR_BG);
    display.drawRect(panel_x, panel_y, panel_w, panel_h, COLOR_ACCENT_WARN);

    // STEP 06: 置中兩行提示文字，垂直位置依量測出的 line_h 排版（非硬編偏移量）
    display.setTextColor(COLOR_ACCENT_WARN);
    const int16_t line1_center_y = panel_y + NOTICE_PANEL_PADDING_Y + line_h / 2;
    const int16_t line2_center_y = line1_center_y + line_h + NOTICE_LINE_GAP_PX;
    display.drawString(line1, SCREEN_W / 2, line1_center_y);
    display.drawString(line2, SCREEN_W / 2, line2_center_y);
}

/**
 * §20.3 低電量開案確認：低電量仍允許開始 OHCA／通氣／Training，但需明確確認。
 * 選「是」啟動、選「否」取消，三個目標顯示完全相同文字（spec 沒有分別措辭），
 * 呼叫端不需要傳入目標——畫面本身不需要知道畫給誰看。
 * @param  無參數
 * @return void（純繪製，無回傳值）
 */
void drawLowBatteryStartConfirm() {
    // STEP 01: 複用既有二次確認對話框 widget，三個目標共用同一組文字
    drawConfirmDialog("低電量", "建議接上行動電源\n是否開始？");
}
