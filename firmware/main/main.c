// tamaclaude — จอแสดงสถานะ Claude Code บนบอร์ด CYD
//
// เส้นทางข้อมูล: BLE write -> staging (mutex) -> ลูปหลัก -> LVGL -> SPI -> จอ
// LVGL ไม่ปลอดภัยกับหลายเธรด ทุกการแตะ UI จึงเกิดในลูปหลักที่เดียว
#include <string.h>

#include "cJSON.h"
#include "ct_ble.h"
#include "ct_lan.h"
#include "ct_lcd.h"
#include "ct_led.h"
#include "ct_mascot.h"
#include "ct_model.h"
#include "ct_pages.h"
#include "ct_touch.h"
#include "ct_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "layout.h"
#include "lvgl.h"
#include "nvs_flash.h"

static const char *TAG = "main";

// ใบล่าสุดที่เราตอบไปแล้ว — กันการแตะซ้ำบนการ์ดใบเดิม
//
// การ์ดยังอยู่บนจออีกราวหนึ่งวินาทีหลังตอบ เพราะมันจะหายก็ต่อเมื่อ Mac ส่ง snapshot
// ใบใหม่ที่ไม่มีคำถามมาแล้ว (ADR-0001 — บอร์ดไม่ลบของที่ยังไม่มีใครบอกให้ลบ)
// นิ้วที่แตะซ้ำในช่วงนั้นต้องไม่ส่งคำตอบใบที่สอง
static char s_answered[CT_ASK_ID_LEN];

// นิ้วลงบนจอตอนมีคำขออนุญาตค้างอยู่
//
// บอร์ดไม่ได้ตัดสินอะไร มันส่ง "คนแตะปุ่มนี้ บนใบนี้" กลับไป แล้ว Mac เป็นคนตัดสิน
// ว่าคำตอบนั้นใช้ได้ไหม (`Risk`) — คำว่า allow จากที่นี่ไม่ใช่คำสั่ง มันคือรายงาน
static void answer_ask(int x, int y)
{
    ct_ask_hit_t hit = ct_pages_ask_hit(x, y);
    if (hit == CT_ASK_HIT_NONE) return;

    const char *id = ct_pages_ask_id();
    if (id[0] == '\0') return;
    if (strcmp(id, s_answered) == 0) return;
    snprintf(s_answered, sizeof(s_answered), "%s", id);

    // ปุ่มขวาที่เขียนว่า "Keyboard" ก็ส่ง allow เหมือนกัน ไม่ใช่ไม่ส่งอะไรเลย —
    // Mac จะปฏิเสธมันด้วย `Risk` แล้วตอบ `ask` กลับไป ซึ่งทำให้ terminal ขึ้นคำถาม
    // *เดี๋ยวนี้* แทนที่จะรอหมดเวลายี่สิบห้าวินาที · ปุ่มนั้นจึงพาไปที่คีย์บอร์ดจริงๆ
    // ตามที่มันเขียนไว้ และเส้นทางที่เดินคือเส้นเดียวกับที่ด่านความปลอดภัยเฝ้าอยู่
    char json[64];
    int n = snprintf(json, sizeof(json), "{\"t\":\"ok\",\"i\":\"%s\",\"a\":%d}", id,
                     hit == CT_ASK_HIT_ALLOW ? 1 : 0);
    ct_ble_notify(json, n);
    // ไฟกะพริบคือการบอกว่า "ได้ยินแล้ว" ไม่ใช่การบอกว่าผลลัพธ์คืออะไร — การ์ดจะหายไป
    // ก็ต่อเมื่อ Mac ตอบกลับมาว่ามันหายแล้วจริงๆ ซึ่งกินเวลาราวหนึ่งวินาที · จอที่เงียบ
    // สนิทตลอดวินาทีนั้นอ่านได้ว่าการแตะไม่ติด แล้วคนจะแตะซ้ำ
    ct_led_flash();
    ESP_LOGI(TAG, "answered %s with %s", id, hit == CT_ASK_HIT_ALLOW ? "allow" : "deny");
}

// บัฟเฟอร์วาดของ LVGL: 1/10 ของจอสองก้อน (~15KB) ไม่ใช่ framebuffer เต็ม 150KB
// บอร์ดนี้ไม่มี PSRAM จึงไม่มีทางเลือกอื่นอยู่แล้ว
#define DRAW_LINES 24
#define DRAW_BUF_PX (CT_SCREEN_WIDTH * DRAW_LINES)

static lv_color_t *s_buf1, *s_buf2;
static SemaphoreHandle_t s_lock;

// ของที่ BLE ฝากไว้ให้ลูปหลักหยิบไปใช้
static ct_snapshot_t s_pending;
static bool s_has_pending;
// เฟรมของหน้าอื่นเดินทางมาดิบๆ แล้วให้หน้านั้นแปลเอง — ที่นี่รู้แค่ว่ามันเป็นของหน้าไหน
// (ADR-0004: การตีความเนื้อในเป็นของหน้าที่วาดมัน) · หนึ่งใบต่อหนึ่งรอบลูปพอ เพราะลูป
// เดินทุก 10 ms ส่วนเฟรมของหน้าอากาศมาทุก 15 นาที
static char s_pending_page[600];
static int s_pending_page_len;
static int s_pending_page_kind = -1;
// หน้าที่ Mac สั่งให้ลืม (ผู้ใช้ปิดมัน) — คนละอย่างกับเฟรมที่ไม่มีข้อมูล
static int s_forget_page = -1;
// กติกาของจอที่ผู้ใช้ตั้งไว้ — แปลตรงที่รับ แล้วส่งก้อนที่แปลงแล้วเข้าลูปหลัก เหมือน snapshot
static ct_page_plan_t s_pending_plan;
static bool s_has_pending_plan;
static bool s_link;
static bool s_link_changed = true;
static bool s_wifi_up;
static char s_ip[16];
static int s_pending_backlight = -1;

static uint32_t millis_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    size_t px = (size_t)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    // LVGL เก็บ RGB565 แบบ little-endian ส่วนจอกินแบบ big-endian
    lv_draw_sw_rgb565_swap(px_map, px);
    ct_lcd_blit(area->x1, area->y1, area->x2, area->y2, px_map, px * 2);
    lv_display_flush_ready(disp);
}

// --- callback จาก NimBLE (คนละเธรดกับ LVGL) ---------------------------------
// เฟรมนี้เป็นของหน้าไหน — คีย์ `g` ที่หายไปคือหน้ามาสคอต ไม่ใช่ความผิดพลาด
//
// ตัวแยกเป็น *การมีคีย์* ไม่ใช่ค่าของมัน เพราะ snapshot ของหน้ามาสคอตมีมาก่อนรอบ
// multi-page และต้องเหมือนเดิมทุกไบต์ (ADR-0003) · `x` คือคำสั่งให้ลืมหน้านั้นทิ้ง
// สแกนหาคีย์ ไม่ใช่ parse ทั้งก้อน — snapshot ของหน้ามาสคอตมาบ่อยที่สุดและถูกส่งต่อไป
// parse เต็มใบอยู่แล้ว การ parse สองรอบต่อหนึ่งเฟรมคือ malloc ชุดที่สองบน ESP32 เพื่อ
// ตอบคำถามเดียว · ปลอดภัยเพราะเครื่องหมายคำพูดใน *ค่า* ของ JSON ถูก escape เสมอ
// ลำดับไบต์ `"g":` จึงโผล่ได้เฉพาะตรงที่เป็นคีย์จริง (เดียวกับที่ on_config ทำกับ "b")
static int frame_page(const char *json, bool *forget)
{
    const char *kind = strstr(json, "\"g\":");
    // `"x":1` เท่านั้น — Mac ส่งค่านี้ค่าเดียว การรับ true/1/"1" ทุกแบบคือการเดาแทนคู่สนทนา
    *forget = strstr(json, "\"x\":1") != NULL;
    return kind ? atoi(kind + 4) : CT_PAGE_MASCOT;
}

static void on_state(const char *json, int len)
{
    // ค่าตั้งของจอเดินมาช่องเดียวกับเฟรมของหน้า เพราะมันต้องไปถึงทั้งทาง BLE และทาง LAN
    // (ช่อง config เป็นของ BLE อย่างเดียว) ตัวแยกคือคีย์ `pl` ซึ่งไม่มีในเฟรมของใครเลย
    if (strstr(json, "\"pl\":")) {
        ct_page_plan_t plan;
        if (!ct_pages_parse_plan(json, len, &plan)) {
            ESP_LOGW(TAG, "page settings arrived in a shape this firmware cannot read");
            return;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_pending_plan = plan;
        s_has_pending_plan = true;
        xSemaphoreGive(s_lock);
        return;
    }

    bool forget = false;
    int page = frame_page(json, &forget);

    if (page != CT_PAGE_MASCOT) {
        if (page >= CT_PAGE_KIND_COUNT) {
            // daemon ใหม่กว่า firmware — ทิ้งไป ไม่ใช่วาดมั่ว การประกาศ capability
            // ตอนเชื่อมต่อมีไว้ไม่ให้สภาพนี้เกิดตั้งแต่แรก
            ESP_LOGW(TAG, "frame for page %d, which this firmware does not know", page);
            return;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (forget) {
            s_forget_page = page;
        } else if (len >= (int)sizeof(s_pending_page)) {
            ESP_LOGW(TAG, "page %d sent %d bytes, more than one frame can be", page, len);
        } else {
            // ช่องเดียว: เฟรมที่มาซ้อนก่อนลูปหลักหยิบไปใช้ (10 ms) จะทับของเดิม ซึ่งเกิดได้
            // เฉพาะตอนบอร์ดต่อกลับแล้วทุกหน้าถูกส่งใหม่พร้อมกัน — พูดออกมา ไม่ใช่หายเงียบ
            if (s_pending_page_kind >= 0 && s_pending_page_kind != page) {
                ESP_LOGW(TAG, "page %d arrived before page %d was drawn", page,
                         s_pending_page_kind);
            }
            memcpy(s_pending_page, json, len);
            s_pending_page_len = len;
            s_pending_page_kind = page;
        }
        xSemaphoreGive(s_lock);
        return;
    }

    ct_snapshot_t parsed;
    if (!ct_model_parse(json, len, &parsed)) {
        ESP_LOGW(TAG, "snapshot was not valid json");
        return;
    }
    ESP_LOGI(TAG, "snapshot %s: %d sessions, %d cards", parsed.clock, parsed.session_count,
             parsed.card_count);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pending = parsed;
    s_has_pending = true;
    xSemaphoreGive(s_lock);
}

// กุญแจของทาง LAN เดินมาช่องเดียวกับรหัส WiFi ด้วยเหตุผลเดียวกัน (ต้องเข้ารหัส) แต่
// เจ้าของคนละโมดูล — แยกออกมาที่นี่แทนที่จะยัดเข้า `ct_wifi_command` เพื่อไม่ให้โมดูล
// WiFi ต้องรู้จักเรื่องการปิดผนึกเฟรม ซึ่งเป็นคนละชั้นกัน
static bool lan_command(const char *json, int len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;
    const cJSON *cmd = cJSON_GetObjectItem(root, "c");
    const cJSON *key = cJSON_GetObjectItem(root, "k");
    bool mine = cJSON_IsString(cmd) && strcmp(cmd->valuestring, "key") == 0;
    if (mine && !ct_lan_set_key(cJSON_IsString(key) ? key->valuestring : NULL)) {
        ESP_LOGW(TAG, "lan key rejected");
    }
    cJSON_Delete(root);
    // ตอบกลับด้วยสถานะเต็มใบ ไม่ใช่ ack เปล่า — Mac ต้องเห็นลายนิ้วมือใหม่เพื่อรู้ว่า
    // กุญแจที่มันเพิ่งส่งไปคือกุญแจที่บอร์ดถืออยู่จริง
    if (mine) ct_wifi_report();
    return mine;
}

static void on_config(const char *json, int len)
{
    // คำสั่ง WiFi มาทางเดียวกับความสว่าง แยกด้วยคีย์ "c" — ช่องนี้บังคับเข้ารหัสอยู่แล้ว
    // เพราะรหัส WiFi ต้องผ่านมันไป (ct_ble.h)
    if (lan_command(json, len)) return;
    if (ct_wifi_command(json, len)) return;

    // คอนฟิกที่เหลือมีค่าเดียว: {"b":0..100}
    const char *p = strstr(json, "\"b\"");
    if (!p) return;
    p = strchr(p, ':');
    if (!p) return;
    int value = atoi(p + 1);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_pending_backlight = value;
    xSemaphoreGive(s_lock);
}

// Mac พร้อมฟังแล้ว — บอกไปว่าบอร์ดตัวนี้รู้จักหน้าอะไรบ้าง (ADR-0006)
//
// ประกาศเป็นความสามารถ ไม่ใช่เลขเวอร์ชัน: บอร์ดนี้ไม่มี OTA ผู้ใช้ลาก `.app` ใหม่ทับ
// ได้ใน 5 วินาที แต่การแฟลชต้องหาสาย USB สภาพ "แอปใหม่ + firmware เก่า" จึงเป็นปกติ
static void on_ready(void)
{
    char json[64];
    int n = ct_pages_capability_json(json, sizeof(json));
    ct_ble_notify(json, n);
}

static void on_link(bool connected)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_link = connected;
    s_link_changed = true;
    xSemaphoreGive(s_lock);
}

// --- callback จาก esp_wifi (คนละเธรดอีกใบ) -----------------------------------
// ประกอบ JSON ด้วย cJSON ไม่ใช่ snprintf: ชื่อเครือข่ายเป็นข้อความที่ผู้อื่นตั้ง และ
// SSID ที่มีเครื่องหมายคำพูดอยู่ข้างในจะทำให้ฝั่ง Mac แปลงไม่ผ่านทั้งรายการ
static void notify_json(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) return;
    ct_ble_notify(text, (int)strlen(text));
    cJSON_free(text);
}

static void on_ap(const char *ssid, int8_t rssi, bool secured)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "t", "ap");
    cJSON_AddStringToObject(root, "s", ssid);
    cJSON_AddNumberToObject(root, "r", rssi);
    cJSON_AddNumberToObject(root, "e", secured ? 1 : 0);
    notify_json(root);
}

static void on_ap_end(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "t", "ap_end");
    notify_json(root);
}

static const char *wifi_state_name(ct_wifi_state_t st)
{
    switch (st) {
        case CT_WIFI_CONNECTING: return "connecting";
        case CT_WIFI_CONNECTED: return "connected";
        case CT_WIFI_FAILED: return "failed";
        default: return "off";
    }
}

static void on_wifi_status(ct_wifi_state_t st, const char *ssid, const char *ip,
                           const char *err)
{
    bool up = (st == CT_WIFI_CONNECTED);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_wifi_up = up;
    snprintf(s_ip, sizeof(s_ip), "%s", up && ip ? ip : "");
    s_link_changed = true;
    xSemaphoreGive(s_lock);

    // server ขึ้นตามการมี IP ไม่ใช่ตามการมีกุญแจ — บอร์ดที่รับสายแล้วปฏิเสธทุกเฟรม
    // บอกฝั่ง Mac ได้ว่า "กุญแจไม่ตรง" ส่วนบอร์ดที่ไม่รับสายเลยแยกไม่ออกจากบอร์ดที่ตาย
    ct_lan_set_up(up, ip);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "t", "wifi");
    cJSON_AddStringToObject(root, "st", wifi_state_name(st));
    cJSON_AddStringToObject(root, "s", ssid ? ssid : "");
    cJSON_AddStringToObject(root, "ip", ip ? ip : "");
    // ลายนิ้วมือกุญแจ LAN — ว่างแปลว่ายังไม่เคยตั้ง Mac จะได้รู้ว่าต้องส่งไปให้
    cJSON_AddStringToObject(root, "kf", ct_lan_key_fingerprint());
    if (err && err[0]) cJSON_AddStringToObject(root, "er", err);

    // รายชื่อที่จำไว้เดินทางมากับสถานะ ไม่ใช่คำสั่งแยก — หน้าตั้งค่าต้องการทั้งคู่พร้อมกัน
    // เสมอ และสองข้อความที่มาไม่พร้อมกันแปลว่ามีจังหวะที่หน้าจอแสดงของครึ่งเดียว
    char saved[CT_WIFI_MAX_NETS][CT_WIFI_SSID_CAP];
    int n = ct_wifi_saved(saved, CT_WIFI_MAX_NETS);
    cJSON *list = cJSON_AddArrayToObject(root, "nets");
    for (int i = 0; list && i < n; i++) {
        cJSON_AddItemToArray(list, cJSON_CreateString(saved[i]));
    }
    notify_json(root);
}

// --- ลูปหลัก -----------------------------------------------------------------
static bool has_alert(const ct_snapshot_t *s)
{
    for (int i = 0; i < s->card_count; i++) {
        if (s->cards[i].kind == CT_CARD_ALERT) return true;
    }
    return false;
}

static void apply_pending(void)
{
    ct_snapshot_t snap;
    char page_json[sizeof(s_pending_page)];
    ct_page_plan_t plan;
    int page_len = 0, page_kind = -1, forget_page = -1;
    bool got_snapshot = false, link = false, link_changed = false, wifi = false;
    bool got_plan = false;
    int backlight = -1;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_has_pending_plan) {
        plan = s_pending_plan;
        s_has_pending_plan = false;
        got_plan = true;
    }
    if (s_has_pending) {
        snap = s_pending;
        s_has_pending = false;
        got_snapshot = true;
    }
    if (s_pending_page_kind >= 0) {
        memcpy(page_json, s_pending_page, s_pending_page_len);
        page_len = s_pending_page_len;
        page_kind = s_pending_page_kind;
        s_pending_page_kind = -1;
    }
    forget_page = s_forget_page;
    s_forget_page = -1;
    link = s_link;
    wifi = s_wifi_up;
    link_changed = s_link_changed;
    s_link_changed = false;
    backlight = s_pending_backlight;
    s_pending_backlight = -1;
    xSemaphoreGive(s_lock);

    // ทาง LAN ไม่มี callback บอกว่ามีใครต่อเข้ามา — ถามเอาตรงนี้ ลูปนี้เดินทุก 10 ms
    // อยู่แล้วและคำตอบคือการอ่านตัวแปรตัวเดียว ถูกกว่าการลากสัญญาณข้ามสองเธรด
    static bool last_lan = false;
    bool lan = ct_lan_client_connected();
    if (lan != last_lan) {
        last_lan = lan;
        link_changed = true;
    }

    if (link_changed) {
        // "สด" คือมีใครสักคนป้อน snapshot อยู่ ไม่ว่าจะทางไหน — จอที่ซ่อนการ์ดทิ้งทั้งที่
        // ข้อมูลกำลังไหลเข้ามาทาง LAN คือจอที่โกหกในทางกลับกัน
        ct_pages_set_connected(link || lan);
        ct_pages_set_link(link, wifi);
    }
    // กติกามาก่อนเนื้อหาเสมอ ทั้งบนสายและตรงนี้ — หน้าที่เพิ่งถูกปิดต้องไม่ถูกวาดอีกหนึ่งครั้ง
    if (got_plan) {
        // พูดออกมาแบบเดียวกับ snapshot: ค่าตั้งที่ไม่มีอาการให้เห็นทันที (เช่นรอบหมุน)
        // แยกไม่ออกจากค่าตั้งที่ไม่เคยมาถึง ถ้าไม่มีบรรทัดนี้
        ESP_LOGI(TAG, "pages: %d in rotation, auto %d, turn %d ms, hold %d ms, jump %d",
                 plan.count, plan.auto_turn, plan.rotation_ms, plan.hold_ms,
                 plan.attention_jump);
        ct_pages_set_plan(&plan);
    }
    if (got_snapshot) {
        static bool had_alert = false;
        bool alert = has_alert(&snap);
        // กะพริบเฉพาะตอนการ์ดแดง *ใบใหม่* ไม่ใช่ทุก snapshot ที่ยังมีใบเดิมค้างอยู่
        if (alert && !had_alert) ct_led_flash();
        had_alert = alert;

        // ส่วนการเด้งกลับหน้ามาสคอตดูจาก *เลขเหตุการณ์* ที่ Mac นับมาให้ ไม่ใช่จากการ์ด:
        // การ์ดแดงเป็นสถานะที่ค้างอยู่ ตัวที่สองที่ขอความช่วยเหลือระหว่างที่ใบแรกยังไม่ถูก
        // ตอบ จึงไม่ขยับขอบขาขึ้นเลย ทั้งที่เป็นเรื่องใหม่ · และท่ารอที่มาจากเทิร์นที่เงียบ
        // เกินเกณฑ์ไม่มีการ์ดแดงเป็นของตัวเองด้วยซ้ำ (การ์ดของมันเป็นใบเขียว "your turn")
        //
        // -1 = ยังไม่เคยเห็น snapshot ตั้งแต่บูต: เฟรมแรกเป็นรายงานสถานะ ไม่ใช่เรื่องที่
        // เพิ่งเกิด · เลขที่ลดลงคือ daemon ที่เพิ่งรีสตาร์ท ซึ่งก็ไม่ใช่เรื่องใหม่เหมือนกัน
        // ตัวนับนี้ *ไม่* ถูกล้างตอนลิงก์หลุดแล้วต่อกลับ โดยตั้งใจ: เรื่องที่เกิดระหว่างที่
        // คุยกันไม่ได้ยังไม่มีใครเห็น จอจึงควรถูกดึงกลับมาให้เห็นตอนได้ยินอีกครั้ง
        static int last_attention = -1;
        if (last_attention >= 0 && snap.attention > last_attention) ct_pages_attention();
        last_attention = snap.attention;

        ct_pages_set_snapshot(&snap);
    }
    if (forget_page >= 0) ct_pages_forget((ct_page_kind_t)forget_page);
    if (page_kind >= 0
        && !ct_pages_set_frame((ct_page_kind_t)page_kind, page_json, page_len)) {
        ESP_LOGW(TAG, "page %d sent a frame it cannot draw", page_kind);
    }
    if (backlight >= 0) ct_lcd_set_backlight(backlight);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_lock = xSemaphoreCreateMutex();
    ct_mascot_init();
    ct_lcd_init();
    ct_led_init();

    lv_init();
    lv_tick_set_cb(millis_cb);

    s_buf1 = heap_caps_malloc(DRAW_BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA);
    s_buf2 = heap_caps_malloc(DRAW_BUF_PX * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(s_buf1 && s_buf2);

    lv_display_t *disp = lv_display_create(CT_SCREEN_WIDTH, CT_SCREEN_HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, s_buf1, s_buf2, DRAW_BUF_PX * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    ct_pages_init();
    // swipe เป็นส่วนเสริมที่ขาดได้ (ADR-0007) — บอร์ดรุ่นย่อยที่ไม่มีชิปสัมผัสเดินต่อ
    // ด้วย page rotation อย่างเดียวได้ครบทุกอย่าง ค่าที่คืนมาจึงเป็นข้อมูล ไม่ใช่เงื่อนไข
    ct_touch_init();
    ct_pages_set_connected(false);
    ct_pages_set_link(false, false);

    ct_ble_cbs_t cbs = {
        .on_state = on_state,
        .on_config = on_config,
        .on_link = on_link,
        .on_ready = on_ready,
    };
    ct_ble_init(&cbs);

    // WiFi ตามหลัง BLE เสมอ: ทางหลักต้องขึ้นก่อน และผลสแกนต้องมีปลายทางให้ส่งไปแล้ว
    ct_wifi_cbs_t wifi_cbs = {
        .on_ap = on_ap,
        .on_ap_end = on_ap_end,
        .on_status = on_wifi_status,
    };
    ct_wifi_init(&wifi_cbs);
    // snapshot ที่มาทาง LAN เข้าประตูเดียวกับที่มาทาง BLE — สองทางเดิน ปลายทางเดียว
    ct_lan_init(on_state);
    ESP_LOGI(TAG, "ready");

    const int step_ms = 10;
    int since_frame = 0;
    int since_heap = 0;
    while (1) {
        apply_pending();
        // อ่านสัมผัสทุกลูป (ตัวมันจับจังหวะ poll เอง) ไม่ใช่ทุกเฟรม — การปัดต้องเปลี่ยนหน้า
        // ทันทีโดยไม่รอ Mac และไม่รอจังหวะวาด
        ct_touch_event_t touch = ct_touch_poll_event(step_ms);
        if (touch.swipe != CT_SWIPE_NONE) {
            // ปัดซ้ายคือดันหน้าที่ดูอยู่ออกไปทางซ้ายเพื่อเปิดหน้าถัดไป เหมือนกองการ์ด
            ct_pages_step(touch.swipe == CT_SWIPE_LEFT);
        } else if (touch.tap) {
            answer_ask(touch.x, touch.y);
        }
        since_frame += step_ms;
        if (since_frame >= 60) {  // ~16 เฟรมต่อวินาที พอสำหรับอนิเมชันบล็อกสี่เหลี่ยม
            ct_pages_tick(since_frame);
            ct_led_tick(since_frame);
            since_frame = 0;
        }
        // DRAM static เหลือ ~24KB เท่านั้น (idf.py size) ทาง LAN กับ mDNS เป็นสองตัวที่กิน
        // เพิ่มล่าสุด — ที่รั่วช้าๆ จะไม่โผล่จนกว่าจะพังจริง นาทีละบรรทัดพอให้เห็นเทรนด์
        // จาก monitor ธรรมดาโดยไม่ต้องต่อเครื่องมืออะไร · min คือก้นที่เคยลงไปถึง
        since_heap += step_ms;
        if (since_heap >= 60000) {
            since_heap = 0;
            // pool ของ LVGL เป็นก้อนคงที่ 48KB แยกจาก heap ของ ESP (LV_MEM_SIZE) และมัน
            // คือก้อนที่ *เต็มก่อน*: init จอกินไปแล้ว ~88% ส่วนที่เหลือคือที่ที่ draw task
            // กับ layer ถูกจองระหว่างวาด · พอจองไม่ได้ `lv_draw_dispatch` วนรอไม่จบ
            // main ไม่คืนจาก `lv_timer_handler` และ watchdog ยิงทุก 5 วินาที ซึ่งอ่านจาก
            // อาการเหมือน "จอค้าง" ล้วนๆ ไม่มีอะไรชี้ไปที่หน่วยความจำเลย จึงพิมพ์คู่กับ heap
            lv_mem_monitor_t lvm;
            lv_mem_monitor(&lvm);
            ESP_LOGI(TAG, "lv_mem free %u used %u%%", (unsigned)lvm.free_size,
                     (unsigned)lvm.used_pct);
            ESP_LOGI(TAG, "heap %u min %u", (unsigned)esp_get_free_heap_size(),
                     (unsigned)esp_get_minimum_free_heap_size());
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
}
