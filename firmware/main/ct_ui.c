#include "ct_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ct_age.h"
#include "ct_color.h"
#include "ct_fonts.h"
#include "ct_mascot.h"
#include "ct_paint.h"
#include "ct_rects.h"
#include "ct_sky.h"
#include "layout.h"
#include "lvgl.h"

// หนึ่งลูปอนิเมชันยาวเท่าไร (ms) — ตรงกับสมมติฐาน "ลูปหนึ่งราว 1 วินาที" ของ mascot.c
#define LOOP_MS 1000

typedef struct {
    lv_obj_t *canvas;  // ตัววาดมาสคอต (วาดเองใน LV_EVENT_DRAW_MAIN)
    lv_obj_t *label;   // ป้ายชื่อโปรเจกต์
    int index;
} slot_t;

typedef struct {
    lv_obj_t *box;
    lv_obj_t *accent;
    // เครื่องหมายชิดขวา — ตัน (alert) · กลวง (info) · ขีด (done)
    // กลวงคือ mark ที่มี mark_hole สีพื้นการ์ดวางทับกลาง ไม่ใช่กรอบที่วาดเอง
    // เพราะ LVGL คิด border จากขอบนอกเข้ามา แล้วรูตรงกลางจะไม่ลงตัวที่ 8px
    lv_obj_t *mark;
    lv_obj_t *mark_hole;
    lv_obj_t *title;
    lv_obj_t *body;
} card_t;

// การ์ดคำขออนุญาต — ปุ่มสองใบที่มีช่องว่างคั่นกลาง (ADR-0014)
//
// ช่องว่างนั้นไม่ใช่ระยะห่างเพื่อความสวยงาม มันคือที่ที่การแตะพลาดตกลงไปแล้วไม่มีอะไร
// เกิดขึ้น ซึ่งเป็นผลลัพธ์ที่ถูกของการเล็งไม่แม่นบนจอที่ให้พิกัดคลาดได้ราว 5px
typedef struct {
    lv_obj_t *box;
    lv_obj_t *title;
    lv_obj_t *deny;
    lv_obj_t *deny_text;
    // ปุ่มขวาเปลี่ยนความหมายตามใบ ไม่ใช่ตามค่าตั้ง — Allow เมื่ออนุญาตได้
    // และ "Keyboard" เมื่อคำขอนั้นต้องไปตอบที่ที่มีข้อความเต็มให้อ่าน
    lv_obj_t *allow;
    lv_obj_t *allow_text;
} ask_t;

typedef struct {
    // ตัวเลขใหญ่ — สิ่งเดียวที่ต้องอ่านออกจากอีกฝั่งห้อง ป้ายเดียว ไม่ทำตัวหนาปลอม:
    // เคยซ้อนป้ายเดิมเยื้อง 1px แทน montserrat ตัวหนาที่ LVGL ไม่มี แต่เส้นที่หนาขึ้น
    // ทำให้ช่องในเลข 0/8/9 ตันจนอ่านยากที่ 320x240 — ขนาด 24 กับสีของระดับดังพอแล้ว
    lv_obj_t *percent;
    lv_obj_t *pill;     // ป้ายบอกว่าเป็นหน้าต่างไหน สีคงที่ ไม่ตามระดับ
    lv_obj_t *pill_text;
    lv_obj_t *track;  // รางแถบ
    lv_obj_t *fill;   // เนื้อแถบ
    lv_obj_t *pace;   // ขีดบอกว่า "ควรใช้ถึงไหนแล้ว" ตามเวลาที่ผ่านไปในหน้าต่าง
    lv_obj_t *reset;  // countdown
} usage_row_t;

// page frame ที่หน้านี้วาด — เจ้าของคือ `ct_pages` ไม่ใช่ที่นี่ ตัวเรนเดอร์อ่านอย่างเดียว
// (ที่เก็บต้องอยู่กับตัวโฮสต์เพราะเฟรมของหน้าที่ไม่ได้แสดงอยู่ก็ต้องรอดและนาฬิกาต้องเดินต่อ)
static const ct_snapshot_t *s_frame;
static bool s_connected = false;
// หลุดลิงก์มากี่วินาทีแล้ว — นับที่ `ct_pages` (นาฬิกาทุกเรือนเดินที่นั่น) ส่งเข้ามาที่นี่
static int s_offline_s;

// --- ฉากท้องฟ้า ---------------------------------------------------------------
// ฟ้า 22..93 แล้วพื้นดินลงไปถึงก้นจอ — วาดในผืนเดียวหลังทุกอย่าง
// ตรรกะทั้งหมดต้องตรงกับ tools/gen/sky.py · ชื่อช่วงกับตัวอ่านเวลาอยู่ที่ ct_sky.h
// เพราะหน้าปฏิทินใช้ชุดเดียวกันกับ *เวลาของนัด*
static const uint16_t SKY_BG[CT_SKY_PHASE_COUNT] = {CT_COL_SKY_NIGHT, CT_COL_SKY_DAWN,
                                                    CT_COL_SKY_DAY, CT_COL_SKY_DUSK};
static const uint16_t SKY_GROUND[CT_SKY_PHASE_COUNT] = {
    CT_COL_GROUND_NIGHT, CT_COL_GROUND_DAWN, CT_COL_GROUND_DAY, CT_COL_GROUND_DUSK};
static const uint16_t SKY_GRASS[CT_SKY_PHASE_COUNT] = {
    CT_COL_GRASS_NIGHT, CT_COL_GRASS_DAWN, CT_COL_GRASS_DAY, CT_COL_GRASS_DUSK};
static const uint16_t SKY_SHADOW[CT_SKY_PHASE_COUNT] = {
    CT_COL_SHADOW_NIGHT, CT_COL_SHADOW_DAWN, CT_COL_SHADOW_DAY, CT_COL_SHADOW_DUSK};
// กลางคืนไม่มีเมฆ ช่องแรกจึงไม่ถูกใช้
static const uint16_t SKY_CLOUD[CT_SKY_PHASE_COUNT] = {0, CT_COL_CLOUD_DAWN, CT_COL_CLOUD_DAY,
                                                       CT_COL_CLOUD_DUSK};
// กลุ่มที่มีแนวเมฆปิดฟ้า — โล่งไม่มีอะไรปิด ส่วนหมอกเป็นแถบนอน ไม่ใช่เพดาน
static inline bool sky_has_deck(ct_wx_kind_t k)
{
    return k == CT_WX_CLOUD || k == CT_WX_RAIN || k == CT_WX_SNOW || k == CT_WX_STORM;
}

// กลางวันที่ฟ้าถูกปิดแต่ยังไม่ถึงพายุ -> ใช้ชุดสี wx_dull_* แทนสีของช่วงเวลา
// เฉพาะ day: ช่วงอื่นฟ้าเข้มอยู่แล้ว และ "ฟ้าใสใต้เพดานเทา" เห็นได้แค่ตอนกลางวัน
// ต้องตรงกับ _dull() ใน tools/gen/sky.py
static inline bool sky_is_dull(ct_sky_phase_t phase, ct_wx_kind_t k)
{
    return phase == CT_SKY_DAY &&
           (k == CT_WX_CLOUD || k == CT_WX_FOG || k == CT_WX_RAIN || k == CT_WX_SNOW);
}

// สีของแนวเมฆ/แถบหมอก/ก้อนลอย — สามทาง ไม่ใช่สองทาง ตั้งแต่มีฟ้าครึ้ม
static inline uint16_t sky_deck_color(ct_sky_phase_t phase, ct_wx_kind_t k)
{
    if (k == CT_WX_STORM) return CT_COL_WX_STORM_DECK;
    return sky_is_dull(phase, k) ? CT_COL_WX_DULL_DECK : CT_SKY_DECK[phase];
}

// เงาใต้เท้า — กว้าง 11 unit วางใต้เส้นขอบฟ้า ตรงกับ SHADOW_W ใน tools/gen/screen.py
#define SHADOW_W_UNIT 11.0f
#define SHADOW_H 5
// กึ่งกลางลำตัวในหน่วย unit (BODY = 1.0 กว้าง 14.0 ใน tools/gen/mascot.py)
#define BODY_CX 8.0f

static lv_obj_t *s_sky;
static ct_sky_phase_t s_sky_phase = CT_SKY_NONE;
static float s_sky_hours = -1.0f;  // เวลาที่ใช้หาตำแหน่งดวง — <0 คือไม่รู้
static int s_cloud_shift = -1;     // เมฆเลื่อนไปกี่พิกเซลแล้ว ใช้ตัดสินว่าต้องวาดใหม่ไหม
// สภาพอากาศบนฟ้าของหน้านี้ — มาจากเฟรมของ *หน้าอากาศ* ที่ ct_pages.c แคชไว้ ไม่ใช่จาก
// snapshot (ADR-0012) · CT_WX_CLEAR คือทั้ง "ฟ้าโล่งจริงๆ" และ "ไม่มีอากาศที่ใช้ได้"
// ซึ่งเป็นค่าเดียวกันโดยตั้งใจ: ทั้งสองต้องได้ฟ้าตามเวลาแบบก่อนหน้านี้เป๊ะ
static ct_wx_kind_t s_wx = CT_WX_CLEAR;

static lv_obj_t *s_stroll;  // มาสคอตเดินข้ามจอตอนไม่มี session — กินแถบ slot ทั้งแถบ
static lv_obj_t *s_clock_big, *s_date;
static lv_obj_t *s_card_more;  // "+N more" ใต้การ์ดใบล่างสุด
static slot_t s_slots[CT_SLOTS_COUNT];
static card_t s_cards[CT_MAX_CARDS];
static ask_t s_ask;
static usage_row_t s_usage[CT_USAGE_ROWS];

static float s_phase = 0.0f;
static int s_cycle = 0;

// ขอบซ้ายของ slot ที่ i เมื่อกำลังแสดง session อยู่ n ตัว
// ระยะห่างคงที่ 80px เสมอ แต่ยกทั้งกลุ่มมาไว้กึ่งกลางจอ — สิ่งที่ต้องนิ่งคือ *ลำดับ*
static int slot_x(int i, int n)
{
    return (int)lroundf((CT_SCREEN_WIDTH - n * CT_SLOTS_WIDTH) / 2.0f) + i * CT_SLOTS_WIDTH;
}

// สัดส่วนของเส้นทาง (0..1) -> จุดกึ่งกลางดวงบนส่วนโค้ง
// ที่ u=0 และ u=1 ดวงอยู่บนเส้นขอบฟ้าพอดี (จมครึ่งดวง) ที่ขอบจอทั้งสองข้าง
//
// ความสูงเป็น `sqrt(sin)` ไม่ใช่ sin ล้วน — ดวงไต่ขึ้นเร็วกว่าตอนเช้าแล้วค้างสูง
// เพื่อให้พ้นหัวมาสคอต (แถบ y 53..120) ตลอดกลางวัน ไม่ใช่แค่ 10:00-14:00
// ต้องตรงกับ _arc ใน tools/gen/sky.py
static void sky_arc(float u, float *x, float *y)
{
    *x = -(float)CT_SKY_ARC_PAD + u * (float)(CT_SCREEN_WIDTH + 2 * CT_SKY_ARC_PAD);
    *y = (float)CT_SKY_HORIZON - sqrtf(sinf((float)M_PI * u)) * (float)CT_SKY_ARC_PEAK;
}

// ดวงอาทิตย์ 05:00->19:00 · ดวงจันทร์ 19:00->05:00 — มีดวงใดดวงหนึ่งบนฟ้าเสมอ
static void sky_disc(float t, float *x, float *y, uint16_t *color)
{
    if (t >= CT_SKY_DAWN_HOUR && t < CT_SKY_NIGHT_HOUR) {
        sky_arc((t - CT_SKY_DAWN_HOUR) / (float)(CT_SKY_NIGHT_HOUR - CT_SKY_DAWN_HOUR), x, y);
        ct_sky_phase_t p = ct_sky_phase_at(t);
        *color = (p == CT_SKY_DAWN || p == CT_SKY_DUSK) ? CT_COL_SUN_LOW : CT_COL_SUN;
        return;
    }
    float span = (float)(24 - CT_SKY_NIGHT_HOUR + CT_SKY_DAWN_HOUR);
    sky_arc(fmodf(t - CT_SKY_NIGHT_HOUR + 24.0f, 24.0f) / span, x, y);
    *color = CT_COL_MOON;
}

static void fill_rect(lv_layer_t *layer, int x0, int y0, int x1, int y1, uint16_t color,
                      int radius)
{
    if (x1 < x0 || y1 < y0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.bg_color = ct_color(color);
    dsc.radius = radius;
    lv_area_t a = {.x1 = x0, .y1 = y0, .x2 = x1, .y2 = y1};
    lv_draw_rect(layer, &dsc, &a);
}

static void draw_stars(lv_layer_t *layer, ct_sky_phase_t phase, ct_wx_kind_t kind)
{
    // พายุไม่มีดาวไม่ว่ากี่โมง — กติกาเดียวกับหน้าอากาศ
    if (phase == CT_SKY_DAY || kind == CT_WX_STORM) return;
    // ดาวเป็นสี่เหลี่ยมอย่างน้อย star_px x star_px ทุกดวง — จุด 1px หายไปเลยบนแผงจริง
    const int d = CT_SKY_STAR_PX - 1;
    if (phase != CT_SKY_NIGHT) {
        // ฟ้ายังสว่างเกินกว่าจะเห็นทั้งหมด — ดวงแรกๆ สีหรี่ ไม่กะพริบ
        for (int i = 0; i < CT_SKY_LOW_STAR_N; i++) {
            int x = ct_sky_stars[i][0], y = ct_sky_stars[i][1];
            fill_rect(layer, x, y, x + d, y + d, CT_COL_STAR_DIM, 0);
        }
        return;
    }
    // ดวงที่กะพริบไล่สว่างขึ้นแล้วหรี่ลง: dim -> mid -> star(โต) -> mid ขั้นละ 1 วินาที
    // ตั้งต้นที่หรี่แล้วสว่างขึ้น ไม่ใช่ตั้งต้นสว่างแล้วดับ — ดาวดับอ่านเป็นจอเสีย
    // ขั้นสว่างสุดโตเป็น star_peak_px ด้วย: บนแผงจริงต่างแค่สีจางเกินกว่าจะจับได้
    // i * 3 ทำให้สี่ดวงเริ่มคนละขั้น (0,3,2,1) ไม่กะพริบพร้อมกันเป็นจังหวะเดียว
    static const uint16_t ramp[4] = {CT_COL_STAR_DIM, CT_COL_STAR_MID, CT_COL_STAR,
                                     CT_COL_STAR_MID};
    static const int ramp_px[4] = {CT_SKY_STAR_PX, CT_SKY_STAR_PX, CT_SKY_STAR_PEAK_PX,
                                   CT_SKY_STAR_PX};
    // ฟ้าที่ถูกเมฆปิดก็เห็นไม่ครบเหมือนกัน — เหตุที่สอง ไม่ใช่กฎที่สอง แต่ *ยังกะพริบ*
    // ต่างจากทาง dawn/dusk ข้างบน เพราะกลางคืนที่เป็นเมฆล้วนไม่มีทั้งเมฆลอย (กลางคืน
    // ไม่มี) และของที่ตกลงมา ดาวที่หยุดกะพริบด้วยจะทำให้จอนิ่งสนิททั้งคืน
    int n = sky_has_deck(kind) ? CT_SKY_LOW_STAR_N : CT_SKY_STARS_COUNT;
    for (int i = 0; i < n; i++) {
        int x = ct_sky_stars[i][0], y = ct_sky_stars[i][1];
        if (i >= CT_SKY_TWINKLE_N) {
            fill_rect(layer, x, y, x + d, y + d, CT_COL_STAR, 0);
            continue;
        }
        int step = (s_cycle + i * 3) % 4, s = ramp_px[step];
        // โตออกจากกึ่งกลาง ไม่ใช่ยืดลงขวา — ไม่งั้นดวงที่โตขึ้นอ่านเป็นดาวเลื่อนที่
        int off = (s - CT_SKY_STAR_PX) / 2;
        fill_rect(layer, x - off, y - off, x - off + s - 1, y - off + s - 1, ramp[step], 0);
    }
}

static void draw_clouds(lv_layer_t *layer, ct_sky_phase_t phase, float t, ct_wx_kind_t kind)
{
    if (phase == CT_SKY_NIGHT) return;
    // ก้อนลอยยืมสีของแนวเมฆทุกครั้งที่ฟ้าถูกปิด ไม่ใช่สีเมฆของช่วงเวลา — cloud_day
    // เกือบขาวสนิท บนฟ้าครึ้มหรือฟ้าพายุมันอ่านเป็นรูรั่วบนเพดาน ไม่ใช่เมฆ
    // ที่ยังต้องมีก้อนลอยเพราะวันที่ปิดสนิทไม่มีทั้งดาวและของที่ตกลงมา (สายฟ้าไม่กะพริบ)
    uint16_t color = kind == CT_WX_CLEAR ? SKY_CLOUD[phase] : sky_deck_color(phase, kind);
    float span = (float)(CT_SCREEN_WIDTH + 2 * CT_SKY_CLOUD_PAD);
    for (int i = 0; i < CT_SKY_CLOUDS_COUNT; i++) {
        float base_x = ct_sky_clouds[i][0];
        int y = ct_sky_clouds[i][1], w = ct_sky_clouds[i][2];
        float x = fmodf(base_x + t * (float)CT_SKY_CLOUD_SPEED_PX_S, span) - CT_SKY_CLOUD_PAD;
        int xi = (int)lroundf(x);
        fill_rect(layer, xi, y, xi + w, y + 9, color, 4);
        // ก้อนบนทำให้อ่านเป็นเมฆ ไม่ใช่แถบ — เยื้องซ้ายของกึ่งกลาง ไม่ใช่สมมาตร
        int bx = (int)lroundf(x + w * 0.2f), bw = (int)lroundf(w * 0.45f);
        fill_rect(layer, bx, y - 5, bx + bw, y + 4, color, 4);
    }
}

// แนวเมฆที่ปิดฟ้าลงมาถึง deck_bottom พร้อมก้อนที่ห้อยลงจากก้นแนว
// ตื้นกว่าของหน้าอากาศมาก และเป็นค่าเดียวทุกสภาพ — เหตุผลอยู่ที่ deck_bottom ใน
// layout.toml (สรุป: ลึกกว่านี้แล้วฟ้าเลิกบอกเวลา) · ต้องตรงกับ _draw_deck ใน gen/sky.py
static void draw_deck(lv_layer_t *layer, ct_sky_phase_t phase, ct_wx_kind_t kind)
{
    uint16_t color = sky_deck_color(phase, kind);
    const int bottom = CT_SKY_DECK_BOTTOM;
    fill_rect(layer, 0, CT_TOPBAR_HEIGHT, CT_SCREEN_WIDTH - 1, bottom - 1, color, 0);
    for (int i = 0; i < CT_SKY_DECK_LUMPS_COUNT; i++) {
        int x = ct_sky_deck_lumps[i][0], w = ct_sky_deck_lumps[i][1];
        int h = ct_sky_deck_lumps[i][2];
        // รัศมีเท่าครึ่งความสูงของกล่อง — ก้อนเป็นวงกลมพอดีเมื่อ w == 2h ซึ่งเป็นกฎของ
        // ตารางนี้ (ดู deck_lumps ใน layout.toml) · กว้างกว่านั้น LVGL clamp รัศมีแล้วได้
        // แคปซูลก้นแบน ซึ่งอ่านเป็นแถบสีขอบหยิก ไม่ใช่เมฆ
        fill_rect(layer, x, bottom - h, x + w - 1, bottom + h - 1, color, h);
    }
}

// ของที่ตกเลื่อนลงมากี่พิกเซลแล้ว ณ วินาทีที่ t — ตัดเป็นจำนวนเต็มก่อนบวก ไม่ใช่บวก
// ทศนิยมแล้วค่อยปัด ความเร็วเป็นจำนวนเต็มพิกเซลต่อเฟรมอยู่แล้วสองฝั่งจึงไม่มีอะไรให้เถียงกัน
// ต้องตรงกับ _fall_shift ใน tools/gen/sky.py
static int fall_shift(float t, int speed)
{
    return (int)(t * (float)speed);
}

// สิ่งที่กำลังตกลงมาจากแนวเมฆ — ฝนเป็นแท่งตั้ง หิมะเป็นสี่เหลี่ยมจัตุรัส
// ต่างจากหน้าอากาศที่ฉากนิ่ง: หน้านี้เป็นหน้า idle ที่ค้างอยู่ทั้งคืน (ดู rain ใน layout.toml)
// เม็ดที่เลยเส้นขอบฟ้าถูกพื้นดินตัดเองตอนวาด ไม่ต้องตัดตรงนี้
static void draw_fall(lv_layer_t *layer, ct_sky_phase_t phase, ct_wx_kind_t kind, float t)
{
    const int top = CT_SKY_DECK_BOTTOM, span = CT_SKY_HORIZON - CT_SKY_DECK_BOTTOM;
    if (kind == CT_WX_RAIN) {
        int shift = fall_shift(t, CT_SKY_RAIN_SPEED_PX_S);
        for (int i = 0; i < CT_SKY_RAIN_COUNT; i++) {
            int x = ct_sky_rain[i][0], len = ct_sky_rain[i][2];
            int y = top + (ct_sky_rain[i][1] - top + shift) % span;
            fill_rect(layer, x, y, x + CT_SKY_RAIN_W - 1, y + len - 1, CT_COL_STEEL, 0);
        }
        return;
    }
    // หิมะบนฟ้าที่สว่างต้องเป็นหมึกเข้ม ไม่ใช่ขาวบนขาว — เงื่อนไขสั้นกว่าของหน้าอากาศ
    // เพราะที่นี่พายุกับหิมะเป็นคนละกลุ่มอยู่แล้ว เหลือแค่ "ช่วง day ไหม"
    uint16_t color = phase == CT_SKY_DAY ? CT_COL_WX_FLAKE_INK : CT_COL_WX_FLAKE;
    int shift = fall_shift(t, CT_SKY_SNOW_SPEED_PX_S);
    for (int i = 0; i < CT_SKY_SNOW_COUNT; i++) {
        int x = ct_sky_snow[i][0], s = ct_sky_snow[i][2];
        int y = top + (ct_sky_snow[i][1] - top + shift) % span;
        fill_rect(layer, x, y, x + s - 1, y + s - 1, color, 0);
    }
}

// แถบหมอกนอนขวางจอ — ไม่มีแนวเมฆ หมอกคือสิ่งที่ *ไม่* มีรูปร่าง
static void draw_fog(lv_layer_t *layer, ct_sky_phase_t phase)
{
    for (int i = 0; i < CT_SKY_FOG_BANDS_COUNT; i++) {
        int y = ct_sky_fog_bands[i][0], h = ct_sky_fog_bands[i][1];
        fill_rect(layer, 0, y, CT_SCREEN_WIDTH - 1, y + h - 1,
                  sky_deck_color(phase, CT_WX_FOG), 0);
    }
}

static void draw_bolt(lv_layer_t *layer)
{
    for (int i = 0; i < CT_SKY_BOLT_COUNT; i++) {
        int x = ct_sky_bolt[i][0], y = ct_sky_bolt[i][1];
        int w = ct_sky_bolt[i][2], h = ct_sky_bolt[i][3];
        fill_rect(layer, x, y, x + w - 1, y + h - 1, CT_COL_ACCENT, 0);
    }
}

// กอหญ้างอกขึ้นจากเส้นขอบฟ้าไปในฟ้า — ก้านกลางสูงสุด ขนาบด้วยก้านสั้นสองข้าง
// งอกขึ้น ไม่ใช่ห้อยลง: หญ้าที่ยื่นลงไปในพื้นอ่านเป็นรอยขีดบนดิน ไม่ใช่ต้นไม้
static void draw_grass_color(lv_layer_t *layer, uint16_t color)
{
    int y = CT_SKY_HORIZON - 1;
    for (int i = 0; i < CT_SKY_GRASS_X_COUNT; i++) {
        int x = ct_sky_grass_x[i];
        int main = 3 + i % 4, side = 2 + i % 3;  // สูงเท่ากันหมดอ่านเป็นรั้ว ไม่ใช่หญ้า
        // ก้านหนา 2px เว้นช่อง 1px — ก้าน 1px หายไปเลยบนแผงจริง
        fill_rect(layer, x, y - main, x + 1, y, color, 0);
        fill_rect(layer, x - 3, y - side, x - 2, y, color, 0);
        fill_rect(layer, x + 3, y - (2 + (side + 1) % 3), x + 4, y, color, 0);
    }
}

static void draw_grass(lv_layer_t *layer, ct_sky_phase_t phase)
{
    draw_grass_color(layer, SKY_GRASS[phase]);
}

static void sky_draw_cb(lv_event_t *e)
{
    if (s_sky_phase == CT_SKY_NONE) return;  // ไม่มีฉาก = ปล่อยให้เป็นพื้นจอเปล่า

    lv_layer_t *layer = lv_event_get_layer(e);
    ct_sky_phase_t phase = s_sky_phase;
    ct_wx_kind_t kind = s_wx;
    float t = (float)s_cycle + s_phase;
    // ฟ้าเริ่มใต้แถบบน ไม่ใช่ที่ขอบบนของแถบมาสคอต — แถบมาสคอตนั่งต่ำกว่านั้นลงมามาก
    // ฟ้าพายุแทนสีของช่วงเวลาทั้งย่าน เหมือนหน้าอากาศเป๊ะ — ผู้ใช้ปัดระหว่างสองหน้านี้
    // พายุที่มืดสนิทหน้าหนึ่งแต่ฟ้าใสอีกหน้าอ่านเป็นข้อมูลขัดกัน ไม่ใช่สองมุมมอง
    // ที่นี่ไม่มีตัวอักษรอยู่ในย่านฟ้าเลย (card.top = 144, แผงโควตา/นาฬิกาอยู่บนพื้นดิน)
    // การกลับขั้วหมึกแบบ ADR-0009 จึงไม่ลามมาถึงหน้านี้ สิ่งเดียวที่ยืนบนฟ้าคือมาสคอต
    uint16_t base = kind == CT_WX_STORM      ? CT_COL_WX_STORM_SKY
                    : sky_is_dull(phase, kind) ? CT_COL_WX_DULL_SKY
                                               : SKY_BG[phase];
    fill_rect(layer, 0, CT_TOPBAR_HEIGHT, CT_SCREEN_WIDTH - 1, CT_SKY_HORIZON - 1, base, 0);

    draw_stars(layer, phase, kind);
    // เมฆก่อนดวง ไม่ใช่ดวงก่อนเมฆ — ตำแหน่งดวงคือเวลา ส่วนเมฆเป็นของประดับที่ลอยผ่าน
    // ของที่บังนาฬิกาได้มีได้อย่างเดียวคือตัวละคร (ตรงกับ tools/gen/sky.py:draw)
    draw_clouds(layer, phase, t, kind);
    float x, y;
    uint16_t color;
    sky_disc(s_sky_hours, &x, &y, &color);
    // ตอนพายุดวงอาทิตย์ใช้สีของดวงที่เตี้ย — ดวงยังต้องอยู่เพราะมันคือนาฬิกา แต่ดวงสีเต็ม
    // บนฟ้าพายุอ่านเป็นข้อผิดพลาด ไม่ใช่แดดที่ส่องผ่านเมฆหนา · ดวงจันทร์หรี่อยู่แล้ว
    if (kind == CT_WX_STORM && color == CT_COL_SUN) color = CT_COL_SUN_LOW;
    int cx = (int)lroundf(x), cy = (int)lroundf(y), r = CT_SKY_DISC_R;
    fill_rect(layer, cx - r, cy - r, cx + r, cy + r, color, LV_RADIUS_CIRCLE);

    // แนวเมฆเป็นข้อยกเว้นเดียวของกติกา "ห้ามอะไรบังดวง" — และมันบังได้ *ทั้งดวง*
    // ฟ้าที่ปิดสนิทแต่ยังเห็นดวงอาทิตย์คือฟ้าที่ขัดแย้งกับตัวเอง ฉากนี้บอกสภาพตอนนี้
    // ส่วนเวลามีนาฬิกาบนแถบบนอยู่แล้ว (เคยตั้งเพดานไว้ไม่ให้บังหมด แล้วกลับคำ — ดู
    // deck_bottom ใน layout.toml) · เมฆลอยยังอยู่ใต้ deck ตามเดิม ไม่ได้ถูกแทน:
    // กลางวันที่ฟ้าปิดและไม่มีอะไรตกลงมาต้องยังมีอะไรขยับ ไม่งั้นจอนิ่งสนิททั้งวัน
    if (sky_has_deck(kind)) draw_deck(layer, phase, kind);
    if (kind == CT_WX_RAIN || kind == CT_WX_SNOW) draw_fall(layer, phase, kind, t);
    if (kind == CT_WX_FOG) draw_fog(layer, phase);
    if (kind == CT_WX_STORM) draw_bolt(layer);

    // พื้นดินวาดทับหลังสุด — ครึ่งล่างของดวงและเมฆที่ต่ำเกินไปถูกตัดที่เส้นขอบฟ้าเอง
    // และฝนที่ตกถึงพื้นก็หายไปที่เส้นนั้นเอง ไม่ต้องมีใครตัดให้
    fill_rect(layer, 0, CT_SKY_HORIZON, CT_SCREEN_WIDTH - 1, CT_SCREEN_HEIGHT - 1,
              kind == CT_WX_STORM ? CT_COL_GROUND_NIGHT : SKY_GROUND[phase], 0);
    // พายุยืมหญ้าของ dusk ไม่ใช่ของ night ด้วยเหตุผลที่หน้าอากาศบันทึกไว้แล้ว:
    // เกณฑ์ของสีหญ้าคือ 2:1 กับฟ้าของช่วงนั้น และฟ้าพายุที่ขอบฟ้าคือแถบสว่างค้าง
    // หญ้าของฟ้าครึ้มไม่ใช่หญ้าของช่วงไหนเลย — เกณฑ์คือ 2:1 กับฟ้า *ที่มันยืนอยู่หน้า*
    if (sky_is_dull(phase, kind)) {
        draw_grass_color(layer, CT_COL_WX_DULL_GRASS);
    } else {
        draw_grass(layer, kind == CT_WX_STORM ? CT_SKY_DUSK : phase);
    }
}

// เงาใต้เท้า — ปักหมุดว่าพื้นอยู่ตรงไหน ทำให้ท่ากระโดดอ่านเป็นกระโดด ไม่ใช่ลอย
// ขนาดคงที่ ไม่ยุบตามความสูงที่กระโดด
static void draw_shadow(lv_layer_t *layer, float body_cx)
{
    if (s_sky_phase == CT_SKY_NONE) return;  // ไม่มีพื้นก็ไม่มีเงา
    float half = SHADOW_W_UNIT * CT_SLOTS_UNIT_PX / 2.0f;
    // เงาจับคู่กับ *พื้น* ที่มันทาบอยู่ ไม่ใช่กับชั่วโมง — พายุใช้พื้นของ night
    // ถ้าใช้ของช่วงเวลาต่อไปจะได้คราบม่วงบนพื้นเทากลางพายุตอนเย็น
    ct_sky_phase_t sp = s_wx == CT_WX_STORM ? CT_SKY_NIGHT : s_sky_phase;
    fill_rect(layer, (int)lroundf(body_cx - half), CT_SKY_HORIZON,
              (int)lroundf(body_cx + half), CT_SKY_HORIZON + SHADOW_H - 1,
              SKY_SHADOW[sp], LV_RADIUS_CIRCLE);
}

// --- การวาดมาสคอต ------------------------------------------------------------
// ลูปวาดจริงอยู่ที่ ct_paint.c — หน้าอากาศวาด rect list ชุดเดียวกันนี้แบบย่อ
static void draw_mascot_rects(lv_layer_t *layer, const ct_rects_t *rects, float ox, float oy)
{
    ct_paint_rects(layer, rects, ox, oy, CT_SLOTS_UNIT_PX);
}

static void slot_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    slot_t *slot = (slot_t *)lv_obj_get_user_data(obj);
    if (!slot || slot->index >= s_frame->session_count) return;

    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const float px = CT_SLOTS_UNIT_PX;
    // ฝ่าเท้าอยู่เหนือขอบล่างของ slot เท่ากับ baseline_pad เสมอ ไม่ว่าท่าไหน
    float foot = coords.y1 + CT_SLOTS_HEIGHT - CT_SLOTS_BASELINE_PAD;
    float oy = foot - CT_BOX_Y1 * px;
    float ox = coords.x1 + CT_SLOTS_WIDTH / 2.0f - (CT_BOX_X0 + CT_BOX_X1) / 2.0f * px;

    // แต่ละตัวเดินคนละจังหวะเล็กน้อย ไม่งั้นดูเป็นหุ่นยนต์ชุดเดียวกัน
    float phase = fmodf(s_phase + slot->index * 0.17f, 1.0f);

    ct_state_t state = s_frame->sessions[slot->index].state;
    draw_shadow(layer, ox + (BODY_CX + ct_mascot_center_dx(state)) * px);

    ct_rects_t rects;
    ct_mascot_build_centered(&rects, state, phase, s_connected, s_cycle + slot->index);
    draw_mascot_rects(layer, &rects, ox, oy);
}

// ท่าที่หยุดทำกลางทาง วนไปตามรอบ — ต้องตรงกับ STROLL_ACTS ใน tools/gen/screen.py
// ชุดท่าเปลี่ยนตามระดับโควตา ไม่ใช่แค่ความเร็ว (เหตุผลอยู่ฝั่ง Python)
static const ct_state_t STROLL_ACTS_CALM[] = {CT_STATE_CELEBRATE, CT_STATE_THINKING,
                                              CT_STATE_SEARCHING, CT_STATE_WAITING};
static const ct_state_t STROLL_ACTS_WARN[] = {CT_STATE_THINKING, CT_STATE_SEARCHING,
                                              CT_STATE_WAITING};
static const ct_state_t STROLL_ACTS_CRIT[] = {CT_STATE_WAITING, CT_STATE_THINKING};
static const ct_state_t *const STROLL_ACTS[] = {STROLL_ACTS_CALM, STROLL_ACTS_WARN,
                                                STROLL_ACTS_CRIT};
static const int STROLL_ACTS_N[] = {
    (int)(sizeof(STROLL_ACTS_CALM) / sizeof(STROLL_ACTS_CALM[0])),
    (int)(sizeof(STROLL_ACTS_WARN) / sizeof(STROLL_ACTS_WARN[0])),
    (int)(sizeof(STROLL_ACTS_CRIT) / sizeof(STROLL_ACTS_CRIT[0])),
};
static const int STROLL_SPEED[] = {CT_STROLL_SPEED_PX_S, CT_STROLL_WARN_SPEED_PX_S,
                                   CT_STROLL_CRIT_SPEED_PX_S};
// ตำแหน่งหยุดเป็นสัดส่วนของเส้นทาง — วนคนละความยาวกับ ACTS เพื่อไม่ให้จับคู่ซ้ำ
static const float STROLL_PAUSE_AT[] = {0.34f, 0.5f, 0.66f};
#define STROLL_TRAVEL (CT_SCREEN_WIDTH + 2 * CT_STROLL_PAD_PX)

// ระดับที่ใช้อยู่ + เวลาที่เที่ยวปัจจุบันเริ่ม — ระดับสลับได้เฉพาะตอนขึ้นเที่ยวใหม่
// เพราะความเร็วที่เปลี่ยนกลางเที่ยวคือความยาวเที่ยวที่เปลี่ยน ซึ่งทำให้ตัวที่กำลังเดิน
// กระโดดไปอีกตำแหน่งทันที · ฝั่ง preview เรนเดอร์ทีละฉากที่ tier คงที่อยู่แล้ว
// สองฝั่งจึงให้ภาพเดียวกันเป๊ะ (t0 = trip * trip_s พอดีเมื่อ tier ไม่เปลี่ยน)
static int s_stroll_tier = 0;
static int s_stroll_trip = 0;
static float s_stroll_t0 = 0.0f;

static bool usage_shown(void);

// ชุดท่าที่หยุดทำได้ — ต้องตรงกับ stroll_acts ใน tools/gen/screen.py
// ตอนหลุดลิงก์ไม่มีอะไรให้ฉลอง จึงยืมชุดท่าของ crit (รอทั้งคู่) แต่ไม่ยืมความเร็ว:
// การเดินช้าลงเป็นสารของโควตา ไม่ใช่ของลิงก์ที่หลุด
static int stroll_acts_tier(int tier)
{
    return s_connected ? tier : 2;
}

// แถวโควตาที่อยู่บนจอ -> ระดับ — ต้องตรงกับ stroll_tier ใน tools/gen/screen.py
static int stroll_tier(void)
{
    int worst = -1;
    if (!usage_shown()) return 0;
    for (int i = 0; i < CT_USAGE_ROWS; i++) {
        int pct = s_frame->usage[i].percent;
        // ไม่รู้ (<0) ไม่ใช่ศูนย์ และไม่ใช่เหตุให้ทำท่าเหนื่อย
        if (pct > worst) worst = pct;
    }
    if (worst >= CT_USAGE_CRIT_PCT) return 2;
    if (worst >= CT_USAGE_WARN_PCT) return 1;
    return 0;
}

static float stroll_walk_s(int tier)
{
    return (float)STROLL_TRAVEL / (float)STROLL_SPEED[tier];
}

// เดินนาฬิกาของเที่ยวไปให้ทันเวลา แล้วรับระดับใหม่ตอนขึ้นเที่ยวเท่านั้น
static void stroll_advance(float t)
{
    int want = stroll_tier();
    float trip_s = stroll_walk_s(s_stroll_tier) + CT_STROLL_PAUSE_S;
    while (t - s_stroll_t0 >= trip_s) {
        s_stroll_t0 += trip_s;
        s_stroll_trip++;
        s_stroll_tier = want;
        trip_s = stroll_walk_s(s_stroll_tier) + CT_STROLL_PAUSE_S;
    }
}

// เวลาที่ผ่านไปในเที่ยวนี้ -> ท่า + x ของขอบซ้ายกรอบวาด
// เที่ยวหนึ่ง = เดินจากนอกจอซ้ายไปนอกจอขวา โดยหยุดทำท่าหนึ่งครั้งกลางทาง
// ต้องตรงกับ stroll_pose ใน tools/gen/screen.py
static void stroll_pose(float u, int trip, int tier, ct_state_t *state, float *x)
{
    const float walk_s = stroll_walk_s(tier);
    float hold_at = walk_s * STROLL_PAUSE_AT[trip % (int)(sizeof(STROLL_PAUSE_AT) /
                                                         sizeof(STROLL_PAUSE_AT[0]))];
    float walked;

    if (u < hold_at) {
        walked = u;
        *state = CT_STATE_ENTERING;
    } else if (u < hold_at + CT_STROLL_PAUSE_S) {
        int a = stroll_acts_tier(tier);
        walked = hold_at;
        *state = STROLL_ACTS[a][trip % STROLL_ACTS_N[a]];
    } else {
        walked = u - CT_STROLL_PAUSE_S;
        *state = CT_STATE_ENTERING;
    }
    *x = -(float)CT_STROLL_PAD_PX + walked * STROLL_SPEED[tier];
}

static void stroll_draw_cb(lv_event_t *e)
{
    if (s_frame->session_count > 0) return;

    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    ct_state_t state;
    float x;
    float t = (float)s_cycle + s_phase;
    stroll_advance(t);
    stroll_pose(t - s_stroll_t0, s_stroll_trip, s_stroll_tier, &state, &x);

    const float px = CT_SLOTS_UNIT_PX;
    float foot = coords.y1 + CT_SLOTS_HEIGHT - CT_SLOTS_BASELINE_PAD;
    float ox = coords.x1 + x - CT_BOX_X0 * px;

    // ตัวเดินเล่นใช้ build() ตรงๆ ไม่ผ่าน build_centered จึงไม่มี dx มาชดเชย
    draw_shadow(layer, ox + BODY_CX * px);

    ct_rects_t rects;
    ct_mascot_build(&rects, state, s_phase, s_connected, s_cycle);
    draw_mascot_rects(layer, &rects, ox, foot - CT_BOX_Y1 * px);
}

// --- ตัวช่วยสร้าง widget ------------------------------------------------------
static lv_obj_t *plain_obj(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *plain_label(lv_obj_t *parent, const lv_font_t *font, uint16_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, ct_color(color), 0);
    lv_label_set_text(l, "");
    return l;
}

// ฉากอยู่หลังทุกอย่าง — ต้องสร้างก่อน widget อื่นทั้งหมด เพราะ LVGL เรียงชั้นตามลำดับสร้าง
// ผืนเดียวตั้งแต่ใต้แถบบนถึงก้นจอ: card วาดพื้นทึบของตัวเองทับอยู่แล้ว
static void build_sky(lv_obj_t *scr)
{
    s_sky = plain_obj(scr, CT_SCREEN_WIDTH, CT_SCREEN_HEIGHT - CT_TOPBAR_HEIGHT);
    lv_obj_set_pos(s_sky, 0, CT_TOPBAR_HEIGHT);
    lv_obj_add_event_cb(s_sky, sky_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

// วาดฟ้าใหม่เฉพาะส่วนที่ขยับจริง — พื้นดินกับหญ้านิ่งตลอดช่วง ไม่ต้องแตะ
// แถบฟ้า 320x98 = 31360 px ซึ่งอยู่ในระดับเดียวกับที่แถบมาสคอตวาดใหม่ทุกเฟรม
// (28620 px) — วาดใหม่ไม่เกินวินาทีละครั้งหรือตอนเมฆขยับ (4 px/s) ไม่ใช่ทุกเฟรม
static void invalidate_sky_band(void)
{
    lv_area_t a = {.x1 = 0, .y1 = CT_TOPBAR_HEIGHT, .x2 = CT_SCREEN_WIDTH - 1,
                   .y2 = CT_SKY_HORIZON - 1};
    lv_obj_invalidate_area(s_sky, &a);
}

// ช่วงเวลาเปลี่ยนเมื่อ clock เปลี่ยน (นาทีละครั้ง) หรือสถานะลิงก์เปลี่ยน
// ต้องวาดใหม่ทั้งผืนตอนช่วงเปลี่ยน เพราะพื้นดินกับหญ้าเปลี่ยนสีด้วย
static void update_sky(void)
{
    ct_sky_phase_t was = s_sky_phase;
    float hours = s_connected ? ct_clock_hours(s_frame->clock) : -1.0f;
    s_sky_hours = hours;
    s_sky_phase = hours < 0.0f ? CT_SKY_NONE : ct_sky_phase_at(hours);
    if (s_sky_phase != was) {
        lv_obj_invalidate(s_sky);
    } else if (s_sky_phase != CT_SKY_NONE) {
        invalidate_sky_band();  // ดวงเลื่อนไปตามนาทีที่เดิน
    }
}

static void build_slots(lv_obj_t *scr)
{
    for (int i = 0; i < CT_SLOTS_COUNT; i++) {
        s_slots[i].index = i;
        lv_obj_t *o = plain_obj(scr, CT_SLOTS_WIDTH, CT_SLOTS_HEIGHT);
        lv_obj_set_pos(o, slot_x(i, CT_SLOTS_COUNT), CT_SLOTS_TOP);
        lv_obj_set_user_data(o, &s_slots[i]);
        lv_obj_add_event_cb(o, slot_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
        s_slots[i].canvas = o;

        lv_obj_t *l = plain_label(scr, ct_font_text_12(), CT_COL_TEXT);
        lv_obj_set_width(l, CT_SLOTS_WIDTH - CT_SLOTS_LABEL_INSET);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        s_slots[i].label = l;
    }
}

// แถบ slot ที่ว่างเปล่าอ่านได้ว่า "อุปกรณ์ค้าง" — ให้มาสคอตเดินผ่านแทน
// ผืนเดียวเต็มความกว้างจอ ไม่ใช่ slot เพราะตัวนี้ข้ามขอบ slot ตลอดเวลา
static void build_stroll(lv_obj_t *scr)
{
    s_stroll = plain_obj(scr, CT_SCREEN_WIDTH, CT_SLOTS_HEIGHT);
    lv_obj_set_pos(s_stroll, 0, CT_SLOTS_TOP);
    lv_obj_add_event_cb(s_stroll, stroll_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(s_stroll, LV_OBJ_FLAG_HIDDEN);
}

static void build_cards(lv_obj_t *scr)
{
    int w = CT_SCREEN_WIDTH - CT_CARD_PAD * 2;
    for (int i = 0; i < CT_MAX_CARDS; i++) {
        // ขนาดและตำแหน่งขึ้นกับว่าการ์ดใบนั้นมีสองบรรทัดหรือบรรทัดเดียว จึงตั้งใน
        // layout_cards ทุกครั้งที่เฟรมเปลี่ยน ไม่ใช่ตรงนี้
        lv_obj_t *box = plain_obj(scr, w, CT_CARD_H_TWO);
        lv_obj_set_style_bg_color(box, ct_color(CT_COL_BG_SLOT), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

        lv_obj_t *accent = plain_obj(box, CT_CARD_RAIL_W, CT_CARD_H_TWO);
        lv_obj_set_pos(accent, 0, 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);

        lv_obj_t *mark = plain_obj(box, CT_CARD_MARK, CT_CARD_MARK);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        lv_obj_t *mark_hole = plain_obj(mark, CT_CARD_MARK - CT_CARD_MARK_STROKE * 2,
                                        CT_CARD_MARK - CT_CARD_MARK_STROKE * 2);
        lv_obj_set_pos(mark_hole, CT_CARD_MARK_STROKE, CT_CARD_MARK_STROKE);
        lv_obj_set_style_bg_opa(mark_hole, LV_OPA_COVER, 0);

        lv_obj_t *title = plain_label(box, ct_font_text_14(), CT_COL_TEXT);
        lv_obj_set_width(title, w - CT_CARD_TEXT_INSET);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        ct_label_set_pos(title, 9, CT_CARD_TITLE_DY);

        lv_obj_t *body = plain_label(box, ct_font_text_12(), CT_COL_TEXT_DIM);
        lv_obj_set_width(body, w - CT_CARD_TEXT_INSET);
        lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
        ct_label_set_pos(body, 9, CT_CARD_BODY_DY);

        s_cards[i] = (card_t){box, accent, mark, mark_hole, title, body};
        lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    }

    // ตำแหน่งแนวตั้งขึ้นกับจำนวนใบที่แสดงจริง — ตั้งตอน layout_cards ไม่ใช่ตรงนี้
    s_card_more = plain_label(scr, &lv_font_montserrat_12, CT_COL_TEXT_DIM);
    lv_obj_add_flag(s_card_more, LV_OBJ_FLAG_HIDDEN);
}

// ปุ่มหนึ่งใบ — พื้นทึบกับข้อความกลางใบ ไม่มีขอบ ไม่มีมุมโค้ง
//
// สิ่งที่ทำให้มันอ่านออกว่าเป็นปุ่มคือ *สี่เหลี่ยมทึบสีเข้มบนพื้นการ์ด* กับคำที่เป็นคำสั่ง
// ไม่ใช่กรอบ — กรอบ 1px บนจอที่ถูกมองจากมุม 60 องศาหายไปก่อนสีเสมอ
static lv_obj_t *ask_button(lv_obj_t *parent, int x, lv_obj_t **out_text)
{
    lv_obj_t *b = plain_obj(parent, CT_ASK_BUTTON_W, CT_ASK_BUTTON_H);
    lv_obj_set_pos(b, x, CT_ASK_BUTTON_TOP);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_t *t = plain_label(b, ct_font_text_14(), CT_COL_INK);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);
    *out_text = t;
    return b;
}

static void build_ask(lv_obj_t *scr)
{
    int w = CT_SCREEN_WIDTH - CT_CARD_PAD * 2;
    lv_obj_t *box = plain_obj(scr, w, CT_ASK_H);
    lv_obj_set_pos(box, CT_CARD_PAD, CT_CARD_TOP + CT_CARD_PAD);
    lv_obj_set_style_bg_color(box, ct_color(CT_COL_BG_CARD_ALERT), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

    lv_obj_t *title = plain_label(box, ct_font_text_14(), CT_COL_TEXT);
    lv_obj_set_width(title, w - CT_CARD_TEXT_INSET);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    ct_label_set_pos(title, 9, CT_ASK_TITLE_DY);

    lv_obj_t *deny_text = NULL;
    lv_obj_t *allow_text = NULL;
    // ปฏิเสธอยู่ซ้าย อนุญาตอยู่ขวา ตลอดไป — ปุ่มที่สลับที่ได้คือปุ่มที่กดผิดได้
    lv_obj_t *deny = ask_button(box, 0, &deny_text);
    lv_obj_set_style_bg_color(deny, ct_color(CT_COL_ALERT), 0);
    lv_label_set_text(deny_text, "Deny");
    lv_obj_t *allow = ask_button(box, CT_ASK_BUTTON_W + CT_ASK_GAP, &allow_text);

    s_ask = (ask_t){box, title, deny, deny_text, allow, allow_text};
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
}

// ขอบซ้าย/ขวาของเนื้อหาในแถว usage — ตรงกับ tools/gen/screen.py:_usage_row
#define USAGE_X0 (CT_CARD_PAD + 8)
#define USAGE_X1 (CT_SCREEN_WIDTH - CT_CARD_PAD - 8)
#define USAGE_W (USAGE_X1 - USAGE_X0)

static const char *const USAGE_LABELS[CT_USAGE_ROWS] = {"Current", "Weekly"};
static const int USAGE_WINDOWS[CT_USAGE_ROWS] = {CT_USAGE_SESSION_WINDOW,
                                                 CT_USAGE_WEEKLY_WINDOW};

// y ของขอบบนแถว i — แผงเตี้ยกว่าพื้นที่ที่มี จึงจัดกลางแนวตั้ง ไม่ชิดบน
// ต้องตรงกับ _usage ใน tools/gen/screen.py
static int usage_row_y(int i)
{
    int block = 2 * CT_USAGE_ROW_H + CT_USAGE_GAP;
    return CT_CARD_TOP + (CT_CARD_HEIGHT - block) / 2 + i * (CT_USAGE_ROW_H + CT_USAGE_GAP);
}

static void build_usage(lv_obj_t *scr)
{
    for (int i = 0; i < CT_USAGE_ROWS; i++) {
        int y = usage_row_y(i);
        usage_row_t *u = &s_usage[i];

        u->percent = plain_label(scr, &lv_font_montserrat_24, CT_COL_GOOD);
        lv_obj_set_pos(u->percent, USAGE_X0, y);

        // pill วาดด้วย obj โค้งมุม ไม่ใช่ label ที่มีพื้นหลัง เพราะต้องกำหนดความกว้าง
        // จากความยาวข้อความเองตอน build (ข้อความคงที่ ไม่เปลี่ยนตามข้อมูล)
        //
        // ป้ายไม่มีสีของตัวเอง เส้นขอบบางกับตัวอักษรเท่านั้น — ป้ายบอก *ว่านี่คือหน้าต่างไหน*
        // ซึ่งไม่เคยเปลี่ยน ป้ายเขียว "Weekly" เคยนั่งอยู่เหนือแถบแดง 71% ห่างกัน 20px
        // แล้วเขียวที่แปลว่า "ปลอดภัย" ทุกที่บนจอนี้ กลับแปลว่า "รายสัปดาห์" ตรงนี้ที่เดียว
        // สิ่งที่บอกว่ากำลังอ่านแถวไหนคือลำดับ (Current บน Weekly ล่าง) กับตัวอักษร
        u->pill = plain_obj(scr, 62, 18);
        lv_obj_set_style_bg_opa(u->pill, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(u->pill, 1, 0);
        lv_obj_set_style_border_color(u->pill, ct_color(CT_COL_TEXT_DIM), 0);
        lv_obj_set_style_border_opa(u->pill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(u->pill, 9, 0);
        lv_obj_set_pos(u->pill, USAGE_X1 - 62, y + 5);

        u->pill_text = plain_label(u->pill, &lv_font_montserrat_12, CT_COL_TEXT);
        lv_label_set_text(u->pill_text, USAGE_LABELS[i]);
        lv_obj_center(u->pill_text);

        // รางต้องสว่างกว่าพื้นจอพอให้เห็นความยาวเต็มของแถบตอนใช้ไปน้อย
        u->track = plain_obj(scr, USAGE_W, CT_USAGE_BAR_H);
        lv_obj_set_style_bg_color(u->track, ct_color(CT_COL_GRAY_DARK), 0);
        lv_obj_set_style_bg_opa(u->track, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(u->track, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(u->track, USAGE_X0, y + 28);

        u->fill = plain_obj(u->track, USAGE_W, CT_USAGE_BAR_H);
        lv_obj_set_style_bg_opa(u->fill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(u->fill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(u->fill, 0, 0);

        u->pace = plain_obj(scr, 1, CT_USAGE_BAR_H + 4);
        lv_obj_set_style_bg_color(u->pace, ct_color(CT_COL_OUTLINE), 0);
        lv_obj_set_style_bg_opa(u->pace, LV_OPA_COVER, 0);
        lv_obj_set_pos(u->pace, USAGE_X0, y + 26);

        // เวลารีเซ็ตอยู่บรรทัดเดียวกับเลข % ไม่ใช่ชั้นใต้แถบ — ประหยัด 16px ต่อแถว
        // โดยไม่ต้องลดขนาดเลข %
        //
        // เกาะขอบขวาของป้ายเลข % ไม่ใช่พิกัดตายตัวที่กันที่ไว้ให้ "100%" ซึ่งทำให้
        // เลขสองหลักดูห่างจนไม่เป็นก้อนเดียวกัน ตำแหน่งจริงคำนวณใน layout_usage
        // หลังตั้งข้อความ — lv_obj_align_to คิดครั้งเดียวตอนเรียก ไม่ตามความกว้างใหม่เอง
        u->reset = plain_label(scr, &lv_font_montserrat_12, CT_COL_TEXT_DIM);

        lv_obj_add_flag(u->percent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(u->pill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(u->track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(u->pace, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(u->reset, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_idle_clock(lv_obj_t *scr)
{
    // ไม่มีอะไรต้องเตือน = ให้พื้นที่นี้ทำหน้าที่นาฬิกาตั้งโต๊ะแทน
    // นี่คือสภาพที่จอเป็นอยู่เกือบตลอดเวลา ปล่อยว่างแล้วดูเหมือนอุปกรณ์พัง
    int cy = CT_CARD_TOP + CT_CARD_HEIGHT / 2;
    s_clock_big = plain_label(scr, &lv_font_montserrat_48, CT_COL_TEXT);
    lv_obj_align(s_clock_big, LV_ALIGN_TOP_MID, 0, cy - 8 - 24);
    s_date = plain_label(scr, &lv_font_montserrat_12, CT_COL_TEXT_DIM);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, cy + 26 - 8);
}

void ct_ui_init(lv_obj_t *parent, const ct_snapshot_t *frame)
{
    s_frame = frame;

    // พื้นหลังของจอเป็นของตัวโฮสต์ ที่นี่รับ `parent` มาแล้วปูทุกอย่างลงไปตามพิกัดเดิม —
    // ผืนนี้เต็มจอและไม่มี style ใดๆ พิกัดของลูกจึงเท่ากับพิกัดบนจอเป๊ะ
    lv_obj_t *scr = parent;

    ct_fonts_init();  // ต้องมาก่อน build_* ทุกตัว — ป้ายถือ pointer ไปยังฟอนต์พวกนี้
    build_sky(scr);
    build_slots(scr);
    build_stroll(scr);
    build_cards(scr);
    build_ask(scr);
    build_usage(scr);
    build_idle_clock(scr);
    ct_ui_redraw();
}

// --- ปรับหน้าจอตาม snapshot ---------------------------------------------------
static void layout_slots(void)
{
    int n = s_frame->session_count;
    if (n == 0) {
        lv_obj_remove_flag(s_stroll, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_stroll, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < CT_SLOTS_COUNT; i++) {
        slot_t *s = &s_slots[i];
        if (i >= n) {
            lv_obj_add_flag(s->canvas, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s->label, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int x = slot_x(i, n);
        lv_obj_remove_flag(s->canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s->label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s->canvas, x, CT_SLOTS_TOP);

        lv_label_set_text(s->label, s_frame->sessions[i].project);
        lv_obj_set_style_text_color(
            s->label, ct_color(s_connected ? CT_COL_TEXT : CT_COL_TEXT_DIM), 0);
        int foot = CT_SLOTS_TOP + CT_SLOTS_HEIGHT - CT_SLOTS_BASELINE_PAD;
        ct_label_set_pos(s->label, x + 2, foot + CT_SLOTS_LABEL_DY);
    }
}

// ชนิดการ์ด -> สามแกนที่ไม่ใช่สี ทุกแกนชี้ทางเดียวกัน
// alert: แถบยาวสุด พื้นสว่างสุด เครื่องหมายตัน · done: แถบสั้นสุด พื้นจมสุด เป็นขีด
// สีอย่างเดียวไม่พอ — alert (L 0.30) กับ good (L 0.33) เทาเท่ากันเมื่อภาพเป็นขาวดำ
// ต้องตรงกับ _CARD_STYLE ใน tools/gen/screen.py
typedef struct {
    uint16_t accent;
    uint16_t plate;
    int rail_inset;
    enum { MARK_SOLID, MARK_HOLLOW, MARK_DASH } mark;
} card_style_t;

static card_style_t card_style(ct_card_kind_t kind)
{
    switch (kind) {
        case CT_CARD_ALERT:
            return (card_style_t){CT_COL_ALERT, CT_COL_BG_CARD_ALERT,
                                  CT_CARD_RAIL_INSET_ALERT, MARK_SOLID};
        case CT_CARD_DONE:
            return (card_style_t){CT_COL_GOOD, CT_COL_BG_CARD_DONE,
                                  CT_CARD_RAIL_INSET_DONE, MARK_DASH};
        default:
            return (card_style_t){CT_COL_ACCENT, CT_COL_BG_SLOT,
                                  CT_CARD_RAIL_INSET_INFO, MARK_HOLLOW};
    }
}

// ทั้งการ์ดและโควตามาจาก host ทั้งคู่ ลิงก์หลุดแล้วไม่มีใครรับรองว่ายังจริง — พื้นที่ล่าง
// จึงว่างทั้งแถบและตกเป็นของนาฬิกา ตรงกับ Screen.shown_{cards,usage}() ใน gen/screen.py
// คำถามที่มีคนค้างรอคำตอบอยู่จริงชนะทุกอย่างในแถบล่าง (ADR-0014)
//
// การ์ดกับโควตาหลบทั้งคู่ ไม่ใช่เบียดกัน — ปุ่มที่ต้องเล็งให้แม่นบนจอที่ให้พิกัดหยาบ
// ต้องได้ที่เต็มแถบ และของอื่นที่อยู่รอบมันมีแต่จะทำให้นิ้วลงผิดที่
static bool ask_shown(void)
{
    return s_connected && s_frame->ask.present;
}

static int shown_card_count(void)
{
    if (ask_shown()) return 0;
    return s_connected ? s_frame->card_count : 0;
}

static bool usage_shown(void)
{
    return s_frame->has_usage && s_connected && !ask_shown();
}

// สิ่งที่แถบบนถามก่อนวาดของของมัน — แถบไม่พูดซ้ำสิ่งที่หน้านี้แสดงใหญ่กว่าอยู่แล้ว
// ต้องตรงกับ shows_idle_clock/shows_usage_panel ใน tools/gen/screen.py
bool ct_ui_shows_clock(void)
{
    // ต้องถาม ask_shown เองด้วย ไม่ใช่พึ่งสองตัวข้างใน — มันกดทั้งคู่ให้เป็นศูนย์/เท็จ
    // ซึ่งแปลว่า "แถบล่างว่าง" ตามนิยามเดิม แล้วนาฬิกาใหญ่จะไปนั่งทับการ์ดคำถามพอดี
    return !ask_shown() && shown_card_count() == 0 && !usage_shown();
}

bool ct_ui_shows_usage(void)
{
    // การ์ดชนะโควตาเสมอ — การ์ดคือสิ่งที่ต้องการการกระทำจากผู้ใช้
    return usage_shown() && shown_card_count() == 0;
}

ct_ask_hit_t ct_ui_ask_hit(int x, int y)
{
    if (!ask_shown()) return CT_ASK_HIT_NONE;
    int top = CT_CARD_TOP + CT_CARD_PAD + CT_ASK_BUTTON_TOP;
    if (y < top || y >= top + CT_ASK_BUTTON_H) return CT_ASK_HIT_NONE;

    int deny_x = CT_CARD_PAD;
    int allow_x = CT_CARD_PAD + CT_ASK_BUTTON_W + CT_ASK_GAP;
    if (x >= deny_x && x < deny_x + CT_ASK_BUTTON_W) return CT_ASK_HIT_DENY;
    if (x >= allow_x && x < allow_x + CT_ASK_BUTTON_W) return CT_ASK_HIT_ALLOW;
    // ตกในช่องว่างระหว่างปุ่ม — ไม่ใช่ความล้มเหลวที่ต้องเดาให้ว่าเขาเล็งอันไหน
    // แต่เป็นคำตอบของมันเอง: การเล็งไม่แม่นจบลงที่ไม่มีอะไรเกิดขึ้น (ADR-0014)
    return CT_ASK_HIT_NONE;
}

const char *ct_ui_ask_id(void)
{
    return ask_shown() ? s_frame->ask.id : "";
}

// การ์ดสองบรรทัดสูงกว่าเพราะกล่องบรรทัดต้องมีที่ให้วรรณยุกต์ไทยจริงๆ ไม่ใช่แค่ตัวละติน
static int card_h(const ct_card_t *c) { return c->body[0] ? CT_CARD_H_TWO : CT_CARD_H_ONE; }

#define CARD_BUDGET (CT_SCREEN_HEIGHT - (CT_CARD_TOP + CT_CARD_PAD))

static int fit_cards(int count, int budget)
{
    int used = 0;
    int n = 0;
    for (int i = 0; i < count && i < CT_MAX_CARDS; i++) {
        int need = card_h(&s_frame->cards[i]) + (n ? CT_CARD_GAP : 0);
        if (used + need > budget) {
            break;
        }
        used += need;
        n++;
    }
    return n;
}

// แสดงได้กี่ใบ — เติมจากบนลงล่างจนกว่าที่จะไม่พอ ต้องตรงกับ card_fit() ใน gen/screen.py
//
// ลองสองรอบ เพราะที่ของบรรทัด "+N more" ต้องกันไว้ก็ต่อเมื่อมีอะไรให้บอกจริงๆ — กันไว้
// ตลอดแปลว่าการ์ดคู่ที่พอดีเป๊ะ (32 + 4 + 56 = 92) เสียใบที่สองให้บรรทัดที่ไม่มีอะไรจะเขียน
static int card_fit(int count)
{
    int n = fit_cards(count, CARD_BUDGET);
    if (n == count && s_frame->card_overflow == 0) {
        return n;
    }
    return fit_cards(count, CARD_BUDGET - CT_CARD_MORE_H);
}

static void layout_cards(void)
{
    int given = shown_card_count();
    int n = card_fit(given);
    int y = CT_CARD_TOP + CT_CARD_PAD;
    for (int i = 0; i < CT_MAX_CARDS; i++) {
        if (i >= n) {
            lv_obj_add_flag(s_cards[i].box, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const ct_card_t *c = &s_frame->cards[i];
        const card_t *cd = &s_cards[i];
        card_style_t st = card_style(c->kind);
        int h = card_h(c);
        lv_obj_remove_flag(cd->box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(cd->box, h);
        lv_obj_set_pos(cd->box, CT_CARD_PAD, y);
        y += h + CT_CARD_GAP;
        lv_obj_set_style_bg_color(cd->box, ct_color(st.plate), 0);
        lv_obj_set_style_bg_color(cd->accent, ct_color(st.accent), 0);
        lv_obj_set_pos(cd->accent, 0, st.rail_inset);
        lv_obj_set_height(cd->accent, h - st.rail_inset * 2);

        // ขีด (done) คือ mark ตัวเดิมที่ถูกบีบให้เตี้ยลงเหลือความหนาของขอบ
        // ไม่ใช่ obj คนละตัว — ตำแหน่งกลางแนวตั้งจึงคำนวณจากความสูงจริงเสมอ
        int mh = st.mark == MARK_DASH ? CT_CARD_MARK_STROKE : CT_CARD_MARK;
        int mw = CT_SCREEN_WIDTH - CT_CARD_PAD * 2;
        lv_obj_set_height(cd->mark, mh);
        lv_obj_set_pos(cd->mark, mw - CT_CARD_MARK_RIGHT - CT_CARD_MARK / 2, (h - mh) / 2);
        lv_obj_set_style_bg_color(cd->mark, ct_color(st.accent), 0);
        if (st.mark == MARK_HOLLOW) {
            lv_obj_set_style_bg_color(cd->mark_hole, ct_color(st.plate), 0);
            lv_obj_remove_flag(cd->mark_hole, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(cd->mark_hole, LV_OBJ_FLAG_HIDDEN);
        }

        // body ว่าง = การ์ดบรรทัดเดียว (daemon ตัดหัวที่ซ้ำกับป้ายใต้มาสคอตทิ้ง)
        // หัวเรื่องอยู่ที่เดิมทั้งสองแบบ ไม่ต้องคำนวณกึ่งกลาง — CT_CARD_H_ONE ถูกเลือกให้
        // กล่องหัวเรื่องพอดีกับขอบบนล่างข้างละ 2 อยู่แล้ว (ดูหมายเหตุใน layout.toml)
        // ต้องตรงกับ _card() ใน tools/gen/screen.py
        bool one_line = c->body[0] == '\0';
        lv_label_set_text(cd->title, c->title);
        lv_label_set_text(cd->body, c->body);
        if (one_line) {
            lv_obj_add_flag(cd->body, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(cd->body, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // การ์ดที่ไม่ได้วาดต้องเหลือร่องรอย ไม่ใช่หายเงียบ — "ไม่มีอะไรค้างแล้ว" กับ
    // "ยังค้างอีกสองเรื่องแต่จอไม่พอ" คือสองสถานะที่ต้องแยกออกได้ในเหลือบเดียว
    //
    // นับสองชั้น: ที่ daemon ตัดทิ้งก่อนส่ง + ที่ส่งมาแล้วแต่จอไม่พอ · ชั้นหลังเพิ่งเป็นไปได้
    // ตอนความสูงการ์ดไม่เท่ากัน ถ้าลืมบวก ตัวเลขจะโกหกทันทีที่การ์ดสองบรรทัดมาสองใบ
    int hidden = s_frame->card_overflow + (given - n);
    if (n > 0 && hidden > 0) {
        // ตัดที่ 99 — เกินกว่านั้นตัวเลขที่แน่นอนไม่ได้บอกอะไรเพิ่มแล้ว มีแต่จะล้นบรรทัด
        int more = hidden > 99 ? 99 : hidden;
        char buf[16];
        snprintf(buf, sizeof(buf), "+%d more", more);
        lv_label_set_text(s_card_more, buf);
        lv_obj_align(s_card_more, LV_ALIGN_TOP_RIGHT, -(CT_CARD_PAD + 8), y - CT_CARD_GAP + 1);
        lv_obj_remove_flag(s_card_more, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_card_more, LV_OBJ_FLAG_HIDDEN);
    }

    // พื้นที่ล่างมีผู้ยึดสองราย (การ์ด, โควตา) — ถ้ามีรายใดรายหนึ่ง นาฬิกาใหญ่ต้องหลบ
    // ขึ้นไปอยู่บนแถบ ไม่งั้นนาฬิกาหายจากจอทั้งใบ หรือโผล่ซ้ำสองที่
    bool taken = n > 0 || usage_shown() || ask_shown();
    if (taken) {
        lv_obj_add_flag(s_clock_big, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_date, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_clock_big, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_date, LV_OBJ_FLAG_HIDDEN);
    }
}

// เวลาสุดท้ายที่ยังรับรองได้ พูดเป็นอดีตกาล — ต้องตรงกับ last_seen_text ใน gen/screen.py
static void last_seen_text(char *out, size_t cap)
{
    // บอร์ดที่เพิ่งบูตและยังไม่เคยได้ snapshot ไม่มีเวลาให้อ้างถึงเลย ("--:--" คือค่าที่
    // ct_model_init ใส่ไว้) "since --:--" คือการอ้างถึงเวลาที่ไม่มีอยู่ — สภาพนั้นมีชื่อของมันเอง
    if (s_frame->clock[0] == '\0' || strcmp(s_frame->clock, "--:--") == 0) {
        snprintf(out, cap, "no contact yet");
    } else if (s_frame->date[0] == '\0') {
        snprintf(out, cap, "since %s", s_frame->clock);
    } else {
        snprintf(out, cap, "since %s %s", s_frame->clock, s_frame->date);
    }
}

// บล็อกกลางจอตอนไม่มีอะไรต้องเตือน — ต้องตรงกับ _idle_clock ใน tools/gen/screen.py
//
// **ตอนหลุดลิงก์ไม่มีนาฬิกา** เวลาไม่ได้เดินต่อบนบอร์ด เลข 48pt ที่ค้างจึงเป็นคำโกหก
// ที่ตัวใหญ่ที่สุดบนจอ และการหรี่เป็นเทาไม่ช่วย: มันยังอ่านว่า "ตอนนี้" อยู่ดี ที่เดียวกัน
// ตกเป็นของสิ่งเดียวที่บอร์ดยังยืนยันได้เอง คือหลุดมานานเท่าไร ส่วนเวลาเดิมถอยลงไป
// อยู่บรรทัดล่างในรูปอดีตกาล — ด้วยเหตุผลเดียวกับที่ฟ้าหายไปทั้งผืน
//
// สองบรรทัดเปลี่ยนสีพร้อมกันเสมอ: บรรทัดล่างที่ยังเป็น TEXT_DIM เท่าเดิมจะสว่างกว่า
// ตัวใหญ่ที่หรี่แล้ว ซึ่งอ่านเป็น "วันที่คือของสด เวลาคือของค้าง" ที่ผิดทั้งคู่
static void layout_idle_clock(void)
{
    char big[24];
    char caption[CT_CLOCK_LEN + CT_DATE_LEN + 8];
    if (s_connected) {
        snprintf(big, sizeof(big), "%s", s_frame->clock);
        snprintf(caption, sizeof(caption), "%s", s_frame->date);
    } else {
        ct_age_gap_text(big, sizeof(big), s_offline_s);
        last_seen_text(caption, sizeof(caption));
    }
    lv_obj_set_style_text_color(s_clock_big, ct_color(s_connected ? CT_COL_TEXT : CT_COL_TEXT_DIM),
                                0);
    lv_obj_set_style_text_color(s_date, ct_color(s_connected ? CT_COL_TEXT_DIM : CT_COL_GRAY), 0);
    lv_label_set_text(s_clock_big, big);
    lv_label_set_text(s_date, caption);
    lv_obj_align(s_clock_big, LV_ALIGN_TOP_MID, 0, CT_CARD_TOP + CT_CARD_HEIGHT / 2 - 32);
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, CT_CARD_TOP + CT_CARD_HEIGHT / 2 + 18);
}

uint16_t ct_ui_usage_color(int percent)
{
    if (percent < 0) return CT_COL_TEXT_DIM;
    if (percent >= CT_USAGE_CRIT_PCT) return CT_COL_ALERT;
    if (percent >= CT_USAGE_WARN_PCT) return CT_COL_ACCENT;
    return CT_COL_GOOD;
}

// สีของแถบ — แดงทันทีที่ใช้เร็วกว่าเวลาที่ผ่านไปในหน้าต่าง ไม่ต้องรอถึงเกณฑ์ %
// "60% ตอนเหลือเวลาอีกครึ่ง" เป็นปัญหาคนละแบบกับ "60% ตอนหมดเวลาพอดี"
// ต้องตรงกับ usage_bar_color ใน tools/gen/screen.py
// และ MenuBadge.alarming ใน host/Sources/TamaCore/MenuBadge.swift (แถบเมนูใช้สูตร pace เดียวกัน แต่ไม่มีเกณฑ์ %)
uint16_t ct_ui_usage_bar_color(const ct_usage_t *u, int window)
{
    if (u->percent < 0) return CT_COL_TEXT_DIM;
    if (u->remaining > 0 && window > 0) {
        int elapsed = window - u->remaining;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > window) elapsed = window;
        if ((int64_t)u->percent * window > (int64_t)elapsed * 100) return CT_COL_ALERT;
    }
    return ct_ui_usage_color(u->percent);
}

// วินาทีที่เหลือ -> ข้อความสั้นที่สุดที่ยังบอกได้ว่าควรรีบไหม
// ต้องตรงกับ fmt_remaining ใน tools/gen/screen.py
static void usage_reset_text(const ct_usage_t *u, char *out, size_t cap)
{
    if (u->remaining < 0) {
        snprintf(out, cap, "no data");
    } else if (u->remaining == 0) {
        snprintf(out, cap, "resetting");
    } else {
        int d = u->remaining / 86400;
        int h = (u->remaining % 86400) / 3600;
        int m = (u->remaining % 3600) / 60;
        if (d) {
            snprintf(out, cap, "Resets in %dd %dh", d, h);
        } else if (h) {
            snprintf(out, cap, "Resets in %dh %02dm", h, m);
        } else {
            snprintf(out, cap, "Resets in %dm", m);
        }
    }
}

static void layout_ask(void)
{
    if (!ask_shown()) {
        lv_obj_add_flag(s_ask.box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_ask.box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_ask.title, s_frame->ask.title);

    // ปุ่มขวาเป็นสองอย่างในที่เดียวกัน ไม่ใช่ปุ่มที่หายไปเมื่ออนุญาตไม่ได้ — การ์ดที่มี
    // ปุ่มเดียวบอกแค่ว่าทำอะไรไม่ได้ ส่วนปุ่มที่จางอยู่พร้อมคำว่า Keyboard บอกว่า
    // เรื่องนี้ต้องใช้คุณจริงๆ ซึ่งเป็นคนละข้อความกัน
    bool may = s_frame->ask.may_allow;
    lv_obj_set_style_bg_color(s_ask.allow, ct_color(may ? CT_COL_GOOD : CT_COL_GRAY_DARK), 0);
    lv_obj_set_style_text_color(s_ask.allow_text,
                                ct_color(may ? CT_COL_INK : CT_COL_TEXT_DIM), 0);
    lv_label_set_text(s_ask.allow_text, may ? "Allow" : "Keyboard");
    lv_obj_align(s_ask.allow_text, LV_ALIGN_CENTER, 0, 0);
}

static void layout_usage(void)
{
    // การ์ดชนะโควตาเสมอ — การ์ดคือสิ่งที่ต้องการการกระทำจากผู้ใช้
    bool show = usage_shown() && shown_card_count() == 0;
    for (int i = 0; i < CT_USAGE_ROWS; i++) {
        usage_row_t *row = &s_usage[i];
        if (!show) {
            lv_obj_add_flag(row->percent, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(row->pill, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(row->track, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(row->pace, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(row->reset, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const ct_usage_t *u = &s_frame->usage[i];
        uint16_t col = ct_ui_usage_bar_color(u, USAGE_WINDOWS[i]);

        lv_obj_remove_flag(row->percent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(row->pill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(row->track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(row->reset, LV_OBJ_FLAG_HIDDEN);

        if (u->percent < 0) {
            lv_label_set_text(row->percent, "--%");
        } else {
            lv_label_set_text_fmt(row->percent, "%d%%", u->percent);
        }
        lv_obj_set_style_text_color(row->percent, ct_color(col), 0);

        // เปอร์เซ็นต์ที่ไม่รู้ = แถบว่าง ไม่ใช่แถบศูนย์ที่ดูเหมือนข้อมูลจริง
        int pct = u->percent;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        int w = (USAGE_W * pct + 50) / 100;
        if (u->percent < 0 || w <= 0) {
            lv_obj_add_flag(row->fill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(row->fill, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_width(row->fill, w);
            lv_obj_set_style_bg_color(row->fill, ct_color(col), 0);
        }

        // ขีด pace หาได้จากเวลาที่เหลือล้วนๆ — ความยาวหน้าต่างเป็นค่าคงที่
        // ไม่ต้องส่งอะไรเพิ่มบนสาย และเดินต่อได้เองตอน BLE หลุด
        if (u->remaining > 0) {
            int elapsed = USAGE_WINDOWS[i] - u->remaining;
            if (elapsed < 0) elapsed = 0;
            if (elapsed > USAGE_WINDOWS[i]) elapsed = USAGE_WINDOWS[i];
            int y = usage_row_y(i) + 26;
            // ใช้เร็วเกินเวลา = ขีดหนา 3px แทน 1px — สีของแถวบอกไม่ได้เมื่อภาพเป็นขาวดำ
            // (alert L 0.30 กับ good L 0.33 เทาเท่ากัน) และตำแหน่งขีดอย่างเดียวก็อ่านยาก
            // เมื่อเกินไปนิดเดียว ความหนาจึงเป็นแกนที่สองที่ไม่พึ่งสีเลย
            bool over = u->percent != CT_USAGE_UNKNOWN &&
                        (int64_t)u->percent * USAGE_WINDOWS[i] > (int64_t)elapsed * 100;
            int half = over ? 1 : 0;
            lv_obj_set_width(row->pace, half * 2 + 1);
            lv_obj_set_pos(row->pace,
                           USAGE_X0 + (int)((int64_t)USAGE_W * elapsed / USAGE_WINDOWS[i]) - half,
                           y);
            lv_obj_remove_flag(row->pace, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(row->pace, LV_OBJ_FLAG_HIDDEN);
        }

        char text[24];
        usage_reset_text(u, text, sizeof(text));
        lv_label_set_text(row->reset, text);

        // ความกว้างของป้ายเลข % เพิ่งเปลี่ยนตามข้อความ ("100%" กว้างกว่า "35%" ~13px)
        // ต้องบังคับให้ LVGL คิดขนาดใหม่ก่อน ไม่งั้นจัดชิดกับความกว้างของเฟรมก่อนหน้า
        lv_obj_update_layout(row->percent);
        lv_obj_align_to(row->reset, row->percent, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    }
}

void ct_ui_redraw(void)
{
    layout_idle_clock();
    update_sky();
    layout_slots();
    layout_cards();
    layout_ask();
    layout_usage();
}

void ct_ui_set_connected(bool connected)
{
    if (connected == s_connected) return;
    s_connected = connected;
    // บล็อกกลางจอสลับเรื่องที่พูดทั้งก้อน ไม่ใช่แค่เปลี่ยนสี — ดู layout_idle_clock
    layout_idle_clock();
    update_sky();  // หลุดลิงก์ = ฉากหายทั้งผืน clock ที่ค้างอยู่ไม่ใช่เวลาจริงอีกต่อไป
    layout_slots();
    // แผงโควตาเข้า/ออกตามลิงก์ และนาฬิกาใหญ่ต้องกลับลงมายึดพื้นที่ที่มันปล่อยไว้
    layout_cards();
    layout_ask();
    layout_usage();
    for (int i = 0; i < CT_SLOTS_COUNT; i++) lv_obj_invalidate(s_slots[i].canvas);
    lv_obj_invalidate(s_stroll);
}

// สภาพอากาศที่ฟ้าของหน้านี้จะวาด — ct_pages.c เป็นคนตัดสินว่าเฟรมที่แคชไว้ยังใช้ได้ไหม
// (ยังไม่เคยได้ / ผู้ใช้ปิดหน้าอากาศ / เก่าเกิน stale) ที่นี่แค่วาดตามที่บอก
//
// `valid == false` -> CT_WX_CLEAR ซึ่งเป็นค่าเดียวกับฟ้าโล่งจริงๆ โดยตั้งใจ: ทั้งสอง
// ต้องได้ฟ้าตามเวลาแบบก่อนหน้านี้เป๊ะ และหน้านี้ไม่มีที่พิมพ์ว่า "ไม่รู้อากาศ" อยู่แล้ว
void ct_ui_set_weather(int code, bool valid)
{
    ct_wx_kind_t kind = valid ? ct_sky_bucket(code) : CT_WX_CLEAR;
    if (kind == s_wx) return;  // รหัสที่เปลี่ยนแต่ยังกลุ่มเดิมไม่ใช่ภาพที่เปลี่ยน
    s_wx = kind;
    if (s_sky_phase != CT_SKY_NONE) lv_obj_invalidate(s_sky);
}

// วาดใหม่เฉพาะตอนข้อความเปลี่ยนจริง — หลังนาทีแรกตัวเลขนี้เปลี่ยนนาทีละครั้ง แต่ตัวโฮสต์
// บอกมาทุกวินาที การ set_text ด้วยข้อความเดิมคือการ invalidate ป้าย 48pt ทิ้งเปล่าๆ
void ct_ui_set_offline_secs(int secs)
{
    s_offline_s = secs;
    if (s_connected) return;  // ตัวเลขนี้ไม่มีที่บนจอตราบใดที่ยังมีคนป้อน snapshot อยู่
    char big[24];
    ct_age_gap_text(big, sizeof(big), secs);
    if (strcmp(big, lv_label_get_text(s_clock_big)) == 0) return;
    layout_idle_clock();
}

void ct_ui_tick(int elapsed_ms)
{
    s_phase += (float)elapsed_ms / (float)LOOP_MS;
    bool second_passed = false;
    while (s_phase >= 1.0f) {
        s_phase -= 1.0f;
        s_cycle++;
        second_passed = true;
    }
    for (int i = 0; i < s_frame->session_count; i++) {
        lv_obj_invalidate(s_slots[i].canvas);
    }
    if (s_frame->session_count == 0) lv_obj_invalidate(s_stroll);

    // ฟ้าวาดใหม่ตอนเมฆขยับถึงพิกเซลถัดไป (~4 ครั้ง/วิ) หรือตอนวินาทีเดิน (ดาวกะพริบ)
    // ไม่ใช่ทุกเฟรม — ที่ 60ms ต่อเฟรมจะได้ 16 ครั้ง/วิ โดยที่ภาพเปลี่ยนแค่ 4 ครั้ง
    //
    // **ยกเว้นตอนมีของตกลงมา**: ฝนเดิน 4px ต่อเฟรม หิมะ 1px จังหวะ 4 ครั้ง/วิ จะทำให้
    // มันกระโดดทีละ 16px แทนที่จะไหล · ราคานี้จ่ายเฉพาะตอนฝน/หิมะจริง และไม่ใช่เฟรมใหม่
    // เป็นการวาดย่านเดิมถี่ขึ้นในลูปที่หมุนอยู่แล้ว
    if (s_sky_phase != CT_SKY_NONE) {
        bool falling = s_wx == CT_WX_RAIN || s_wx == CT_WX_SNOW;
        int shift = (int)(((float)s_cycle + s_phase) * (float)CT_SKY_CLOUD_SPEED_PX_S);
        if (falling || shift != s_cloud_shift || second_passed) {
            s_cloud_shift = shift;
            invalidate_sky_band();
        }
    }

}

// วาดใหม่เฉพาะตอนวินาทีเดิน และเฉพาะตอนแผงโผล่อยู่ — LVGL วาดเฉพาะสิ่งที่ invalidate
// เท่านั้น การเรียก layout_usage ทุกเฟรมจะกินเวลาไปเปล่าๆ · ตอนหลุดลิงก์ countdown
// ยังเดินในหน่วยความจำของตัวโฮสต์ (ค่าที่ถูกตอนกลับมาต่อ) แต่ไม่มีอะไรให้วาด
void ct_ui_redraw_usage(void)
{
    if (!usage_shown() || shown_card_count() > 0) return;
    layout_usage();
}
