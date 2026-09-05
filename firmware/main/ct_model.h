// snapshot ที่ daemon ส่งมา — page frame ของหน้ามาสคอต (ADR-0003)
//
// firmware ไม่มีตรรกะ แต่มี cache: ก้อนนี้ถูกเก็บไว้ที่ `ct_pages` แล้ววาดจากมันอย่างเดียว
// เนื้อหาทุกอย่างยังตัดสินที่ daemon เหมือนเดิม
//
// รูปแบบบนสาย (คีย์สั้นเพราะต้องพอดี 1 MTU):
//   {"c":"14:32","d":"Mon 27 Jul","o":0,
//    "s":[{"p":"tamaclaude","s":"building"}],
//    "n":[{"t":"tamaclaude","b":"allow Bash?","k":"alert"}],"m":0,
//    "u":[[35,10800],[48,111600]],"a":3,
//    "q":{"i":"A1B2C3D4","t":"Bash: npm test","a":1}}
#pragma once

#include <stdbool.h>

#include "layout.h"

#define CT_MAX_CARDS CT_CARD_MAX
#define CT_PROJECT_LEN 20
#define CT_TITLE_LEN 40
#define CT_BODY_LEN 52
#define CT_CLOCK_LEN 8
#define CT_DATE_LEN 16
// id ของคำขออนุญาต — Mac แจกมาเป็นเลขฐานสิบหกแปดตัว บวกตัวปิดสตริง
#define CT_ASK_ID_LEN 12

typedef enum {
    CT_CARD_INFO = 0,
    CT_CARD_ALERT,
    CT_CARD_DONE,
} ct_card_kind_t;

typedef struct {
    char project[CT_PROJECT_LEN];
    ct_state_t state;
} ct_session_t;

typedef struct {
    char title[CT_TITLE_LEN];
    char body[CT_BODY_LEN];
    ct_card_kind_t kind;
} ct_card_t;

// คำขออนุญาตที่ค้างอยู่ — มีได้ทีละใบ (คีย์ "q")
//
// บอร์ดไม่ได้ตัดสินอะไรเกี่ยวกับมันเลย มันแค่วาดแล้วรับนิ้ว: `may_allow` มาจาก Mac
// (`Risk`) และ Mac ตัดสินซ้ำอีกครั้งตอนรับคำตอบกลับ เฟิร์มแวร์ที่ถูกแก้ให้ส่ง allow
// กับทุกใบจึงเปลี่ยนอะไรไม่ได้ (ADR-0001, ADR-0014)
typedef struct {
    bool present;
    // ต้องส่งกลับไปพร้อมคำตอบ — คำตอบผูกกับ *ใบ* ไม่ใช่แค่คำว่าอนุญาต
    char id[CT_ASK_ID_LEN];
    char title[CT_TITLE_LEN];
    // false = ปุ่มขวาไม่ใช่ Allow แต่เป็นคำบอกว่าต้องไปตอบที่คีย์บอร์ด
    bool may_allow;
} ct_ask_t;

// โควตาหนึ่งหน้าต่าง — คู่ [percent, วินาทีที่เหลือ] ที่ daemon ส่งมาใน "u"
//
// -1 แปลว่า "ไม่รู้" ไม่ใช่ศูนย์ — ศูนย์เป็นค่าจริง เปอร์เซ็นต์ที่ไม่รู้ต้องวาดเป็น "--"
// ห้ามเดาจากอะไรทั้งสิ้น
#define CT_USAGE_UNKNOWN (-1)
#define CT_USAGE_ROWS 2

typedef struct {
    int percent;
    int remaining;  // วินาที — บอร์ดนับถอยลงเอง ระหว่างที่ยังไม่มี snapshot ใหม่
} ct_usage_t;

typedef struct {
    char clock[CT_CLOCK_LEN];
    char date[CT_DATE_LEN];
    int overflow;
    int session_count;
    ct_session_t sessions[CT_SLOTS_COUNT];
    int card_count;
    ct_card_t cards[CT_MAX_CARDS];
    // การ์ดที่มีอยู่จริงแต่ไม่ได้ส่งมา — จอบอกเป็น "+N" ใต้ใบล่างสุด
    // การเตือนที่หายไปเงียบๆ แย่กว่าการเตือนที่อ่านไม่ครบ
    int card_overflow;
    // false = ไม่เคยได้ข้อมูลโควตาเลย -> จอถอยไปเป็นนาฬิกาตั้งโต๊ะ ไม่ใช่วาดโครงเปล่า
    bool has_usage;
    ct_usage_t usage[CT_USAGE_ROWS];
    // เลขเหตุการณ์ "มี session ต้องการคน" ที่ Mac นับให้ — ไม่ใช่สถานะ ไม่ใช่จำนวนที่รออยู่
    //
    // บอร์ดเด้งกลับหน้ามาสคอตเมื่อเลขนี้ *โตขึ้น* เท่านั้น การดูจากสถานะแทนจะทำให้จอถูก
    // กระชากกลับทุก snapshot ตลอดสิบนาทีที่คำขออนุญาตค้างอยู่ จนใช้หน้าอื่นไม่ได้เลย
    int attention;
    ct_ask_t ask;
} ct_snapshot_t;

// เดินนาฬิกาถอยหลังไป `secs` วินาที — เรียกจากลูปหลัก ไม่ใช่ตอนรับ snapshot
//
// countdown เดินบนบอร์ดเพราะเวลารีเซ็ตเป็นค่าสัมบูรณ์: BLE หลุดแล้วเวลายังจริงอยู่
// ส่วนเปอร์เซ็นต์หยุดนิ่ง (ซึ่งถูก — มันหยุดจริง) และกลายเป็น "ไม่รู้" เมื่อถึง 0
void ct_model_tick_usage(ct_snapshot_t *s, int secs);

// แปลง JSON หนึ่งก้อนเป็น snapshot — คืน false เมื่อ JSON ใช้ไม่ได้ (ของเดิมต้องไม่ถูกแตะ)
bool ct_model_parse(const char *json, int len, ct_snapshot_t *out);

// snapshot ว่าง: ไม่มี session ไม่มีการ์ด นาฬิกาเป็นขีด
void ct_model_clear(ct_snapshot_t *s);

// ท่าเดียวที่แทนทั้งเครื่อง — ตัวที่ต้องการคนมากที่สุดชนะ
//
// มาสคอตจิ๋วบนหน้าอื่นมีที่ให้ท่าเดียว ถ้าหยิบ session ตัวแรกมาแสดง (ซึ่งเรียงซ้าย->ขวา
// ตามลำดับที่เข้ามา ไม่ใช่ตามความเร่งด่วน) การรออนุญาตของตัวที่สามจะหายไปจากทุกหน้า
// ที่ไม่ใช่หน้ามาสคอต ไม่มี session เลย = หลับ ซึ่งต่างจาก "ไม่มีมาสคอต" ที่แปลว่าจอ
// หลุดจาก Mac · ลำดับต้องตรงกับ VisualState.priority ใน TamaCore/Protocol.swift
ct_state_t ct_model_lead_state(const ct_snapshot_t *s);
