# Session Progress Snapshot
> 自動存檔於 2026-04-13 21:xx

## 本次 session 完成的工作（2026-04-13）

- [x] 確認開發環境：PlatformIO + VS Code（曾短暫試 Cursor，改回 VS Code）
- [x] 建立 `firmware/platformio.ini`（esp32dev + arduino + Adafruit SSD1306/GFX）
- [x] 建立 `firmware/src/main.cpp`（Phase 0 測試韌體）
- [x] Build 成功，Upload 成功
- [x] **Phase 0 驗證完成**：ESP32 開機 + OLED 顯示 + GPIO 15 按鈕 count 全部正常

## 上次 session 完成的工作（2026-04-05）

- [x] 專案需求定義（CLAUDE.md）：硬體規格、使用情境、資料模型、錄音功能、倒數提醒機制
- [x] 硬體選型與 GPIO 完整配置規劃（8 按鈕 + OLED + 蜂鳴器 + 麥克風 + SD卡）
- [x] 電路配置示意圖（ems_timer_schematic.png）
- [x] 外觀概念圖（ems_timer_exterior.png）
- [x] README.md 建立並索引所有文件
- [x] Git 初始化並推送至 GitHub（MaxHo-Jubo/ems_timer）

## 未完成 / 後續待處理

### Phase 1 — 進行中
- [ ] **接線：8 顆按鈕全部接上麵包板**（下次繼續）
  - GPIO 分配：15(右側), 14/13/12/27/26/25/33(左側)
  - 顏色規則：棕=GND幹線, 紅=3V3, 無=按鈕GND橋, 藍=SDA, 白=SCL, 橘/黃/綠=BTN訊號
  - 共地策略：ESP32 GND → 電源軌藍軌，所有按鈕 GND 接同一軌
  - 空間問題：ESP32 塞滿單板 → 兩板並排或飛線
- [ ] Phase 1 韌體：8 顆按鈕全驗證後改寫支援所有 GPIO
- [ ] Phase 1 計時邏輯：按下 → 記錄時間戳 + elapsed_ms
- [ ] Phase 1 蜂鳴器提醒（倒數結束 + 區間提醒）
- [ ] Phase 1 麥克風錄音 + SD 卡存檔

### Phase 2-4
- [ ] Phase 2: BLE GATT Server 實作
- [ ] Phase 3: 手機 App 開發（React Native）
- [ ] Phase 4: 整合測試與優化

## Phase 3 前置決策：App 分發策略

**使用情境定調**
- 目標使用者：特定單位內部人員（非公開上架）
- 錄音檔留在裝置 SD 卡，**不同步到 App**
- App 僅透過 BLE 接收事件紀錄，純內部紀錄用途
- 因此 App 不需要麥克風權限，隱私面相對單純

### iOS 分發路線

**優先順序**
1. **TestFlight 內部測試員**（首選，< 100 人試用）
   - $99/年 Apple Developer Program
   - 用 Apple ID 邀請，免收 UDID
   - 建置 90 天過期要重新上傳
   - 內部測試員免審查；若用外部測試員（最多 10,000 人）需過一次寬鬆審查
2. **Ad Hoc 發佈**（穩定長期部署）
   - 同樣 $99/年帳號
   - 需收集使用者 iPhone UDID，年度上限 100 台（累積計算，移除不 reset）
   - Provisioning Profile 效期 1 年，過期要重新打包
   - 安裝：Apple Configurator、Finder、或自架 OTA 網頁
3. **Custom App / Apple Business Manager**（單位有 MDM 時）
   - 透過 Managed Apple ID 分發，對指定組織可見
   - 仍需上架但走私有通道，審查較寬鬆
4. **不考慮 Enterprise Program（$299/年）**：門檻高、需 D-U-N-S、限內部員工，違規會被撤銷

**iOS 待確認**
- [ ] 單位規模：試用 < 100 人 → TestFlight；長期 > 100 人 → 評估 Custom App
- [ ] 是否有 MDM（Jamf / Intune / Apple Business Manager）
- [ ] 是否能收集使用者 UDID（若走 Ad Hoc）
- [ ] App Store Connect 的 App 描述避開「救護」「醫療判斷」字眼，定位為「事件時間紀錄工具」
- [ ] `Info.plist` 加 `NSBluetoothAlwaysUsageDescription`，說明 BLE 用途
- [ ] 若需背景接收資料，啟用 `bluetooth-central` background mode 並準備合理性說明

### Android 分發路線

**優先順序**
1. **自架 APK 分發**（首選）
   - 零成本，不需 Google Play Console
   - 放官方網頁或 email 發送，使用者開啟「允許安裝未知來源」即可
   - 無 UDID、無裝置數量、無過期問題（憑證 25 年效期）
2. **Google Play Internal Testing**（若想走官方管道）
   - $25 美金一次性 Play Console 註冊費
   - 最多 100 email 邀請，幾乎不審查
3. **Managed Google Play Private App**（單位有 Google Workspace / MDM 時）
   - 只對特定組織可見，透過 MDM 推送

**Android 待確認**
- [ ] 決定分發管道：自架 APK / Internal Testing / Managed Private App
- [ ] `AndroidManifest.xml` 加 `BLUETOOTH_SCAN` / `BLUETOOTH_CONNECT` runtime permission（Android 12+）
- [ ] 測試各廠牌背景服務行為（小米、華為、OPPO 會殺背景）— 長時間 BLE 連線需驗證
- [ ] target SDK 選定策略（新版 SDK 權限要求嚴，舊版分發阻力低但側載警告多）
- [ ] 簽名金鑰妥善保管（自架 APK 若金鑰遺失無法更新）

### 跨平台實作
- [ ] 決定技術棧：React Native vs Flutter（BLE library 成熟度 + 團隊熟悉度）
- [ ] BLE 抽象層設計：兩平台權限模型差異集中處理
- [ ] 本地儲存：SQLite（歷史紀錄依 session 分組）

## 硬體備註

- ESP32 型號：仿製板，標示「ESP-32 WiFi+BT SoC」，board ID 用 `esp32dev`
- 按鈕為 6 腳 tactile button（上排 3 腳相通，下排 3 腳相通）
- 杜邦線長度由短到長：無 < 紅 < 橘 < 黃 < 綠 < 藍 < 灰 < 白 < 棕 < 紅(長) < 橘(長) < 黃(長) < 綠(長)
