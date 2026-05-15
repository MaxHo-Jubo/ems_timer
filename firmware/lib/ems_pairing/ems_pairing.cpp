// EMS DoseSync Pro — Impl-Phase F: 配對碼純函式 lib 實作
//
// 對應 header：ems_pairing.h
// 對應 test： firmware/test/test_pairing/test_main.cpp

#include "ems_pairing.h"

#include <cstdio>
#include <cstring>

namespace ems {

namespace {

// SplitMix64 演算法標準係數（Vigna 2018, "Further scramblings of Marsaglia's xorshift generators"）。
// 三個 odd 64-bit prime 用於 bit avalanche，把線性 input 散成 uniform output。
constexpr uint64_t SPLITMIX64_INCREMENT = 0x9E3779B97F4A7C15ULL;  // 黃金比例 2^64 倍數
constexpr uint64_t SPLITMIX64_MUL_1     = 0xBF58476D1CE4E5B9ULL;
constexpr uint64_t SPLITMIX64_MUL_2     = 0x94D049BB133111EBULL;

// counter 混淆乘數：任意 odd 64-bit 常數，把線性 counter (1,2,3,...)
// 散成 high-entropy seed，餵進 splitmix64 避免相鄰 counter 產相鄰輸出。
// 來源：手選 odd 質數樣式，可替換為任意 high bit-density odd 值。
constexpr uint64_t COUNTER_SCRAMBLE_MUL = 0xD1B54A32D192ED03ULL;

// SplitMix64 mixer — 把 (now_ms, counter) 兩個 64-bit 種子混成 uniform 隨機數。
// 用途：保證連續 generate（即使同 now_ms）產不同 4 位碼。
inline uint64_t splitmix64(uint64_t x) {
    x += SPLITMIX64_INCREMENT;
    x = (x ^ (x >> 30)) * SPLITMIX64_MUL_1;
    x = (x ^ (x >> 27)) * SPLITMIX64_MUL_2;
    return x ^ (x >> 31);
}

}  // namespace

PairingCode pairing_generate(uint64_t now_ms) {
    // STEP 01: 取下一個 monotonic counter（同 ms 連呼叫也保證不同碼）
    static uint64_t counter = 0;
    ++counter;

    // STEP 02: 把 counter 先 scramble 再與 now_ms XOR，餵 splitmix64 混成 4 位碼
    uint64_t scrambled_seed = now_ms ^ (counter * COUNTER_SCRAMBLE_MUL);
    uint64_t mixed = splitmix64(scrambled_seed);
    uint32_t four_digit = static_cast<uint32_t>(mixed % 10000);

    // STEP 03: 組裝 PairingCode（snprintf 自動補零 + null terminator）
    PairingCode code{};
    std::snprintf(code.digits, PAIRING_CODE_BUF_SIZE, "%04u",
                  static_cast<unsigned>(four_digit));
    code.expires_at_ms = now_ms + PAIRING_TTL_MS;
    code.failure_count = 0;
    return code;
}

PairingVerifyResult pairing_verify(PairingCode& code, const char* input,
                                   size_t input_len, uint64_t now_ms) {
    // STEP 01: lockout 優先序最高，不再累加 failure_count（避免 overflow + 不被過期/Mismatch 干擾）
    if (pairing_is_locked_out(code)) {
        return PairingVerifyResult::LockedOut;
    }

    // STEP 02: input 防護：nullptr / 長度不符 → InvalidInput（**不計 failure**）
    //   原因：BLE 寫入長度錯是 client protocol bug 或 attacker 探測，不該耗 user 的 3 次額度。
    //   先於 expired 判斷，避免 nullptr 走進 strlen / memcmp。
    if (input == nullptr || input_len != PAIRING_CODE_LEN) {
        return PairingVerifyResult::InvalidInput;
    }

    // STEP 03: 過期 → Expired，計 failure_count++（避免攻擊者拖過期繞 lockout）
    if (pairing_is_expired(code, now_ms)) {
        ++code.failure_count;
        return PairingVerifyResult::Expired;
    }

    // STEP 04: 數位逐字比對（memcmp 不依賴 '\0'，避開 BLE raw bytes 無終止符的 UB）
    if (std::memcmp(code.digits, input, PAIRING_CODE_LEN) != 0) {
        ++code.failure_count;
        return PairingVerifyResult::Mismatch;
    }

    // STEP 05: 成功 — 不重置 failure_count（避免重複成功被當作 reset）
    return PairingVerifyResult::Ok;
}

bool pairing_is_expired(const PairingCode& code, uint64_t now_ms) {
    // 邊界 inclusive：now == expires_at 視為過期（phase-f-todo.md F-1）
    return now_ms >= code.expires_at_ms;
}

bool pairing_is_locked_out(const PairingCode& code) {
    return code.failure_count >= PAIRING_MAX_FAILURES;
}

}  // namespace ems
