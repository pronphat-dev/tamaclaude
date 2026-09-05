#include "ct_model.h"

#include <string.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "model";

static void copy_str(char *dst, size_t cap, const cJSON *item)
{
    dst[0] = '\0';
    if (!cJSON_IsString(item) || !item->valuestring) return;
    strncpy(dst, item->valuestring, cap - 1);
    dst[cap - 1] = '\0';
}

static ct_state_t state_from_name(const char *name)
{
    if (!name) return CT_STATE_IDLE;
    for (int i = 0; i < CT_STATE_COUNT; i++) {
        if (strcmp(name, ct_state_names[i]) == 0) return (ct_state_t)i;
    }
    // ชื่อที่ firmware ไม่รู้จัก = daemon ใหม่กว่า firmware ให้ยืนเฉยๆ ดีกว่าจอค้าง
    ESP_LOGW(TAG, "unknown state '%s'", name);
    return CT_STATE_IDLE;
}

static ct_card_kind_t kind_from_name(const char *name)
{
    if (!name) return CT_CARD_INFO;
    if (strcmp(name, "alert") == 0) return CT_CARD_ALERT;
    if (strcmp(name, "done") == 0) return CT_CARD_DONE;
    return CT_CARD_INFO;
}

void ct_model_clear(ct_snapshot_t *s)
{
    memset(s, 0, sizeof(*s));
    strcpy(s->clock, "--:--");
    for (int i = 0; i < CT_USAGE_ROWS; i++) {
        s->usage[i].percent = CT_USAGE_UNKNOWN;
        s->usage[i].remaining = CT_USAGE_UNKNOWN;
    }
}

// ต้องตรงกับ VisualState.priority ใน host/Sources/TamaCore/Protocol.swift
static int state_priority(ct_state_t state)
{
    switch (state) {
        case CT_STATE_ALERT:
        case CT_STATE_ERROR: return 40;
        case CT_STATE_WAITING: return 30;
        case CT_STATE_ENTERING:
        case CT_STATE_LEAVING: return 25;
        case CT_STATE_CONDUCTING: return 22;
        case CT_STATE_CELEBRATE: return 15;
        case CT_STATE_IDLE: return 10;
        case CT_STATE_SLEEPING: return 0;
        default: return 20;  // ท่าที่มาจาก tool ทุกตัว
    }
}

ct_state_t ct_model_lead_state(const ct_snapshot_t *s)
{
    if (s->session_count <= 0) return CT_STATE_SLEEPING;
    ct_state_t best = s->sessions[0].state;
    for (int i = 1; i < s->session_count; i++) {
        if (state_priority(s->sessions[i].state) > state_priority(best)) {
            best = s->sessions[i].state;
        }
    }
    return best;
}

void ct_model_tick_usage(ct_snapshot_t *s, int secs)
{
    if (!s->has_usage) return;
    for (int i = 0; i < CT_USAGE_ROWS; i++) {
        ct_usage_t *u = &s->usage[i];
        if (u->remaining <= 0) continue;
        u->remaining -= secs;
        if (u->remaining > 0) continue;
        // หน้าต่างหมุนไปแล้ว: เปอร์เซ็นต์ที่ถืออยู่ผิดแน่นอนและเราไม่มีทางรู้ค่าใหม่
        // จนกว่า snapshot ถัดไปจะมาถึง ค่าที่ถูกคือ "ไม่รู้" ไม่ใช่ศูนย์
        u->remaining = 0;
        u->percent = CT_USAGE_UNKNOWN;
    }
}

bool ct_model_parse(const char *json, int len, ct_snapshot_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;

    ct_snapshot_t tmp;
    ct_model_clear(&tmp);

    copy_str(tmp.clock, sizeof(tmp.clock), cJSON_GetObjectItem(root, "c"));
    copy_str(tmp.date, sizeof(tmp.date), cJSON_GetObjectItem(root, "d"));
    const cJSON *overflow = cJSON_GetObjectItem(root, "o");
    if (cJSON_IsNumber(overflow)) tmp.overflow = overflow->valueint;

    const cJSON *sessions = cJSON_GetObjectItem(root, "s");
    if (cJSON_IsArray(sessions)) {
        const cJSON *it = NULL;
        cJSON_ArrayForEach(it, sessions) {
            if (tmp.session_count >= CT_SLOTS_COUNT) break;
            ct_session_t *sess = &tmp.sessions[tmp.session_count++];
            copy_str(sess->project, sizeof(sess->project), cJSON_GetObjectItem(it, "p"));
            const cJSON *st = cJSON_GetObjectItem(it, "s");
            sess->state = state_from_name(cJSON_IsString(st) ? st->valuestring : NULL);
        }
    }

    const cJSON *cards = cJSON_GetObjectItem(root, "n");
    if (cJSON_IsArray(cards)) {
        const cJSON *it = NULL;
        cJSON_ArrayForEach(it, cards) {
            if (tmp.card_count >= CT_MAX_CARDS) break;
            ct_card_t *card = &tmp.cards[tmp.card_count++];
            copy_str(card->title, sizeof(card->title), cJSON_GetObjectItem(it, "t"));
            copy_str(card->body, sizeof(card->body), cJSON_GetObjectItem(it, "b"));
            const cJSON *k = cJSON_GetObjectItem(it, "k");
            card->kind = kind_from_name(cJSON_IsString(k) ? k->valuestring : NULL);
        }
    }
    const cJSON *card_over = cJSON_GetObjectItem(root, "m");
    if (cJSON_IsNumber(card_over)) tmp.card_overflow = card_over->valueint;

    // ไม่มีคีย์นี้ = daemon รุ่นก่อนที่ยังไม่นับเหตุการณ์ให้ ซึ่งอ่านได้เป็นศูนย์ตลอด แปลว่า
    // ไม่มีอะไรใหม่ให้เด้ง ไม่ใช่ว่ามีเรื่องใหม่ทุกเฟรม
    const cJSON *attention = cJSON_GetObjectItem(root, "a");
    if (cJSON_IsNumber(attention)) tmp.attention = attention->valueint;

    // ไม่มีคีย์นี้ = ไม่มีใครถามอะไรอยู่ ซึ่งเป็นสภาพปกติของเกือบทุกเฟรม
    // (`present` เป็น false อยู่แล้วจาก ct_model_clear ที่ `tmp` เริ่มมา)
    const cJSON *ask = cJSON_GetObjectItem(root, "q");
    if (cJSON_IsObject(ask)) {
        copy_str(tmp.ask.id, sizeof(tmp.ask.id), cJSON_GetObjectItem(ask, "i"));
        copy_str(tmp.ask.title, sizeof(tmp.ask.title), cJSON_GetObjectItem(ask, "t"));
        // รับทั้งเลขและ true — Mac ส่งเลขมา (กติกาเดิมของสายนี้) ส่วน JSON ที่คนเขียน
        // เองตอนทดสอบมักเขียน true · ค่าที่อ่านไม่ออกแปลว่า "ไม่อนุญาต" ซึ่งเป็นฝั่งที่
        // ปลอดภัย แต่ค่าที่อ่านออกได้ต้องอ่านให้ถูก ไม่ใช่ตกไปอยู่ฝั่งปลอดภัยเพราะถามผิดข้อ
        const cJSON *may = cJSON_GetObjectItem(ask, "a");
        tmp.ask.may_allow = cJSON_IsTrue(may) || (cJSON_IsNumber(may) && may->valueint != 0);
        // ไม่มี id = ตอบกลับไม่ได้ ซึ่งแปลว่าปุ่มบนจอจะกดแล้วไม่เกิดอะไรขึ้น
        // การ์ดที่กดไม่ได้แย่กว่าไม่มีการ์ด เพราะมันบอกว่ามีอะไรให้ทำทั้งที่ไม่มี
        tmp.ask.present = tmp.ask.id[0] != '\0';
    }

    // "u" เป็น array ของคู่ [percent, วินาทีที่เหลือ] ไม่ใช่ object — คีย์กินไบต์บนสาย
    // ไม่มีคีย์นี้เลย = ยังไม่เคยมีข้อมูลโควตา ซึ่งไม่เหมือนกับ "มีแต่ไม่รู้ค่า"
    const cJSON *usage = cJSON_GetObjectItem(root, "u");
    if (cJSON_IsArray(usage)) {
        int row = 0;
        const cJSON *pair = NULL;
        cJSON_ArrayForEach(pair, usage) {
            if (row >= CT_USAGE_ROWS) break;
            const cJSON *pct = cJSON_GetArrayItem(pair, 0);
            const cJSON *rem = cJSON_GetArrayItem(pair, 1);
            if (cJSON_IsNumber(pct)) tmp.usage[row].percent = pct->valueint;
            if (cJSON_IsNumber(rem)) tmp.usage[row].remaining = rem->valueint;
            row++;
        }
        tmp.has_usage = row > 0;
    }

    cJSON_Delete(root);
    *out = tmp;  // เขียนทับทีเดียวตอนท้าย — JSON พังกลางทางต้องไม่ทิ้งภาพครึ่งๆ
    return true;
}
