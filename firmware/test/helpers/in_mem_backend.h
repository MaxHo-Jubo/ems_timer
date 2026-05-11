// EMS DoseSync Pro — Phase E test helper: InMemoryBackend
//
// 提供 IStorageBackend 的記憶體實作，供 native test 注入。
// 採 header-only + static 全域狀態（每個翻譯單元獨立），由 setUp() 呼叫 reset 清空。
//
// 為何 header-only：避免在 test/ 下放非 test_* 目錄帶來的 PlatformIO 探測歧義；
// 此 header 由各 test_main.cpp 各自 include 一份（lifetime 隔離，互不影響）。
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <map>
#include <string>
#include <vector>

#include "ems_storage_logic.h"

// ============================================================
//  InMemoryBackend：以 std::map<path, bytes> 模擬檔案系統
// ============================================================

struct InMemFs {
    std::map<std::string, std::vector<uint8_t>> files;
};

namespace test_helpers {

// 把 ems_storage_logic.h 內定義的 IStorageBackend 帶進 test_helpers 命名空間
using ems::IStorageBackend;

/** 取得 translation-unit 私有的 fs 實例（每個 .cpp 各自一份） */
inline InMemFs& in_mem_fs() {
    static InMemFs s_fs;
    return s_fs;
}

inline void in_mem_reset() {
    in_mem_fs().files.clear();
}

inline bool im_read_file(void* /*ctx*/, const char* path,
                         uint8_t* out, size_t buf_max, size_t* out_len) {
    auto& fs = in_mem_fs();
    auto it = fs.files.find(path);
    if (it == fs.files.end()) {
        return false;
    }
    if (it->second.size() > buf_max) {
        return false;
    }
    if (!it->second.empty()) {
        memcpy(out, it->second.data(), it->second.size());
    }
    if (out_len) {
        *out_len = it->second.size();
    }
    return true;
}

inline bool im_write_file(void* /*ctx*/, const char* path,
                          const uint8_t* buf, size_t len) {
    auto& fs = in_mem_fs();
    fs.files[path] = std::vector<uint8_t>(buf, buf + len);
    return true;
}

inline bool im_delete_file(void* /*ctx*/, const char* path) {
    auto& fs = in_mem_fs();
    fs.files.erase(path);
    return true;
}

inline bool im_exists(void* /*ctx*/, const char* path) {
    auto& fs = in_mem_fs();
    return fs.files.count(path) > 0;
}

inline bool im_rename_file(void* /*ctx*/, const char* from, const char* to) {
    auto& fs = in_mem_fs();
    auto it = fs.files.find(from);
    if (it == fs.files.end()) {
        return false;
    }
    fs.files[to] = it->second;
    fs.files.erase(it);
    return true;
}

inline size_t im_list_dir(void* /*ctx*/, const char* dir,
                          char out_names[][EMS_STORAGE_NAME_MAX],
                          size_t max_names) {
    auto& fs = in_mem_fs();
    std::string prefix(dir);
    if (prefix.empty() || prefix.back() != '/') {
        prefix += '/';
    }
    size_t n = 0;
    for (auto& kv : fs.files) {
        if (kv.first.rfind(prefix, 0) != 0) {
            continue;
        }
        std::string rest = kv.first.substr(prefix.size());
        // 只列直屬檔案，不遞迴子目錄
        if (rest.find('/') != std::string::npos) {
            continue;
        }
        if (n >= max_names) {
            break;
        }
        strncpy(out_names[n], rest.c_str(), EMS_STORAGE_NAME_MAX - 1);
        out_names[n][EMS_STORAGE_NAME_MAX - 1] = '\0';
        n++;
    }
    return n;
}

/** 連線一份 backend struct 到 InMemFs（呼叫前先 in_mem_reset()） */
inline void wire_in_mem_backend(IStorageBackend* be) {
    be->ctx         = nullptr;
    be->read_file   = im_read_file;
    be->write_file  = im_write_file;
    be->delete_file = im_delete_file;
    be->exists      = im_exists;
    be->rename_file = im_rename_file;
    be->list_dir    = im_list_dir;
}

}  // namespace test_helpers
