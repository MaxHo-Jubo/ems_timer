# efontTW_24 → vlw 自訂字型評估

> 立案：2026-05-09
> 觸發：PM 反饋「電擊」兩字在 efontTW_24 視覺不平衡（筆畫密度 + 寬度不一致）
> 目標：評估換 vlw 自訂字型方案的可行性、開銷、實作成本

---

## 1. 問題分析

### 1.1 現況

韌體 `firmware/src/main.cpp` 中文渲染統一用 `&fonts::efontTW_24`（LovyanGFX 內建），共 **45 處** `setFont(efontTW_24)` 呼叫。Flash 開銷：fontmap ~600KB（從 commit `0ab4060` 起 11% → 27.6%）。

### 1.2 efontTW_24 不平衡原因

efont 是日韓中字型整合，24px 點陣字型（無灰階反鋸齒）。短處：
- 對筆畫密度高的字（電/擊/響/驟）擠壓嚴重，筆畫黏連
- 字寬不一致：固定寬點陣字型很難對所有 CJK 字符做視覺等寬
- 沒灰階 → 邊緣鋸齒明顯，320×240 TFT 上更顯眼

具體案例：「電擊」並列時「電」筆畫多但塞滿、「擊」筆畫繁複但被截斷，兩字視覺份量不均。

---

## 2. vlw 方案介紹

### 2.1 vlw 格式

vlw（Variable Line Width）是 TFT_eSPI / LovyanGFX 都支援的點陣字型格式，特性：
- **可選任意 TrueType 來源**：思源黑體、Noto Sans CJK、Cwtex Q（高品質繁中字型）
- **支援灰階反鋸齒**：4-bit per pixel（16 級灰階），邊緣平滑
- **可變字寬**：每個字符獨立寬度，視覺平衡度遠超 efont
- **任意像素高度**：24px / 28px / 32px 都可生成

### 2.2 工具鏈

| 工具 | 用途 | 來源 |
|------|------|------|
| Processing IDE + `createFont()` | 從 TTF 生成 vlw | https://processing.org |
| `lv_font_conv` | 替代方案（npm，可選 vlw 輸出） | Node.js |
| `fontconvert`（Adafruit GFX） | 不支援 vlw（GFXfont 格式只能 ASCII） | 排除 |

**建議用 Processing**：官方支援 vlw 格式，可指定字符集、像素高度、smooth（反鋸齒）。

### 2.3 LovyanGFX 載入 API

```cpp
#include <LovyanGFX.hpp>
// 方式 A：SD/LittleFS 動態載入
display.loadFont("ChineseFont24.vlw");
display.setTextSize(1);
display.drawString("電擊", x, y);
display.unloadFont();

// 方式 B：靜態編譯（uint8_t array）
const uint8_t font_data[] PROGMEM = { ... };
display.loadFont(font_data, sizeof(font_data));
```

---

## 3. 字符集規劃

### 3.1 韌體實際 UI 字符集

掃描 `firmware/src/main.cpp` 字串字面值（剝註解後）：**127 個唯一繁中字符**

```
一上下不並主事件作修倒停備入共再分前功取可史啟單回奏如始存完定實將尚已建式影往待後快成戳手按接提撤擇擊操改整數新時暫未本束查案模機檢次歷段氣消準無版物由登看確示秒立筆節簡系紀純結給統練總繼續能藥表補要覽訓設認調請軸返通速選重量銷錄鍵鎖鐘長閉開間關階電音響
```

### 3.2 字符集擴充建議

加入但目前未用的「保險字符」：
- 救/護/醫/院/血/壓/呼/吸（未來補登/Training 模式可能用到）
- 0~9 全形數字（demo 部分用半形，韌體已半形，可暫不加）
- 標點符號：`，。、！？：（）「」` 全形（demo 用半形 ASCII，韌體已半形，可暫不加）

**建議封版字集**：127 + 保險 30 = **~160 字**

---

## 4. Flash 開銷估算

### 4.1 vlw 二進位大小（每字符）

vlw 4-bit 灰階 24px：
- bitmap：24 × 24 × 4 bit / 8 = **288 bytes**
- metadata（codepoint + bbox + width + offset）：~16 bytes
- 合計 **~304 bytes/char**

### 4.2 總大小

| 字集規模 | 大小 | 對比 efontTW_24（~600KB） |
|---------|------|---------------------------|
| 127 字（最小） | ~38 KB | **節省 562 KB（93%）** |
| 160 字（含保險） | ~49 KB | **節省 551 KB（92%）** |
| 250 字（保守） | ~76 KB | 節省 524 KB（87%） |

**結論**：vlw 自訂字集比 efontTW 全字集省 **>90% Flash**。當前 Flash 用量 44.0%（commit `578bc1e` 後實測），換 vlw 後可降至 ~17%，留大量空間給 Phase E LittleFS / Phase F BLE 配對碼等後續功能。

---

## 5. 實作步驟

### Phase 1：PoC（半天）
1. 挑選來源 TTF：建議 **Cwtex Q 中黑** 或 **Noto Sans TC Bold**（免費可商用）
2. 用 Processing IDE 寫 `createFont("CwtexQHei-Bold", 24, true, chars)`，`chars` 帶 127 字 + 30 保險字
3. 匯出 `.vlw` 檔放 `firmware/data/fonts/ems_zh_24.vlw`
4. PlatformIO 上傳 LittleFS：`pio run -t uploadfs`
5. 韌體加 `display.loadFont("/fonts/ems_zh_24.vlw")` 初始化（一次性，setup 階段）

### Phase 2：替換 efontTW_24（1 小時）
1. 全檔 search-replace `&fonts::efontTW_24` → 不需指定 font 物件（loadFont 後自動套用）
2. 移除 `lib_deps` 對 efontTW 的引用（不適用，efontTW 是 LovyanGFX 內建，無法選擇性移除）
3. 改編譯 flag 排除 efontTW：`build_flags = -DLGFX_NO_EFONT_TW`（待確認 LovyanGFX 是否支援）

### Phase 3：實機驗收
1. 跑完所有 13 個畫面確認字符無缺
2. PM 視覺比對「電擊」、「請給藥」、「驟」等高密度筆畫字
3. 量測 Flash 變化（預期從 44% → ~20%）

---

## 6. 風險與待確認

| 風險 | 等級 | 緩解 |
|------|------|------|
| LovyanGFX 是否能 build-time 排除 efontTW 字集 | 🟡 | 若不行，vlw 與 efontTW 並存，省不了 600KB（但仍解視覺問題） |
| Processing IDE 在 Apple Silicon 是否穩定 | 🟢 | 改用 `lv_font_conv` (npm) 或 Linux Docker |
| LittleFS 動態載入啟動延遲 | 🟢 | setup 一次性 ~50ms，可接受 |
| 來源 TTF 商用授權 | 🟢 | Noto Sans TC（OFL）+ Cwtex Q（GPL）皆可商用 |
| 字符集遺漏 → 渲染空白 | 🟡 | 字集封版前再用 grep 全掃；保留 fallback：缺字回 efontTW |
| 24px 是否足夠對齊 demo 視覺份量 | 🟡 | 已用 setTextSize(1.2/1.5) 補；vlw 可直接生 28/32px 字型避免 scale |

---

## 7. 建議

### 7.1 短期（POC，本週）
- 用 Cwtex Q 中黑生 24px vlw + 韌體實機驗證一個畫面（drawTimeline 或 drawOhcaLocked，含「電擊」字）
- PM 視覺驗收後再決定是否全量替換

### 7.2 中期（Phase B 收尾後）
- 全量替換 efontTW_24 → vlw（45 處 setFont 呼叫）
- 加入 LittleFS 字型動態載入（與 Phase E 持久化合併實作）
- 評估是否再生 28px / 32px 變體做 emphasis（取代 setTextSize 縮放，避免 4-bit 灰階 scale 後鋸齒）

### 7.3 不建議（過度設計）
- 不必生中日韓全字集（efont 已經很大也沒用滿）
- 不必動態下載字型（韌體無 WiFi 必要場景）
- 不必做字型熱切換（單一字集足夠）

---

## 8. 結論

**值得做**。vlw 方案三大優勢：
1. **品質**：4-bit 灰階反鋸齒，根本解決 efontTW 點陣黏連問題
2. **空間**：精確 127 字封版省 >90% Flash（44% → ~17%）
3. **可控**：可選任意 TTF 來源，視覺份量直接對齊 demo

**單一阻力**：Processing IDE 工具流（一次性，PoC 通過後不重做）。

**下一步**：等用戶決定是否進 PoC，或直接合併 Phase B 收尾後的工作清單。
