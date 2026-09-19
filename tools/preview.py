#!/usr/bin/env python3
"""เรนเดอร์ทุกสถานะและจอทั้งใบเป็น PNG/GIF — dev loop ที่ไม่ต้องแตะบอร์ด

    python3 tools/preview.py           สร้างทุกอย่างลง out/
    python3 tools/preview.py --sheet   เฉพาะ contact sheet
    python3 tools/preview.py --limits  วัดว่าแต่ละป้ายรับได้กี่ช่อง (ที่มาของ Text.Limit)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

from gen import calendar, crypto, footer, mascot, pages, screen, stocks, topbar, weather  # noqa: E402,E501
from gen.config import L, PAL, REPO_DIR  # noqa: E402
from gen.props import BOX_X0, BOX_X1, BOX_Y0, BOX_Y1  # noqa: E402
from gen.render import quantize565, render_rects  # noqa: E402

OUT = REPO_DIR / "out"
# BOX_Y1 คือระดับฝ่าเท้าพอดี และไม่มีอะไรยื่นต่ำกว่านั้นอีกแล้ว
BOX = (BOX_X0, BOX_Y0, BOX_X1, BOX_Y1)
SESSION_WINDOW = L.usage.session_window
WEEKLY_WINDOW = L.usage.weekly_window
FRAMES = 12  # เฟรมต่อหนึ่งลูป (~1 วินาที)
LOOPS = 4  # GIF ยาวหลายลูป ไม่งั้นจะไม่มีวันเห็นการกะพริบตา
# ฉากมาสคอตเดินเล่น: หนึ่งเที่ยว = (320 + 2*96) / 34 + 2.5 ~ 17.6 วินาที
STROLL_LOOPS = 18
# รอบหมุนสมมติสำหรับ preview — บอร์ดคำนวณของจริงเอง (`ct_pages_position`)
# ที่นี่ตรึงไว้กลางรอบเพื่อให้เห็นทั้งสองแกนพร้อมกัน: หน้าไหนกำลังแสดง (pip กว้าง
# ไหลไปตามตำแหน่ง) และเหลือเวลาเท่าไรก่อนหมุน (รางที่พร่องไปแล้ว)
ROT_MS = 10_000


def _pos(page: str, left: float = 0.55) -> footer.Pos:
    """ทุกหน้าเปิดครบและปัดถึงได้ — ฉากที่หน้าถูกปิดเป็นเรื่องของบอร์ด ไม่ใช่ของ preview"""
    return footer.Pos(index=pages.PAGES.index(page), count=len(pages.PAGES),
                      left_ms=int(ROT_MS * left), total_ms=ROT_MS)


def _cell(state: str, phase: float, px: int, connected: bool = True,
          cycle: int = 0) -> Image.Image:
    return render_rects(
        mascot.build_centered(state, phase, connected, cycle), px, BOX, PAL.bg_slot
    )


def contact_sheet(px: int = 7, cols: int = 6) -> Image.Image:
    """ทุกสถานะเรียงในภาพเดียว — ใช้ตัดสินว่ามาสคอตสื่ออารมณ์ได้จริงไหม"""
    states = mascot.all_states()
    cw = round((BOX_X1 - BOX_X0) * px)
    ch = round((BOX_Y1 - BOX_Y0) * px)
    pad, label_h = 8, 16
    rows = (len(states) + cols - 1) // cols
    W = cols * (cw + pad) + pad
    H = rows * (ch + label_h + pad) + pad
    sheet = Image.new("RGB", (W, H), quantize565(PAL.bg))
    draw = ImageDraw.Draw(sheet)
    for i, st in enumerate(states):
        cx = pad + (i % cols) * (cw + pad)
        cy = pad + (i // cols) * (ch + label_h + pad)
        sheet.paste(_cell(st, 0.25, px), (cx, cy))
        draw.text((cx + cw / 2, cy + ch + label_h / 2), st, font=screen.font(12),
                  fill=quantize565(PAL.text), anchor="mm")
    return sheet


def state_gif(state: str, px: int = 7, connected: bool = True) -> list[Image.Image]:
    return [
        _cell(state, (f % FRAMES) / FRAMES, px, connected, f // FRAMES)
        for f in range(FRAMES * LOOPS)
    ]


SCENES: dict[str, screen.Screen] = {
    # ฉากที่ตัดสินกฎ "พูดชื่อเดียวครั้งเดียว": สอง session ของ tamaclaude เป็นป้ายเดียว
    # ที่มีเลขนับ · การ์ดใบบนเป็นของ tamaclaude ซึ่งมีมาสคอตยืนอยู่แล้ว หัวการ์ดจึงหายไป
    # เหลือประโยคเดียวตัวโต · ใบล่างเป็นของโปรเจกต์ที่ไม่มีป้ายบนจอ ชื่อจึงต้องอยู่
    "busy": screen.Screen(
        sessions=[
            screen.Session("tamaclaude x2", "building", 0.0),
            screen.Session("sprite-gen", "reading", 0.6),
        ],
        overflow=3,
        cards=[
            screen.Card("needs permission to run git push", "", "alert"),
            screen.Card("infra-scripts", "Stopped - waiting for your reply", "info"),
            screen.Card("sprite-gen", "Build finished, 0 warnings", "done"),
        ],
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 88, 42 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 61, 3 * 86400),
        ],
    ),
    "idle": screen.Screen(
        sessions=[screen.Session("tamaclaude", "sleeping", 0.0)],
        clock="02:14",
    ),
    # โควตาปกติ — สภาพที่จอจะเป็นเกือบตลอดเวลาที่ไม่มีอะไรต้องเตือน
    "usage": screen.Screen(
        sessions=[
            screen.Session("tamaclaude", "writing", 0.0),
            screen.Session("docs", "reading", 0.4),
        ],
        clock="17:04",
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 35, 3 * 3600 + 5 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 48, 31 * 3600),
        ],
    ),
    # ใกล้เต็มทั้งคู่ + ใช้เร็วกว่าเวลา (ขีด pace อยู่ซ้ายของเนื้อแถบ)
    "usage_hot": screen.Screen(
        sessions=[screen.Session("tamaclaude", "building", 0.0)],
        clock="09:41",
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 92, 4 * 3600 + 20 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 71, 2 * 86400 + 5 * 3600),
        ],
    ),
    # หน้าต่างหมุนไปแล้ว + weekly หายไปทั้งตัว — ทั้งคู่ต้องเป็น `--` ห้ามเดา
    "usage_unknown": screen.Screen(
        sessions=[screen.Session("tamaclaude", "idle", 0.0)],
        clock="06:20",
        usage=[
            screen.Usage("Current", SESSION_WINDOW, None, 0),
            screen.Usage("Weekly", WEEKLY_WINDOW, None, None),
        ],
    ),
    # ภาษาไทยบนการ์ด — วาดด้วยฟอนต์บิตแมปตัวเดียวกับที่แฟลชลงบอร์ด ไม่ใช่ TTF ต้นฉบับ
    # (ADR-0008) ฉากนี้คือที่ที่ตำแหน่งวรรณยุกต์ถูกตัดสินก่อนเห็นของจริง: ที่ (วรรณยุกต์
    # เหนือสระบน) · ปั๊ (ฐานหางสูง) · ญู (สระล่างใต้ฐานหางยาว) อยู่ในบรรทัดเดียวกันหมด
    "thai": screen.Screen(
        sessions=[screen.Session("tamaclaude", "thinking", 0.0)],
        clock="09:41",
        cards=[
            screen.Card("ประชุมทีม", "ที่ปั๊มน้ำมัน 10:30", "info"),
            screen.Card("กตัญญู", "ฝั่งโน้น ญู ฐู ฟ้า ปี", "alert"),
        ],
    ),
    "done": screen.Screen(
        sessions=[
            screen.Session("tamaclaude", "celebrate", 0.0),
            screen.Session("docs", "idle", 0.4),
        ],
        cards=[screen.Card("Build finished, 0 warnings", "", "done")],
    ),
    # มีทั้งการ์ดและตัวเลขโควตาค้างอยู่ในมือ แต่ต้องไม่ขึ้นจอสักอย่าง — หลุดลิงก์แล้ว
    # ไม่มีใครรับรองว่ายังจริง รวมถึงตัวนาฬิกาเอง กลางจอจึงเหลือระยะเวลาที่หลุด
    # (บอร์ดนับเอง) กับเวลาล่าสุดที่เคยได้ยิน พูดเป็นอดีตกาล
    "offline": screen.Screen(
        sessions=[
            screen.Session("tamaclaude", "idle", 0.0),
            screen.Session("docs", "idle", 0.5),
        ],
        connected=False,
        offline_s=4 * 60,
        cards=[screen.Card("Needs your answer", "", "alert")],
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 43, 1 * 3600 + 40 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 8, 5 * 86400 + 8 * 3600),
        ],
    ),
    # BLE หลุด บอร์ดขึ้นเน็ตแล้วแต่ Mac ยังหาไม่เจอ — ข้อมูลบนจอตายเหมือน "offline"
    # ต่างกันที่ไอคอนซึ่งเป็นคลื่น WiFi ส่วนป้ายซ้ายพูดว่า "no link" เหมือนกัน: ที่อยู่ของบอร์ด
    # อยู่ในหน้าตั้งค่าบน Mac ที่เดียว ซึ่งคือที่ที่ผู้ใช้ต้องเอาไปกรอกอยู่แล้ว
    "wifi": screen.Screen(
        sessions=[screen.Session("tamaclaude", "idle", 0.0)],
        connected=False,
        wifi=True,
        offline_s=3 * 3600 + 12 * 60,
        cards=[screen.Card("Needs your answer", "", "alert")],
    ),
    # เพิ่งแฟลชเสร็จ ยังไม่เคยจับคู่กับ Mac เลย — จอแรกที่ผู้ใช้ใหม่เห็น ไม่มีเวลาให้อ้างถึง
    # และตัวเลขที่เดินคือเวลาตั้งแต่เสียบไฟ ซึ่งเป็นคำตอบที่ถูกของ "รออะไรอยู่"
    "cold": screen.Screen(
        sessions=[],
        connected=False,
        clock=screen.CLOCK_UNKNOWN,
        date="",
        offline_s=48,
    ),
    # BLE หลุดแต่ snapshot ยังเดินทางมาทาง LAN — ข้อมูลสดทั้งจอ ไอคอนเป็นคลื่น WiFi
    "lan": screen.Screen(
        sessions=[
            screen.Session("tamaclaude", "writing", 0.0),
            screen.Session("docs", "idle", 0.5),
        ],
        connected=True,
        ble=False,
        wifi=True,
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 43, 1 * 3600 + 40 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 8, 5 * 86400 + 8 * 3600),
        ],
    ),
    # ไม่มี session เลย — มาสคอตเดินข้ามแถบ slot ที่ว่างอยู่
    "empty": screen.Screen(
        sessions=[],
        clock="14:22",
        date="Mon 27 Jul",
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 35, 3 * 3600 + 5 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 48, 31 * 3600),
        ],
    ),
    # ฉากเดียวกันตอนโควตาใกล้ตัน — คู่เทียบของ "empty" ที่พิสูจน์ว่าตัวละครรู้จักตัวเลข
    # ใต้เท้ามัน: เดินช้าลงจาก 34 เหลือ 17 px/s และไม่มีท่าฉลองอยู่ในชุดท่าอีกต่อไป
    # (เทียบสองภาพนี้เป็น GIF คู่กัน จังหวะเท้าคือสิ่งที่ต่าง ไม่ใช่แค่สีของแถบ)
    "empty_hot": screen.Screen(
        sessions=[],
        clock="21:07",
        date="Mon 27 Jul",
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 92, 40 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 88, 2 * 86400),
        ],
    ),
    # ทั้งสองใบเป็นของโปรเจกต์ที่มีมาสคอตอยู่บนจอ — การ์ดจึงเป็นสองประโยคตัวโต
    # ไม่มีชื่อโปรเจกต์โผล่ซ้ำสักตัว · ป้าย "x2" คือตัวที่ยกมือ (waiting) ชนะตัวที่กำลังค้นหา
    "waiting": screen.Screen(
        sessions=[
            screen.Session("tamaclaude x2", "waiting", 0.0),
            screen.Session("sprite-gen", "alert", 0.25),
        ],
        cards=[
            screen.Card("Stopped: test suite failed (3 failing)", "", "alert"),
            screen.Card("needs permission to write layout.h", "", "info"),
        ],
    ),
    # คำถามกินแถบการ์ดทั้งแถบ — ฉากนี้จึงมีทั้งการ์ดและโควตาค้างอยู่ *โดยตั้งใจ*
    # ภาพที่ออกมาต้องไม่มีทั้งสองอย่าง ซึ่งเป็นสิ่งเดียวที่พิสูจน์ว่ากองการ์ดหลบจริง
    # ไม่ใช่บังเอิญไม่มีอะไรจะวาด · แถบบนยังมีเปอร์เซ็นต์อยู่ เพราะแผงเต็มหลบ ไม่ใช่ตัวเลขหาย
    "ask": screen.Screen(
        sessions=[screen.Session("tamaclaude", "waiting", 0.0)],
        clock="11:20",
        ask=screen.Ask("Bash: npm test"),
        cards=[screen.Card("sprite-gen", "Build finished, 0 warnings", "done")],
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 35, 3 * 3600 + 5 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 48, 31 * 3600),
        ],
    ),
    # คำสั่งที่ `Risk` ฝั่ง Mac ไม่ยอมให้นิ้วเดียวอนุมัติ — ปุ่มขวาไม่ได้หายไป มันเปลี่ยนเป็น
    # คำที่บอกว่าเรื่องนี้ต้องใช้คุณจริงๆ · การ์ดที่มีปุ่มเดียวบอกแค่ว่าทำอะไรไม่ได้
    "ask_keyboard": screen.Screen(
        sessions=[screen.Session("tamaclaude", "waiting", 0.0)],
        clock="11:21",
        ask=screen.Ask("Bash: rm -rf build", may_allow=False),
    ),
}


# หน้าอากาศ — หน้าที่สองของจอ วาดจากค่าคงที่ชุดเดียวกับ firmware
# ฉากถูกเลือกให้ครบสิ่งที่ต้องตัดสินด้วยตา: สัญลักษณ์ทุกกลุ่ม · ชื่อเมืองภาษาไทย ·
# อายุข้อมูลที่ยังปกติกับที่เก่าจนต้องตะโกน · หน้าที่ยังไม่เคยได้ข้อมูล · ลิงก์หลุด
WEATHER_SCENES: dict[str, weather.Weather] = {
    "rain": weather.Weather(mascot_state="writing"),
    "clear": weather.Weather(place="Chiang Mai", temp=36, high=38, low=27, code=0,
                             age=45, mascot_state="waiting"),
    # หิมะตอนเช้ามืด — ฟ้า dawn เป็นช่วงที่แคบที่สุด (2 ชม.) และเป็นช่วงที่หมึกยังไม่กลับขั้ว
    "cold": weather.Weather(place="Sapporo", temp=28, high=31, low=19, code=73, unit="F",
                            age=40 * 60, mascot_state="sleeping",
                            hour_start=6, hours=[weather.Hour(t, 73) for t in
                                                 (27, 27, 28, 29, 30)],
                            bar=topbar.Bar(clock="06:20", has_usage=True, pct=12)),
    "storm": weather.Weather(place="กรุงเทพ", temp=29,
                             high=33, low=26, code=95, age=12 * 60, mascot_state="alert"),
    # หมอกตอนพลบค่ำ — ฉากเดียวที่ไม่มีแนวเมฆเลย และช่วงเวลาที่เหลืออีกช่วง
    "fog": weather.Weather(place="Chiang Rai", temp=19, high=24, low=17, code=45,
                           age=3 * 60, mascot_state="thinking",
                           hour_start=18, hours=[weather.Hour(t, c) for t, c in
                                                 ((18, 45), (18, 45), (17, 3), (17, 0),
                                                  (16, 0))],
                           bar=topbar.Bar(clock="18:05", has_usage=True, pct=44)),
    # กลางคืนโล่ง — ดวงในไอคอนต้องเป็นดวงจันทร์ ไม่ใช่ดวงอาทิตย์ซีด และคอลัมน์ที่ข้าม
    # 05:00 ต้องกลับเป็นดวงอาทิตย์เองทีละคอลัมน์
    "night": weather.Weather(place="Bangkok", temp=26, high=33, low=25, code=1,
                             age=8 * 60, mascot_state="idle",
                             hour_start=3, hours=[weather.Hour(t, c) for t, c in
                                                  ((26, 1), (25, 0), (25, 0), (26, 0),
                                                   (28, 2))],
                             bar=topbar.Bar(clock="02:40", has_usage=True, pct=8)),
    # เฟรมที่ไม่มีพยากรณ์มาด้วย — พื้นดินว่างแต่ไม่ใช่จอเปล่า (ADR-0002)
    "nohours": weather.Weather(place="Bangkok", temp=31, high=34, low=26, code=3,
                               age=6 * 60, hour_start=-1, hours=[],
                               mascot_state="reading"),
    # เก่าเกิน 10 เท่าของรอบดึง — ตัวเลขยังอ่านได้ แต่ต้องไม่มีใครเข้าใจว่ามันสด
    "stale": weather.Weather(place="Bangkok", temp=31, high=34, low=26, code=3,
                             age=4 * 3600, mascot_state="thinking",
                             # BLE ตายแต่ snapshot ยังมาทาง LAN — ไอคอนเป็นคลื่น ป้ายยังเป็นชื่อหน้า
                             bar=topbar.Bar(clock="21:15", ble=False, wifi=True,
                                            has_usage=True, pct=61)),
    # ยังไม่เคยได้เฟรมของหน้านี้เลย — ห้ามเป็นจอเปล่าหรือโครงว่าง (ADR-0002)
    "empty": weather.Weather(has_frame=False, mascot_state="idle"),
    # Mac หายไป: ตัวเลขค้างอยู่แต่ไม่มีใครรับรองแล้ว และมาสคอตจิ๋วหายไปทั้งตัว
    # (หายไป = ไม่มี Mac · หลับ = ไม่มี session)
    "offline": weather.Weather(place="Bangkok", age=95 * 60, connected=False),
}


# หน้าคริปโต — ใบแรกที่มี watchlist หน้าหุ้นจะยืมโครงนี้ไปใช้ต่อ
# รูป 24 ชม. สำหรับฉากสาธิต — ระดับ 0..15 สิบหกจุด อย่างที่มันมาถึงบอร์ด
# จุดแรกคือเส้นฐาน ฉะนั้นจุดสุดท้ายเทียบจุดแรกต้องไปทางเดียวกับเปอร์เซ็นต์ของแถวนั้น
# ไม่งั้นฉากสาธิตจะพิสูจน์เลย์เอาต์ที่เล่าเรื่องขัดกันเอง
_DIP = [11, 12, 10, 9, 11, 8, 6, 4, 5, 3, 2, 4, 6, 5, 7, 6]     # ลง
_CLIMB = [5, 4, 6, 5, 7, 6, 8, 9, 8, 10, 12, 11, 13, 12, 14, 13]  # ขึ้น
_FLAT = [8, 9, 7, 8, 9, 7, 8, 8, 9, 7, 8, 9, 8, 7, 9, 8]        # นิ่ง
_SPIKE = [2, 2, 3, 2, 4, 3, 5, 4, 7, 6, 9, 8, 12, 11, 15, 14]   # ขึ้นแรง

# ฉากถูกเลือกให้ครบสิ่งที่ต้องตัดสินด้วยตา: ราคาที่ยาวไม่เท่ากันเรียงหลักตรงกัน · ขึ้น ลง
# และนิ่งในจอเดียว · watchlist ที่ไม่เต็มห้าตัว · หน้าที่ยังไม่เคยได้ข้อมูล · ลิงก์หลุด
# ราคา >= 1 มีทศนิยมสองตำแหน่งเสมอ ส่วนต่ำกว่า 1 ได้ความจริงเต็ม (ดู `decimals(for:)`)
CRYPTO_SCENES: dict[str, crypto.Crypto] = {
    "full": crypto.Crypto(coins=[
        crypto.Coin("BTC", "64,230.00", -21, _DIP),
        crypto.Coin("ETH", "3,125.00", 11, _CLIMB),
        crypto.Coin("SOL", "172.05", 30, _CLIMB),
        crypto.Coin("DOGE", "0.1423", -5, _DIP),
        crypto.Coin("PEPE", "0.000008", 140, _SPIKE),
    ], mascot_state="writing"),
    # สามตัวต้องดูเหมือน watchlist สามตัว ไม่ใช่ห้าตัวที่หายไปสอง
    # แถวเต็มความกว้างที่สุดของหน้านี้อยู่ใต้มาสคอตพอดี — ฉากนี้พิสูจน์ว่ามันไม่ทับกัน
    "short": crypto.Crypto(coins=[
        crypto.Coin("BTC", "64,230.00", 0, _FLAT),
        crypto.Coin("ETH", "3,125.00", -152, _DIP),
        crypto.Coin("XRP", "2.41", 3, _CLIMB),
    ], age=8, mascot_state="waiting",
        bar=topbar.Bar(clock="17:04", overflow=3, has_usage=True, pct=92)),
    # เหรียญเดียว = หน้าที่ยังตั้งไม่เสร็จ ไม่ใช่ watchlist ที่ครบแล้ว — ฉากเดียวที่มีคำใบ้
    # และฉากเดียวที่จำนวนเต็มยาวเกิน `int_digits_max` จนราคาต้องลดทั้งก้อนลงมา
    "one": crypto.Crypto(coins=[
        crypto.Coin("BTC", "104,250.00", 62, _SPIKE),
    ], age=20, mascot_state="thinking"),
    # เก่าเกิน 10 เท่าของรอบดึง — ราคายังอ่านได้ แต่ต้องไม่มีใครเข้าใจว่ามันสด
    # และเป็นฉากที่บริการไม่ให้ประวัติมา: กล่องรูปต้องเป็นเส้นเปล่า ไม่ใช่กล่องว่าง
    "stale": crypto.Crypto(coins=[
        crypto.Coin("BTC", "64,230.00", -21),
        crypto.Coin("ETH", "3,125.00", 11),
    ], age=3 * 3600),
    # ยังไม่เคยได้เฟรมของหน้านี้เลย — ห้ามเป็นจอเปล่าหรือโครงว่าง (ADR-0002)
    "empty": crypto.Crypto(has_frame=False),
    # Mac หายไป: ตัวเลขค้างอยู่แต่ไม่มีใครรับรองแล้ว ลูกศรกับรูปยังบอกทิศได้โดยไม่ต้องมีสี
    # และการ์ดต้องกลับไปเป็นพื้นกลาง — ใบเขียวตอนไม่มีใครรับรองคือการโกหก
    "offline": crypto.Crypto(coins=[
        crypto.Coin("BTC", "64,230.00", -21, _DIP),
        crypto.Coin("ETH", "3,125.00", 11, _CLIMB),
    ], age=45 * 60, connected=False),
}


# หน้าหุ้น — โครงเดียวกับหน้าคริปโต บวกสิ่งที่หุ้นมีแต่คริปโตไม่มี: ตลาดที่ปิดได้
# ฉากถูกเลือกให้ครบสิ่งที่ต้องตัดสินด้วยตา: ขึ้น ลง นิ่งในจอเดียว · ตลาดปิดที่ต้องไม่อ่าน
# ว่าท่อพัง · เฟรมที่ถูกบีบจนไม่มีคอลัมน์เปอร์เซ็นต์ · หน้าที่ยังไม่เคยได้ข้อมูล · ลิงก์หลุด ·
# แถบช่วงราคาที่ตัวเลขยาวสุดและหมุดชนปลายราง ซึ่งเป็นสองขีดจำกัดของบล็อกนั้น
STOCK_SCENES: dict[str, stocks.Stocks] = {
    "full": stocks.Stocks(rows=[
        stocks.Stock("AAPL", "189.44", -21),
        stocks.Stock("MSFT", "412.90", 11),
        stocks.Stock("NVDA", "1,204.55", 30),
        stocks.Stock("TSLA", "177.02", -152),
        stocks.Stock("BRK.B", "412.10", 0),
    ], day_range=stocks.DayRange("184", "193", 60), age=25, mascot_state="thinking"),
    # สองตัวต้องดูเหมือน watchlist สองตัว ไม่ใช่ห้าตัวที่หายไปสาม
    "short": stocks.Stocks(rows=[
        stocks.Stock("AAPL", "189.44", 3),
        stocks.Stock("SPY", "534.21", -8),
    ], day_range=stocks.DayRange("187", "191", 61), age=8),
    # ขีดจำกัดของแถบช่วงราคา: ตัวเลขห้าหลักที่ยาวสุดเท่าที่บล็อกรับไหว และหมุดที่ชนปลายราง
    # ขวาพอดี (ราคาปิดที่จุดสูงสุดของวัน ซึ่งเกิดจริงในวันที่ขึ้นแรง) — หมุดต้องไม่ล้นออกไป
    "range_edge": stocks.Stocks(rows=[
        stocks.Stock("NVDA", "1,204.55", 74),
        stocks.Stock("AAPL", "189.44", 3),
    ], day_range=stocks.DayRange("1,121", "1,204", 100), age=12),
    # ตลาดปิด: ราคาเก่าเป็นชั่วโมงคือเรื่องปกติ บรรทัดล่างต้องอธิบาย ไม่ใช่ตะโกนว่า stale
    "closed": stocks.Stocks(rows=[
        stocks.Stock("AAPL", "189.44", -21),
        stocks.Stock("MSFT", "412.90", 11),
        stocks.Stock("NVDA", "1,204.55", 30),
    ], day_range=stocks.DayRange("186", "192", 57), age=11 * 3600, market_closed=True,
        mascot_state="sleeping"),
    # เฟรมที่ถูกบีบจนต้องทิ้งคอลัมน์เปอร์เซ็นต์ — ราคายังครบ ทิศทางหายไปทั้งหน้า และ
    # ช่วงราคาก็ถูกทิ้งไปกับมันในขั้นก่อนหน้า (ดู `StocksFrame.encoded`) บล็อกจึงหายทั้งบล็อก
    "no_pct": stocks.Stocks(rows=[
        stocks.Stock("AAPL", "189.44", None),
        stocks.Stock("MSFT", "412.90", None),
        stocks.Stock("NVDA", "1,204.55", None),
    ], age=40, bar=topbar.Bar(clock="06:20", has_usage=True, pct=None)),
    # ยังไม่เคยได้เฟรมของหน้านี้เลย — ห้ามเป็นจอเปล่าหรือโครงว่าง (ADR-0002)
    "empty": stocks.Stocks(has_frame=False),
    # Mac หายไป: ตัวเลขค้างอยู่แต่ไม่มีใครรับรองแล้ว ลูกศรยังบอกทิศได้โดยไม่ต้องมีสี
    # หมุดชนปลายซ้าย = ปิดที่จุดต่ำสุดของวัน ซึ่งเข้ากับ -2.1% ที่อยู่ข้างบนพอดี
    "offline": stocks.Stocks(rows=[
        stocks.Stock("AAPL", "189.44", -21),
        stocks.Stock("MSFT", "412.90", 11),
    ], day_range=stocks.DayRange("189", "196", 0), age=45 * 60, connected=False),
}


# หน้าปฏิทิน — ใบเดียวที่อ่านข้อมูลจากเครื่องเอง ไม่ใช่จากเน็ต (ADR-0005)
# ฉากถูกเลือกให้ครบสิ่งที่ต้องตัดสินด้วยตา: ชื่อนัดภาษาไทยที่มีวรรณยุกต์เหนือสระบน ·
# ชื่อที่ยาวจนถูกตัด · นัดทั้งวันที่ไม่มีเวลา · ทุกสภาพที่ไม่มีนัดให้แสดง · ลิงก์หลุด
CALENDAR_SCENES: dict[str, calendar.Calendar] = {
    "full": calendar.Calendar(events=[
        calendar.Appointment("Today", "09:30", "ประชุมทีมที่ปั๊มน้ำมัน"),
        calendar.Appointment("Today", "14:00", "1:1 with Ann"),
        calendar.Appointment("Tomorrow", "all day", "กตัญญู ฝั่งโน้น ฐู ฟ้า ปี"),
    ], age=95, mins=44, mascot_state="alert"),
    # นัดถัดไปเป็นนัดทั้งวัน — ไม่มีอะไรให้นับถอยหลัง ช่องขวาของการ์ดจึงเป็นชื่อวันแทน
    # การ์ดที่ว่างครึ่งขวาอ่านเป็นการ์ดที่โหลดไม่ครบ
    "allday": calendar.Calendar(events=[
        calendar.Appointment("Tomorrow", "all day", "กตัญญู ฝั่งโน้น ฐู ฟ้า ปี"),
        calendar.Appointment("Fri", "08:15", "Flight BKK -> CNX"),
    ], age=30),
    # นัดที่เริ่มแล้วระหว่างที่เฟรมยังไม่ถึงรอบถัดไป — ตัวนับถอยหลังเดินด้วยอายุข้อมูล
    # ตัวเดียวกับบรรทัดล่าง มันจึงถึง "now" เองโดยไม่ต้องรอเฟรมใหม่
    "now": calendar.Calendar(events=[
        calendar.Appointment("Today", "09:30", "ยืนคุยกับทีมหน้าไวท์บอร์ด"),
        calendar.Appointment("Today", "14:00", "1:1 with Ann"),
    ], age=280, mins=3),
    # นัดพรุ่งนี้เช้าที่ยังอยู่ในระยะนับถอยหลัง — ชื่อวันต้องอยู่ *ด้วย* ไม่ใช่ถูกตัวนับ
    # แทนที่ ไม่งั้น "07:00" อ่านเป็นเช้านี้ ซึ่งเป็นคนละวันกับที่มันหมายถึง
    "tomorrow_soon": calendar.Calendar(events=[
        calendar.Appointment("Tomorrow", "07:00", "รถรับที่ล็อบบี้"),
        calendar.Appointment("Tomorrow", "09:30", "ประชุมทีมที่ปั๊มน้ำมัน"),
    ], age=45, mins=9 * 60),
    # นัดเดียวต้องดูเหมือนนัดเดียว ไม่ใช่สามนัดที่หายไปสอง — และไม่มีสันเวลาเพราะ
    # ไม่มีแถวให้กั้น
    "one": calendar.Calendar(events=[
        calendar.Appointment("Tomorrow", "10:00", "หมอฟัน"),
    ], age=12, mins=1490),
    # ชื่อยาวเต็มเพดานสองบรรทัดของการ์ด — ตัวตัดคำต้องตรงกับ LVGL และจุดไข่ปลาต้องอยู่
    # ท้ายคลัสเตอร์ ไม่ใช่กลางวรรณยุกต์
    "long": calendar.Calendar(events=[
        calendar.Appointment("Today", "16:45",
                             "ประชุมที่ปั๊มน้ำมันกับผู้รับเหมาเรื่องหลังคาใหม่และท่อ"),
        calendar.Appointment("Fri", "08:15", "Flight BKK -> CNX with the whole team"),
    ], age=200, mins=430),
    # สามใบที่เหลือของพื้นการ์ด — ช่วงเวลาของ *นัด* ไม่ใช่ของตอนนี้ (นาฬิกาบนแถบบน
    # เป็น 14:32 ทั้งสามฉาก) ใบกลางคืนคือใบที่พิสูจน์ว่าขอบ 1px จำเป็น
    "dusk": calendar.Calendar(events=[
        calendar.Appointment("Today", "17:30", "ซ้อมวิ่งกับกลุ่มที่สวน"),
        calendar.Appointment("Today", "19:00", "Dinner with the Chens"),
    ], age=40, mins=175),
    "night": calendar.Calendar(events=[
        calendar.Appointment("Today", "21:00", "โทรหาทีมที่ซานฟราน"),
        calendar.Appointment("Tomorrow", "06:30", "Inter Miami CF - Atlanta"),
    ], age=25, mins=385),
    "dawn": calendar.Calendar(events=[
        calendar.Appointment("Tomorrow", "05:40", "รถไปสนามบิน"),
        calendar.Appointment("Tomorrow", "08:15", "Flight BKK -> CNX"),
    ], age=90, mins=920),
    # สัปดาห์ที่ว่างจริงๆ — หน้านี้กลายเป็นปฏิทินตั้งโต๊ะ ไม่ใช่ประโยคบอกว่าไม่มีอะไร
    "empty": calendar.Calendar(state=calendar.EMPTY, age=60),
    # ว่างเหมือนกันแต่ยังไม่เคยได้ snapshot เลย จึงยังไม่รู้ว่าวันนี้วันอะไร — ถอยกลับไป
    # สองบรรทัดเดิม ห้ามโชว์ช่องว่างตัวใหญ่กลางจอ
    "empty_nodate": calendar.Calendar(state=calendar.EMPTY, age=60, date=""),
    # วันที่ก็ค้างเป็นเทาเหมือนนาฬิกาเมื่อ Mac หายไป ไม่ใช่หายไปทั้งบรรทัด
    "empty_offline": calendar.Calendar(state=calendar.EMPTY, age=40 * 60, connected=False),
    # ถูกปฏิเสธสิทธิ์ TCC ไปแล้ว — ทางแก้อยู่ที่ System Settings เท่านั้น
    # สิทธิ์ปฏิทินไม่เกี่ยวกับสถานะ session — มาสคอตยังต้องอยู่บนหน้าที่บอกว่าอ่านไม่ได้
    "denied": calendar.Calendar(state=calendar.NEEDS_ACCESS, age=20, mascot_state="writing"),
    # ยังไม่เคยถูกถามเลย — ก้าวถัดไปคือปุ่มในแอป ไม่ใช่ System Settings ที่ยังไม่มีแถวให้กด
    "unasked": calendar.Calendar(state=calendar.NOT_ASKED, age=20),
    # ได้สิทธิ์แล้วแต่ยังไม่ได้ติ๊กปฏิทินสักใบ (จอนี้วางให้คนอื่นเห็นได้)
    "unpicked": calendar.Calendar(state=calendar.NO_CALENDARS, age=20),
    # Mac หายไป: นัดที่ค้างอยู่เป็นเทา เหมือนราคาบนหน้าคริปโต
    "offline": calendar.Calendar(events=[
        calendar.Appointment("Today", "09:30", "ประชุมทีมที่ปั๊มน้ำมัน"),
        calendar.Appointment("Wed", "13:00", "Design review"),
    ], age=50 * 60, mins=20, connected=False),
}


# แถบบนของหน้าย่อย — มาจาก snapshot ของหน้ามาสคอต ไม่ใช่จากเฟรมของหน้านั้น
# ฉากส่วนใหญ่ใช้ใบเดียวกัน เพราะแถบไม่ใช่สิ่งที่ฉากเหล่านั้นทดสอบ ส่วนฉากที่ตั้ง `bar` เอง
# คือฉากที่ตรวจตัวแถบตรงๆ: ลิงก์ที่วิ่งบน LAN · โควตาที่ไม่รู้ค่า · session ที่ล้นสามช่อง
PAGE_BAR = topbar.Bar(clock="09:41", has_usage=True, pct=35)


def _fill_bars() -> None:
    blank = topbar.Bar()
    for scenes in (WEATHER_SCENES, CRYPTO_SCENES, STOCK_SCENES, CALENDAR_SCENES):
        for sc in scenes.values():
            if sc.bar == blank:
                sc.bar = PAGE_BAR


_fill_bars()


# ฉากตรวจท้องฟ้า — ไม่มี session เลย ซึ่งเป็นสภาพที่จอเป็นเกือบตลอดเวลา
# และเป็นตอนที่ฟ้าโล่งที่สุด ส่วนตอนถูกมาสคอตบังดูได้จาก screen_busy/waiting
SKY_CLOCKS = {"dawn": "05:40", "day": "12:00", "dusk": "18:10", "night": "02:14"}

# ฟ้าที่รู้จักสภาพอากาศแล้ว (ADR-0012) — **ไม่ใช่เมทริกซ์เต็ม** 6 bucket x 4 ช่วง = 24 ใบ
# ที่ไม่มีใครไล่ดูครบ · หกใบนี้เลือกจากจุดที่การออกแบบพังได้จริง อีกสิบแปดช่องที่เหลือ
# เป็นการรวมกันของสิ่งที่หกใบนี้พิสูจน์ไปแล้ว:
#   cloud_day   ดวงตอนสูงสุดต้องโผล่ออกมาจากใต้ขอบ deck ไม่ถูกกลืนทั้งดวง
#   cloud_night ดาวเหลือ 6 ดวงและยังกะพริบ (ไม่มีเมฆลอยตอนกลางคืน = ต้องมีอะไรขยับ)
#   rain_day    แท่งฝนกับแนวเมฆบนฟ้าสว่าง + ฝนถูกพื้นดินตัดที่เส้นขอบฟ้า
#   snow_night  เกล็ดสีขาวบนฟ้ามืด (ฝั่งกลับของ wx_flake_ink)
#   fog_dusk    ทางเดียวที่ไม่มี deck เลย
#   storm_day   palette ทั้งชุดถูกแทน: ฟ้า พื้นดิน หญ้า เงา + สายฟ้าที่ต้องไม่อยู่หลังมาสคอต
SKY_WX = {
    "cloud_day": ("12:00", 3),
    "cloud_night": ("02:14", 3),
    "rain_day": ("12:00", 61),
    "snow_night": ("02:14", 73),
    "fog_dusk": ("18:10", 45),
    "storm_day": ("12:00", 95),
}


def sky_scene(clock: str, code: int | None = None) -> screen.Screen:
    return screen.Screen(
        sessions=[],
        clock=clock,
        date="Mon 27 Jul",
        weather_code=code,
        usage=[
            screen.Usage("Current", SESSION_WINDOW, 35, 3 * 3600 + 5 * 60),
            screen.Usage("Weekly", WEEKLY_WINDOW, 48, 31 * 3600),
        ],
    )


# ป้ายที่ daemon ต้องตัดข้อความเองก่อนส่ง — (ชื่อใน Text.Limit, กว้างกี่พิกเซล, ฟอนต์บนบอร์ด)
#
# ความกว้างมาจากค่าที่ ct_ui.c ตั้งให้ป้ายจริง ไม่ใช่ค่าที่ preview อยากให้เป็น: ป้ายบนบอร์ด
# ตัดด้วย LV_LABEL_LONG_DOT อยู่แล้ว เลขที่สูงเกินจึงไม่ทำให้ข้อความล้นทับอะไร แต่มันแปลว่า
# daemon จ่ายไบต์ (ไทยตัวละ 3) ให้ตัวอักษรที่ไม่มีวันขึ้นจอ และการตัดไปอยู่ที่บอร์ดแทนที่จะ
# อยู่ที่เดียวกับ "..." ที่ daemon เติม
#
# `box` คือความสูงที่ป้ายนั้นมีจริงก่อนจะไปชนของชิ้นถัดไป — None แปลว่าไม่มีอะไรอยู่ใต้มัน
# ให้ชน ค่านี้ต้องไม่น้อยกว่า `band` (กล่องบรรทัดของฟอนต์) ไม่งั้นสระล่างกับวรรณยุกต์
# โดน LVGL หนีบทิ้งบนบอร์ด ซึ่งเป็นบั๊กที่มองไม่เห็นจนกว่าจะมีคนใช้ชื่อภาษาไทย
_RISE = {size: screen.bitmapfont.font(size).rise for size in (12, 14)}
_CARD_TITLE_TOP = L.card.title_dy - _RISE[14]
_CARD_BODY_TOP = L.card.body_dy - _RISE[12]
_PROJECT_TOP = (L.slots.top + L.slots.height - L.slots.baseline_pad
                + L.slots.label_dy - _RISE[12])

LABELS = (
    ("project", screen.PROJECT_W, 12, L.card.top - _PROJECT_TOP),
    ("cardTitle", screen.CARD_TEXT_W, 14, _CARD_BODY_TOP - _CARD_TITLE_TOP),
    ("cardBody", screen.CARD_TEXT_W, 12, L.card.h_two - _CARD_BODY_TOP),
    ("Weather.placeLimit", L.weather.icon_x - L.weather.place_x - 8, 14, None),
    ("Calendar.titleLimit", L.calendar.title_w, L.calendar.title_font,
     L.calendar.row_h - (L.calendar.title_dy - _RISE[L.calendar.title_font])),
    # ชื่อนัดบนการ์ดได้ทั้งความกว้างการ์ด *และสองบรรทัด* — เพดานคนละตัวกับแถวล่าง
    # โดยตั้งใจ ตัวเลขที่ Swift ถือคือค่านี้คูณจำนวนบรรทัด แล้วเผื่อระยะเท่ากับแถวล่าง
    ("Calendar.heroLine", L.calendar.hero_title_w, 14,
     L.calendar.hero_h - (L.calendar.hero_title_dy - _RISE[14])),
)


def print_limits() -> None:
    """วัดว่าแต่ละป้ายรับได้กี่ช่อง — ที่มาของตัวเลขใน host/Sources/TamaCore/Text.swift

    ฝั่งไทยวัดได้ *เป๊ะ* เพราะฟอนต์บิตแมปที่แฟลชลงบอร์ดคือไฟล์เดียวกับที่อ่านตรงนี้ และทุก
    ช่องกว้างเท่ากันหมด (สระบนกับวรรณยุกต์กว้างศูนย์ จึงไม่กินที่ของใคร)
    ฝั่ง ASCII เป็นค่าประมาณ เพราะบอร์ดวาดด้วย Montserrat ซึ่ง preview ไม่มีตัวจริง —
    นั่นคือเหตุผลที่สองภาษาถือเลขคนละตัว ไม่ใช่เลขเดียวที่ประนีประนอมทั้งคู่
    """
    d = ImageDraw.Draw(Image.new("RGB", (1, 1)))
    print(f"{'label':<20} {'width':>6} {'font':>5} {'thai':>5} {'ascii~':>7} "
          f"{'band':>5} {'box':>5}")
    bad = []
    for name, width, board, box in LABELS:
        bf = screen.bitmapfont.font(board)
        # ช่องไทยกว้างคงที่ วัดจากพยัญชนะตัวไหนก็ได้
        thai_cell = bf.length("ก")
        pil = {12: 9, 14: 12}[board]
        f = screen.font(pil)
        letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
        ascii_cell = sum(d.textlength(c, font=f) for c in letters) / len(letters)
        tight = box is not None and box < bf.line_height
        if tight:
            bad.append(name)
        print(f"{name:<20} {width:>6} {board:>5} {int(width // thai_cell):>5} "
              f"{int(width // ascii_cell):>7} {bf.line_height:>5} "
              f"{('-' if box is None else box):>5}{'  <- สระล่างโดนตัด' if tight else ''}")
    if bad:
        raise SystemExit(f"box < band: {', '.join(bad)} — ขยายที่ให้บรรทัด อย่ายอมให้สระตก")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sheet", action="store_true", help="เฉพาะ contact sheet")
    ap.add_argument("--limits", action="store_true", help="วัดความยาวสูงสุดของแต่ละป้าย")
    ap.add_argument("--scale", type=int, default=3, help="ขยายภาพจอตอน export")
    args = ap.parse_args()

    if args.limits:
        print_limits()
        return

    OUT.mkdir(exist_ok=True)
    (OUT / "anim").mkdir(exist_ok=True)

    sheet = contact_sheet()
    sheet.save(OUT / "states.png")
    print(f"states.png            {sheet.width}x{sheet.height}  {len(mascot.all_states())} states")
    if args.sheet:
        return

    for st in mascot.all_states():
        frames = state_gif(st)
        frames[0].save(OUT / "anim" / f"{st}.gif", save_all=True,
                       append_images=frames[1:], duration=90, loop=0)
    print(f"anim/*.gif            {len(mascot.all_states())} ไฟล์")

    for name, sc in SCENES.items():
        # ฉากที่ไม่มี session ต้องสุ่มเวลาเอง: ที่ t=0 มาสคอตยังอยู่นอกจอทั้งตัว
        # ภาพนิ่งเลยเคยออกมาเป็นฟ้าโล่ง ทั้งที่จอกำลังมีตัวละครเดินอยู่
        # ภาพนิ่งหยุดกลางช่วงทำท่า ส่วน GIF เริ่มตอนตัวแตะขอบจอพอดี
        tier = screen.stroll_tier(sc.shown_usage())
        still = screen.stroll_still_t(tier) if not sc.sessions else 0.25
        img = screen.render(sc, still % 1, int(still), _pos("mascot"))
        img.save(OUT / f"screen_{name}.png")
        big = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
        big.save(OUT / f"screen_{name}@{args.scale}x.png")
        # ฉากที่ไม่มี session ใช้ลูปยาวกว่า — เที่ยวเดินหนึ่งรอบกินเวลาหลายสิบวินาที
        # ถ้าตัดที่ 4 ลูปเหมือนฉากอื่นจะเห็นแค่มาสคอตขยับทีละไม่กี่พิกเซล
        loops = STROLL_LOOPS if not sc.sessions else LOOPS
        t0 = int(screen.stroll_enter_t(tier)) if not sc.sessions else 0
        frames = [
            screen.render(sc, (f % FRAMES) / FRAMES, t0 + f // FRAMES, _pos("mascot"))
            for f in range(FRAMES * loops)
        ]
        frames[0].save(OUT / f"screen_{name}.gif", save_all=True,
                       append_images=frames[1:], duration=90, loop=0)
    print(f"screen_*.png/gif      {len(SCENES)} ฉาก  (320x240)")

    for name, w in WEATHER_SCENES.items():
        img = weather.render(w, 0.25, pos=_pos("weather"))
        img.save(OUT / f"weather_{name}.png")
        big = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
        big.save(OUT / f"weather_{name}@{args.scale}x.png")
        frames = [
            weather.render(w, (f % FRAMES) / FRAMES, f // FRAMES, _pos("weather"))
            for f in range(FRAMES * LOOPS)
        ]
        frames[0].save(OUT / f"weather_{name}.gif", save_all=True,
                       append_images=frames[1:], duration=90, loop=0)
    print(f"weather_*.png/gif     {len(WEATHER_SCENES)} ฉาก  (320x240)")

    # GIF ของหน้าที่เรียงเป็นแถวมีไว้ดูมาสคอตจิ๋วอย่างเดียว — แถวเองไม่ขยับ แต่ท่าที่
    # กระโดดออกนอกกรอบจะไปทับแถวแรก ซึ่งเป็นสิ่งเดียวบนหน้านี้ที่ภาพนิ่งพิสูจน์ไม่ได้
    for name, c in CRYPTO_SCENES.items():
        img = crypto.render(c, 0.25, pos=_pos("crypto"))
        img.save(OUT / f"crypto_{name}.png")
        big = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
        big.save(OUT / f"crypto_{name}@{args.scale}x.png")
        frames = [
            crypto.render(c, (f % FRAMES) / FRAMES, f // FRAMES, _pos("crypto"))
            for f in range(FRAMES * LOOPS)
        ]
        frames[0].save(OUT / f"crypto_{name}.gif", save_all=True,
                       append_images=frames[1:], duration=90, loop=0)
    print(f"crypto_*.png/gif      {len(CRYPTO_SCENES)} ฉาก  (320x240)")

    for name, s_ in STOCK_SCENES.items():
        img = stocks.render(s_, 0.25, pos=_pos("stocks"))
        img.save(OUT / f"stocks_{name}.png")
        big = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
        big.save(OUT / f"stocks_{name}@{args.scale}x.png")
        frames = [
            stocks.render(s_, (f % FRAMES) / FRAMES, f // FRAMES, _pos("stocks"))
            for f in range(FRAMES * LOOPS)
        ]
        frames[0].save(OUT / f"stocks_{name}.gif", save_all=True,
                       append_images=frames[1:], duration=90, loop=0)
    print(f"stocks_*.png/gif      {len(STOCK_SCENES)} ฉาก  (320x240)")

    for name, c in CALENDAR_SCENES.items():
        img = calendar.render(c, 0.25, pos=_pos("calendar"))
        img.save(OUT / f"calendar_{name}.png")
        big = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
        big.save(OUT / f"calendar_{name}@{args.scale}x.png")
        frames = [
            calendar.render(c, (f % FRAMES) / FRAMES, f // FRAMES, _pos("calendar"))
            for f in range(FRAMES * LOOPS)
        ]
        frames[0].save(OUT / f"calendar_{name}.gif", save_all=True,
                       append_images=frames[1:], duration=90, loop=0)
    print(f"calendar_*.png/gif    {len(CALENDAR_SCENES)} ฉาก  (320x240)")

    skies = {n: (c, None) for n, c in SKY_CLOCKS.items()} | SKY_WX
    for name, (clock, code) in skies.items():
        sc = sky_scene(clock, code)
        # เวลาเดียวกับภาพนิ่งของฉากที่ไม่มี session — ที่ cycle 0 มาสคอตยังอยู่นอกจอ
        # แล้วภาพตรวจจะไม่มีสิ่งที่ต้องตรวจ (มาสคอตยืนบนพื้น + contrast กับฟ้า)
        still = screen.stroll_still_t()
        img = screen.render(sc, still % 1, int(still), _pos("mascot"))
        img.save(OUT / f"sky_{name}.png")
        big = img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST)
        big.save(OUT / f"sky_{name}@{args.scale}x.png")
        frames = [
            screen.render(sc, (f % FRAMES) / FRAMES, f // FRAMES, _pos("mascot"))
            for f in range(FRAMES * STROLL_LOOPS)
        ]
        frames[0].save(OUT / f"sky_{name}.gif", save_all=True,
                       append_images=frames[1:], duration=90, loop=0)
    print(f"sky_*.png/gif         {len(skies)} ฉาก (ช่วงเวลา + สภาพอากาศ)")
    print(f"\nout/ -> {OUT}")


if __name__ == "__main__":
    main()
