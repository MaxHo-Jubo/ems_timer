#!/usr/bin/env python3
"""EMS Timer 裝置示意圖產生器 — v2 精緻版"""

from PIL import Image, ImageDraw, ImageFont
import os, math

# === 畫布設定 ===
W, H = 3200, 2200
BG = "#0D1117"
WARM_ACCENT = "#E8634A"
COOL_ACCENT = "#4A9BE8"
SIGNAL_GREEN = "#3DDC84"
BUTTON_YELLOW = "#F0C040"
MUTED_WHITE = "#C9D1D9"
DIM_GRAY = "#30363D"
MID_GRAY = "#586069"
BUZZER_ORANGE = "#FF9F43"
SD_PURPLE = "#A371F7"
REC_RED = "#FF4444"

# === 字型 ===
FONT_DIR = os.path.expanduser(
    "~/.claude/plugins/marketplaces/anthropic-agent-skills/skills/canvas-design/canvas-fonts"
)

def lf(name, size):
    """載入字型"""
    try:
        return ImageFont.truetype(os.path.join(FONT_DIR, name), size)
    except Exception:
        return ImageFont.load_default()

# 放大所有字型
ft_title    = lf("BigShoulders-Bold.ttf", 72)
ft_subtitle = lf("JetBrainsMono-Regular.ttf", 28)
ft_comp     = lf("BigShoulders-Bold.ttf", 38)
ft_section  = lf("BigShoulders-Bold.ttf", 34)
ft_label    = lf("JetBrainsMono-Regular.ttf", 24)
ft_label_b  = lf("JetBrainsMono-Bold.ttf", 24)
ft_pin      = lf("JetBrainsMono-Bold.ttf", 20)
ft_small    = lf("JetBrainsMono-Regular.ttf", 18)
ft_btn      = lf("BigShoulders-Bold.ttf", 22)
ft_btn_zh   = lf("JetBrainsMono-Regular.ttf", 16)
ft_screen   = lf("JetBrainsMono-Bold.ttf", 20)
ft_screen_s = lf("JetBrainsMono-Regular.ttf", 16)
ft_table    = lf("JetBrainsMono-Regular.ttf", 20)
ft_table_h  = lf("JetBrainsMono-Bold.ttf", 22)
ft_legend   = lf("JetBrainsMono-Regular.ttf", 20)

img = Image.new("RGB", (W, H), BG)
draw = ImageDraw.Draw(img)

# === 輔助函式 ===
def rrect(xy, fill=None, outline=None, width=1, r=8):
    """圓角矩形"""
    draw.rounded_rectangle(xy, radius=r, fill=fill, outline=outline, width=width)

def wire(pts, color, w=2):
    """多段折線"""
    for i in range(len(pts) - 1):
        draw.line([pts[i], pts[i+1]], fill=color, width=w)

def ctxt(text, font, y, color, cx=None):
    """置中文字"""
    bb = draw.textbbox((0, 0), text, font=font)
    tw = bb[2] - bb[0]
    x = (cx if cx else W // 2) - tw // 2
    draw.text((x, y), text, fill=color, font=font)

def rtxt(text, font, x, y, color):
    """右對齊文字"""
    bb = draw.textbbox((0, 0), text, font=font)
    tw = bb[2] - bb[0]
    draw.text((x - tw, y), text, fill=color, font=font)

# ============================================================
# 標題區
# ============================================================
draw.rectangle([(0, 0), (W, 110)], fill="#161B22")
ctxt("EMS TIMER — DEVICE SCHEMATIC", ft_title, 16, MUTED_WHITE)
ctxt("ESP32  ·  8 BUTTONS  ·  OLED  ·  BUZZER  ·  MIC  ·  SD CARD", ft_subtitle, 78, MID_GRAY)

# ============================================================
# 左上：元件清單
# ============================================================
lx, ly = 80, 150
draw.text((lx, ly), "COMPONENTS", fill=MID_GRAY, font=ft_section)
ly += 50
for dot_c, name in [
    (WARM_ACCENT,   "ESP32-WROOM-32 DevKit"),
    (SIGNAL_GREEN,  "SSD1306 OLED 0.96\" (I2C)"),
    (BUTTON_YELLOW, "Tactile Button × 8"),
    (BUZZER_ORANGE, "Passive Buzzer (PWM)"),
    (COOL_ACCENT,   "INMP441 I2S Microphone"),
    (SD_PURPLE,     "MicroSD Card Module (SPI)"),
]:
    draw.ellipse([(lx, ly+6), (lx+14, ly+20)], fill=dot_c)
    draw.text((lx + 24, ly), name, fill=MUTED_WHITE, font=ft_label)
    ly += 36

# ============================================================
# 中央：ESP32 主板
# ============================================================
ESP_X, ESP_Y = 980, 200
ESP_W, ESP_H = 360, 950
rrect([(ESP_X, ESP_Y), (ESP_X+ESP_W, ESP_Y+ESP_H)],
      fill="#1A1F26", outline=WARM_ACCENT, width=4, r=14)
ctxt("ESP32", ft_comp, ESP_Y+20, WARM_ACCENT, ESP_X+ESP_W//2)
ctxt("WROOM-32", ft_small, ESP_Y+62, MID_GRAY, ESP_X+ESP_W//2)

# USB
uw, uh = 80, 30
ux = ESP_X + ESP_W//2 - uw//2
uy = ESP_Y + ESP_H - 8
rrect([(ux, uy), (ux+uw, uy+uh)], fill="#30363D", outline=MID_GRAY, width=2, r=5)
ctxt("USB-C", ft_small, uy+5, MID_GRAY, ESP_X+ESP_W//2)

# === 引腳 ===
PIN_Y0 = ESP_Y + 110
PIN_SP = 72  # 間距加大

# 左側引腳
L_PINS = [
    ("3V3",     COOL_ACCENT),
    ("GND",     COOL_ACCENT),
    ("GPIO 15", BUTTON_YELLOW),
    ("GPIO 14", BUTTON_YELLOW),
    ("GPIO 13", BUTTON_YELLOW),
    ("GPIO 12", BUTTON_YELLOW),
    ("GPIO 27", BUTTON_YELLOW),
    ("GPIO 26", BUTTON_YELLOW),
    ("GPIO 25", BUTTON_YELLOW),
    ("GPIO 33", BUTTON_YELLOW),
    ("GPIO 32", BUZZER_ORANGE),
]
for i, (name, c) in enumerate(L_PINS):
    py = PIN_Y0 + i * PIN_SP
    draw.ellipse([(ESP_X-8, py-5), (ESP_X+8, py+5)], fill=c)
    rtxt(name, ft_pin, ESP_X-16, py-11, c)

# 右側引腳
R_PINS = [
    ("VIN",           COOL_ACCENT),
    ("GND",           COOL_ACCENT),
    ("GPIO 21 (SDA)", SIGNAL_GREEN),
    ("GPIO 22 (SCL)", SIGNAL_GREEN),
    ("GPIO 23 (MOSI)",SD_PURPLE),
    ("GPIO 18 (SCK)", SD_PURPLE),
    ("GPIO 19 (MISO)",SD_PURPLE),
    ("GPIO 5  (CS)",  SD_PURPLE),
    ("GPIO 35 (SD)",  COOL_ACCENT),
    ("GPIO 34 (SCK)", COOL_ACCENT),
    ("GPIO 4  (WS)",  COOL_ACCENT),
]
for i, (name, c) in enumerate(R_PINS):
    py = PIN_Y0 + i * PIN_SP
    draw.ellipse([(ESP_X+ESP_W-8, py-5), (ESP_X+ESP_W+8, py+5)], fill=c)
    draw.text((ESP_X+ESP_W+18, py-11), name, fill=c, font=ft_pin)

# ============================================================
# 左側：8 顆按鈕
# ============================================================
BTN_X = 140
BTN_START_Y = PIN_Y0 + 2 * PIN_SP  # 對齊 GPIO 15

BUTTONS = [
    ("BTN 1", "注射 Epi"),
    ("BTN 2", "注射 Atropine"),
    ("BTN 3", "CPR 開始"),
    ("BTN 4", "CPR 結束"),
    ("BTN 5", "電擊"),
    ("BTN 6", "插管"),
    ("BTN 7", "到院"),
    ("BTN 8", "錄音"),
]

for i, (bid, blabel) in enumerate(BUTTONS):
    py = BTN_START_Y + i * PIN_SP
    bx = BTN_X
    by = py - 22
    bw, bh = 180, 44

    # 按鈕底色依類型
    btn_fill = "#2A2015" if i < 7 else "#2A1520"  # 錄音按鈕稍紅
    btn_border = BUTTON_YELLOW if i < 7 else REC_RED

    rrect([(bx, by), (bx+bw, by+bh)], fill=btn_fill, outline=btn_border, width=2, r=8)

    # 按鈕文字：上方 ID，下方中文
    ctxt(bid, ft_btn, by+3, btn_border, bx+bw//2)
    ctxt(blabel, ft_btn_zh, by+24, MID_GRAY, bx+bw//2)

    # 接線：按鈕右邊 → ESP32 左側引腳
    pin_y = PIN_Y0 + (i + 2) * PIN_SP
    # 用不同 x 偏移避免重疊
    mid_x = ESP_X - 40 - (7 - i) * 14
    wire([(bx+bw+4, py), (mid_x, py), (mid_x, pin_y), (ESP_X-10, pin_y)],
         BUTTON_YELLOW, w=2)

# 按鈕共地標示
gnd_bar_x = BTN_X + 90
gnd_top = BTN_START_Y - 22
gnd_bot = BTN_START_Y + 7 * PIN_SP + 22
draw.line([(gnd_bar_x - 50, gnd_bot + 20), (gnd_bar_x + 50, gnd_bot + 20)], fill=COOL_ACCENT, width=2)
draw.line([(gnd_bar_x, gnd_bot + 20), (gnd_bar_x, gnd_top)], fill=COOL_ACCENT, width=1)
for i in range(8):
    py = BTN_START_Y + i * PIN_SP
    draw.line([(gnd_bar_x, py), (BTN_X - 4, py)], fill=COOL_ACCENT, width=1)
    draw.ellipse([(BTN_X-8, py-4), (BTN_X, py+4)], fill=COOL_ACCENT)
ctxt("GND", ft_pin, gnd_bot + 24, COOL_ACCENT, gnd_bar_x)

# ============================================================
# 右上：OLED 螢幕
# ============================================================
OX, OY = 1750, 200
OW, OH = 380, 260
rrect([(OX, OY), (OX+OW, OY+OH)], fill="#050810", outline=SIGNAL_GREEN, width=3, r=12)

# 螢幕內容區
sm = 20
sx, sy = OX+sm, OY+sm
sw, sh = OW-sm*2, OH-sm*2
draw.rectangle([(sx, sy), (sx+sw, sy+sh)], fill="#000000", outline="#0A2A0A", width=1)

# 螢幕模擬顯示
draw.text((sx+12, sy+10),  "SESSION #047", fill="#00FF41", font=ft_screen_s)
draw.text((sx+12, sy+35),  "EPI      03:42", fill="#00FF41", font=ft_screen)
draw.text((sx+12, sy+62),  "CPR      01:15", fill="#00FF41", font=ft_screen)
draw.text((sx+12, sy+90),  "ELAPSED  08:23", fill="#00FF41", font=ft_screen)
draw.rectangle([(sx+12, sy+120), (sx+sw-12, sy+122)], fill="#0A3A0A")
draw.text((sx+12, sy+130), "COUNTDOWN  01:48", fill="#FFAA00", font=ft_screen)
draw.text((sx+12, sy+158), "REC", fill=REC_RED, font=ft_screen)
draw.ellipse([(sx+52, sy+162), (sx+60, sy+170)], fill=REC_RED)

# OLED 標籤
ctxt("SSD1306 OLED", ft_comp, OY+OH+15, SIGNAL_GREEN, OX+OW//2)
ctxt("128×64  I2C  0.96\"", ft_small, OY+OH+55, MID_GRAY, OX+OW//2)

# OLED 接線（I2C）
sda_y = PIN_Y0 + 2 * PIN_SP  # GPIO 21
scl_y = PIN_Y0 + 3 * PIN_SP  # GPIO 22
wire([(ESP_X+ESP_W+10, sda_y), (1580, sda_y), (1580, OY+OH-40), (OX-4, OY+OH-40)],
     SIGNAL_GREEN, w=3)
draw.text((1590, sda_y-20), "SDA", fill=SIGNAL_GREEN, font=ft_small)

wire([(ESP_X+ESP_W+10, scl_y), (1610, scl_y), (1610, OY+OH-80), (OX-4, OY+OH-80)],
     SIGNAL_GREEN, w=3)
draw.text((1620, scl_y-20), "SCL", fill=SIGNAL_GREEN, font=ft_small)

# ============================================================
# 右中上：蜂鳴器
# ============================================================
BZ_CX, BZ_CY = 1940, 680
BZ_R = 80
draw.ellipse([(BZ_CX-BZ_R, BZ_CY-BZ_R), (BZ_CX+BZ_R, BZ_CY+BZ_R)],
             fill="#1F1A10", outline=BUZZER_ORANGE, width=3)
for r in [60, 44, 28, 12]:
    draw.ellipse([(BZ_CX-r, BZ_CY-r), (BZ_CX+r, BZ_CY+r)],
                 outline="#3D3020", width=1)
draw.ellipse([(BZ_CX-6, BZ_CY-6), (BZ_CX+6, BZ_CY+6)], fill=BUZZER_ORANGE)

ctxt("BUZZER", ft_comp, BZ_CY+BZ_R+15, BUZZER_ORANGE, BZ_CX)
ctxt("PASSIVE  PWM", ft_small, BZ_CY+BZ_R+55, MID_GRAY, BZ_CX)

# 蜂鳴器接線 → GPIO 32（左側最後一個引腳）
gpio32_y = PIN_Y0 + 10 * PIN_SP
# 從 ESP32 左側引腳，繞下方到右邊蜂鳴器
wire([(ESP_X-10, gpio32_y),
      (ESP_X-120, gpio32_y),
      (ESP_X-120, ESP_Y+ESP_H+60),
      (BZ_CX, ESP_Y+ESP_H+60),
      (BZ_CX, BZ_CY+BZ_R+4)],
     BUZZER_ORANGE, w=3)
draw.text((ESP_X-200, gpio32_y-20), "PWM", fill=BUZZER_ORANGE, font=ft_small)

# ============================================================
# 右中下：麥克風
# ============================================================
MX, MY = 1750, 850
MW, MH = 260, 170
rrect([(MX, MY), (MX+MW, MY+MH)], fill="#0F1520", outline=COOL_ACCENT, width=3, r=10)

# 麥克風孔
mcx, mcy = MX+MW//2, MY+MH//2-5
for r in [35, 26, 17, 8]:
    c = "#2A4060" if r > 8 else COOL_ACCENT
    draw.ellipse([(mcx-r, mcy-r), (mcx+r, mcy+r)], outline=c, width=2 if r==8 else 1)
draw.ellipse([(mcx-3, mcy-3), (mcx+3, mcy+3)], fill=COOL_ACCENT)

ctxt("INMP441", ft_comp, MY+MH+15, COOL_ACCENT, MX+MW//2)
ctxt("I2S  DIGITAL MIC", ft_small, MY+MH+55, MID_GRAY, MX+MW//2)

# 麥克風接線（I2S）
i2s_labels = ["SD", "SCK", "WS"]
for j in range(3):
    src_y = PIN_Y0 + (8 + j) * PIN_SP
    tgt_y = MY + 30 + j * 50
    mx_off = 1640 + j * 20
    wire([(ESP_X+ESP_W+10, src_y), (mx_off, src_y), (mx_off, tgt_y), (MX-4, tgt_y)],
         COOL_ACCENT, w=2)
    draw.text((mx_off+5, src_y-20), i2s_labels[j], fill=COOL_ACCENT, font=ft_small)

# ============================================================
# 右下：SD 卡模組
# ============================================================
SX, SY = 1750, 1160
SW, SH = 280, 180
rrect([(SX, SY), (SX+SW, SY+SH)], fill="#151020", outline=SD_PURPLE, width=3, r=10)

# SD 卡插槽
slx, sly = SX+40, SY+30
slw, slh = 200, 100
rrect([(slx, sly), (slx+slw, sly+slh)], fill="#1A1530", outline="#3A2A5A", width=1, r=5)
# 金手指
for k in range(8):
    fx = slx + 18 + k * 22
    draw.rectangle([(fx, sly+slh-22), (fx+12, sly+slh-4)], fill="#B8860B")
# SD label
ctxt("μSD", ft_pin, sly+10, "#3A2A5A", slx+slw//2)

ctxt("SD CARD", ft_comp, SY+SH+15, SD_PURPLE, SX+SW//2)
ctxt("SPI  MICROSD", ft_small, SY+SH+55, MID_GRAY, SX+SW//2)

# SD 接線（SPI）
spi_labels = ["MOSI", "SCK", "MISO", "CS"]
for j in range(4):
    src_y = PIN_Y0 + (4 + j) * PIN_SP
    tgt_y = SY + 20 + j * 40
    mx_off = 1660 + j * 18
    wire([(ESP_X+ESP_W+10, src_y), (mx_off, src_y), (mx_off, tgt_y), (SX-4, tgt_y)],
         SD_PURPLE, w=2)
    draw.text((mx_off+5, src_y-20), spi_labels[j], fill=SD_PURPLE, font=ft_small)

# ============================================================
# 右側：提醒模式 & 圖例
# ============================================================
# 提醒模式
ax, ay = 2200, 550
draw.text((ax, ay), "ALERT MODES", fill=BUZZER_ORANGE, font=ft_section)
ay += 50

# 倒數結束
rrect([(ax, ay), (ax+400, ay+90)], outline=DIM_GRAY, width=1, r=6)
draw.text((ax+15, ay+8), "COUNTDOWN END", fill=BUZZER_ORANGE, font=ft_label_b)
draw.text((ax+15, ay+38), "■ ■ ■ ■ ■  continuous", fill=BUZZER_ORANGE, font=ft_legend)
draw.text((ax+15, ay+62), "until button press", fill=MID_GRAY, font=ft_small)
ay += 110

# 區間提醒
rrect([(ax, ay), (ax+400, ay+90)], outline=DIM_GRAY, width=1, r=6)
draw.text((ax+15, ay+8), "INTERVAL ALERT", fill=BUTTON_YELLOW, font=ft_label_b)
draw.text((ax+15, ay+38), "■ · · · ■ · · · ■", fill=BUTTON_YELLOW, font=ft_legend)
draw.text((ax+15, ay+62), "pulse every N seconds", fill=MID_GRAY, font=ft_small)

# 圖例
lgx, lgy = 2200, 850
draw.text((lgx, lgy), "WIRE LEGEND", fill=MID_GRAY, font=ft_section)
lgy += 45
for c, label in [
    (BUTTON_YELLOW, "GPIO INPUT_PULLUP (Buttons)"),
    (SIGNAL_GREEN,  "I2C Bus (OLED)"),
    (COOL_ACCENT,   "I2S Bus (Microphone)"),
    (SD_PURPLE,     "SPI Bus (SD Card)"),
    (BUZZER_ORANGE, "PWM Output (Buzzer)"),
    (COOL_ACCENT,   "GND (Common Ground)"),
]:
    draw.line([(lgx, lgy+10), (lgx+40, lgy+10)], fill=c, width=4)
    draw.text((lgx+55, lgy), label, fill=MUTED_WHITE, font=ft_legend)
    lgy += 35

# ============================================================
# 底部：GPIO 配置表
# ============================================================
TBL_Y = 1600
draw.rectangle([(0, TBL_Y-15), (W, TBL_Y-12)], fill=DIM_GRAY)
ctxt("GPIO ALLOCATION TABLE", ft_section, TBL_Y, MID_GRAY)

TBL_Y += 50
COL_W = 520
cols = [
    ("BUTTONS (INPUT_PULLUP)", BUTTON_YELLOW, [
        "GPIO 15  →  BTN 1  注射 Epinephrine",
        "GPIO 14  →  BTN 2  注射 Atropine",
        "GPIO 13  →  BTN 3  CPR 開始",
        "GPIO 12  →  BTN 4  CPR 結束",
        "GPIO 27  →  BTN 5  電擊 Defibrillation",
        "GPIO 26  →  BTN 6  插管 Intubation",
        "GPIO 25  →  BTN 7  到院 Arrival",
        "GPIO 33  →  BTN 8  錄音 Record",
    ]),
    ("I2C / I2S / PWM", SIGNAL_GREEN, [
        "GPIO 21  →  OLED SDA (I2C)",
        "GPIO 22  →  OLED SCL (I2C)",
        "GPIO 35  →  MIC I2S_SD (Data)",
        "GPIO 34  →  MIC I2S_SCK (Clock)",
        "GPIO 4   →  MIC I2S_WS (Word Sel)",
        "GPIO 32  →  BUZZER (PWM)",
    ]),
    ("SPI (SD CARD)", SD_PURPLE, [
        "GPIO 23  →  SD MOSI",
        "GPIO 18  →  SD SCK",
        "GPIO 19  →  SD MISO",
        "GPIO 5   →  SD CS",
    ]),
    ("POWER", COOL_ACCENT, [
        "3V3  →  OLED VCC",
        "3V3  →  MIC VCC",
        "3V3  →  SD VCC",
        "GND  →  ALL GND (Common)",
    ]),
]

for idx, (title, color, items) in enumerate(cols):
    cx = 100 + idx * (COL_W + 60)
    draw.text((cx, TBL_Y), title, fill=color, font=ft_table_h)
    for j, item in enumerate(items):
        draw.text((cx, TBL_Y + 32 + j * 28), item, fill=MUTED_WHITE, font=ft_table)

# 底部版本
draw.rectangle([(0, H-45), (W, H)], fill="#161B22")
ctxt("EMS TIMER v0.1  ·  BREADBOARD PROTOTYPE  ·  8-BUTTON CONFIGURATION", ft_small, H-36, MID_GRAY)

# === 儲存 ===
out = "/Users/maxhero/Documents/MaxHero/Projects/ems_timer/ems_timer_schematic.png"
img.save(out, "PNG", dpi=(300, 300))
print(f"saved: {out}")
