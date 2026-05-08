// TFT 接線 smoke test — 確認 ST7789 2.8" 240x320 模組接線正確
// Library: Adafruit_ST7789 + Adafruit_GFX（不用 TFT_eSPI，原因見 platformio.ini 註解）
// 燒錄：pio run -e tft-smoke-test -t upload && pio device monitor -e tft-smoke-test
//
// 接線（ESP32-S3 GOOUUU N16R8 octal PSRAM 模組安全）：
//   TFT MOSI/SDI → GPIO 2     TFT VCC → 5V (VIN)
//   TFT SCK      → GPIO 3     TFT GND → GND
//   TFT CS       → GPIO 21    TFT LED → 3.3V
//   TFT DC       → GPIO 48    其他腳位（MISO/T_*）不接
//   TFT RST      → GPIO 47
//
// 預期：螢幕順序顯示 紅 → 綠 → 藍 → 白底黑字 "EMS TFT OK" + 計數器
// 雪花/錯亂：driver 換 ILI9341（換 #include 跟建構子）
// 顏色反相：建構子換成 init(240, 320, SPI_MODE2) 或調 invertDisplay()

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

constexpr int8_t TFT_CS_PIN   = 21;
constexpr int8_t TFT_DC_PIN   = 48;
constexpr int8_t TFT_RST_PIN  = 47;
constexpr int8_t TFT_MOSI_PIN = 2;
constexpr int8_t TFT_SCLK_PIN = 3;

/** 每次填色後停留的毫秒數，讓眼睛能確認顏色正確 */
constexpr uint16_t FILL_DELAY_MS  = 800;
/** Serial 啟動穩定等待毫秒數，避免首行 log 遺失 */
constexpr uint16_t SERIAL_INIT_MS = 300;
/** loop 每輪更新計數器的間隔毫秒數 */
constexpr uint16_t TICK_DELAY_MS  = 500;

// 採用 Adafruit_ST7789 的 5-arg 軟體 SPI（bitbang）建構子：明確指定 CS/DC/MOSI/SCLK/RST 五腳。
// smoke test 階段優先求穩 — bitbang 不依賴 HSPI/FSPI 暫存器配置，避開 ESP32-S3 N16R8 上踩過的雷。
// 若後續需提速（>30 fps），改用 3-arg 硬體 SPI 建構子並透過 SPI.begin(SCLK, MISO, MOSI, CS) 指定 FSPI bus。
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS_PIN, TFT_DC_PIN, TFT_MOSI_PIN, TFT_SCLK_PIN, TFT_RST_PIN);

/**
 * 以指定顏色填滿螢幕，並將顏色名稱印到 Serial。
 * @param color  ST77XX_* 顏色常數
 * @param name   顏色名稱字串（僅用於 Serial log）
 */
static void fillAndPrint(uint16_t color, const char* name) {
  Serial.printf("Fill: %s\n", name);
  tft.fillScreen(color);
  delay(FILL_DELAY_MS);
}

/**
 * Arduino setup — 初始化 Serial、TFT，依序跑紅/綠/藍填色，最後顯示靜態確認文字。
 */
void setup() {
  Serial.begin(115200);
  delay(SERIAL_INIT_MS);
  Serial.println();
  Serial.println("=== TFT smoke test (Adafruit_ST7789) ===");
  Serial.println("Pins: MOSI=2 SCK=3 CS=21 DC=48 RST=47 BL=3.3V (N16R8 safe)");

  // 240x320 @ 2.8" ST7789
  tft.init(240, 320);
  tft.setRotation(1);  // 1 = 橫向 320x240
  // Adafruit_ST7789 init 預設送 INVON（IPS 面板大宗）。蝦皮這顆紅板出廠 polarity 跟主流相反，
  // 要 INVOFF 才正常 → invertDisplay(false)。
  tft.invertDisplay(false);

  Serial.println("init OK");

  fillAndPrint(ST77XX_RED,   "RED");
  fillAndPrint(ST77XX_GREEN, "GREEN");
  fillAndPrint(ST77XX_BLUE,  "BLUE");

  tft.fillScreen(ST77XX_WHITE);
  tft.setTextColor(ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setCursor(20, 20);
  tft.print("EMS TFT OK");
  tft.setCursor(20, 60);
  tft.print("ST7789 240x320");
  tft.setCursor(20, 100);
  tft.print("Rot=1 landscape");

  Serial.println("setup done");
}

/**
 * Arduino loop — 每 TICK_DELAY_MS 毫秒更新螢幕上的計數器，確認持續寫入不當機。
 */
void loop() {
  static uint32_t counter = 0; // 累計 tick 次數，用於驗證 loop 持續運作
  tft.fillRect(20, 160, 280, 30, ST77XX_WHITE);
  tft.setCursor(20, 160);
  tft.print("tick: ");
  tft.print(counter++);
  delay(TICK_DELAY_MS);
}
