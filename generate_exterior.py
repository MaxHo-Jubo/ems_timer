#!/usr/bin/env python3
"""EMS Timer 外觀概念圖 — 手持裝置俯視圖"""

from PIL import Image, ImageDraw, ImageFont
import os, math

W, H = 3200, 2400
BG = "#0D1117"

# 色彩
BODY_DARK = "#1C1C1E"
BODY_EDGE = "#3A3A3C"
SCREEN_BG = "#000000"
SCREEN_BORDER = "#2A2A2C"
BTN_FACE = "#2C2C2E"
BTN_EDGE = "#4A4A4C"
YELLOW = "#F0C040"
RED = "#FF4444"
GREEN = "#3DDC84"
SIGNAL_GREEN = "#3DDC84"
ORANGE = "#FF9F43"
BLUE = "#4A9BE8"
PURPLE = "#A371F7"
WHITE = "#E5E5E7"
GRAY = "#636366"
DIM = "#48484A"
LABEL_BG = "#1C1C1E"

FONT_DIR = os.path.expanduser(
    "~/.claude/plugins/marketplaces/anthropic-agent-skills/skills/canvas-design/canvas-fonts"
)

def lf(name, size):
    try:
        return ImageFont.truetype(os.path.join(FONT_DIR, name), size)
    except:
        return ImageFont.load_default()

ft_title   = lf("BigShoulders-Bold.ttf", 64)
ft_sub     = lf("JetBrainsMono-Regular.ttf", 24)
ft_comp    = lf("BigShoulders-Bold.ttf", 36)
ft_section = lf("BigShoulders-Bold.ttf", 30)
ft_label   = lf("JetBrainsMono-Bold.ttf", 22)
ft_label_s = lf("JetBrainsMono-Regular.ttf", 18)
ft_btn_big = lf("BigShoulders-Bold.ttf", 28)
ft_btn_zh  = lf("JetBrainsMono-Regular.ttf", 15)
ft_screen  = lf("JetBrainsMono-Bold.ttf", 22)
ft_screen_s= lf("JetBrainsMono-Regular.ttf", 16)
ft_dim     = lf("JetBrainsMono-Regular.ttf", 20)
ft_note    = lf("JetBrainsMono-Regular.ttf", 22)
ft_note_b  = lf("JetBrainsMono-Bold.ttf", 22)
ft_spec    = lf("JetBrainsMono-Regular.ttf", 20)

img = Image.new("RGB", (W, H), BG)
draw = ImageDraw.Draw(img)

def rrect(xy, fill=None, outline=None, width=1, r=8):
    draw.rounded_rectangle(xy, radius=r, fill=fill, outline=outline, width=width)

def ctxt(text, font, y, color, cx=None):
    bb = draw.textbbox((0, 0), text, font=font)
    tw = bb[2] - bb[0]
    x = (cx if cx else W // 2) - tw // 2
    draw.text((x, y), text, fill=color, font=font)

def draw_shadow(xy, r=20, offset=8, layers=5):
    """多層陰影"""
    x1, y1, x2, y2 = xy[0][0], xy[0][1], xy[1][0], xy[1][1]
    for i in range(layers, 0, -1):
        alpha_hex = hex(int(15 + i * 5))[2:].zfill(2)
        shade = f"#0A0A0A"
        rrect([(x1+offset*i//layers, y1+offset*i//layers,),
               (x2+offset*i//layers, y2+offset*i//layers)],
              fill=shade, r=r)

# ============================================================
# 標題
# ============================================================
draw.rectangle([(0, 0), (W, 100)], fill="#161B22")
ctxt("EMS TIMER — EXTERIOR CONCEPT", ft_title, 14, WHITE)
ctxt("HANDHELD DEVICE  ·  TOP VIEW  ·  SCALE ~1.5:1", ft_sub, 68, GRAY)

# ============================================================
# 裝置本體（居中）
# ============================================================
# 裝置尺寸概念：約 18cm x 10cm 的手持裝置
DEV_W, DEV_H = 700, 1300
DEV_X = W // 2 - DEV_W // 2 - 200  # 稍微偏左，右邊放標註
DEV_Y = 200

# 陰影
draw_shadow([(DEV_X, DEV_Y), (DEV_X+DEV_W, DEV_Y+DEV_H)], r=40, offset=15)

# 本體外殼
rrect([(DEV_X, DEV_Y), (DEV_X+DEV_W, DEV_Y+DEV_H)],
      fill=BODY_DARK, outline=BODY_EDGE, width=3, r=40)

# 內框線（機殼分模線效果）
rrect([(DEV_X+8, DEV_Y+8), (DEV_X+DEV_W-8, DEV_Y+DEV_H-8)],
      outline="#2A2A2C", width=1, r=36)

# ============================================================
# OLED 螢幕（上方 1/3）
# ============================================================
SCR_W, SCR_H = 440, 240
SCR_X = DEV_X + DEV_W // 2 - SCR_W // 2
SCR_Y = DEV_Y + 60

# 螢幕外框（塑膠邊框）
rrect([(SCR_X-12, SCR_Y-12), (SCR_X+SCR_W+12, SCR_Y+SCR_H+12)],
      fill="#0A0A0C", outline=SCREEN_BORDER, width=2, r=8)

# 螢幕顯示區
rrect([(SCR_X, SCR_Y), (SCR_X+SCR_W, SCR_Y+SCR_H)],
      fill=SCREEN_BG, outline="#1A1A1C", width=1, r=4)

# 螢幕模擬內容
draw.text((SCR_X+20, SCR_Y+15), "SESSION #047", fill="#00FF41", font=ft_screen_s)
draw.text((SCR_X+20, SCR_Y+42), "EPI       03:42", fill="#00FF41", font=ft_screen)
draw.text((SCR_X+20, SCR_Y+72), "CPR       01:15", fill="#00FF41", font=ft_screen)
draw.rectangle([(SCR_X+20, SCR_Y+102), (SCR_X+SCR_W-20, SCR_Y+104)], fill="#0A3A0A")
draw.text((SCR_X+20, SCR_Y+112), "COUNTDOWN 01:48", fill="#FFAA00", font=ft_screen)
draw.text((SCR_X+20, SCR_Y+142), "ELAPSED   08:23", fill="#00FF41", font=ft_screen)
# 狀態指示
draw.ellipse([(SCR_X+SCR_W-50, SCR_Y+15), (SCR_X+SCR_W-38, SCR_Y+27)], fill=RED)
draw.text((SCR_X+SCR_W-90, SCR_Y+148), "BLE", fill=BLUE, font=ft_screen_s)
draw.ellipse([(SCR_X+SCR_W-40, SCR_Y+150), (SCR_X+SCR_W-30, SCR_Y+160)], fill=BLUE)

# ============================================================
# 按鈕區（中下方 2/3）
# ============================================================
BTN_AREA_Y = SCR_Y + SCR_H + 50

# 按鈕配置：上排 4 顆 + 下排 4 顆
BTN_W, BTN_H = 130, 80
BTN_GAP_X = 22
BTN_GAP_Y = 30

# 計算按鈕區域起始 x（置中）
total_btn_w = 4 * BTN_W + 3 * BTN_GAP_X
btn_start_x = DEV_X + DEV_W // 2 - total_btn_w // 2

buttons = [
    # 上排
    [("EPI",     "注射 Epi",     YELLOW,  False),
     ("ATR",     "注射 Atropine",YELLOW,  False),
     ("CPR▶",   "CPR 開始",     GREEN,   False),
     ("CPR■",   "CPR 結束",     ORANGE,  False)],
    # 下排
    [("DEFIB",   "電擊",         YELLOW,  False),
     ("INTUB",   "插管",         YELLOW,  False),
     ("ARRV",    "到院",         GREEN,   False),
     ("●REC",    "錄音",         RED,     True)],
]

for row_i, row in enumerate(buttons):
    for col_i, (label, zh, color, is_special) in enumerate(row):
        bx = btn_start_x + col_i * (BTN_W + BTN_GAP_X)
        by = BTN_AREA_Y + row_i * (BTN_H + BTN_GAP_Y)

        # 按鈕凸起效果（3D 感）
        # 底層（陰影）
        rrect([(bx+3, by+3), (bx+BTN_W+3, by+BTN_H+3)],
              fill="#0A0A0C", r=12)
        # 按鈕邊緣
        btn_edge_color = color if is_special else BTN_EDGE
        rrect([(bx, by), (bx+BTN_W, by+BTN_H)],
              fill=BTN_FACE, outline=btn_edge_color, width=2, r=12)
        # 按鈕頂面（稍微內縮，凸起感）
        rrect([(bx+4, by+2), (bx+BTN_W-4, by+BTN_H-6)],
              fill="#3A3A3C", r=10)

        # 色條標識（按鈕左邊緣）
        draw.rectangle([(bx+8, by+12), (bx+12, by+BTN_H-16)], fill=color)

        # 按鈕文字
        ctxt(label, ft_btn_big, by+12, WHITE, bx+BTN_W//2+6)
        ctxt(zh, ft_btn_zh, by+BTN_H-28, DIM, bx+BTN_W//2+6)

# ============================================================
# 底部元件（麥克風孔、蜂鳴器孔、SD 卡槽）
# ============================================================
BOTTOM_Y = BTN_AREA_Y + 2 * (BTN_H + BTN_GAP_Y) + 50

# 蜂鳴器（左下）
BZ_CX = DEV_X + 140
BZ_CY = BOTTOM_Y + 80
BZ_R = 50
draw.ellipse([(BZ_CX-BZ_R, BZ_CY-BZ_R), (BZ_CX+BZ_R, BZ_CY+BZ_R)],
             fill="#0A0A0C", outline=DIM, width=2)
# 蜂鳴器孔陣列
for angle in range(0, 360, 30):
    for dist in [15, 28, 38]:
        hx = BZ_CX + int(dist * math.cos(math.radians(angle)))
        hy = BZ_CY + int(dist * math.sin(math.radians(angle)))
        draw.ellipse([(hx-2, hy-2), (hx+2, hy+2)], fill="#2A2A2C")
draw.text((BZ_CX-35, BZ_CY+BZ_R+10), "BUZZER", fill=DIM, font=ft_label_s)

# 麥克風（中下）
MIC_CX = DEV_X + DEV_W // 2
MIC_CY = BOTTOM_Y + 80
MIC_R = 20
draw.ellipse([(MIC_CX-MIC_R-8, MIC_CY-MIC_R-8),
              (MIC_CX+MIC_R+8, MIC_CY+MIC_R+8)],
             fill="#0A0A0C", outline=DIM, width=1)
# 麥克風小孔
for angle in range(0, 360, 45):
    for dist in [6, 12]:
        hx = MIC_CX + int(dist * math.cos(math.radians(angle)))
        hy = MIC_CY + int(dist * math.sin(math.radians(angle)))
        draw.ellipse([(hx-1, hy-1), (hx+1, hy+1)], fill="#2A2A2C")
draw.ellipse([(MIC_CX-3, MIC_CY-3), (MIC_CX+3, MIC_CY+3)], fill="#3A3A3C")
draw.text((MIC_CX-15, MIC_CY+MIC_R+15), "MIC", fill=DIM, font=ft_label_s)

# SD 卡槽（右下，側邊）
SD_X = DEV_X + DEV_W - 160
SD_Y = BOTTOM_Y + 55
SD_W, SD_H = 80, 50
rrect([(SD_X, SD_Y), (SD_X+SD_W, SD_Y+SD_H)],
      fill="#0A0A0C", outline=DIM, width=1, r=3)
# 插槽開口
draw.rectangle([(SD_X+10, SD_Y+8), (SD_X+SD_W-10, SD_Y+SD_H-8)], fill="#151515")
draw.text((SD_X+5, SD_Y+SD_H+10), "μSD SLOT", fill=DIM, font=ft_label_s)

# ============================================================
# LED 指示燈（螢幕下方）
# ============================================================
LED_Y = SCR_Y + SCR_H + 18
led_cx = DEV_X + DEV_W // 2
# 電源 LED
draw.ellipse([(led_cx-60-5, LED_Y-5), (led_cx-60+5, LED_Y+5)], fill=GREEN)
draw.text((led_cx-60-12, LED_Y+10), "PWR", fill=DIM, font=ft_btn_zh)
# BLE LED
draw.ellipse([(led_cx-5, LED_Y-5), (led_cx+5, LED_Y+5)], fill=BLUE)
draw.text((led_cx-12, LED_Y+10), "BLE", fill=DIM, font=ft_btn_zh)
# REC LED
draw.ellipse([(led_cx+60-5, LED_Y-5), (led_cx+60+5, LED_Y+5)], fill=RED)
draw.text((led_cx+60-12, LED_Y+10), "REC", fill=DIM, font=ft_btn_zh)

# ============================================================
# 尺寸標註線
# ============================================================
DIM_COLOR = "#586069"

# 高度標註（左側）
dim_x = DEV_X - 80
draw.line([(dim_x, DEV_Y), (dim_x, DEV_Y+DEV_H)], fill=DIM_COLOR, width=1)
draw.line([(dim_x-10, DEV_Y), (dim_x+10, DEV_Y)], fill=DIM_COLOR, width=1)
draw.line([(dim_x-10, DEV_Y+DEV_H), (dim_x+10, DEV_Y+DEV_H)], fill=DIM_COLOR, width=1)
# 旋轉文字用水平替代
draw.text((dim_x-70, DEV_Y+DEV_H//2-10), "~180mm", fill=DIM_COLOR, font=ft_dim)

# 寬度標註（上方）
dim_y = DEV_Y - 50
draw.line([(DEV_X, dim_y), (DEV_X+DEV_W, dim_y)], fill=DIM_COLOR, width=1)
draw.line([(DEV_X, dim_y-10), (DEV_X, dim_y+10)], fill=DIM_COLOR, width=1)
draw.line([(DEV_X+DEV_W, dim_y-10), (DEV_X+DEV_W, dim_y+10)], fill=DIM_COLOR, width=1)
ctxt("~100mm", ft_dim, dim_y-25, DIM_COLOR, DEV_X+DEV_W//2)

# ============================================================
# 右側標註（callout lines）
# ============================================================
NOTE_X = DEV_X + DEV_W + 120
CALLOUT_COLOR = "#3A3A3C"

annotations = [
    (SCR_Y + SCR_H // 2, "OLED DISPLAY", "0.96\" 128×64 I2C", SIGNAL_GREEN,
     "高對比度，救護現場可讀"),
    (BTN_AREA_Y + 40, "EVENT BUTTONS", "4 × 2 矩陣排列", YELLOW,
     "大間距防誤觸，色條快速辨識"),
    (BTN_AREA_Y + BTN_H + BTN_GAP_Y + 40, "FUNCTION BUTTONS", "電擊/插管/到院/錄音", ORANGE,
     "錄音鍵紅色邊框區隔"),
    (LED_Y, "STATUS LEDs", "PWR / BLE / REC", GREEN,
     "三色指示燈即時回饋"),
    (BOTTOM_Y + 80, "AUDIO / STORAGE", "蜂鳴器 + 麥克風 + SD卡", BLUE,
     "底部配置，不干擾操作"),
]

for i, (target_y, title, subtitle, color, desc) in enumerate(annotations):
    note_y = 260 + i * 180

    # 引出線
    # 從裝置邊緣到標註
    start_x = DEV_X + DEV_W + 4
    mid_x = NOTE_X - 30
    draw.line([(start_x, target_y), (mid_x, target_y), (mid_x, note_y + 15), (NOTE_X - 8, note_y + 15)],
              fill=CALLOUT_COLOR, width=1)
    # 端點圓點
    draw.ellipse([(start_x-4, target_y-4), (start_x+4, target_y+4)], fill=color)

    # 標註內容
    draw.text((NOTE_X, note_y), title, fill=color, font=ft_note_b)
    draw.text((NOTE_X, note_y + 28), subtitle, fill=WHITE, font=ft_note)
    draw.text((NOTE_X, note_y + 55), desc, fill=GRAY, font=ft_spec)

# ============================================================
# 左下：材質 & 規格
# ============================================================
SPEC_X = 80
SPEC_Y = 1700
draw.text((SPEC_X, SPEC_Y), "SPECIFICATIONS", fill=GRAY, font=ft_section)
SPEC_Y += 45

specs = [
    ("外殼材質", "ABS 塑膠 / 矽膠防滑側邊"),
    ("防護等級", "IPX4 防潑水（建議）"),
    ("供電", "USB-C 充電 / 18650 鋰電池"),
    ("續航", "估計 8-12 小時（OLED + BLE 間歇）"),
    ("重量", "預估 150-200g（含電池）"),
    ("按鈕力道", "200-300gf 觸覺回饋"),
    ("操作溫度", "0°C ~ 45°C"),
]

for label, value in specs:
    draw.text((SPEC_X, SPEC_Y), f"{label}:", fill=GRAY, font=ft_spec)
    draw.text((SPEC_X + 220, SPEC_Y), value, fill=WHITE, font=ft_spec)
    SPEC_Y += 32

# ============================================================
# 左下：操作流程
# ============================================================
FLOW_X = 80
FLOW_Y = SPEC_Y + 50
draw.text((FLOW_X, FLOW_Y), "OPERATION FLOW", fill=GRAY, font=ft_section)
FLOW_Y += 45

steps = [
    ("1", "長按電源鍵開機，螢幕亮起"),
    ("2", "自動建立新 Session，計時開始"),
    ("3", "按下事件按鈕 → 記錄時間戳 + 經過時長"),
    ("4", "倒數計時自動啟動 → 蜂鳴器提醒"),
    ("5", "按錄音鍵 → 口述備註存 SD 卡"),
    ("6", "事後 BLE 連線手機 → 傳輸紀錄"),
]

for num, desc in steps:
    # 步驟圓圈
    draw.ellipse([(FLOW_X, FLOW_Y+2), (FLOW_X+24, FLOW_Y+26)],
                 outline=GREEN, width=2)
    ctxt(num, ft_label_s, FLOW_Y+3, GREEN, FLOW_X+12)
    draw.text((FLOW_X + 36, FLOW_Y+2), desc, fill=WHITE, font=ft_spec)
    FLOW_Y += 38

# ============================================================
# 右下：側面視圖（簡易）
# ============================================================
SIDE_X = 2200
SIDE_Y = 1700
draw.text((SIDE_X, SIDE_Y), "SIDE VIEW", fill=GRAY, font=ft_section)
SIDE_Y += 50

# 簡易側面輪廓
sw, sh = 500, 80
rrect([(SIDE_X, SIDE_Y), (SIDE_X+sw, SIDE_Y+sh)],
      fill=BODY_DARK, outline=BODY_EDGE, width=2, r=15)

# USB-C 孔（底部）
draw.rectangle([(SIDE_X+sw-4, SIDE_Y+sh//2-8), (SIDE_X+sw+8, SIDE_Y+sh//2+8)],
               fill="#0A0A0C", outline=DIM, width=1)
draw.text((SIDE_X+sw+15, SIDE_Y+sh//2-10), "USB-C", fill=DIM, font=ft_label_s)

# SD 卡槽（右側面）
draw.rectangle([(SIDE_X+sw-100, SIDE_Y-4), (SIDE_X+sw-60, SIDE_Y+4)],
               fill="#0A0A0C", outline=DIM, width=1)
draw.text((SIDE_X+sw-105, SIDE_Y-25), "μSD", fill=DIM, font=ft_label_s)

# 螢幕凸起
draw.rectangle([(SIDE_X+40, SIDE_Y-3), (SIDE_X+220, SIDE_Y)],
               fill=SCREEN_BORDER)
draw.text((SIDE_X+80, SIDE_Y-25), "OLED", fill=GRAY, font=ft_label_s)

# 按鈕凸起
for bx_off in [250, 290, 330, 370]:
    draw.rectangle([(SIDE_X+bx_off, SIDE_Y-5), (SIDE_X+bx_off+25, SIDE_Y)],
                   fill=BTN_EDGE, outline=DIM, width=1)

# 厚度標註
draw.line([(SIDE_X-40, SIDE_Y), (SIDE_X-40, SIDE_Y+sh)], fill=DIM_COLOR, width=1)
draw.line([(SIDE_X-50, SIDE_Y), (SIDE_X-30, SIDE_Y)], fill=DIM_COLOR, width=1)
draw.line([(SIDE_X-50, SIDE_Y+sh), (SIDE_X-30, SIDE_Y+sh)], fill=DIM_COLOR, width=1)
draw.text((SIDE_X-80, SIDE_Y+sh//2-10), "~20mm", fill=DIM_COLOR, font=ft_label_s)

# ============================================================
# 底部
# ============================================================
draw.rectangle([(0, H-45), (W, H)], fill="#161B22")
ctxt("EMS TIMER v0.1  ·  EXTERIOR CONCEPT  ·  NOT FINAL", ft_label_s, H-36, GRAY)

# === 儲存 ===
out = "/Users/maxhero/Documents/MaxHero/Projects/ems_timer/ems_timer_exterior.png"
img.save(out, "PNG", dpi=(300, 300))
print(f"saved: {out}")
