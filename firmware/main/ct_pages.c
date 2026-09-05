#include "ct_pages.h"

#include <stdio.h>

#include "cJSON.h"
#include "ct_calendar.h"
#include "ct_calendar_ui.h"
#include "ct_color.h"
#include "ct_crypto.h"
#include "ct_crypto_ui.h"
#include "ct_fonts.h"
#include "ct_footer.h"
#include "ct_mini.h"
#include "ct_stocks.h"
#include "ct_stocks_ui.h"
#include "ct_topbar.h"
#include "ct_ui.h"
#include "ct_weather.h"
#include "ct_weather_ui.h"
#include "layout.h"
#include "lvgl.h"

// สิ่งที่ทุกหน้ามีเหมือนกัน — ส่วนตัว page frame เองมีรูปร่างต่างกันตามชนิด (ADR-0003)
// จึงเก็บแยกเป็นตัวแปรของแต่ละหน้า ไม่ใช่ union ที่มีสมาชิกเดียว
typedef struct {
    lv_obj_t *root;  // ทุก widget ของหน้าอยู่ใต้ตัวนี้ — ซ่อนทั้งหน้าด้วยการซ่อนตัวเดียว
    bool has_frame;
} ct_page_t;

static ct_page_t s_pages[CT_PAGE_KIND_COUNT];
static ct_page_kind_t s_active = CT_PAGE_MASCOT;

// กติกาที่ใช้อยู่ — ค่าตั้งต้นคือทุกหน้าตามลำดับ enum และรอบจาก layout.toml
// บอร์ดที่ยังไม่เคยคุยกับ Mac (หรือคุยกับแอปรุ่นเก่า) ก็ต้องหมุนหน้าเป็นเหมือนเดิม
static ct_page_plan_t s_plan;

// page frame ของหน้ามาสคอต — `Snapshot` เดิมทั้งดุ้น ไม่ได้ถูกแตะเพราะมีหลายหน้า
static ct_snapshot_t s_mascot;
static ct_weather_t s_weather;
static ct_crypto_t s_crypto;
static ct_calendar_t s_calendar;
static ct_stocks_t s_stocks;

// เศษเวลาที่ยังไม่ครบวินาที — นาฬิกาของหน้าเดินด้วยเวลาจริง ไม่ใช่ด้วยจำนวนเฟรม
static int s_since_second;
// ลิงก์ขึ้นอยู่ไหม + หลุดมากี่วินาทีแล้ว — เริ่มนับตั้งแต่บูต (`ct_pages_set_connected(false)`
// ที่ app_main) บอร์ดที่ยังไม่เคยเจอ Mac จึงบอกได้ว่ารอมานานเท่าไร ไม่ใช่แค่ว่ารออยู่
static bool s_connected;
static int s_offline_s;
// นาฬิกาของ page rotation อยู่บนบอร์ด ไม่ใช่บน Mac — ถ้า Mac เป็นคนสั่งเปลี่ยนหน้า
// Mac ที่หลับก็เท่ากับจอค้างหน้าเดียวทั้งคืน
static int s_since_turn;
// เวลาที่เหลือของการยึดหน้าที่ผู้ใช้เลือกเอง — ระหว่างนี้รอบหมุนหยุดสนิท ไม่ใช่แค่ช้าลง
static int s_hold_left;
// หน้าที่ต้องกลับไปหลังการเด้ง ใช้เฉพาะตอนรอบหมุนถูกปิด — `CT_PAGE_KIND_COUNT` คือ
// "ไม่มี" ไม่ใช่ `CT_PAGE_MASCOT` ซึ่งเป็นหน้าจริงที่ผู้ใช้ปัดมาค้างไว้ได้
//
// ตอนรอบหมุนเปิด มาสคอตได้เวลาเต็มหนึ่งรอบแล้วรอบหมุนก็พาไปต่อเอง ปิดรอบหมุนแล้ว
// "ไปต่อ" ไม่มีความหมาย แต่ "กลับที่เดิม" มี — ไม่งั้นการเตือนวาบเดียวตอนไม่อยู่โต๊ะ
// จะพรากหน้าที่ผู้ใช้ตั้งใจปัดค้างไว้ไปถาวร
static ct_page_kind_t s_return_to = CT_PAGE_KIND_COUNT;

// ประกาศล่วงหน้า — การเปลี่ยนหน้าอยู่ใกล้ตรรกะของรอบหมุนท้ายไฟล์ ส่วนคนเรียกมีตั้งแต่ต้น
static void switch_to(ct_page_kind_t kind);

// ผืนเต็มจอไร้ style: พิกัดของลูกจึงเท่ากับพิกัดบนจอ และการเปลี่ยนหน้าไม่ต้องแตะ widget ใด
static lv_obj_t *make_root(lv_obj_t *scr)
{
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, CT_SCREEN_WIDTH, CT_SCREEN_HEIGHT);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    return root;
}

static void default_plan(ct_page_plan_t *plan)
{
    for (int i = 0; i < CT_PAGE_KIND_COUNT; i++) plan->order[i] = (ct_page_kind_t)i;
    plan->count = CT_PAGE_KIND_COUNT;
    plan->auto_turn = CT_ROTATION_AUTO != 0;
    plan->rotation_ms = CT_ROTATION_SECONDS * 1000;
    plan->hold_ms = CT_ROTATION_HOLD_SECONDS * 1000;
    plan->attention_jump = true;
}

void ct_pages_init(void)
{
    default_plan(&s_plan);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, ct_color(CT_COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < CT_PAGE_KIND_COUNT; i++) {
        s_pages[i].root = make_root(scr);
        s_pages[i].has_frame = false;
    }

    // ฟอนต์ต้องพร้อมก่อนป้ายใบแรกของหน้าไหนก็ตาม — ป้ายถือ pointer ไปยังฟอนต์พวกนั้น
    ct_fonts_init();

    // เฟรมที่ยังไม่เคยได้ข้อมูลคือเฟรมว่าง ไม่ใช่หน่วยความจำที่ไม่ได้ตั้งค่า —
    // หน้ามาสคอตวาดสภาพนี้เป็นนาฬิกาตั้งโต๊ะอยู่แล้ว ส่วนหน้าอื่นมีหน้าตาของตัวเอง
    ct_model_clear(&s_mascot);
    ct_ui_init(s_pages[CT_PAGE_MASCOT].root, &s_mascot);
    // หน้ามาสคอตได้ pip แต่ไม่ได้แถบ — ที่นั่นมาสคอตตัวจริงอยู่กลางจอแล้ว ไม่มีข้อมูล
    // ที่ดึงมาจึงไม่มีอายุให้บอก และพื้นดินกินถึงขอบล่าง แถบทึบจะตัดฉากทิ้ง
    // แต่ pip ต้องมี: หน้านี้คือหน้าที่บอร์ดกระโดดกลับมาบ่อยที่สุด คนที่ยืนอยู่ตรงนี้แล้ว
    // ไม่เห็นอะไรบอกว่าปัดได้ คือคนที่ไม่รู้ว่าอีกสี่หน้ามีอยู่
    ct_footer_attach(s_pages[CT_PAGE_MASCOT].root, false);

    // มาสคอตจิ๋วอ่าน pose จาก snapshot ใบเดียวกับหน้ามาสคอต และต้องผูกก่อนหน้าแรกจะ
    // attach ผืนของมัน
    ct_mini_init(&s_mascot);

    ct_weather_ui_init(s_pages[CT_PAGE_WEATHER].root, &s_weather,
                       &s_pages[CT_PAGE_WEATHER].has_frame, &s_mascot);
    ct_crypto_ui_init(s_pages[CT_PAGE_CRYPTO].root, &s_crypto,
                      &s_pages[CT_PAGE_CRYPTO].has_frame);
    ct_calendar_ui_init(s_pages[CT_PAGE_CALENDAR].root, &s_calendar,
                        &s_pages[CT_PAGE_CALENDAR].has_frame, &s_mascot);
    ct_stocks_ui_init(s_pages[CT_PAGE_STOCKS].root, &s_stocks,
                      &s_pages[CT_PAGE_STOCKS].has_frame);

    // แถบบนอยู่นอกทุกหน้าและสร้างเป็นตัวสุดท้าย — LVGL เรียงชั้นตามลำดับสร้าง
    // แถบจึงอยู่บนสุดเสมอ ไม่ว่าหน้าไหนจะขึ้นมา
    ct_topbar_init(scr, &s_mascot);
    ct_topbar_set_page(s_active);

    // หน้าที่แสดงอยู่คือหน้าเดียวที่ไม่ถูกซ่อน — การเปลี่ยนหน้าคือการย้ายธงใบนี้
    lv_obj_remove_flag(s_pages[s_active].root, LV_OBJ_FLAG_HIDDEN);
}

void ct_pages_set_snapshot(const ct_snapshot_t *snap)
{
    s_mascot = *snap;
    s_pages[CT_PAGE_MASCOT].has_frame = true;
    // เฟรมของหน้าที่ไม่ได้แสดงอยู่ก็เก็บไว้เหมือนกัน แค่ไม่มีอะไรให้วาด (ADR-0002)
    if (s_active == CT_PAGE_MASCOT) ct_ui_redraw();
    // แถบบนอ่าน snapshot ก้อนเดียวกันนี้ทุกหน้า — เวลา โควตา และจำนวน session ที่ล้น
    // เปลี่ยนพร้อมกันเสมอ (ต้องมาหลัง ct_ui_redraw: กติกา "ไม่พูดซ้ำ" ถามหน้ามาสคอตว่า
    // ตอนนี้มันแสดงอะไรอยู่)
    ct_topbar_redraw();
    // หน้าอากาศอ่าน snapshot ใบนี้เอา *นาฬิกา* ไปเลือกฟ้าของฉาก (ADR-0009) — และวาดใหม่
    // เฉพาะตอนข้ามขอบช่วงเวลาจริงๆ ไม่ใช่ทุกวินาที ผืนฉากกินเกือบเต็มจอ การ invalidate
    // มันทุกวินาทีคือการวาดจอใหม่ทั้งใบตลอดเวลาเพื่อภาพที่เหมือนเดิมวันละ 86396 ครั้ง
    // (มาสคอตจิ๋วมีจังหวะของตัวเองที่ `ct_mini_tick` ไม่ได้พึ่งเส้นทางนี้)
    if (s_active == CT_PAGE_WEATHER) ct_weather_ui_redraw_clock();
    // หน้าปฏิทินอ่าน snapshot ใบนี้ด้วย: สัปดาห์ที่ว่างวาดวันที่ตัวใหญ่ ซึ่งต้องข้ามเที่ยงคืน
    // ไปพร้อมกับนาฬิกาบนแถบ ไม่ใช่รอเฟรมปฏิทินใบถัดไปอีกห้านาที
    if (s_active == CT_PAGE_CALENDAR) ct_calendar_ui_redraw();
}

// ป้อนสภาพอากาศให้ฟ้าของหน้ามาสคอต (ADR-0012) — หน้านั้นวาดเฟรมของหน้า *อื่น*
// การตัดสินว่าเฟรมยังใช้ได้ไหมอยู่ที่นี่ ไม่ใช่ที่ ct_ui.c เพราะทะเบียนหน้ากับนาฬิกาอายุ
// อยู่ที่นี่ทั้งคู่ · เก่าเกิน stale = ไม่มีอากาศ ไม่ใช่อากาศเก่า: หน้ามาสคอตไม่มีบรรทัดอายุ
// มาแก้ต่างให้แบบหน้าอากาศ ฝนที่หยุดไปสามชั่วโมงแล้วจึงเป็นคำโกหกที่ไม่มีอะไรถ่วง
static void push_weather_to_sky(void)
{
    bool ok = s_pages[CT_PAGE_WEATHER].has_frame && !ct_weather_is_stale(&s_weather);
    ct_ui_set_weather(s_weather.code, ok);
}

bool ct_pages_set_frame(ct_page_kind_t kind, const char *json, int len)
{
    switch (kind) {
        case CT_PAGE_WEATHER:
            if (!ct_weather_parse(json, len, &s_weather)) return false;
            s_pages[CT_PAGE_WEATHER].has_frame = true;
            push_weather_to_sky();
            if (s_active == CT_PAGE_WEATHER) ct_weather_ui_redraw();
            return true;
        case CT_PAGE_CRYPTO:
            if (!ct_crypto_parse(json, len, &s_crypto)) return false;
            s_pages[CT_PAGE_CRYPTO].has_frame = true;
            if (s_active == CT_PAGE_CRYPTO) ct_crypto_ui_redraw();
            return true;
        case CT_PAGE_CALENDAR:
            if (!ct_calendar_parse(json, len, &s_calendar)) return false;
            s_pages[CT_PAGE_CALENDAR].has_frame = true;
            if (s_active == CT_PAGE_CALENDAR) ct_calendar_ui_redraw();
            return true;
        case CT_PAGE_STOCKS:
            if (!ct_stocks_parse(json, len, &s_stocks)) return false;
            s_pages[CT_PAGE_STOCKS].has_frame = true;
            if (s_active == CT_PAGE_STOCKS) ct_stocks_ui_redraw();
            return true;
        default:
            // หน้ามาสคอตไม่เดินทางมาทางนี้ (เฟรมของมันไม่มีคีย์ `g`) และชนิดที่ firmware
            // ยังไม่รู้จักคือ daemon ที่ใหม่กว่า — ทิ้งเฟรมไป ไม่ใช่วาดมั่ว
            return false;
    }
}

void ct_pages_forget(ct_page_kind_t kind)
{
    if (kind <= CT_PAGE_MASCOT || kind >= CT_PAGE_KIND_COUNT) return;  // มาสคอตปิดไม่ได้
    s_pages[kind].has_frame = false;
    // ด้วยเหตุผลเดียวกับใน `ct_pages_set_plan` — คำสั่งลืมเฟรมมาเป็นคนละเฟรมกับแผน
    // และมาถึงก่อนหรือหลังก็ได้ ทั้งสองทางจึงต้องล้างเจตนาเก่าเอง ไม่ใช่พึ่งอีกฝั่ง
    if (s_return_to == kind) s_return_to = CT_PAGE_KIND_COUNT;
    if (kind == CT_PAGE_WEATHER) {
        ct_weather_t empty = {0};
        empty.unit = 'C';
        s_weather = empty;
        // หน้าที่ถูกปิดต้องหายไปจากฟ้าของหน้ามาสคอตด้วย ไม่ใช่แค่หายจากรอบ rotation
        push_weather_to_sky();
    } else if (kind == CT_PAGE_CRYPTO) {
        ct_crypto_t empty = {0};
        s_crypto = empty;
    } else if (kind == CT_PAGE_CALENDAR) {
        ct_calendar_t empty = {0};
        s_calendar = empty;
    } else if (kind == CT_PAGE_STOCKS) {
        ct_stocks_t empty = {0};
        s_stocks = empty;
    }
    // หน้าที่เพิ่งถูกถอนออกจากรอบอาจเป็นหน้าที่กำลังแสดงอยู่ — กลับไปหน้ามาสคอตทันที
    // ดีกว่าค้างอยู่บนหน้าที่เพิ่งกลายเป็น "ยังไม่เคยได้ข้อมูล"
    if (s_active == kind) {
        s_hold_left = 0;
        switch_to(CT_PAGE_MASCOT);
    }
}

int ct_pages_capability_json(char *out, int size)
{
    // ประกาศเป็นความสามารถ ไม่ใช่เลขเวอร์ชัน (ADR-0006) — แอปใหม่กับ firmware เก่า
    // เป็นสภาพปกติ ไม่ใช่กรณีขอบ
    //
    // นับความยาวจากสิ่งที่ *เขียนลงไปจริง* ไม่ใช่จากค่าที่ snprintf คืน: ค่าที่มันคืนคือ
    // ความยาวที่ข้อความจะมีถ้าที่พอ ตอนถูกตัดมันจึงโตกว่าที่เขียนจริง แล้วผู้เรียกจะส่ง
    // ไบต์ที่ไม่เคยถูกเขียนออกไปบนสาย
    if (size <= 0) return 0;
    int n = snprintf(out, size, "{\"t\":\"cap\",\"p\":[");
    if (n < 0 || n >= size) return 0;
    for (int i = 0; i < CT_PAGE_KIND_COUNT; i++) {
        int wrote = snprintf(out + n, size - n, i ? ",%d" : "%d", i);
        if (wrote < 0 || wrote >= size - n) return n;  // ที่ไม่พอ = ตัดตรงที่ยังถูกต้อง
        n += wrote;
    }
    int wrote = snprintf(out + n, size - n, "]}");
    if (wrote < 0 || wrote >= size - n) return n;
    return n + wrote;
}

void ct_pages_set_connected(bool connected)
{
    // ตัวนับเริ่มใหม่ที่ *ขอบ* ของการเปลี่ยนสถานะเท่านั้น — ผู้เรียกบอกมาเมื่อลิงก์เปลี่ยน
    // แต่ค่าที่ซ้ำเดิมต้องไม่ล้างเวลาที่หลุดสะสมไว้ให้กลายเป็น "เพิ่งหลุด"
    if (connected != s_connected) {
        s_connected = connected;
        s_offline_s = 0;
    }
    ct_ui_set_connected(connected);
    ct_ui_set_offline_secs(s_offline_s);  // หลังหน้ามาสคอตเสมอ — มันเพิ่งเขียนบล็อกนั้นใหม่
    // แถบบนเป็นของทุกหน้า จึงถูกบอกครั้งเดียวเหมือนมาสคอตจิ๋ว — และต้องมาหลังหน้ามาสคอต
    // เพราะแผงโควตาของมันเพิ่งเข้า/ออกตามลิงก์ ซึ่งเป็นสิ่งที่แถบถามก่อนวาด
    ct_topbar_set_connected(connected);
    // มาสคอตจิ๋วเป็นของทุกหน้า จึงถูกบอกครั้งเดียว ไม่ใช่หน้าละครั้ง
    ct_mini_set_connected(connected);
    // แท่นต้องหายไปพร้อมตัวที่ยืนอยู่บนมัน — แท่นเปล่าอ่านว่า "ตัวหายไปไหน"
    ct_footer_set_connected(connected);
    ct_weather_ui_set_connected(connected);
    ct_crypto_ui_set_connected(connected);
    ct_calendar_ui_set_connected(connected);
    ct_stocks_ui_set_connected(connected);
}

void ct_pages_set_link(bool ble, bool wifi)
{
    ct_topbar_set_link(ble, wifi);
}

// ตำแหน่งของหน้าในแผน หรือ -1 ถ้าไม่อยู่ในนั้น
//
// รับแผนมาเป็นพารามิเตอร์ ไม่ใช่อ่าน `s_plan` เอง — ตอนแปลงเฟรม แผนที่กำลังประกอบอยู่
// ยังไม่ใช่แผนที่ใช้งานอยู่ ตัวที่อ่านผิดก้อนจะกันหน้าซ้ำโดยเทียบกับแผนของเมื่อครู่
static int index_in(const ct_page_plan_t *plan, ct_page_kind_t kind)
{
    for (int i = 0; i < plan->count; i++) {
        if (plan->order[i] == kind) return i;
    }
    return -1;
}

static bool in_plan(ct_page_kind_t kind) { return index_in(&s_plan, kind) >= 0; }

// หน้าที่มีสิทธิ์อยู่ในรอบ — ต้องทั้งอยู่ในแผนของผู้ใช้และมีอะไรให้ดู
//
// หน้าที่ยังไม่เคยได้เฟรมไม่ถูกหมุนไปหา ยกเว้นหน้ามาสคอตซึ่งมีสภาพ "ยังไม่ได้คุยกับ Mac"
// เป็นของตัวเอง (นาฬิกาตั้งโต๊ะสีเทา) ซึ่งเป็นสิ่งที่ผู้ใช้ต้องเห็น ไม่ใช่สิ่งที่ต้องซ่อน
static bool in_rotation(ct_page_kind_t kind)
{
    if (!in_plan(kind)) return false;
    return kind == CT_PAGE_MASCOT || s_pages[kind].has_frame;
}

static void switch_to(ct_page_kind_t kind)
{
    if (kind == s_active) return;
    lv_obj_add_flag(s_pages[s_active].root, LV_OBJ_FLAG_HIDDEN);
    s_active = kind;
    lv_obj_remove_flag(s_pages[s_active].root, LV_OBJ_FLAG_HIDDEN);
    s_since_turn = 0;

    // หน้าที่เพิ่งขึ้นมาถือของที่อาจเก่ากว่าตอนที่มันถูกซ่อนไป — วาดใหม่ทั้งใบก่อนเสมอ
    ct_topbar_set_page(s_active);
    if (s_active == CT_PAGE_MASCOT) {
        ct_ui_redraw();
    } else if (s_active == CT_PAGE_WEATHER) {
        ct_weather_ui_redraw();
    } else if (s_active == CT_PAGE_CRYPTO) {
        ct_crypto_ui_redraw();
    } else if (s_active == CT_PAGE_CALENDAR) {
        ct_calendar_ui_redraw();
    } else if (s_active == CT_PAGE_STOCKS) {
        ct_stocks_ui_redraw();
    }
}

// เดินไปข้างหน้าหรือถอยหลังตาม *ลำดับที่ผู้ใช้จัด* ไม่ใช่ลำดับของ enum
// คืน false เมื่อไม่มีหน้าอื่นให้ไป — มีหน้าเดียวที่พร้อมแสดงคือไม่มีอะไรให้หมุน
static bool advance(bool forward)
{
    int n = s_plan.count;
    if (n <= 0) return false;
    // หน้าที่แสดงอยู่อาจไม่อยู่ในแผนแล้ว — หน้าแรกของแผนคือตัวเลือกแรก ไม่ใช่ตัวสุดท้าย
    // จึงเริ่มนับที่ระยะ 0 ไม่ใช่ 1 (ระยะ 0 จากหน้าที่ยังอยู่ในแผนคือตัวมันเอง ซึ่งถูกข้าม)
    int at = index_in(&s_plan, s_active);
    int first = at < 0 ? 0 : 1;
    if (at < 0) at = 0;
    for (int i = first; i <= n; i++) {
        // % ของ C คืนค่าลบเมื่อตัวตั้งเป็นลบ — บวก n ก่อนเสมอ ไม่งั้นการปัดกลับหลัง
        // จากหน้าแรกจะอ่านนอกอาร์เรย์
        int at_i = ((at + (forward ? i : -i)) % n + n) % n;
        ct_page_kind_t candidate = s_plan.order[at_i];
        if (candidate != s_active && in_rotation(candidate)) {
            switch_to(candidate);
            return true;
        }
    }
    return false;
}

void ct_pages_set_plan(const ct_page_plan_t *plan)
{
    s_plan = *plan;
    s_since_turn = 0;
    // แผนใหม่คือผู้ใช้เพิ่งเปลี่ยนใจที่ Mac — หน้าที่จำไว้ก่อนการเด้งอาจเป็นหน้าที่เขาเพิ่งปิด
    // การพากลับไปหามันคือการย้อนคำสั่งที่เขาเพิ่งให้ ทิ้งเจตนาเก่าตรงนี้ ไม่ใช่ปล่อยให้ตัวชี้
    // ค้างไว้แล้วไปพบว่าใช้ไม่ได้ทีหลัง
    s_return_to = CT_PAGE_KIND_COUNT;

    // หน้าที่แสดงอยู่อาจเพิ่งถูกผู้ใช้ปิด — ผลของสวิตช์ต้องเห็นเดี๋ยวนี้ ไม่ใช่เมื่อครบรอบ
    // (คำสั่งลืมเฟรมมาเป็นอีกเฟรมหนึ่ง ซึ่งอาจมาถึงก่อนหรือหลังก็ได้)
    if (!in_plan(s_active)) {
        s_hold_left = 0;
        switch_to(s_plan.count > 0 ? s_plan.order[0] : CT_PAGE_MASCOT);
    }
}

bool ct_pages_parse_plan(const char *json, int len, ct_page_plan_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;
    const cJSON *order = cJSON_GetObjectItem(root, "pl");
    bool ok = cJSON_IsArray(order);
    if (ok) {
        default_plan(out);
        out->count = 0;
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, order) {
            if (!cJSON_IsNumber(item)) continue;
            int kind = item->valueint;
            // หน้าที่ firmware นี้ไม่รู้จักคือ Mac ที่ใหม่กว่า — ข้ามไป ไม่ใช่ทิ้งทั้งแผน
            // ส่วนหน้าซ้ำจะทำให้รอบหมุนวนมาหามันสองครั้ง ซึ่งไม่ใช่สิ่งที่ผู้ใช้จัดไว้
            if (kind < 0 || kind >= CT_PAGE_KIND_COUNT) continue;
            if (index_in(out, (ct_page_kind_t)kind) >= 0) continue;
            out->order[out->count++] = (ct_page_kind_t)kind;
        }
        // แผนว่างเปล่าไม่มีความหมาย และเป็นตัวหารของรอบหมุน — หน้ามาสคอตปิดไม่ได้อยู่แล้ว
        // ที่ฝั่ง Mac ตรงนี้จึงเป็นการกันตัวหารเป็นศูนย์ ไม่ใช่การเดาแทนผู้ใช้
        if (out->count == 0) {
            out->order[0] = CT_PAGE_MASCOT;
            out->count = 1;
        }
        const cJSON *r = cJSON_GetObjectItem(root, "r");
        const cJSON *h = cJSON_GetObjectItem(root, "h");
        const cJSON *j = cJSON_GetObjectItem(root, "j");
        const cJSON *t = cJSON_GetObjectItem(root, "t");
        // ค่าที่ผิดรูปคือค่าตั้งต้น ไม่ใช่ 0 — รอบหมุน 0 วินาทีคือจอที่กระพริบทั้งวัน
        if (cJSON_IsNumber(r) && r->valueint > 0) out->rotation_ms = r->valueint * 1000;
        if (cJSON_IsNumber(h) && h->valueint >= 0) out->hold_ms = h->valueint * 1000;
        if (cJSON_IsNumber(j)) out->attention_jump = j->valueint != 0;
        // การปิดรอบหมุนเป็นคีย์ของตัวเอง ไม่ใช่ `r` ที่เป็นศูนย์ — `r` ยังต้องมีค่าที่ใช้ได้
        // ขณะปิดอยู่ (ระยะที่มาสคอตอยู่บนจอหลังเด้ง) และค่าที่ผู้ใช้ตั้งไว้ต้องรอดข้ามการปิด
        if (cJSON_IsNumber(t)) out->auto_turn = t->valueint != 0;
    }
    cJSON_Delete(root);
    return ok;
}

void ct_pages_step(bool forward)
{
    // ปัดแล้วไม่มีหน้าอื่นให้ไป = ไม่มีอะไรเกิดขึ้นเลย ไม่ใช่การยึดหน้าที่อยู่แล้วไว้ห้านาที
    if (!advance(forward)) return;
    s_hold_left = s_plan.hold_ms;
    // ปัดระหว่างที่มาสคอตขึ้นมาเพราะการเด้ง = ผู้ใช้มาถึงโต๊ะแล้วและเลือกเองว่าจะดูอะไร
    // การพากลับไปหาหน้าที่เขาทิ้งไว้เมื่อครู่จึงเป็นการแย่งจอคืนจากคนที่กำลังใช้มันอยู่
    s_return_to = CT_PAGE_KIND_COUNT;
}

ct_ask_hit_t ct_pages_ask_hit(int x, int y)
{
    if (s_active != CT_PAGE_MASCOT) return CT_ASK_HIT_NONE;
    return ct_ui_ask_hit(x, y);
}

const char *ct_pages_ask_id(void)
{
    return s_active == CT_PAGE_MASCOT ? ct_ui_ask_id() : "";
}

void ct_pages_attention(void)
{
    if (!s_plan.attention_jump) return;
    if (!in_plan(CT_PAGE_MASCOT)) return;  // ปิดหน้ามาสคอตไม่ได้ แต่ไม่เดาแทนแผนที่ได้มา
    bool moved = s_active != CT_PAGE_MASCOT;
    // จำหน้าที่กำลังถูกพรากไปเฉพาะตอนรอบหมุนปิด — ตอนเปิด รอบหมุนพาจอเดินต่อเองอยู่แล้ว
    // ตัวชี้ที่ค้างไว้จะกลายเป็นการกระตุกกลับที่ไม่มีใครสั่ง
    // (ถูกล้างทิ้งเมื่อแผนใหม่มาถึง เมื่อหน้านั้นถูกลืม และเมื่อผู้ใช้ปัดเอง)
    if (moved && !s_plan.auto_turn) s_return_to = s_active;
    switch_to(CT_PAGE_MASCOT);
    // การยึดที่เดินอยู่เป็นของหน้าที่ผู้ใช้ปัดมา ซึ่งการเด้งเพิ่งพรากไป — ถ้าปล่อยให้นับต่อ
    // จอจะค้างอยู่ที่มาสคอตจนครบระยะยึดของหน้า *อื่น* แทนที่จะกลับเข้ารอบตามปกติ
    // แต่ถ้ามาสคอตคือหน้าที่ผู้ใช้ปัดมาเอง การยึดนั้นยังเป็นของเขา เรื่องใหม่ไม่ใช่เหตุให้ริบ
    if (moved) s_hold_left = 0;
    // เรื่องที่เพิ่งเกิดต้องได้เวลาเต็มรอบให้อ่าน ไม่ใช่เศษวินาทีที่เหลือของรอบเดิม
    // (`switch_to` ตั้งให้แล้วตอนเปลี่ยนหน้าจริง ตรงนี้คือกรณีที่มาสคอตอยู่บนจออยู่แล้ว)
    s_since_turn = 0;
}

// บอกฐานว่าอยู่หน้าที่เท่าไรและเหลือเวลาอีกเท่าไร — บอร์ดรู้ทั้งสองอย่างอยู่แล้ว
// ไม่มีไบต์ไหนถูกจ่ายบนสายให้เรื่องนี้
//
// นับเฉพาะหน้าที่ *ปัดถึงได้จริง* (`in_rotation`) ไม่ใช่ทุกหน้าในแผน: หน้าที่ยังไม่เคยได้
// เฟรมถูกข้ามตอนปัด pip ที่นับมันด้วยคือคำสัญญาว่าปัดสองทีจะถึง ซึ่งไม่จริง
static void push_position(void)
{
    ct_footer_pos_t p = {.index = -1, .count = 0, .left_ms = -1, .total_ms = 0};
    for (int i = 0; i < s_plan.count; i++) {
        ct_page_kind_t kind = s_plan.order[i];
        if (!in_rotation(kind)) continue;
        if (kind == s_active) p.index = p.count;
        p.count++;
    }
    // การยึดหลังปัดกับรอบหมุนตอบคำถามเดียวกัน ("อีกนานเท่าไรจอจะเปลี่ยนเอง") จึงใช้ราง
    // เดียวกัน ไม่ใช่สองสัญลักษณ์ที่ผู้ใช้ต้องเรียนรู้ว่าอันไหนกำลังเดินอยู่
    if (s_hold_left > 0) {
        p.left_ms = s_hold_left;
        p.total_ms = s_plan.hold_ms;
    } else if (s_plan.auto_turn || s_return_to < CT_PAGE_KIND_COUNT) {
        // ปิดรอบหมุนแล้วยังมีนาฬิกาเดินได้กรณีเดียว: การเด้งเพิ่งพรากหน้าของผู้ใช้ไป
        // แล้วรอบเดียวกันนี้คือระยะที่มาสคอตได้อยู่บนจอก่อนคืนที่ให้ (ดู `ct_pages_tick`)
        p.left_ms = s_plan.rotation_ms - s_since_turn;
        if (p.left_ms < 0) p.left_ms = 0;
        p.total_ms = s_plan.rotation_ms;
    }
    ct_footer_set_pos(&p);
}

// นาฬิกาทุกเรือนเดินที่นี่ ส่วนฐานถูกบอกทีเดียวหลังจบ — ตัวที่ return กลางทางมีสามที่
// (ยึดหน้าอยู่ · ปิดรอบหมุนแล้วไม่มีอะไรให้นับ · ปิดรอบหมุนแล้วยังไม่ครบ) ทั้งสามยัง
// ต้องอัปเดตราง จึงห่อไว้แทนที่จะไล่แปะก่อน return ทีละจุดแล้วลืมจุดที่สี่ในวันหลัง
static void tick_clocks(int elapsed_ms)
{
    bool second_passed = false;
    s_since_second += elapsed_ms;
    while (s_since_second >= 1000) {
        s_since_second -= 1000;
        second_passed = true;
    }

    // countdown เดินด้วยนาฬิกาของบอร์ดเอง ไม่ใช่ตามจังหวะที่ snapshot มาถึง — เวลารีเซ็ต
    // เป็นค่าสัมบูรณ์ ลิงก์หลุดแล้วตัวเลขนี้ยังจริง ส่วนเปอร์เซ็นต์หยุดนิ่ง (ซึ่งถูก มันหยุดจริง)
    // เดินที่โฮสต์เพราะเวลาของหน้าที่ไม่ได้แสดงอยู่ก็ต้องเดินเหมือนกัน
    if (second_passed && s_pages[CT_PAGE_MASCOT].has_frame) {
        ct_model_tick_usage(&s_mascot, 1);
        if (s_active == CT_PAGE_MASCOT) ct_ui_redraw_usage();
        // แถบบนแสดงเปอร์เซ็นต์ล้วน ไม่มี countdown — วินาทีที่เดินไม่เปลี่ยนอะไรบนแถบ
    }
    // เวลาที่หลุดลิงก์เดินด้วยเหตุผลเดียวกันอีกข้อ: มันคือของที่บอร์ดยืนยันได้เองล้วนๆ
    // และมันต้องเดินขณะที่หน้าอื่นอยู่บนจอด้วย ไม่งั้นคนที่ปัดกลับมาหน้ามาสคอตจะเห็นเลข
    // ที่หยุดไว้ตอนปัดออกไป — ป้ายวาดใหม่เฉพาะตอนข้อความเปลี่ยนจริง (ct_ui_set_offline_secs)
    // บอกทุกวินาทีไม่ว่าหน้าไหนอยู่บนจอ ไม่ใช่เฉพาะตอนมาสคอตแสดงอยู่ — คนที่ปัดกลับมา
    // ต้องไม่เจอเลขที่หยุดไว้ตอนปัดออกไป และป้ายที่ถูกซ่อนอยู่ไม่ทำให้เกิดการวาดใดๆ
    if (second_passed && !s_connected) {
        s_offline_s++;
        ct_ui_set_offline_secs(s_offline_s);
    }
    // อายุข้อมูลเดินด้วยเหตุผลเดียวกัน และเป็นเหตุผลที่เฟรมพก *อายุ* มา ไม่ใช่เวลาสัมบูรณ์:
    // บอร์ดไม่มีนาฬิกาที่ตั้งเวลาไว้ แต่นับต่อจากเลขที่ได้มาได้เสมอ
    if (second_passed && s_pages[CT_PAGE_WEATHER].has_frame) {
        ct_weather_tick(&s_weather, 1);
        // เส้น stale ถูกข้ามระหว่างสองวินาทีนี้ได้ ฟ้าของหน้ามาสคอตจึงต้องถูกถามซ้ำ
        // ทุกวินาทีด้วย — `ct_ui_set_weather` คืนทันทีเมื่อกลุ่มไม่เปลี่ยน ไม่มีการวาด
        push_weather_to_sky();
        if (s_active == CT_PAGE_WEATHER) ct_weather_ui_redraw_age();
    }
    if (second_passed && s_pages[CT_PAGE_CRYPTO].has_frame) {
        ct_crypto_tick(&s_crypto, 1);
        if (s_active == CT_PAGE_CRYPTO) ct_crypto_ui_redraw_age();
    }
    if (second_passed && s_pages[CT_PAGE_CALENDAR].has_frame) {
        ct_calendar_tick(&s_calendar, 1);
        if (s_active == CT_PAGE_CALENDAR) ct_calendar_ui_redraw_age();
    }
    if (second_passed && s_pages[CT_PAGE_STOCKS].has_frame) {
        ct_stocks_tick(&s_stocks, 1);
        if (s_active == CT_PAGE_STOCKS) ct_stocks_ui_redraw_age();
    }

    // อนิเมชันเดินเฉพาะหน้าที่แสดงอยู่ และเดินด้วยเวลาก้อนเดียวกับนาฬิกาข้างบน
    // ทุกหน้าที่ไม่ใช่มาสคอตมีมาสคอตจิ๋วเหมือนกันหมด จึงเดินด้วยนาฬิกาเรือนเดียวที่ `ct_mini`
    if (s_active == CT_PAGE_MASCOT) {
        ct_ui_tick(elapsed_ms);
    } else {
        ct_mini_tick(elapsed_ms);
        // หน้าอากาศมีฉากฟ้าที่ขยับด้วย — *เพิ่ม* จากมาสคอตจิ๋ว ไม่ใช่แทนมัน ฉากอยู่หลังจอ
        // ส่วนมาสคอตจิ๋วอยู่บนแถบ footer ซึ่งทุกหน้ามีเหมือนกัน · ต่างจาก `ct_mini` ตรงที่
        // เรือนนี้เป็นของหน้าเดียวและหยุดเมื่อถูกปัดออกไป: ฟ้าที่ไม่มีใครดูไม่ต้องเดินต่อ
        if (s_active == CT_PAGE_WEATHER) ct_weather_ui_tick(elapsed_ms);
    }

    // หน้าที่ผู้ใช้เลือกเองชนะรอบหมุนจนกว่าจะครบระยะยึด — คนที่ปัดมาดูหน้าหนึ่งกำลังอ่านมัน
    // อยู่ ตัวจับเวลาที่เดินต่อระหว่างนั้นแปลว่าหน้าถูกกระชากไปทันทีที่ครบรอบพอดี
    if (s_hold_left > 0) {
        s_hold_left -= elapsed_ms;
        s_since_turn = 0;
        return;
    }

    s_since_turn += elapsed_ms;

    // ผู้ใช้ปิดรอบหมุน — จอค้างที่หน้าที่แสดงอยู่ ยกเว้นตอนที่การเด้งเพิ่งพรากหน้าของเขาไป
    // ซึ่งใช้เวลารอบเดียวกันนี้เป็นระยะที่มาสคอตได้อยู่บนจอ เท่ากับตอนรอบหมุนเปิด ต่างกัน
    // แค่ปลายทาง: ที่นี่คือกลับที่เดิม ไม่ใช่หน้าถัดไป
    if (!s_plan.auto_turn) {
        if (s_return_to >= CT_PAGE_KIND_COUNT) {
            s_since_turn = 0;  // ไม่มีอะไรให้นับถึง — ตัวนับที่ปล่อยให้ล้นคือ UB เปล่าๆ
            return;
        }
        if (s_since_turn >= s_plan.rotation_ms) {
            s_since_turn = 0;
            ct_page_kind_t back = s_return_to;
            s_return_to = CT_PAGE_KIND_COUNT;
            if (in_rotation(back)) switch_to(back);
        }
        return;
    }

    if (s_since_turn >= s_plan.rotation_ms) {
        s_since_turn = 0;
        // รอบหมุนกลับเข้าที่ *หน้าถัดจากหน้าที่ผู้ใช้ปัดมา* ไม่ใช่ที่หน้ามาสคอต —
        // การยึดที่ครบระยะคือรอบเดิมที่เดินต่อ ไม่ใช่การเริ่มรอบใหม่
        advance(true);
    }
}

void ct_pages_tick(int elapsed_ms)
{
    tick_clocks(elapsed_ms);
    push_position();
}
