// EMS DoseSync Pro — Phase E: LittleFS adapter for IStorageBackend
//
// 對應規格：plan ticklish-exploring-dragonfly.md §1 Partition layout + §3 Storage 模組
//
// 為什麼分檔：
//   - logic 層在 native test 跑（不能 include Arduino.h / LittleFS.h）
//   - 本檔只在 ESP32 build 編譯，提供把 IStorageBackend 綁到 LittleFS 的 factory
//   - 入口：emsStorage_fs_mount() 一次完成 mount + 建立 backend
#pragma once

#include "ems_storage_logic.h"

namespace ems {

/**
 * Mount LittleFS partition 並回填 backend 指標表。
 * 須先在 ESP32 上呼叫，且 partitions.csv 已有 "littlefs" partition。
 *
 * @param out_be  輸出：被連線到 LittleFS 的 backend
 * @return true 成功；false 表 mount 失敗（用 LittleFS.begin(true) 第一次會自動格式化）
 */
bool emsStorage_fs_mount(IStorageBackend* out_be);

/** 確保 /cases, /cases/ohca, /cases/training 三個目錄存在（mkdir -p） */
bool emsStorage_fs_ensure_dirs();

}  // namespace ems
