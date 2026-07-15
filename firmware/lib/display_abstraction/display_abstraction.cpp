// EMS DoseSync Pro — Display mock 實作（native test 共用）
//
// 這個 .cpp 檔提供 display_abstraction.h 中 extern 宣告的 single definition，
// 確保所有 translation unit 共享同一份 mock state。

#include "display_abstraction.h"

// ============================================================
//  Mock state single definitions（extern，跨 translation unit 共享）
// ============================================================

int16_t mock_last_fill_x = 0;
int16_t mock_last_fill_y = 0;
int16_t mock_last_fill_w = 0;
int16_t mock_last_fill_h = 0;
uint32_t mock_last_fill_color = 0;

const char* mock_last_text = nullptr;
int16_t mock_last_x = 0;
int16_t mock_last_y = 0;
int16_t mock_last_fontsize = 0;
uint32_t mock_last_color = 0;
