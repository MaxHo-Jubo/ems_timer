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
#include <stdlib.h>

// 從「可見列數」換算成「最後一項 0-based 索引」時要減去的固定量（=1）。
// 最後一項本身的索引是 visible_rows - 1，會隨 visible_rows 變動；本常數不是
// 那個變動值，是那個公式裡固定的「-1」。
#define LAST_VISIBLE_INDEX_OFFSET 1

/**
 * 捲動視窗跟隨游標的 clamp 計算（純函式，無副作用）。
 *
 * 游標超出可見視窗時，回傳讓游標重新落入視窗邊緣的新 offset；游標仍在視窗內
 * 則原樣回傳 offset（不動）。呼叫端負責把回傳值寫回自己的 scroll offset 全域。
 *
 * visible_rows == 0 是呼叫端違反函式契約——這是一個內部 helper，正常呼叫路徑的
 * visible_rows 恆為編譯期常數（如 SETTINGS_VISIBLE_ROWS），不該有執行期變動的
 * 可能。依 no-fallback-after-root-cause 原則，違反契約時立即中止，不靜默回傳
 * 看似合法的 offset——後者會讓呼叫端無從分辨「游標仍在視窗內」與「傳入無效
 * 參數」，兩者回傳值完全相同（原始版本 e5bb45d 的 CRITICAL silent-failure，
 * 2026-09-02 補跑 review 翻案取代）。用 abort() 而非 assert()：assert 在
 * -DNDEBUG 建置下會被完全編譯掉，届時本函式會靜默算出比原始 bug 更糟的值
 * （STEP 03 對 visible_rows==0 的減法會產生 cursor+1，不是原本的 offset 不變）；
 * abort() 不受 NDEBUG 影響，本專案目前雖未定義 NDEBUG，但這是一次成本為零的
 * 加固（見 8217679 review 的 IMPORTANT 發現）。
 *
 * @param cursor       目前游標值（清單中的索引）
 * @param offset       目前捲動視窗起點（目前顯示清單第幾筆開始）
 * @param visible_rows 可見視窗大小（一次顯示幾列，如 SETTINGS_VISIBLE_ROWS）；必須 > 0，
 *                     否則觸發 abort() 中止（fail-fast，不回傳任何值；不受
 *                     -DNDEBUG 影響）
 * @return             新的捲動視窗起點
 */
inline uint16_t clampScrollOffset(uint16_t cursor, uint16_t offset, uint8_t visible_rows) {
    // STEP 01: 契約檢查——visible_rows == 0 是呼叫端程式錯誤，fail-fast 中止而非
    //   靜默回傳 offset（避免呼叫端誤判為「游標仍在視窗內」的正常結果）。用
    //   abort() 而非 assert()，讓這個契約檢查不受 -DNDEBUG 影響。
    if (visible_rows == 0) {
        abort();
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
