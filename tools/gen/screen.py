"""ประกอบจอทั้งใบ 320x240 — ตัวแทนของหน้าจอจริงตอน dev

จูน layout ที่นี่ได้ทันทีโดยไม่ต้องแฟลชบอร์ด
ค่าคงที่ทั้งหมดมาจาก layout.toml ชุดเดียวกับ firmware
"""

from __future__ import annotations

from dataclasses import dataclass, field

from PIL import Image, ImageDraw, ImageFont

# `footer` วนกลับมา import ไฟล์นี้ผ่าน `age` — ใช้ได้เพราะทั้งสองฝั่งแตะกันตอน *เรียก*
# ไม่ใช่ตอน import (Python ผูกโมดูลที่ยัง init ไม่จบให้ได้) อย่าย้ายไปเรียกที่ระดับโมดูล
from . import age, bitmapfont, footer, mascot, pages, sky, thai, topbar
from .config import L, PAL
from .mascot import BODY
from .props import BOX_X0, BOX_X1, BOX_Y1

BODY_CX = BODY[0] + BODY[2] / 2  # กึ่งกลางลำตัวในหน่วย unit — จุดที่เงาต้องอยู่ใต้
from .render import draw_rects, quantize565

_FONTS: dict[int, ImageFont.FreeTypeFont] = {}


def font(size: int) -> ImageFont.FreeTypeFont:
    if size not in _FONTS:
        _FONTS[size] = ImageFont.load_default(size=size)
    return _FONTS[size]


def price_text(draw: ImageDraw.ImageDraw, xy: tuple[float, float], text: str,
               fnt: ImageFont.FreeTypeFont, fill: str, anchor: str = "ls") -> None:
    """ตัวเลขราคาบนการ์ดใหญ่ — ป้ายเดียว ไม่ทำตัวหนาปลอม

    เคยวาดซ้ำเยื้อง 1px ลงขวาแทน montserrat ตัวหนาที่ LVGL ที่คอมไพล์มาไม่มี (และการ
    เพิ่มเข้าไปคือแฟลชอีกก้อนต่อหนึ่งขนาด) แต่ที่ 36px ขอบที่เยื้องอ่านเป็นเงาเหลื่อม
    มากกว่าน้ำหนัก · สิ่งที่ทำให้ราคาเด่นคือขนาดกับตำแหน่งบนการ์ดอยู่แล้ว
    ฝั่งบอร์ดคือป้ายใบเดียวเช่นกัน (`price_int`/`price_frac` ใน ct_crypto_ui.c)
    """
    draw.text(xy, text, font=fnt, fill=quantize565(fill), anchor=anchor)


def _drawable_only(**fields: str) -> None:
    """กันข้อความสาธิตที่ฟอนต์บนบอร์ดวาดไม่ได้ — จะได้กล่องสี่เหลี่ยมแทน

    ข้อความจริงผ่าน Text.sanitize ฝั่ง daemon (host/Sources/TamaCore/Text.swift)
    มาแล้ว จอจึงเห็นแค่ ASCII 0x20..0x7E กับอักขระไทยเสมอ ถ้า preview ยอมให้ใส่
    em dash ได้ ภาพที่ออกมาจะสวยกว่าของจริง ซึ่งแย่กว่าการพังตรงนี้
    """
    for name, value in fields.items():
        bad = sorted({c for c in value if not (" " <= c <= "~") and not thai.in_block(ord(c))})
        if bad:
            raise ValueError(
                f"{name}={value!r} มีอักขระที่ฟอนต์บนบอร์ดไม่มี: {bad} "
                f"— daemon จะแทนที่ให้ก่อนส่ง ใส่ตัวที่แทนแล้วมาตรงนี้"
            )


def _is_thai(s: str) -> bool:
    return any(thai.in_block(ord(c)) for c in s)


def _cells(draw: ImageDraw.ImageDraw, text: str, pil: int, board: int) -> list[tuple]:
    """แตกเป็นช่องพร้อมความกว้าง — (ข้อความ, เป็นไทยไหม, กว้างกี่พิกเซล)

    ไทยไปทางฟอนต์บิตแมปของบอร์ด ที่เหลือไปทางเดิม เพราะบอร์ดก็ทำแบบนี้: ป้ายใช้
    Montserrat แล้วตกไปที่ฟอนต์ไทยเป็นราย codepoint ที่มันไม่มี (ct_ui.c) ถ้าที่นี่
    ลากทั้งบรรทัดไปทางฟอนต์ไทย บรรทัดที่มีทั้งไทยและอังกฤษจะออกมาไม่เหมือนบอร์ด
    """
    f, bf = font(pil), bitmapfont.font(board)
    out = []
    for c in thai.clusters(text):
        if _is_thai(c):
            out.append((c, True, bf.length(c)))
        else:
            out.append((c, False, draw.textlength(c, font=f)))
    return out


def wrap(draw: ImageDraw.ImageDraw, text: str, *, pil: int, board: int, max_w: int,
         max_lines: int) -> list[str]:
    """ตัดข้อความเป็นหลายบรรทัด — พอร์ตของ LV_LABEL_LONG_DOT ที่ความกว้างคงที่

    LVGL ตัดที่ช่องว่างถ้ามี ไม่มีก็ตัดตรงตัวที่ล้น — ภาษาไทยไม่มีช่องว่างระหว่างคำ
    มันจึงตกทางหลังเสมอ และการตัดต้องเป็น *ช่อง* ไม่ใช่ scalar ไม่งั้นวรรณยุกต์จะหลุด
    จากฐานของมัน (เหตุผลเดียวกับ `line`)

    บรรทัดสุดท้ายที่ยังเหลือข้อความต่อท้ายจบด้วย "..." เหมือนที่บอร์ดทำ
    """
    cells = _cells(draw, text, pil, board)
    ell = draw.textlength("...", font=font(pil))
    lines: list[str] = []
    while cells and len(lines) < max_lines:
        last = len(lines) == max_lines - 1
        take, w = 0, 0.0
        while take < len(cells) and w + cells[take][2] <= max_w:
            w += cells[take][2]
            take += 1
        if take == len(cells):
            lines.append("".join(c for c, _, _ in cells))
            return lines
        if last:
            # เหลือข้อความต่อท้าย — ถอยจนที่ว่างพอสำหรับจุดไข่ปลา
            while take > 0 and w + ell > max_w:
                take -= 1
                w -= cells[take][2]
            lines.append("".join(c for c, _, _ in cells[:take]) + "...")
            return lines
        # ตัดที่ช่องว่างท้ายสุดของบรรทัดนี้ถ้ามี — กติกาเดียวกับ LVGL กับข้อความละติน
        brk = next((i for i in range(take - 1, 0, -1) if cells[i][0] == " "), None)
        cut = brk if brk is not None else max(take, 1)
        lines.append("".join(c for c, _, _ in cells[:cut]))
        cells = cells[cut + 1:] if brk is not None else cells[cut:]
    return lines


def line(draw: ImageDraw.ImageDraw, xy: tuple[float, float], text: str, *,
          pil: int, board: int, fill: str, anchor: str, max_w: int | None = None) -> None:
    """วาดข้อความหนึ่งบรรทัด

    `pil` คือขนาดที่ฟอนต์สาธิตฝั่ง PIL ใช้ `board` คือขนาดฟอนต์จริงบนบอร์ด สองค่านี้
    ไม่เท่ากันเพราะฟอนต์คนละตัว (ดูหมายเหตุที่ lv_font_montserrat_24 ใน draw_usage)
    ส่วนภาษาไทยไม่มีทางเลือก: ต้องเป็นบิตแมปตัวเดียวกับที่แฟลชลงบอร์ด (ADR-0008)

    แนวตั้งมีสองแบบ และแบบที่ควรใช้กับป้ายคือ `t`:

    - `t` — `y` คือ **ขอบบนของตัวละติน** ซึ่งเป็นเลขเดียวกับที่ `lv_obj_set_pos` รับบนบอร์ด
      (ผ่าน `ct_label_set_pos` ที่ถอย `ct_font_rise` ให้เอง) ที่ว่างเหนือมันสำหรับ
      วรรณยุกต์ไทยเป็นของฟอนต์ ไม่ใช่ของเลย์เอาต์ — เลขในเลย์เอาต์จึงยังหมายถึงที่ที่
      ตัวอักษรไปโผล่ ไม่ใช่ขอบบนของกล่องที่มีที่ว่างปนอยู่
    - `m` — `y` คือกึ่งกลางกล่องบรรทัด ใช้เมื่อของที่ต้องจัดกลางเป็นกล่องอื่น

    ทั้งสองแบบวางตัว ASCII บนเส้นฐานของกล่องบรรทัดเดียวกับไทยเสมอ ไม่ใช่ให้ PIL จัดเอง —
    บนบอร์ดตัว ASCII กับตัวไทยอยู่บรรทัดเดียวกันและใช้เส้นฐานเดียว
    """
    col = quantize565(fill)
    bf = bitmapfont.font(board)
    # ขอบบนของกล่องบรรทัด — จุดตั้งต้นเดียวของทั้งสองภาษา
    top = xy[1] - bf.rise if anchor[1] == "t" else xy[1] - bf.line_height / 2
    if not _is_thai(text):
        f = font(pil)
        s = _fit(draw, text, f, max_w) if max_w is not None else text
        draw.text((xy[0], top + bf.ascent), s, font=f, fill=col, anchor=f"{anchor[0]}s")
        return

    f = font(pil)
    cells = _cells(draw, text, pil, board)
    if max_w is not None:
        # ตัดทีละช่อง ไม่ใช่ทีละ scalar — ไม่งั้นวรรณยุกต์จะหลุดจากฐานของมัน
        ell = draw.textlength("...", font=f)
        if sum(w for _, _, w in cells) > max_w:
            while cells and sum(w for _, _, w in cells) + ell > max_w:
                cells.pop()
            cells.append(("...", False, ell))
    total = sum(w for _, _, w in cells)

    x = xy[0]
    if anchor[0] == "m":
        x -= total / 2
    elif anchor[0] == "r":
        x -= total
    y = top
    # เส้นฐานของบรรทัดมาจากฟอนต์ของบอร์ด แล้วตัวที่วาดด้วย PIL ไปเกาะเส้นเดียวกัน
    base = y + bf.ascent
    for s, is_thai, w in cells:
        if is_thai:
            bf.draw(draw, x, y, s, col)
        else:
            draw.text((x, base), s, font=f, fill=col, anchor="ls")
        x += w


@dataclass(slots=True)
class Session:
    project: str
    state: str = "idle"
    phase_offset: float = 0.0

    def __post_init__(self) -> None:
        _drawable_only(project=self.project)


@dataclass(slots=True)
class Card:
    """หนึ่งการ์ด — `body` ว่างแปลว่าการ์ดบรรทัดเดียว ไม่ใช่การ์ดที่เนื้อหาหาย

    daemon ตัดหัวการ์ดทิ้งเมื่อชื่อโปรเจกต์นั้นมีมาสคอตยืนอยู่บนจอเดียวกันแล้ว แล้วเลื่อน
    ประโยคขึ้นมาเป็น `title` (ดู SessionStore.card(from:onScreen:)) ชื่อจะกลับมาก็ต่อเมื่อ
    session ของมันตกจอ — ตอนนั้นการ์ดเป็นที่เดียวที่บอกได้ว่าใครเป็นคนขอ
    """

    title: str
    body: str
    kind: str = "info"  # info | alert | done

    def __post_init__(self) -> None:
        _drawable_only(title=self.title, body=self.body)


@dataclass(slots=True)
class Ask:
    """คำขออนุญาตที่มีคนค้างรอคำตอบอยู่จริง — ตรงกับ `ct_ask_t` ใน firmware/main/ct_model.h

    ไม่มี `id` ทั้งที่บนสายมี: id มีไว้ให้คำตอบเดินทางกลับถูกคำถาม ไม่ได้มีไว้ให้วาด
    ภาพที่ออกมาจึงเหมือนกันทุกประการไม่ว่า id จะเป็นอะไร

    `may_allow` เป็นคำตัดสินของ `Risk` ฝั่ง Mac ไม่ใช่ของบอร์ด (ADR-0001) — ที่นี่แค่
    วาดตาม เหมือนที่ `layout_ask()` แค่ทาสีตาม
    """

    title: str
    may_allow: bool = True

    def __post_init__(self) -> None:
        _drawable_only(title=self.title)


@dataclass(slots=True)
class Usage:
    """หนึ่งหน้าต่างโควตา — ตรงกับ `rate_limits.five_hour` / `.seven_day` ที่ Claude Code ป้อน

    `pct` และ `remaining` เป็น None แยกกันได้ — เอกสารระบุว่าแต่ละหน้าต่างหายอิสระต่อกัน
    None คือ "ไม่รู้" ไม่ใช่ศูนย์ ศูนย์เป็นค่าจริง (ADR-0001: reported, never derived)
    """

    label: str
    window: int  # ความยาวหน้าต่างเป็นวินาที — ค่าคงที่ ใช้หาตำแหน่งขีด pace
    pct: int | None = None
    remaining: int | None = None  # วินาทีถึงรีเซ็ต — บอร์ดนับถอยลงเอง


@dataclass(slots=True)
class Screen:
    sessions: list[Session] = field(default_factory=list)
    overflow: int = 0
    clock: str = "14:32"
    date: str = "Mon 27 Jul"
    # มี snapshot สดอยู่ไหม — ไม่สนใจว่ามาทางไหน ตรงกับ ct_ui_set_connected
    connected: bool = True
    # บอร์ดอยู่บน WiFi แล้วหรือยัง
    wifi: bool = False
    # ลิงก์ BLE ขึ้นอยู่ไหม — None = เหมือน connected (ฉากเก่าทั้งหมดคุยกันทาง BLE)
    # แยกจาก connected ตั้งแต่ M2 เพราะ snapshot เดินทางมาทาง LAN ได้แล้ว: จอที่ข้อมูล
    # สดแต่ BLE ตายเป็นสภาพที่มีจริง และไอคอนต้องบอกให้ถูก
    ble: bool | None = None
    # หลุดลิงก์มากี่วินาทีแล้ว — บอร์ดนับเองด้วยนาฬิกาของตัวเอง (ct_pages.c) ไม่ได้มาจาก
    # host ด้วยเหตุผลเดียวกับ countdown ของโควตา: ตอนที่ตัวเลขนี้มีความหมายที่สุดคือ
    # ตอนที่ไม่มีใครส่งอะไรมาให้แล้ว · ไม่มีความหมายเมื่อ connected
    offline_s: int = 0

    @property
    def ble_link(self) -> bool:
        return self.connected if self.ble is None else self.ble
    # ใหม่สุดอยู่บน — session ที่เกิน 4 ตัวไม่มีมาสคอต แต่การเตือนยังมาโผล่ตรงนี้
    cards: list[Card] = field(default_factory=list)
    # การ์ดที่มีอยู่จริงแต่ไม่ได้ส่ง/วาดไม่พอ — daemon นับมาให้ (คีย์ "m" บนสาย)
    card_overflow: int = 0
    # คำถามที่ค้างอยู่ — None คือไม่มีใครรออะไรจากคนตรงหน้าจอ (คีย์ "q" บนสาย)
    ask: Ask | None = None
    # None = ไม่เคยได้ข้อมูลเลย -> ถอยไปเป็นนาฬิกาตั้งโต๊ะ ไม่ใช่โครงเปล่าที่ดูเหมือนพัง
    usage: list[Usage] | None = None
    # รหัส WMO ของสภาพอากาศตอนนี้ — **ไม่ได้มากับ snapshot** แต่มาจากเฟรมของหน้าอากาศ
    # ที่บอร์ดแคชไว้ (ADR-0012) · None = ยังไม่เคยได้เฟรม / ผู้ใช้ปิดหน้าอากาศ / เฟรม
    # เก่าเกิน 2.5 ชม. — ทั้งสามได้ฟ้าตามเวลาแบบเดิม ตรงกับ `ct_ui_set_weather(_, false)`
    weather_code: int | None = None

    def shown_ask(self) -> Ask | None:
        """คำถามที่มีสิทธิ์ขึ้นจอ — ต้องตรงกับ `ask_shown()` ใน firmware/main/ct_ui.c

        ลิงก์หลุดแล้วไม่มีคำถาม ด้วยเหตุผลที่หนักกว่าการ์ดกับโควตา: การ์ดที่ค้างอยู่คือ
        ข่าวเก่าที่ไม่มีใครรับรอง ส่วนปุ่มที่ค้างอยู่คือปุ่มที่กดแล้วคำตอบไม่มีทางถึงใคร
        """
        return self.ask if self.connected else None

    def shown_cards(self) -> list[Card]:
        """การ์ดที่มีสิทธิ์ขึ้นจอ — ลิงก์หลุดแล้วเหลือศูนย์ใบ ดูที่ shown_usage()"""
        return list(self.cards) if self.connected else []

    def shown_usage(self) -> list[Usage]:
        """โควตาที่มีสิทธิ์ขึ้นจอ — ลิงก์หลุดแล้วเหลือศูนย์แถว

        ตอนหลุดลิงก์ สิ่งเดียวที่บอร์ดยืนยันเองได้คือนาฬิกา ทั้งการ์ดและเปอร์เซ็นต์เป็นค่าที่
        host เคยบอกไว้และไม่มีใครรับรองแล้วว่ายังจริง — ตอนหลุด สิ่งเดียวที่จอยืนยันได้เอง
        คือระยะเวลาที่มันไม่ได้ยินอะไรเลย ซึ่งขึ้นแทนนาฬิกา
        """
        return list(self.usage) if self.connected and self.usage else []


def _fit(draw: ImageDraw.ImageDraw, text: str, f: ImageFont.FreeTypeFont, max_w: int) -> str:
    """ตัดข้อความให้พอดีความกว้าง — daemon ต้องทำแบบเดียวกันก่อนส่งบน BLE"""
    if draw.textlength(text, font=f) <= max_w:
        return text
    ell = "..."
    while text and draw.textlength(text + ell, font=f) > max_w:
        text = text[:-1]
    return text + ell


def slot_x(i: int, n: int) -> int:
    """ขอบซ้ายของ slot ที่ i เมื่อกำลังแสดง session อยู่ n ตัว

    ระยะห่างคงที่ 80px เสมอ แต่ยกทั้งกลุ่มมาไว้กึ่งกลางจอ
    สิ่งที่ต้องนิ่งคือ *ลำดับ* ซ้าย→ขวา ไม่ใช่พิกัดสัมบูรณ์ — ตัวละครไม่เคยสลับที่กัน
    มีแค่ทั้งกลุ่มเลื่อนพร้อมกันตอนมี session เข้า/ออก
    """
    return round((L.screen.width - n * L.slots.width) / 2) + i * L.slots.width


# เงาใต้เท้า — กว้าง 11 unit (แคบกว่าช่วงขา 14 unit) วางอยู่ใต้เส้นขอบฟ้าทั้งก้อน
# ขนาดคงที่ ไม่ยุบตามความสูงที่กระโดด: หน้าที่ของมันคือปักหมุดว่า "พื้นอยู่ตรงนี้"
# เงาที่ยุบตามจะกลายเป็นสิ่งที่ต้องมองแทนที่จะเป็นสิ่งที่ทำให้มองตัวละครถูก
SHADOW_W = 11.0


def _shadow(draw: ImageDraw.ImageDraw, cx: float, color: str | None) -> None:
    if color is None:
        return
    half = SHADOW_W * L.slots.unit_px / 2
    y = L.sky.horizon
    # แคปซูล ไม่ใช่วงรี — ฝั่ง LVGL วาดด้วย lv_draw_rect ที่ radius ถูก clamp ครึ่งด้านสั้น
    # ซึ่งได้แคปซูล ถ้า preview ใช้วงรีจริง สองฝั่งจะไม่ตรงกัน
    draw.rounded_rectangle([round(cx - half), y, round(cx + half), y + 4],
                           radius=2, fill=quantize565(color))


def _slot(draw: ImageDraw.ImageDraw, i: int, sess: Session | None, s: Screen,
          phase: float, cycle: int, n: int) -> None:
    sw, top, sh = L.slots.width, L.slots.top, L.slots.height
    if sess is None:
        return
    x = slot_x(i, n)
    # ไม่มีทั้งพื้นหลัง slot สลับสีและเส้นคั่น — ฉากเป็นผืนเดียวกันทั้งจอ เส้นแบ่งใดๆ
    # ตัดมันออกเป็นชิ้น ส่วนหน้าที่เดิมของเส้นคั่น (กันไม่ให้ prop ของตัวหนึ่งอ่านเป็น
    # ของตัวข้างๆ) ตอนนี้เงาใต้เท้าทำแทน: มันบอกว่าแต่ละตัวยืนอยู่ตรงไหน

    px = L.slots.unit_px
    foot_px = top + sh - L.slots.baseline_pad
    oy = foot_px - BOX_Y1 * px
    ox = x + sw / 2 - (BOX_X0 + BOX_X1) / 2 * px

    p = phase + sess.phase_offset
    # เงาเกาะกับ *ลำตัว* ไม่ใช่กึ่งกลาง slot — build_centered จัดกึ่งกลางกรอบที่รวม prop
    # ด้วย ท่าที่ถือของชิ้นใหญ่จึงมีลำตัวเยื้องไปจากกึ่งกลาง slot
    bx0, _, bx1, _ = mascot.state_box(sess.state)
    body_cx = x + sw / 2 + (BODY_CX - (bx0 + bx1) / 2) * px
    _shadow(draw, body_cx, sky.shadow_color(s.clock, s.connected, s.weather_code))

    rects = mascot.build_centered(sess.state, p % 1.0, s.connected, cycle + int(p))
    draw_rects(draw, rects, px, ox, oy)

    # ความกว้างต้องเท่ากับ `lv_obj_set_width(l, CT_SLOTS_WIDTH - 4)` ใน ct_ui.c เป๊ะ —
    # ถ้าที่นี่แคบกว่า preview จะตัดชื่อโปรเจกต์เร็วกว่าบอร์ด แล้วเลขใน `Text.Limit`
    # ที่วัดจากตรงนี้ก็จะเตี้ยกว่าที่จอรับได้จริง
    line(draw, (x + sw / 2, foot_px + L.slots.label_dy), sess.project, pil=9, board=12,
          fill=PAL.text if s.connected else PAL.text_dim, anchor="mt", max_w=PROJECT_W)


# --- มาสคอตเดินเล่นตอนไม่มี session ------------------------------------------
# ระดับที่มาสคอตเดินเล่นรับรู้ — ขั้นเดียวกับสีของแถบโควตา ไม่ใช่เกณฑ์ชุดที่สอง
STROLL_CALM, STROLL_WARN, STROLL_CRIT = 0, 1, 2
# ท่าที่หยุดทำกลางทาง วนไปตามรอบ — ต้องตรงกับ STROLL_ACTS ใน firmware/main/ct_ui.c
#
# ชุดท่าเปลี่ยนตามระดับ ไม่ใช่แค่ความเร็ว: ท่าฉลองตอนโควตาเหลือน้อยอ่านเป็นการเยาะเย้ย
# ไม่ใช่ความน่ารัก · ที่ crit เหลือสองท่าที่เป็นการ "รอ" ทั้งคู่ จึงไม่มีท่าไหนดูกระตือรือร้น
# ห้ามใส่ sleeping: ท่านั่งหลับคือสารของ session ที่หลับอยู่ ไม่ใช่ของโควตาที่ใกล้หมด
STROLL_ACTS = (
    ("celebrate", "thinking", "searching", "waiting"),
    ("thinking", "searching", "waiting"),
    ("waiting", "thinking"),
)
# ตำแหน่งหยุดเป็นสัดส่วนของเส้นทาง — วนคนละความยาวกับ ACTS เพื่อไม่ให้จับคู่ซ้ำ
STROLL_PAUSE_AT = (0.34, 0.5, 0.66)
STROLL_TRAVEL = L.screen.width + 2 * L.stroll.pad_px
STROLL_SPEED = (L.stroll.speed_px_s, L.stroll.warn_speed_px_s, L.stroll.crit_speed_px_s)


def stroll_acts(tier: int, connected: bool) -> tuple[str, ...]:
    """ชุดท่าที่หยุดทำได้ — สองแกน ไม่ใช่แกนเดียว

    ตอนหลุดลิงก์ไม่มีอะไรให้ฉลองและไม่มีอะไรให้ค้นหา ตัวเทาที่ยกแขนฉลองอยู่ใต้คำว่า
    "no link" คือจอที่พูดสองเรื่องสวนทางกัน จึงยืมชุดท่าของ crit มาใช้ (รอทั้งคู่)
    แต่ *ไม่* ยืมความเร็ว: การเดินช้าลงเป็นสารของโควตา ไม่ใช่ของลิงก์ที่หลุด

    ต้องตรงกับ stroll_acts ใน firmware/main/ct_ui.c
    """
    return STROLL_ACTS[STROLL_CRIT] if not connected else STROLL_ACTS[tier]


def stroll_tier(rows: list[Usage]) -> int:
    """แถวโควตาที่อยู่บนจอ -> ระดับที่มาสคอตเดินด้วย

    เอาแถวที่แย่ที่สุด ไม่ใช่แถวแรก — "เหลือที่ให้ทำงานอีกเท่าไร" คือหน้าต่างที่ตันก่อน
    ไม่ว่ามันจะเป็นรายชั่วโมงหรือรายสัปดาห์
    เปอร์เซ็นต์ที่ไม่รู้ (None) ไม่ใช่ศูนย์ และไม่ใช่เหตุให้ทำท่าเหนื่อย — มันคือ calm

    ต้องตรงกับ stroll_tier ใน firmware/main/ct_ui.c
    """
    known = [u.pct for u in rows if u.pct is not None]
    if not known:
        return STROLL_CALM
    worst = max(known)
    if worst >= L.usage.crit_pct:
        return STROLL_CRIT
    if worst >= L.usage.warn_pct:
        return STROLL_WARN
    return STROLL_CALM


def stroll_walk_s(tier: int) -> float:
    """วินาทีที่ใช้เดินจนสุดเส้นทางหนึ่งเที่ยว (ไม่รวมช่วงหยุดทำท่า)"""
    return STROLL_TRAVEL / STROLL_SPEED[tier]


def stroll_pose(t: float, tier: int = STROLL_CALM,
                connected: bool = True) -> tuple[str, float]:
    """เวลาสัมบูรณ์ (วินาที) -> (state, x ของขอบซ้ายกรอบวาด)

    เที่ยวหนึ่ง = เดินจากนอกจอซ้ายไปนอกจอขวา โดยหยุดทำท่าหนึ่งครั้งกลางทาง

    `tier` คงที่ตลอดฟังก์ชันนี้โดยตั้งใจ: ความเร็วเปลี่ยน = ความยาวเที่ยวเปลี่ยน
    ถ้าเปลี่ยนกลางเที่ยว ตัวที่กำลังเดินอยู่จะกระโดดไปอีกตำแหน่งทันที บอร์ดจึงสลับระดับ
    เฉพาะตอนขึ้นเที่ยวใหม่ (ดู stroll_advance ใน ct_ui.c) ส่วน preview เรนเดอร์ทีละฉาก
    ซึ่ง tier คงที่อยู่แล้ว ผลลัพธ์ทั้งสองฝั่งจึงตรงกันเป๊ะ

    ต้องตรงกับ stroll_pose ใน firmware/main/ct_ui.c
    """
    speed = STROLL_SPEED[tier]
    walk_s = stroll_walk_s(tier)
    trip_s = walk_s + L.stroll.pause_s
    trip = int(t // trip_s)
    u = t - trip * trip_s
    hold_at = walk_s * STROLL_PAUSE_AT[trip % len(STROLL_PAUSE_AT)]
    acts = stroll_acts(tier, connected)

    if u < hold_at:
        walked = u
        state = "entering"
    elif u < hold_at + L.stroll.pause_s:
        walked = hold_at
        state = acts[trip % len(acts)]
    else:
        walked = u - L.stroll.pause_s
        state = "entering"
    return state, -L.stroll.pad_px + walked * speed


def stroll_enter_t(tier: int = STROLL_CALM) -> float:
    """เวลาที่ขอบซ้ายของกรอบวาดแตะขอบจอพอดี — จุดเริ่มที่ถูกของภาพเคลื่อนไหว"""
    return L.stroll.pad_px / STROLL_SPEED[tier]


def stroll_still_t(tier: int = STROLL_CALM) -> float:
    """เวลาที่ควรใช้สุ่ม *ภาพนิ่ง* ของฉากที่ไม่มี session

    ที่ t=0 มาสคอตยังอยู่นอกจอทั้งตัว ภาพนิ่งที่สุ่มตรงนั้นจึงเป็นฟ้าโล่ง — ซึ่งเป็น
    คนละเรื่องกับสิ่งที่จอทำอยู่จริง และเคยถูกอ่านเป็น "หน้ามาสคอตที่ไม่มีมาสคอต"
    กลางช่วงหยุดทำท่าคือเฟรมที่เป็นตัวแทนของทั้งเที่ยวที่สุด: ตัวอยู่กลางจอและกำลังพูด
    """
    return stroll_walk_s(tier) * STROLL_PAUSE_AT[0] + L.stroll.pause_s / 2


def _stroll(draw: ImageDraw.ImageDraw, s: Screen, phase: float, cycle: int) -> None:
    state, x = stroll_pose(cycle + phase, stroll_tier(s.shown_usage()), s.connected)
    px = L.slots.unit_px
    foot_px = L.slots.top + L.slots.height - L.slots.baseline_pad
    ox = x - BOX_X0 * px
    # ตัวเดินเล่นใช้ build() ตรงๆ ไม่ผ่าน build_centered จึงไม่มี dx มาชดเชย
    _shadow(draw, ox + BODY_CX * px, sky.shadow_color(s.clock, s.connected, s.weather_code))
    draw_rects(draw, mascot.build(state, phase, s.connected, cycle), px,
               ox, foot_px - BOX_Y1 * px)


CARD_GAP = L.card.gap
CARD_MAX = L.card.max
# ที่ที่กองการ์ดมีจริง ตั้งแต่ใบแรกถึงก้นจอ — ต้องตรงกับ card_fit() ใน ct_ui.c
CARD_BUDGET = L.screen.height - (L.card.top + L.card.pad)


def card_h(c: Card) -> int:
    return L.card.h_two if c.body else L.card.h_one


def _fit_cards(cards: list[Card], budget: int) -> int:
    used = 0
    n = 0
    for c in cards[:CARD_MAX]:
        need = card_h(c) + (CARD_GAP if n else 0)
        if used + need > budget:
            break
        used += need
        n += 1
    return n


def card_fit(cards: list[Card], overflow: int) -> int:
    """แสดงได้กี่ใบ — เติมจากบนลงล่างจนกว่าที่จะไม่พอ

    เป็นการ *วัด* ไม่ใช่กฎที่เขียนไว้ตายตัวว่า "สองบรรทัดได้ใบเดียว" เพราะความสูงของ
    กล่องบรรทัดมาจากไฟล์ฟอนต์ วันที่ thai.toml เพิ่มร่างที่สูงกว่าเดิม เลขนี้ต้องขยับตาม
    เองโดยไม่มีใครต้องจำว่ามีกฎซ่อนอยู่ที่ไหนอีก

    ลองสองรอบ เพราะที่ของบรรทัด "+N more" จะต้องกันไว้ก็ต่อเมื่อมีอะไรให้บอกจริงๆ —
    กันไว้ตลอดแปลว่าการ์ดคู่ที่พอดีเป๊ะ (32 + 4 + 56 = 92) เสียใบที่สองไปให้บรรทัดที่
    ไม่มีอะไรจะเขียน
    """
    n = _fit_cards(cards, CARD_BUDGET)
    if n == len(cards) and overflow == 0:
        return n
    return _fit_cards(cards, CARD_BUDGET - L.card.more_h)
# ความกว้างที่ข้อความมีจริง — ต้องตรงกับ `lv_obj_set_width(title, w - 18)` ใน ct_ui.c
# ทั้งสองค่านี้คือที่มาของ `Text.Limit` ฝั่ง Swift (ดู `preview.py --limits`)
CARD_TEXT_W = L.screen.width - L.card.pad * 2 - L.card.text_inset
PROJECT_W = L.slots.width - L.slots.label_inset


# ชนิดการ์ด -> (สีแถบ, สีพื้น, ระยะร่นหัวท้ายของแถบ, รูปทรงเครื่องหมาย)
# สามแกนหลังไม่ใช่สี ทุกแกนชี้ทางเดียวกัน: alert ยาวสุด สว่างสุด ตัน · done สั้นสุด จมสุด เป็นขีด
# ต้องตรงกับ card_style() ใน firmware/main/ct_ui.c
_CARD_STYLE = {
    "alert": (PAL.alert, PAL.bg_card_alert, L.card.rail_inset_alert, "solid"),
    "done": (PAL.good, PAL.bg_card_done, L.card.rail_inset_done, "dash"),
}
_CARD_INFO = (PAL.accent, PAL.bg_slot, L.card.rail_inset_info, "hollow")


def _card_mark(draw: ImageDraw.ImageDraw, shape: str, cx: int, cy: int,
               col: str, plate: str) -> None:
    """เครื่องหมายชิดขวา — บล็อกสี่เหลี่ยมตามภาษาเดียวกับมาสคอต ไม่ใช่ glyph จากฟอนต์

    ฟอนต์บนบอร์ดมี charset จำกัดและทุกอย่างที่เป็นตัวอักษรต้องผ่าน Text.swift ก่อน
    สัญลักษณ์สถานะจึงต้องเป็น rect ที่วาดเอง ไม่ใช่ตัวอักษรที่อาจถูกถอดทิ้งกลางทาง
    """
    m, st = L.card.mark, L.card.mark_stroke
    h = m // 2
    fill = quantize565(col)
    if shape == "dash":
        draw.rectangle([cx - h, cy - st // 2, cx + h - 1, cy + st // 2 - 1], fill=fill)
        return
    draw.rectangle([cx - h, cy - h, cx + h - 1, cy + h - 1], fill=fill)
    if shape == "hollow":
        draw.rectangle([cx - h + st, cy - h + st, cx + h - st - 1, cy + h - st - 1],
                       fill=quantize565(plate))


def _card(draw: ImageDraw.ImageDraw, c: Card, y: int) -> None:
    pad, w = L.card.pad, L.screen.width
    h = card_h(c)
    accent, plate, inset, mark = _CARD_STYLE.get(c.kind, _CARD_INFO)
    draw.rectangle([pad, y, w - pad - 1, y + h - 1], fill=quantize565(plate))
    draw.rectangle([pad, y + inset, pad + L.card.rail_w - 1, y + h - 1 - inset],
                   fill=quantize565(accent))
    _card_mark(draw, mark, w - pad - L.card.mark_right, y + h // 2, accent, plate)

    tx = pad + 9
    # ขนาดฝั่งบอร์ดคือ 14/12 (montserrat กับฟอนต์ไทย) ฝั่ง PIL คือ 12/10 เพราะฟอนต์คนละตัว
    # y คือขอบบนของตัวละติน ที่ว่างเหนือมันเป็นของวรรณยุกต์ไทย — ต้องตรงกับ
    # layout_cards() ใน firmware/main/ct_ui.c
    line(draw, (tx, y + L.card.title_dy), c.title, pil=12, board=14,
         fill=PAL.text, anchor="lt", max_w=CARD_TEXT_W)
    if not c.body:
        return
    line(draw, (tx, y + L.card.body_dy), c.body, pil=10, board=12,
         fill=PAL.text_dim, anchor="lt", max_w=CARD_TEXT_W)


def _cards(draw: ImageDraw.ImageDraw, cards: list[Card], overflow: int) -> None:
    y = L.card.top + L.card.pad
    n = card_fit(cards, overflow)
    for c in cards[:n]:
        _card(draw, c, y)
        y += card_h(c) + CARD_GAP
    # การ์ดที่ไม่ได้วาดต้องเหลือร่องรอย ไม่ใช่หายเงียบ — "ไม่มีอะไรค้างแล้ว" กับ
    # "ยังค้างอีกสองเรื่องแต่จอไม่พอ" คือสองสถานะที่ต้องแยกออกจากกันได้ในเหลือบเดียว
    #
    # นับสองชั้น: ที่ daemon ตัดทิ้งก่อนส่ง + ที่ส่งมาแล้วแต่จอไม่พอ · ชั้นหลังเพิ่งเป็นไป
    # ได้ตอนความสูงการ์ดไม่เท่ากัน ถ้าลืมบวก ตัวเลขจะโกหกทันทีที่การ์ดสองบรรทัดมาสองใบ
    overflow += len(cards) - n
    if overflow > 0:
        draw.text((L.screen.width - L.card.pad - 8, y + 1), f"+{overflow} more",
                  font=font(11), fill=quantize565(PAL.text_dim), anchor="rm")


def _ask_button(draw: ImageDraw.ImageDraw, x: int, y: int, *,
                plate: str, label: str, ink: str) -> None:
    draw.rectangle([x, y, x + L.ask.button_w - 1, y + L.ask.button_h - 1],
                   fill=quantize565(plate))
    # กลางปุ่มทั้งสองแกน — ตรงกับ `lv_obj_align(t, LV_ALIGN_CENTER, 0, 0)` ใน ask_button()
    line(draw, (x + L.ask.button_w / 2, y + L.ask.button_h / 2), label,
         pil=12, board=14, fill=ink, anchor="mm",
         max_w=L.ask.button_w - L.ask.button_inset * 2)


def _ask(draw: ImageDraw.ImageDraw, a: Ask) -> None:
    """การ์ดคำขออนุญาต — ต้องตรงกับ build_ask()/layout_ask() ใน firmware/main/ct_ui.c

    ช่องว่างกลางไม่ได้ถูกวาด แต่มันคือส่วนที่สำคัญที่สุดของภาพนี้: การแตะที่ตกลงตรงนั้น
    ต้องไม่ทำอะไรเลย (ADR-0014) พรีวิวจึงต้องแสดงมันตามขนาดจริง ไม่ใช่ขยับปุ่มให้ดูสวย
    """
    # กว้างเท่ากองการ์ด ไม่ใช่เต็มจอ — มันยึด *ที่ของการ์ด* ทั้งแถบ ไม่ได้เป็นของใหม่ที่อื่น
    # ข้อความจึงกว้างเท่าหัวการ์ด (CARD_TEXT_W) พอดี ไม่ใช่เลขที่บังเอิญเท่ากัน — เพดาน
    # `Text.Limit.cardTitle` ฝั่ง Swift จึงคุมบรรทัดนี้อยู่แล้ว ไม่ต้องมีเพดานตัวที่สอง
    pad = L.card.pad
    y = L.card.top + pad
    draw.rectangle([pad, y, L.screen.width - pad - 1, y + L.ask.h - 1],
                   fill=quantize565(PAL.bg_card_alert))
    # บรรทัดคำถามอยู่ระดับเดียวกับหัวการ์ดปกติ — tx เดียวกับ _card()
    line(draw, (pad + 9, y + L.ask.title_dy), a.title, pil=12, board=14,
         fill=PAL.text, anchor="lt", max_w=CARD_TEXT_W)

    by = y + L.ask.button_top
    # ปฏิเสธซ้าย อนุญาตขวา ตลอดไป — ปุ่มที่สลับที่ได้คือปุ่มที่กดผิดได้
    _ask_button(draw, pad, by, plate=PAL.alert, label="Deny", ink=PAL.ink)
    # ปุ่มขวาเป็นสองอย่างในที่เดียวกัน ไม่ใช่ปุ่มที่หายไปเมื่ออนุญาตไม่ได้
    if a.may_allow:
        _ask_button(draw, pad + L.ask.button_w + L.ask.gap, by,
                    plate=PAL.good, label="Allow", ink=PAL.ink)
    else:
        _ask_button(draw, pad + L.ask.button_w + L.ask.gap, by,
                    plate=PAL.gray_dark, label="Keyboard", ink=PAL.text_dim)


def fmt_remaining(secs: int | None) -> str:
    """วินาทีที่เหลือ -> ข้อความสั้นที่สุดที่ยังบอกได้ว่าควรรีบไหม

    ระดับความละเอียดลดลงตามระยะ: ใกล้ = นาที, ไกล = ชั่วโมง/วัน
    ที่ 0 ไม่ใช่ "0m" แต่เป็น "resetting" เพราะ % ที่ถืออยู่หมดอายุไปแล้ว
    """
    if secs is None:
        return "no data"  # `--` ลอยเดี่ยวบนบรรทัดข้อความอ่านเป็นขยะ ไม่ใช่สถานะ
    if secs <= 0:
        return "resetting"
    d, rem = divmod(secs, 86400)
    h, rem = divmod(rem, 3600)
    m = rem // 60
    if d:
        return f"{d}d {h}h"
    if h:
        return f"{h}h {m:02d}m"
    return f"{m}m"


def usage_color(pct: int | None) -> str:
    if pct is None:
        return PAL.text_dim
    if pct >= L.usage.crit_pct:
        return PAL.alert
    if pct >= L.usage.warn_pct:
        return PAL.accent
    return PAL.good


def usage_bar_color(u: Usage) -> str:
    """แดงทันทีที่ใช้เร็วกว่าเวลาที่ผ่านไปในหน้าต่าง ไม่ต้องรอถึงเกณฑ์ %

    "60% ตอนเหลือเวลาอีกครึ่ง" เป็นปัญหาคนละแบบกับ "60% ตอนหมดเวลาพอดี"
    ต้องตรงกับ usage_bar_color ใน firmware/main/ct_ui.c
    และ MenuBadge.alarming ใน host/Sources/TamaCore/MenuBadge.swift (แถบเมนูใช้สูตร pace เดียวกัน แต่ไม่มีเกณฑ์ %)
    """
    if u.pct is None:
        return PAL.text_dim
    if u.remaining is not None and u.remaining > 0 and u.window > 0:
        elapsed = max(0, min(u.window, u.window - u.remaining))
        if u.pct * u.window > elapsed * 100:
            return PAL.alert
    return usage_color(u.pct)


def _usage_row(draw: ImageDraw.ImageDraw, u: Usage, y: int) -> None:
    pad, w = L.card.pad, L.screen.width
    x0, x1 = pad + 8, w - pad - 8
    col = usage_bar_color(u)

    # เปอร์เซ็นต์ตัวใหญ่ — สิ่งเดียวที่ต้องอ่านออกจากอีกฝั่งห้อง
    # 24 ตรงกับ lv_font_montserrat_24 บนบอร์ด — ฟอนต์คนละตัวแต่ขนาดต้องไม่หลุดกัน
    # ไม่ทำตัวหนาปลอม (เคยซ้อนป้ายเยื้อง 1px ฝั่ง LVGL / stroke_width=1 ฝั่งนี้):
    # ขนาด 24 กับสีของระดับดังพออยู่แล้ว ส่วนเส้นที่หนาขึ้นทำให้ช่องในเลข 0/8/9
    # ตันจนอ่านยากที่ 320x240 — ความหนาถูกสงวนไว้ให้ขีด pace ที่ใช้มันเป็นสัญญาณ
    big = "--%" if u.pct is None else f"{u.pct}%"
    big_col = quantize565(col if u.pct is not None else PAL.text_dim)
    draw.text((x0, y + 14), big, font=font(24), fill=big_col, anchor="lm")

    # เวลารีเซ็ตอยู่บรรทัดเดียวกับเลข % ไม่ใช่ชั้นใต้แถบ — เกาะขอบขวาของเลขจริง
    # ไม่ใช่พิกัดตายตัวที่กันที่ไว้ให้ "100%" ซึ่งทำให้เลขสองหลักดูห่างจนไม่เป็นก้อนเดียวกัน
    # "resetting" / "no data" ยืนลำพัง — เติม "Resets in" ข้างหน้าแล้วอ่านไม่เป็นภาษา
    left = fmt_remaining(u.remaining)
    txt = f"Resets in {left}" if u.remaining and u.remaining > 0 else left
    big_w = draw.textlength(big, font=font(24))
    draw.text((x0 + big_w + 12, y + 14), txt, font=font(11),
              fill=quantize565(PAL.text_dim), anchor="lm")

    # ป้ายชื่อหน้าต่างชิดขวา — ไม่มีสีของตัวเอง เส้นขอบบางกับตัวอักษรเท่านั้น
    # ป้ายบอก *ว่านี่คือหน้าต่างไหน* ซึ่งไม่เคยเปลี่ยน จึงไม่ควรใช้สีเลย: ป้ายเขียว
    # "Weekly" เคยนั่งอยู่เหนือแถบแดง 71% ห่างกัน 20px แล้วเขียวที่แปลว่า "ปลอดภัย"
    # ทุกที่บนจอนี้ กลับแปลว่า "รายสัปดาห์" ตรงนี้ที่เดียว — เหลือบครั้งเดียวอ่านผิด
    # สิ่งที่บอกว่ากำลังอ่านแถวไหนคือลำดับ (Current บน Weekly ล่าง) กับตัวอักษร
    # ทั้งคู่มีอยู่แล้วและไม่ต้องแย่งช่องสัญญาณกับระดับการใช้
    fl = font(11)
    lw = draw.textlength(u.label, font=fl)
    px0 = x1 - lw - 14
    draw.rounded_rectangle([px0, y + 5, x1, y + 23], radius=9,
                           outline=quantize565(PAL.text_dim), width=1)
    draw.text(((px0 + x1) / 2, y + 14), u.label, font=fl,
              fill=quantize565(PAL.text), anchor="mm")

    # แถบ — รางต้องสว่างกว่าพื้นจอพอให้เห็นความยาวเต็มตอนใช้ไปน้อย
    bh = L.usage.bar_h
    by = y + 28
    r = bh // 2
    draw.rounded_rectangle([x0, by, x1, by + bh - 1], radius=r,
                           fill=quantize565(PAL.gray_dark))
    if u.pct is not None:
        fill_w = round((x1 - x0) * min(max(u.pct, 0), 100) / 100)
        if fill_w > 0:
            draw.rounded_rectangle([x0, by, x0 + fill_w, by + bh - 1], radius=r,
                                   fill=quantize565(col))

    # ขีด pace — "ควรใช้ถึงไหนแล้ว" ตามเวลาที่ผ่านไปในหน้าต่าง
    # ขีดอยู่ขวาของเนื้อแถบ = ใช้ช้ากว่าเวลา · อยู่ซ้าย = ใช้เร็วเกินไป
    # ตอนใช้เร็วเกินขีดหนาขึ้นเป็น 3px ด้วย — สีของแถวบอกไม่ได้เมื่อภาพเป็นขาวดำ
    # (alert L 0.30 กับ good L 0.33 เทาเท่ากัน) ตำแหน่งขีดอย่างเดียวก็อ่านยากเมื่อ
    # เกินไปนิดเดียว ความหนาจึงเป็นแกนที่สองที่ไม่พึ่งสีเลย
    if u.remaining is not None and u.remaining > 0:
        elapsed = max(0, min(u.window, u.window - u.remaining))
        mx = x0 + round((x1 - x0) * elapsed / u.window)
        over = u.pct is not None and u.window > 0 and u.pct * u.window > elapsed * 100
        half = 1 if over else 0
        draw.rectangle([mx - half, by - 2, mx + half, by + bh + 1],
                       fill=quantize565(PAL.outline))


def _usage(draw: ImageDraw.ImageDraw, rows: list[Usage]) -> None:
    # แผงเตี้ยกว่าพื้นที่ที่มี — จัดกลางแนวตั้ง ไม่ชิดบน ไม่งั้นก้นจอโล่งเป็นแถบ
    # แล้วอ่านเป็น "ของหาย" แทนที่จะเป็นการตัดสินใจ
    block = 2 * L.usage.row_h + L.usage.gap
    y = L.card.top + (L.card.height - block) // 2
    for u in rows[:2]:
        _usage_row(draw, u, y)
        y += L.usage.row_h + L.usage.gap


# เวลาที่ยังไม่เคย sync — ตรงกับค่าที่ ct_model_init ใส่ไว้ให้ `clock`
CLOCK_UNKNOWN = "--:--"


def last_seen_text(s: Screen) -> str:
    """เวลาสุดท้ายที่ยังรับรองได้ พูดเป็นอดีตกาล — ต้องตรงกับ last_seen_text ใน ct_ui.c

    บอร์ดที่เพิ่งบูตและยังไม่เคยได้ snapshot ไม่มีเวลาให้อ้างถึงเลย ("--:--" คือค่านั้น)
    "since --:--" คือการอ้างถึงเวลาที่ไม่มีอยู่ — สภาพนั้นมีชื่อของมันเอง
    """
    if not s.clock or s.clock == CLOCK_UNKNOWN:
        return "no contact yet"
    return f"since {s.clock} {s.date}".rstrip()


def _idle_clock(draw: ImageDraw.ImageDraw, s: Screen) -> None:
    """ไม่มีอะไรต้องเตือน = ให้พื้นที่นี้ทำหน้าที่นาฬิกาตั้งโต๊ะแทน

    นี่คือสิ่งที่จอเป็นอยู่เกือบตลอดเวลา ถ้าปล่อยว่างจะดูเหมือนอุปกรณ์พัง

    **ตอนหลุดลิงก์ไม่มีนาฬิกา** — เวลาไม่ได้เดินต่อบนบอร์ด เลข 46pt ที่ค้างจึงเป็น
    คำโกหกที่ตัวใหญ่ที่สุดบนจอ และการหรี่เป็นเทาไม่ช่วย: มันยังอ่านว่า "ตอนนี้" อยู่ดี
    ที่เดียวกันตกเป็นของสิ่งเดียวที่บอร์ดยังยืนยันได้เอง คือหลุดมานานเท่าไร ส่วนเวลาเดิม
    ถอยลงไปอยู่บรรทัดล่างในรูปอดีตกาล — ด้วยเหตุผลเดียวกับที่ฟ้าหายไปทั้งผืน
    """
    cx = L.screen.width // 2
    cy = L.card.top + L.card.height // 2
    if s.connected:
        big, caption = s.clock, s.date
        big_col, caption_col = PAL.text, PAL.text_dim
    else:
        big, caption = age.gap_text(s.offline_s), last_seen_text(s)
        # ลำดับความสว่างยังเป็นของเดิม (ตัวใหญ่สว่างกว่าตัวเล็ก) แค่ถอยลงมาพร้อมกัน
        # หนึ่งขั้นจากสีของข้อมูลสด · บรรทัดล่างที่ยังเป็น text_dim เท่าเดิมเคยสว่างกว่า
        # ตัวใหญ่ที่หรี่แล้ว ซึ่งอ่านเป็น "วันที่คือของสด เวลาคือของค้าง" ที่ผิดทั้งคู่
        big_col, caption_col = PAL.text_dim, PAL.gray
    draw.text((cx, cy - 8), big, font=font(46), fill=quantize565(big_col), anchor="mm")
    if caption:
        draw.text((cx, cy + 26), caption, font=font(12),
                  fill=quantize565(caption_col), anchor="mm")


def shows_idle_clock(s: Screen) -> bool:
    """หน้านี้ยึดเวลาไว้เองไหม — แถบบนถามก่อนวาดนาฬิกาเล็กของมัน

    ตอนหลุดลิงก์บล็อกกลางจอไม่ใช่นาฬิกาอีกแล้ว แต่ยังพูดถึงเวลาล่าสุดอยู่ ("since 14:32")
    คำตอบจึงยังเป็น True — ไม่งั้น "14:32" เดิมจะโผล่สองที่พร้อมกัน อันหนึ่งเป็นอดีตกาล
    อีกอันเป็นเลขลอยๆ บนแถบ

    ต้องตรงกับ `ct_ui_shows_clock` ใน firmware/main/ct_ui.c
    """
    return not s.shown_ask() and not s.shown_cards() and not s.shown_usage()


def shows_usage_panel(s: Screen) -> bool:
    """แผงโควตาเต็มโผล่อยู่ไหม — การ์ดชนะโควตาเสมอ (การ์ดต้องการการกระทำ)

    ต้องตรงกับ `ct_ui_shows_usage` ใน firmware/main/ct_ui.c
    """
    return bool(s.shown_usage()) and not s.shown_cards() and not s.shown_ask()


def _bar(s: Screen) -> topbar.Bar:
    usage = s.shown_usage()
    return topbar.Bar(clock=s.clock, ble=s.ble, wifi=s.wifi, overflow=s.overflow,
                      has_usage=bool(usage), pct=usage[0].pct if usage else None,
                      remaining=usage[0].remaining if usage else None)


def render(s: Screen, phase: float = 0.0, cycle: int = 0,
           pos: footer.Pos | None = None) -> Image.Image:
    img = Image.new("RGB", (L.screen.width, L.screen.height), quantize565(PAL.bg))
    draw = ImageDraw.Draw(img)
    # ฉากอยู่หลังทุกอย่าง กินเต็มจอใต้แถบบน — มาสคอตยืนทับ ยอมให้บังดาวและดวงที่เตี้ย
    # การถูกบังคือระยะลึก ไม่ใช่ของหาย · แต่ตำแหน่งดวงคือ *เวลา* ไม่ใช่ของประดับ
    # ส่วนโค้งจึงถูกดันขึ้นให้ดวงพ้นหัวมาสคอตตลอดกลางวัน (ดู sky._arc) แทนที่จะแก้ที่
    # ลำดับการวาด — ดวงอาทิตย์ที่ลอยทับตัวละครไม่ใช่ระยะลึกอีกต่อไป มันคือสติกเกอร์
    sky.draw(draw, s.clock, s.connected, cycle + phase, s.weather_code)
    topbar.draw(draw, _bar(s), page=pages.LABELS["mascot"], connected=s.connected,
                page_shows_clock=shows_idle_clock(s), page_shows_usage=shows_usage_panel(s))
    n = min(len(s.sessions), L.slots.count)
    if n == 0:
        # แถบ slot ที่ว่างเปล่าอ่านได้ว่า "อุปกรณ์ค้าง" — ให้มาสคอตเดินผ่านแทน
        _stroll(draw, s, phase, cycle)
    for i in range(n):
        _slot(draw, i, s.sessions[i], s, phase, cycle, n)
    # ลำดับความสำคัญของพื้นที่ล่าง: คำถาม > การเตือน > โควตา > นาฬิกา
    # โควตาไม่เคยชนะ card เพราะ card คือสิ่งที่ต้องการการกระทำจากผู้ใช้ · และคำถามชนะ
    # card ด้วยเหตุผลที่แรงกว่า: การ์ดบอกว่ามีอะไรเกิดขึ้น ส่วนคำถามมีคนหยุดรออยู่ปลายทาง
    if ask := s.shown_ask():
        _ask(draw, ask)
    elif cards := s.shown_cards():
        _cards(draw, cards, s.card_overflow)
    elif usage := s.shown_usage():
        _usage(draw, usage)
    else:
        _idle_clock(draw, s)
    # **หน้านี้ได้ pip แต่ไม่ได้แถบ** — มาสคอตตัวจริงอยู่กลางจอแล้ว ไม่ต้องมีตัวจิ๋วซ้ำ
    # ไม่มีข้อมูลที่ดึงมาจึงไม่มีอายุให้บอก และพื้นดินกินถึงขอบล่าง แถบทึบจะตัดฉากทิ้ง
    #
    # แต่ pip ต้องมี: หน้านี้คือหน้าที่บอร์ดกระโดดกลับมาบ่อยที่สุด คนที่ยืนอยู่ตรงนี้แล้ว
    # ไม่เห็นอะไรบอกว่าปัดได้ คือคนที่ไม่รู้ว่าอีกสี่หน้ามีอยู่ · affordance ที่มี 4 ใน 5 หน้า
    # คือ affordance ที่สอนไม่สำเร็จ
    footer.pips(draw, pos or footer.Pos(), has_mini=False)
    return img
