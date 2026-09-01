// EMS DoseSync Pro — Impl-Phase G: 捲動視窗 clamp 共用邏輯
//
// 為什麼存在：歷史紀錄清單（historyScrollOffset）與系統設定選單
// （settingsScrollOffset）都需要「游標移出可見視窗時視窗跟著捲」的同一種判斷。
// 依 EXTRACT-SHARED-HELPER 規則（同一概念判斷出現 2+ 呼叫點即該抽），抽成這個
// 純函式。Task 3 會把兩處呼叫點改用此函式，統一維護邏輯。
//
// 對應規格：docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md §3.2
#ifndef EMS_UI_SCROLL_H
#define EMS_UI_SCROLL_H

#include <stdint.h>

// 視窗內最後一項相對起點的索引偏移（0-indexed 下，視窗的最後一項是第 visible_rows-1 個）
#define LAST_VISIBLE_INDEX_OFFSET 1

/**
 * 捲動視窗跟隨游標的 clamp 計算（純函式，無副作用）。
 *
 * 游標超出可見視窗時，回傳讓游標重新落入視窗邊緣的新 offset；游標仍在視窗內
 * 則原樣回傳 offset（不動）。呼叫端負責把回傳值寫回自己的 scroll offset 全域。
 *
 * 無效輸入（visible_rows == 0）時回傳 offset 不變（不觸發捲動）。呼叫端應確保
 * visible_rows > 0——這是一個內部 helper，正常呼叫路徑的 visible_rows 恆為編譯期
 * 常數（如 SETTINGS_VISIBLE_ROWS），此保護只是避免無效輸入時的未定義行為。
 *
 * @param cursor       目前游標值（清單中的索引）
 * @param offset       目前捲動視窗起點（目前顯示清單第幾筆開始）
 * @param visible_rows 可見視窗大小（一次顯示幾列，如 SETTINGS_VISIBLE_ROWS）；必須 > 0
 * @return             新的捲動視窗起點
 */
inline uint16_t clampScrollOffset(uint16_t cursor, uint16_t offset, uint8_t visible_rows) {
    // STEP 01: 確保 visible_rows > 0，否則計算會產生整數下溢
    if (visible_rows == 0) {
        // STEP 01.01: 無效參數，回傳 offset 不變
        return offset;
    }
    // STEP 02: 游標在視窗上緣之上（游標值小於視窗起點）→ 視窗跟著往上捲，
    //   游標成為新視窗第一項
    if (cursor < offset) {
        // STEP 02.01: 回傳 cursor 作為新視窗起點
        return cursor;
    }
    // STEP 03: 游標在視窗下緣之下（游標值大於視窗起點 + 可見列數 - 1）→ 視窗跟著
    //   往下捲，游標成為新視窗最後一項；使用減法避免加法溢位
    if (cursor - offset >= visible_rows) {
        // STEP 03.01: 回傳讓 cursor 成為新視窗最後一項的起點
        return (uint16_t)(cursor - (visible_rows - LAST_VISIBLE_INDEX_OFFSET));
    }
    // STEP 04: 游標仍在視窗內，不動
    return offset;
}

#endif  // EMS_UI_SCROLL_H
