// EMS DoseSync Pro — Impl-Phase F: 配對碼純函式 lib
//
// SoT 對齊：docs/EMS_DoseSync_Pro_Prototype_V1.md §16（配對碼）
// 工程規格：docs/pm-dev-spec.md §14（BLE 通訊）/ §16（配對碼流程）
// 計畫對應：docs/phase-f-web-validation-plan.md §7.1 / §10
// 測試入口：firmware/test/test_pairing/test_main.cpp
//
// 用途：
//   產生 4 位數字配對碼 + 120s TTL，驗證網頁端送回的碼。
//   配對失敗 3 次 lockout：必須重新 generate() 才能再次驗證。
//
// 設計：
//   純函式 + 純資料 struct，無 Arduino 依賴，可在 native env 跑 unit test。
//
// Threading 契約：
//   pairing_generate() 內含 static counter，**必須單一 task 呼叫**。
//   ESP32 BLE callback 跑在 GATT task，主迴圈跑在 loop task — 兩邊都要呼 generate
//   時必須先 enqueue 到主迴圈處理（對齊 feedback memory ble_callback_non_blocking）。
//   pairing_verify() 純讀寫傳入的 PairingCode，無內部狀態，跨 task 安全（前提：
//   呼叫端對同一 code 物件序列化存取）。

#pragma once

#include <cstddef>
#include <cstdint>

namespace ems {

// 配對碼 TTL：120 秒（SoT §16.4）
constexpr uint64_t PAIRING_TTL_MS = 120000;

// 配對碼長度（不含 null terminator）+ buffer 大小（含 '\0'）
constexpr size_t PAIRING_CODE_LEN = 4;
constexpr size_t PAIRING_CODE_BUF_SIZE = PAIRING_CODE_LEN + 1;

// 失敗 lockout 門檻（phase-f-plan §10）
constexpr uint8_t PAIRING_MAX_FAILURES = 3;

/**
 * 驗證結果列舉。
 *
 * bool 合併會讓 caller 無法區分四種失敗原因（lockout / 過期 / input 格式錯 / 數字錯），
 * F-3 case_sync_dispatcher 要分流 log 與 UI 提示（"格式錯誤" vs "密碼錯誤"），所以
 * 用 enum 顯式回傳。Mismatch 與 InvalidInput 對 failure_count 影響不同 — 見 verify()。
 */
enum class PairingVerifyResult : uint8_t {
    Ok            = 0,   // 驗證通過
    LockedOut     = 1,   // failure_count >= PAIRING_MAX_FAILURES
    Expired       = 2,   // now_ms >= expires_at_ms
    InvalidInput  = 3,   // input == nullptr / 長度 != PAIRING_CODE_LEN（不計 failure，視為 protocol bug）
    Mismatch      = 4,   // 長度正確但數字不符（計 failure_count++，視為 user typo / brute-force）
};

/**
 * 配對碼狀態。
 *
 * 欄位：
 *   digits         — 4 位 ASCII digit + '\0' 結尾，由 generate() 產生
 *   expires_at_ms  — 絕對到期時間（ms epoch），= generate now_ms + PAIRING_TTL_MS
 *   failure_count  — 失敗次數，達 PAIRING_MAX_FAILURES 後鎖定（verify 永遠回 LockedOut）
 */
struct PairingCode {
    char     digits[PAIRING_CODE_BUF_SIZE];
    uint64_t expires_at_ms;
    uint8_t  failure_count;
};

/**
 * 產生新配對碼。
 *
 * 連續呼叫（即使在同一 ms）必須產生不同碼，靠內部 monotonic counter 防碰撞。
 * 重設 failure_count = 0，故可作為 lockout 解除入口。
 *
 * @param now_ms  當前時間（ms epoch）
 * @return        新配對碼，expires_at_ms = now_ms + PAIRING_TTL_MS
 */
PairingCode pairing_generate(uint64_t now_ms);

/**
 * 驗證網頁端送回的碼。
 *
 * 失敗條件（按優先序）：
 *   1. LockedOut：failure_count >= PAIRING_MAX_FAILURES（**不再累加 failure_count**）
 *   2. Expired：now_ms >= expires_at_ms（**計 failure_count++**，避免攻擊者拖過期繞 lockout）
 *   3. InvalidInput：input == nullptr / input_len != PAIRING_CODE_LEN（**不計 failure**，
 *      視為 protocol bug，避免 malformed client 耗 user 的 lockout 額度）
 *   4. Mismatch：長度對但數字不符（**計 failure_count++**）
 *
 * Buffer 安全：
 *   API 強制接 (input, input_len) 不接 null-terminated string，避免 BLE 寫入未 \0 終止
 *   的 4-byte payload 導致 strlen OOB read（UB）。caller 應傳 BLE 寫入長度。
 *
 * 成功時 failure_count 保留累積值（不歸零，避免重複成功被當作 reset）；
 * caller 想清空計數請呼叫 pairing_generate() 重發新碼。
 *
 * @param code       配對碼狀態（mutable，會更新 failure_count）
 * @param input      網頁端送來的 byte buffer（可為 nullptr，會回 InvalidInput）
 * @param input_len  buffer 長度（不含 '\0'），必須 == PAIRING_CODE_LEN 才會進比對
 * @param now_ms     當前時間（ms epoch）
 * @return           PairingVerifyResult 列舉值
 */
PairingVerifyResult pairing_verify(PairingCode& code, const char* input,
                                   size_t input_len, uint64_t now_ms);

/**
 * 判斷配對碼是否過期。
 *
 * 邊界：now_ms == expires_at_ms 視為過期（inclusive，phase-f-todo.md F-1 boundary）
 *
 * @param code     配對碼狀態
 * @param now_ms   當前時間（ms epoch）
 * @return         true = 已過期
 */
bool pairing_is_expired(const PairingCode& code, uint64_t now_ms);

/**
 * 判斷是否已 lockout（3 次失敗後）。
 *
 * @param code  配對碼狀態
 * @return      true = 已 lockout，verify 永遠回 LockedOut 直到 generate 新碼
 */
bool pairing_is_locked_out(const PairingCode& code);

}  // namespace ems
