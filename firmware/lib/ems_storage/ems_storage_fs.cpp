// EMS DoseSync Pro — Phase E: LittleFS adapter（僅 ESP32 編譯）
//
// 設計要點：
//   - native test 不編譯本檔（src 對 Arduino.h 有依賴）
//   - 透過 IStorageBackend 抽象注入 storage_logic，邏輯層 native 與 ESP32 共用同份程式碼
//   - 路徑 mapping：IStorageBackend 用絕對路徑 "/cases/..."，LittleFS 直接吃絕對路徑
#ifdef ARDUINO

#include "ems_storage_fs.h"
#include "ems_storage_logic.h"

#include <Arduino.h>
#include <LittleFS.h>

namespace ems {

namespace {

bool fs_read_file(void* /*ctx*/, const char* path,
                  uint8_t* out, size_t buf_max, size_t* out_len) {
    // STEP 01: 開檔
    File f = LittleFS.open(path, "r");
    if (!f) {
        return false;
    }
    // STEP 02: 容量檢查
    size_t sz = f.size();
    if (sz > buf_max) {
        f.close();
        return false;
    }
    // STEP 03: 讀
    size_t n = f.read(out, sz);
    f.close();
    if (out_len) {
        *out_len = n;
    }
    if (n != sz) {
        // short read 與「open 失敗」回傳值無法區分；明確標出「讀到一半」幫忙 diagnose
        // index.json 載入 / fill_meta_from_bin 失敗時可用此訊息辨別 partition 故障
        Serial.printf("[FS] WARN short read: %s expected=%u actual=%u\n",
                      path, (unsigned)sz, (unsigned)n);
        return false;
    }
    return true;
}

bool fs_write_file(void* /*ctx*/, const char* path,
                   const uint8_t* buf, size_t len) {
    File f = LittleFS.open(path, "w");
    if (!f) {
        return false;
    }
    size_t n = f.write(buf, len);
    f.close();
    return (n == len);
}

bool fs_delete_file(void* /*ctx*/, const char* path) {
    // 一律 return true 的設計理由：
    //   1) enforce_fifo_cap 的刪檔失敗不該擋下整個 save_case（救護現場以「保住新案件」優先）
    //   2) caller 對單檔刪除失敗無 recovery 路徑（再 retry 也是同樣失敗）
    //   3) 下次 storage_init STEP 05.02.02 雙向 sync 會把孤兒清掉，最終一致性可達
    // 但「明確存在卻刪不掉」（partition 損毀 / 全滿）必須 log，不可靜默吞掉
    if (LittleFS.exists(path) && !LittleFS.remove(path)) {
        Serial.printf("[FS] WARN delete failed: %s\n", path);
    }
    return true;
}

bool fs_exists(void* /*ctx*/, const char* path) {
    return LittleFS.exists(path);
}

bool fs_rename_file(void* /*ctx*/, const char* from, const char* to) {
    // 若 to 已存在，LittleFS rename 失敗 → 先刪
    if (LittleFS.exists(to)) {
        if (!LittleFS.remove(to)) {
            // remove 失敗 → 後續 rename 一定失敗。log 出來方便 diagnose
            // partition 故障，避免只看到 rename failed 卻不知道根因在 remove
            Serial.printf("[FS] WARN rename pre-remove failed: %s\n", to);
        }
    }
    return LittleFS.rename(from, to);
}

size_t fs_list_dir(void* /*ctx*/, const char* dir,
                   char out_names[][EMS_STORAGE_NAME_MAX],
                   size_t max_names) {
    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        return 0;
    }
    size_t n = 0;
    File f = root.openNextFile();
    while (f && n < max_names) {
        if (!f.isDirectory()) {
            const char* nm = f.name();
            // f.name() 在 LittleFS 不同版本可能回完整路徑或裸檔名；統一抽 basename
            const char* slash = strrchr(nm, '/');
            const char* base = slash ? (slash + 1) : nm;
            strncpy(out_names[n], base, EMS_STORAGE_NAME_MAX - 1);
            out_names[n][EMS_STORAGE_NAME_MAX - 1] = '\0';
            n++;
        }
        f.close();
        f = root.openNextFile();
    }
    if (f) {
        f.close();
    }
    root.close();
    return n;
}

}  // anonymous namespace

bool emsStorage_fs_mount(IStorageBackend* out_be) {
    if (!out_be) {
        return false;
    }
    // STEP 01: Mount（true = 失敗時自動 format，首次燒入也能跑）
    if (!LittleFS.begin(/*formatOnFail=*/true,
                        /*basePath=*/"/littlefs",
                        /*maxOpenFiles=*/5,
                        /*partitionLabel=*/"littlefs")) {
        return false;
    }
    // STEP 02: 確保目錄存在
    emsStorage_fs_ensure_dirs();
    // STEP 03: 連線 backend 指標表
    out_be->ctx         = nullptr;
    out_be->read_file   = fs_read_file;
    out_be->write_file  = fs_write_file;
    out_be->delete_file = fs_delete_file;
    out_be->exists      = fs_exists;
    out_be->rename_file = fs_rename_file;
    out_be->list_dir    = fs_list_dir;
    return true;
}

bool emsStorage_fs_ensure_dirs() {
    // LittleFS 隱含目錄（寫 /a/b.txt 自動建 /a），但有些版本需要 mkdir
    LittleFS.mkdir("/cases");
    LittleFS.mkdir("/cases/ohca");
    LittleFS.mkdir("/cases/training");
    return true;
}

}  // namespace ems

#endif  // ARDUINO
