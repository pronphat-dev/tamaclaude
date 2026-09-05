#include "ct_touch.h"

#include "driver/spi_master.h"
#include "esp_log.h"
#include "layout.h"

static const char *TAG = "touch";

// ยืนยันแล้วจากบอร์ดจริง (docs/hardware.md) — คนละ host กับจอ จึงไม่ต้องล็อกอะไรร่วมกับการวาด
#define PIN_T_SCLK 25
#define PIN_T_MOSI 32
#define PIN_T_MISO 39
#define PIN_T_CS 33
// IRQ (GPIO 36) ไม่ได้ถูกใช้: ท่าทางเดียวที่รับต้องอ่านค่าตลอดการลากอยู่แล้ว การปลุกด้วย
// ขานี้จึงไม่ประหยัดอะไร และขา 36 เป็นอินพุตอย่างเดียวที่พึ่ง pull-up ภายนอกของบอร์ด

#define TOUCH_HOST SPI3_HOST
// XPT2046 รับได้ราว 2MHz ตอนวัด 12 บิต — 1MHz ไว้ก่อน เท่ากับที่ probe ใช้วัดค่าที่จดไว้
#define TOUCH_CLOCK_HZ (1 * 1000 * 1000)

// control byte: S A2 A1 A0 MODE SER/DFR PD1 PD0
// MODE=0 คือผล 12 บิต · SER/DFR=0 คือวัดแบบ differential (นิ่งกว่า) · PD=00 ให้ IRQ ทำงานต่อ
#define T_CMD_X 0xD0
#define T_CMD_Y 0x90
#define T_CMD_Z1 0xB0

static spi_device_handle_t s_dev;
static bool s_present;

// สถานะของท่าทางที่กำลังเกิด — ทั้งหมดเป็นหน่วย ADC ดิบ ไม่ใช่พิกเซล
static bool s_down;         // มีนิ้วอยู่บนจอตอนนี้ (ผ่านเกณฑ์ติดกันครบแล้ว)
static int s_press_run;     // จำนวนตัวอย่างติดกันที่เกินเกณฑ์ (ยังไม่ถึง = ยังไม่ใช่นิ้ว)
static int s_release_run;   // และนับกลับกันตอนยกนิ้ว ด้วยเหตุผลเดียวกัน
static int s_start_h, s_start_v;
static int s_last_h, s_last_v;
static int s_down_ms;
static int s_since_poll;

// ส่งคำสั่ง 1 ไบต์แล้วรับ 2 ไบต์ ผล 12 บิตวางชิดซ้ายอยู่ในสองไบต์นั้น
static uint16_t read_raw(uint8_t cmd)
{
    uint8_t tx[3] = {cmd, 0x00, 0x00};
    uint8_t rx[3] = {0};
    spi_transaction_t t = {
        .length = 24,
        .rxlength = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_polling_transmit(s_dev, &t) != ESP_OK) return 0;
    return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 3);
}

static uint16_t median3(uint8_t cmd)
{
    uint16_t a = read_raw(cmd), b = read_raw(cmd), c = read_raw(cmd);
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > c) { uint16_t t = b; b = c; c = t; }
    if (a > b) { uint16_t t = a; a = b; b = t; }
    return b;
}

bool ct_touch_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_T_MOSI,
        .miso_io_num = PIN_T_MISO,
        .sclk_io_num = PIN_T_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    if (spi_bus_initialize(TOUCH_HOST, &bus, SPI_DMA_DISABLED) != ESP_OK) {
        ESP_LOGW(TAG, "spi bus failed - swipe off");
        return false;
    }
    spi_device_interface_config_t dev = {
        .clock_speed_hz = TOUCH_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = PIN_T_CS,
        .queue_size = 1,
    };
    if (spi_bus_add_device(TOUCH_HOST, &dev, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "spi device failed - swipe off");
        spi_bus_free(TOUCH_HOST);
        return false;
    }

    // ไม่มีชิปหรือขาไม่ตรง = MISO ลอย แล้วทุกคำสั่งจะคืนค่าเดียวกันที่สุดขั้วของราง
    // บอร์ดที่มีชิปจริงตอนไม่แตะได้ x=0 y=4095 z1=1-2 ซึ่ง *ปนกัน* — นั่นคือตัวแยก
    // อ่านซ้ำหลายรอบ เพราะขาที่ลอยจริงๆ อาจสะบัดพ้นรางได้หนึ่งครั้งจากสัญญาณรบกวนรอบตัว
    // แล้วรอบเดียวนั้นจะทำให้บอร์ดที่ไม่มีชิปเปิด swipe ค้างไว้ตลอดอายุการใช้งาน
    uint16_t x = 0, y = 0, z = 0;
    bool mixed = false;
    for (int i = 0; i < 4 && !mixed; i++) {
        x = read_raw(T_CMD_X);
        y = read_raw(T_CMD_Y);
        z = read_raw(T_CMD_Z1);
        mixed = !(x == y && y == z && (x == 0 || x == 4095));
    }
    if (!mixed) {
        ESP_LOGW(TAG, "no XPT2046 (x=%u y=%u z=%u) - swipe off", x, y, z);
        return false;
    }
    s_present = true;
    ESP_LOGI(TAG, "XPT2046 ready (idle x=%u y=%u z=%u)", x, y, z);
    return true;
}

// ADC หนึ่งค่า -> พิกเซลหนึ่งแกน · หยาบโดยธรรมชาติ (ADR-0014) ผู้เรียกต้องออกแบบ
// เป้าให้ใหญ่พอรับความคลาดราว 5px ที่ขอบไกล
static int to_px(int adc, int origin, int per_px, int span)
{
    int px = (adc - origin) / per_px;
    if (px < 0) return 0;
    if (px >= span) return span - 1;
    return px;
}

// ท่าทางที่เพิ่งจบตอนยกนิ้ว — ว่างเปล่าเมื่อไม่เข้าเกณฑ์ของอะไรเลย
static ct_touch_event_t finish(void)
{
    ct_touch_event_t out = {.swipe = CT_SWIPE_NONE, .tap = false, .x = 0, .y = 0};

    int dh = s_last_h - s_start_h;
    int dv = s_last_v - s_start_v;
    // แปลงเป็นพิกเซลก่อนเทียบสองแกน เพราะความชันของสองแกนไม่เท่ากัน (11 กับ 14 counts/px)
    // การเทียบ ADC ดิบตรงๆ จะเอนเข้าข้างแกนตั้งเสมอ
    int px_h = (dh < 0 ? -dh : dh) / CT_TOUCH_COUNTS_PER_PX_H;
    int px_v = (dv < 0 ? -dv : dv) / CT_TOUCH_COUNTS_PER_PX_V;

    // แตะมาก่อน เพราะเกณฑ์ของมันแคบกว่าและไม่ทับกับของการปัดเลย — นิ้วที่ขยับเกิน
    // tap_max_px แต่ไม่ถึง swipe_min_px ไม่ใช่ทั้งสองอย่าง และไม่ควรกลายเป็นอย่างใด
    // อย่างหนึ่งเพราะบังเอิญถูกถามก่อน
    if (s_down_ms <= CT_TOUCH_TAP_MAX_MS
        && px_h <= CT_TOUCH_TAP_MAX_PX && px_v <= CT_TOUCH_TAP_MAX_PX) {
        out.tap = true;
        out.x = to_px(s_start_h, CT_TOUCH_H_ORIGIN, CT_TOUCH_COUNTS_PER_PX_H, CT_SCREEN_WIDTH);
        out.y = to_px(s_start_v, CT_TOUCH_V_ORIGIN, CT_TOUCH_COUNTS_PER_PX_V, CT_SCREEN_HEIGHT);
        return out;
    }

    // สัมผัสที่ค้างอยู่นานเกินคนปัด = ของที่วางทับจอ ไม่ใช่ท่าทาง — ตอนหยิบออกมันขยับ
    if (s_down_ms > CT_TOUCH_SWIPE_MAX_MS) return out;
    if (px_h < CT_TOUCH_SWIPE_MIN_PX) return out;
    // การลากที่เอียงจนแกนตั้งชนะคือท่าทางอื่นที่จอนี้ไม่รับ ไม่ใช่การปัดที่มือไม่ตรง
    if (px_v >= px_h) return out;
    out.swipe = dh < 0 ? CT_SWIPE_LEFT : CT_SWIPE_RIGHT;
    return out;
}

ct_swipe_t ct_touch_poll(int elapsed_ms)
{
    return ct_touch_poll_event(elapsed_ms).swipe;
}

ct_touch_event_t ct_touch_poll_event(int elapsed_ms)
{
    const ct_touch_event_t none = {.swipe = CT_SWIPE_NONE, .tap = false, .x = 0, .y = 0};
    if (!s_present) return none;

    if (s_down) s_down_ms += elapsed_ms;
    s_since_poll += elapsed_ms;
    if (s_since_poll < CT_TOUCH_POLL_MS) return none;
    s_since_poll = 0;

    // แรงกดที่วัดได้ตอนแตะจริงอยู่ที่ 260-1570 — ค่าที่ทะลุ CT_TOUCH_Z_MAX ขึ้นไปคือ
    // ขาที่ลอย ไม่ใช่คนที่กดแรงกว่านั้น และการรับไว้แปลว่านิ้วที่ไม่มีอยู่จริงกำลังลากอยู่
    uint16_t z = median3(T_CMD_Z1);
    if (z <= CT_TOUCH_Z_MIN || z >= CT_TOUCH_Z_MAX) {
        s_press_run = 0;
        if (!s_down) return none;
        // แรงกดตกต่ำกว่าเกณฑ์ชั่วขณะระหว่างลากเร็วๆ เกิดขึ้นได้บนจอ resistive — จบท่าทาง
        // ที่ตัวอย่างเดียวแปลว่าการปัดครั้งเดียวถูกอ่านเป็นสองครึ่งที่สั้นเกินเกณฑ์ทั้งคู่
        if (++s_release_run < CT_TOUCH_PRESS_SAMPLES) return none;
        s_down = false;
        return finish();
    }
    s_release_run = 0;

    // แกนของชิปสลับกับแกนของจอ: ADC y คือแกนนอนของภาพ ADC x คือแกนตั้ง (docs/hardware.md)
    // สลับที่นี่ที่เดียว ที่เหลือของไฟล์คิดเป็นแกนของภาพล้วนๆ
    int h = median3(T_CMD_Y);
    int v = median3(T_CMD_X);

    if (!s_down) {
        // นิ้วที่เพิ่งแตะยังไม่ใช่นิ้ว จนกว่าจะเห็นติดกันหลายครั้ง — ตัวอย่างเดียวที่กระโดด
        // ขึ้นมาแล้วหายไปคือ noise ซึ่งจะกลายเป็นจุดเริ่มของการลากที่ผิดที่
        if (++s_press_run < CT_TOUCH_PRESS_SAMPLES) return none;
        s_down = true;
        s_down_ms = 0;
        s_start_h = h;
        s_start_v = v;
    }
    s_last_h = h;
    s_last_v = v;
    return none;
}
