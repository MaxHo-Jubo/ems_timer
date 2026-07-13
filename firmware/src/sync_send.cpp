/**
 * sync_send.cpp — EMS DoseSync Pro — Impl-Phase F MVP3：case_sync chunked 真送
 *
 * 把已封版的純邏輯 lib 接成 SENDING state 的實際傳輸：
 *   storage 讀 case → caseSummary_build → case_sync_serialize_to_json
 *   → ble_chunker 切分 → BleNus::send() 定速逐 chunk notify。
 *
 * ACK 模型（self-ACK）：
 *   網頁端（ble-client.js）為 fire-and-forget，純以 '\n' 切句重組，無 per-chunk
 *   ACK。故韌體於每個 chunk notify 成功後「自行」dispatch CHUNK_ACKED 推進
 *   ems_sync_dispatcher，不需改動已封版且有 native test 覆蓋的
 *   dispatcher / chunker / serializer / pairing lib。
 *
 *   已知限制：BleNus::send() 的 notify() 是 fire-and-forget，回 true 僅代表已連線
 *   並送入 controller，不保證對端收到。若 controller TX 佇列壅塞而靜默丟封包，
 *   self-ACK 仍會推進、可能誤判 DONE。MVP3 以 SYNC_CHUNK_INTERVAL_MS 定速降低壅塞
 *   機率；真實吞吐與是否需 notify 完成回呼做真流控，待 F-4b 實機驗證。
 *
 * 對應：tasks/phase-f-todo.md F-3；docs/pm-dev-spec.md §14.1（payload schema）。
 */

#include "app_globals.h"

#include <cstring>

#include "case_sync_serializer.h"
#include "ble_chunker.h"

// ════════════════════════════════════════════════════════════════
// 模組內常數
// ════════════════════════════════════════════════════════════════

/**
 * case_sync JSON payload buffer 上限（bytes）。
 * 最壞情形 MAX_EVENTS(50) 筆事件 + summary + meta，measureJson 估算 < 8KB，
 * 取 10KB 留餘裕，確保 case_sync_serialize_to_json 不會因容量不足回 0。
 */
static constexpr size_t SYNC_PAYLOAD_BUF_MAX = 10240;

/**
 * chunk 推送定速間隔（ms）。
 * BLE 連線間隔偏好上限 22.5ms（ble_nus.h BLE_CONN_INTERVAL_MAX），取 20ms 讓每個
 * notify 大致有一個連線事件可送出，降低 controller TX 佇列壅塞丟封包的機率。
 * 註：連線間隔為偏好值、central 可協商更大值；20ms 為保守起點，實際最佳值
 * 待 F-4b 實機 throughput 校正。
 */
static constexpr uint32_t SYNC_CHUNK_INTERVAL_MS = 20;

// ════════════════════════════════════════════════════════════════
// 模組內狀態（file-static，不對外暴露；syncSendingPrepare 寫、syncSendingPump 讀）
// ════════════════════════════════════════════════════════════════

// 序列化後的 case_sync payload（含結尾 '\n'）
static char     s_payload_buf[SYNC_PAYLOAD_BUF_MAX];
// payload 實際長度（bytes，含 '\n'）
static size_t   s_payload_len = 0;
// 單一 chunk 容量（= att_mtu − ATT_HEADER_OVERHEAD），prepare 時依協商 MTU 鎖定
static size_t   s_chunk_size = 0;
// 上次送出 chunk 的時間（millis()），用於定速節流
static uint32_t s_last_chunk_ms = 0;
// 自 storage 載入的事件陣列（與顯示用全域 events[] 隔離，避免 SENDING 對顯示有副作用）
static ems_event_t s_events[MAX_EVENTS];
// storage_list metadata 掃描緩衝；大小取 OHCA 案件數上限，OHCA / training 兩種案件
// 型別共用此緩衝。static_assert 確保 training cap 不超過此容量，否則 build 期即報錯。
static case_meta_t s_meta_scan[EMS_STORAGE_OHCA_CAP];
static_assert(EMS_STORAGE_TRAINING_CAP <= EMS_STORAGE_OHCA_CAP,
              "s_meta_scan 以 OHCA cap 配置；training cap 超過將漏掃案件");

// ════════════════════════════════════════════════════════════════
// 模組內 helper
// ════════════════════════════════════════════════════════════════

/**
 * 準備失敗時把狀態機帶到 ERROR。
 *
 * ems_sync_dispatcher 無「準備失敗」專屬事件；BLE_DISCONNECTED 是唯一可由程式
 * 觸發、且在任一活躍 state 都轉 ERROR 的入口。配合 Serial log 確保失敗不靜默。
 */
static void abort_prepare_to_error() {
    // STEP 01: 清零 payload 狀態 —— 即使狀態機未如預期轉離 SENDING，syncSendingPump 的
    //   chunk_at 也會因 s_chunk_size == 0 取得空 range 而不送出 stale payload（防呆）
    s_payload_len = 0;
    s_chunk_size  = 0;
    // STEP 02: 借 BLE_DISCONNECTED 把狀態機帶到 ERROR（理由見上方 JSDoc）
    ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::BLE_DISCONNECTED, millis());
}

// ════════════════════════════════════════════════════════════════
// 公開 API（宣告於 app_globals.h，由 main.cpp loop observer 呼叫）
// ════════════════════════════════════════════════════════════════

/**
 * SENDING state 進入時呼叫一次：讀取 g_sync_target 指定的 case、序列化成
 * case_sync JSON payload、切 chunk，並設定 dispatcher 的 total_chunks。
 *
 * 失敗（storage 未就緒 / case 不存在 / 序列化失敗 / MTU 不可用）會 Serial log
 * 並把狀態機帶到 ERROR，不靜默成功。
 */
void syncSendingPrepare() {
    // STEP 01: storage 未就緒或無同步目標 → 無法準備 payload
    if (!g_storage_ready || g_sync_target.empty()) {
        Serial.printf("[SYNC] ERROR prepare aborted (ready=%d id=%s)\n",
                      (int)g_storage_ready,
                      g_sync_target.empty() ? "<empty>" : g_sync_target.id);
        abort_prepare_to_error();
        return;
    }

    // STEP 02: 從 storage 掃出 g_sync_target.id 對應的 case metadata（取案件起訖時間）
    const uint16_t meta_n = storage_list(&g_storage_be, g_sync_target.type,
                                         s_meta_scan, EMS_STORAGE_OHCA_CAP);
    const case_meta_t* meta = nullptr;
    for (uint16_t i = 0; i < meta_n; ++i) {
        if (strncmp(s_meta_scan[i].id, g_sync_target.id, EMS_STORAGE_ID_LEN) == 0) {
            meta = &s_meta_scan[i];
            break;
        }
    }
    if (meta == nullptr) {
        Serial.printf("[SYNC] ERROR case not found id=%s\n", g_sync_target.id);
        abort_prepare_to_error();
        return;
    }

    // STEP 03: 載入該 case 的事件陣列到模組緩衝（與顯示用 events[] 隔離）
    //   load 失敗以 0 事件續行：empty case 仍可序列化（serializer 已涵蓋），不靜默中止
    uint16_t event_count = 0;
    if (!storage_load_events(&g_storage_be, g_sync_target.type, g_sync_target.id,
                             s_events, MAX_EVENTS, &event_count)) {
        Serial.printf("[SYNC] WARN load_events failed id=%s, 視為 0 事件\n",
                      g_sync_target.id);
        event_count = 0;
    }

    // STEP 04: 聚合 case summary（與 drawOhcaSummary 同一純函式，case 起訖取自 meta）
    ohca_case_summary_t summary;
    caseSummary_build(&summary, s_events, event_count, meta->start_ms, meta->end_ms);

    // STEP 05: 序列化 case_sync JSON（schema 對齊 pm-dev-spec §14.1），保留 1 byte 給 '\n'
    ems::CaseSyncMeta js_meta;
    js_meta.case_id       = g_sync_target.id;
    js_meta.mode          = (g_sync_target.type == CASE_MODE_OHCA) ? "ohca" : "training";
    js_meta.device_name   = SYNC_DEVICE_NAME;
    js_meta.device_id     = SYNC_DEVICE_ID;
    js_meta.fw_version    = SYNC_FW_VERSION;
    js_meta.started_at_ms = meta->start_ms;
    js_meta.ended_at_ms   = meta->end_ms;
    const size_t json_len = ems::case_sync_serialize_to_json(
        s_payload_buf, SYNC_PAYLOAD_BUF_MAX - 1,
        js_meta, s_events, event_count, summary);
    if (json_len == 0) {
        // buffer 已依 MAX_EVENTS 最壞情形設計，理論上不會發生；防呆失敗即 ERROR
        Serial.println("[SYNC] FATAL serialize failed (payload buffer too small)");
        abort_prepare_to_error();
        return;
    }

    // STEP 06: 補行結尾分隔符（對齊網頁端 ble-client.js 以 '\n' 切句重組）
    s_payload_buf[json_len] = '\n';
    s_payload_len = json_len + 1;

    // STEP 07: 依協商後 MTU 算 chunk 容量與總數，設定 dispatcher total_chunks
    s_chunk_size = ems::chunk_size_from_mtu(g_ble.att_mtu());
    const size_t total = ems::chunk_count(s_payload_len, s_chunk_size);
    if (s_chunk_size == 0 || total == 0) {
        // s_chunk_size == 0 僅在 MTU < 4（不可用）發生；total == 0 另需 payload 為空，
        // 但 STEP 06 已保證 s_payload_len >= 1，故此分支實務上僅 MTU 不可用才觸發
        Serial.printf("[SYNC] FATAL chunk plan invalid (mtu=%u chunk=%u)\n",
                      (unsigned)g_ble.att_mtu(), (unsigned)s_chunk_size);
        abort_prepare_to_error();
        return;
    }
    ems::sync_dispatcher_set_total_chunks(&g_sync_ctx, (uint32_t)total);

    // STEP 08: 重置定速基準，pump 在下一個間隔送出第一個 chunk
    s_last_chunk_ms = millis();
    Serial.printf("[SYNC] prepare OK id=%s bytes=%u mtu=%u chunk=%u total=%u\n",
                  g_sync_target.id, (unsigned)s_payload_len,
                  (unsigned)g_ble.att_mtu(), (unsigned)s_chunk_size, (unsigned)total);
}

/**
 * SENDING state 期間每個 loop tick 呼叫：定速逐 chunk 推送 payload。
 *
 * 達節流間隔且仍有未送 chunk 時，取下一段 BleNus::send()；送出成功則自行
 * dispatch CHUNK_ACKED 推進狀態機（self-ACK，見檔頭 ACK 模型）。送出失敗
 * （BLE 已斷）不推進，由 loop observer 的 BLE 邊緣偵測 dispatch
 * BLE_DISCONNECTED → ERROR。
 */
void syncSendingPump() {
    // STEP 01: 全部 chunk 已送出（最後一個 chunk 的 CHUNK_ACKED 已在前次 tick 推動
    //   狀態機轉 DONE）→ 本 tick 不再送
    if (g_sync_ctx.sent_chunks_count >= g_sync_ctx.total_chunks) {
        return;
    }

    // STEP 02: 節流 — 未達定速間隔則本 tick 不送
    const uint32_t now = millis();
    if (now - s_last_chunk_ms < SYNC_CHUNK_INTERVAL_MS) {
        return;
    }

    // STEP 03: 取「下一個待送 chunk」range（sent_chunks_count 即下一個 chunk 索引）
    const ems::chunk_range_t range = ems::chunk_at(
        s_payload_len, s_chunk_size, g_sync_ctx.sent_chunks_count);
    if (range.len == 0) {
        // 防呆：索引越界（STEP 01 已擋，理論上不會走到）—— 真發生則 log 供除錯
        Serial.printf("[SYNC] WARN chunk_at empty range idx=%u\n",
                      (unsigned)g_sync_ctx.sent_chunks_count);
        return;
    }

    // STEP 04: notify 送出；成功才 self-ACK 推進狀態機，失敗留待 BLE 斷線偵測處理
    const uint8_t* chunk_ptr = reinterpret_cast<const uint8_t*>(s_payload_buf + range.offset);
    if (g_ble.send(chunk_ptr, range.len)) {
        ems::sync_dispatcher_dispatch(&g_sync_ctx, ems::SyncEvent::CHUNK_ACKED, now);
        s_last_chunk_ms = now;
    }
}
