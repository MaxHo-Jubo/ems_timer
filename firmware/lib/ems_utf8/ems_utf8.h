// EMS DoseSync Pro — Impl-Phase G: UTF-8 字元邊界純邏輯
//
// 為什麼存在：drawDeviceInfo()（ui_screens.cpp，Task 5）裝置名稱超長時逐字元裁切
// 尾端直到符合螢幕寬度，裁切迴圈本身依賴 display.textWidth() 量測（不可測），
// 但「往回退一個完整 UTF-8 字元」這段純位元運算與 display 無關，依計畫 Global
// Constraints「純邏輯一律先抽到 lib/ 再由 src/ 呼叫，native test 測 lib/ 那份」
// 原則抽出（Phase G 全分支整合 review 抓到的 IMPORTANT：這段迴圈先前只在
// ui_screens.cpp 內，native build 排除 src/，完全無回歸保護，只靠一個沒進 repo
// 的獨立 g++ harness 手動驗證過）。
//
// 對應規格：docs/superpowers/specs/2026-09-01-phase-g-device-info-design.md §4.3
#ifndef EMS_UTF8_H
#define EMS_UTF8_H

#include <stddef.h>
#include <stdlib.h>

// UTF-8 continuation byte 的位元遮罩與比對值：最高兩位是 0b10（0x80~0xBF）
// 代表這個 byte 是某個多 byte 字元的延續部分，不是字元起始 byte。
#define UTF8_CONTINUATION_BYTE_MASK  0xC0
#define UTF8_CONTINUATION_BYTE_VALUE 0x80

/**
 * 判斷一個 byte 是否為 UTF-8 continuation byte（延續前一個多 byte 字元，
 * 不是字元起始 byte）。與 ems_settings.cpp 的 device_name_sanitize() 共用
 * ——兩處原本各自實作同一個位元判斷式（`(b & 0xC0) == 0x80`），依
 * EXTRACT-SHARED-HELPER 規則抽成這個共用 helper，避免其中一處修改判斷邏輯
 * 而另一處沒跟上，導致名稱輸入淨化與畫面截斷採用不同的字元邊界規則。
 *
 * @param b 待判斷的 byte
 * @return  true 表示是 continuation byte，false 表示不是
 */
inline bool utf8IsContinuationByte(unsigned char b) {
    // STEP 01: 用位元遮罩比對最高兩位是否為 0b10
    return (b & UTF8_CONTINUATION_BYTE_MASK) == UTF8_CONTINUATION_BYTE_VALUE;
}

/**
 * 從 UTF-8 字串目前的候選長度往回退一個完整字元，回傳退掉該字元後的新長度
 * （byte offset）。
 *
 * 用於「逐字元裁切字串直到符合某個外部限制（如螢幕寬度）」的迴圈：呼叫端
 * 每次拿目前候選長度 idx 呼叫本函式，取得的回傳值就是「去掉字串尾端最後一個
 * 完整 UTF-8 字元」後的新長度，可以直接用 `%.*s` 搭配這個長度輸出不會腰斬
 * 中文字造成亂碼。
 *
 * 演算法：先退一個 byte（字元本身至少佔 1 byte），若退到的位置落在 UTF-8
 * continuation byte（最高兩位 0b10）上，代表切在多 byte 字元中間，繼續往前退，
 * 直到落在字元起始 byte 或抵達字串開頭（idx==0）為止。
 *
 * @param s   要裁切的 UTF-8 字串。傳入 NULL 是呼叫端程式錯誤，觸發 abort()
 *            中止（fail-fast），不靜默回傳看似合法的邊界值——理由同下方
 *            idx 契約檢查。
 * @param idx 目前候選長度（byte offset）。idx == 0 代表字串已裁到空，呼叫端
 *            應在自己的迴圈邊界停止呼叫，不該再傳進來；傳入 0 一樣是呼叫端
 *            程式錯誤，觸發 abort() 中止，不像早期版本只在文件裡宣告契約、
 *            函式內部卻直接無條件遞減——那會讓違規呼叫在遞減後產生
 *            SIZE_MAX（下溢），下一輪 `s[idx]` 存取直接越界讀取，且找不到
 *            任何錯誤訊號可供除錯（Phase G 全分支整合 review 抓到的
 *            CRITICAL）。同 clampScrollOffset() 的既有慣例（ui_scroll.h）：
 *            內部 helper 用 abort() 而非 assert()，不受 -DNDEBUG 影響。
 * @return    退一個完整 UTF-8 字元後的新 byte offset（0 ~ idx-1）
 */
inline size_t utf8PrevCharBoundary(const char* s, size_t idx) {
    // STEP 01: 契約檢查——s == nullptr 或 idx == 0 都是呼叫端程式錯誤，
    //   fail-fast 中止而非靜默下溢或回傳看似合法的值
    if (s == nullptr || idx == 0) {
        // STEP 01.01: 中止行程，不回傳任何值
        abort();
    }
    // STEP 02: 先退一個 byte（字元本身至少佔 1 byte）
    idx--;
    // STEP 03: 若退到的位置落在 continuation byte 上，代表切在多 byte 字元中間，
    //   繼續往前退，直到落在字元起始 byte 或抵達字串開頭
    while (idx > 0 && utf8IsContinuationByte(static_cast<unsigned char>(s[idx]))) {
        // STEP 03.01: 略過這一個 continuation byte，繼續往前退
        idx--;
    }
    // STEP 04: idx 現在落在完整 UTF-8 字元的起始 byte（或字串開頭）
    return idx;
}

#endif  // EMS_UTF8_H
