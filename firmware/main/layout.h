// สร้างอัตโนมัติจาก tools/layout.toml — ห้ามแก้ไฟล์นี้ด้วยมือ
// แก้ที่ layout.toml แล้วรัน: python3 tools/export_layout.py
#pragma once

#include <stdint.h>

#define CT_SCREEN_WIDTH              320
#define CT_SCREEN_HEIGHT             240

#define CT_TOPBAR_HEIGHT             22
#define CT_TOPBAR_LINK_ICON_W        11
#define CT_TOPBAR_LINK_ICON_H        9
#define CT_TOPBAR_LINK_ICON_GAP      6

#define CT_SLOTS_COUNT               3
#define CT_SLOTS_WIDTH               106
#define CT_SLOTS_TOP                 49
#define CT_SLOTS_HEIGHT              90
#define CT_SLOTS_UNIT_PX             4
#define CT_SLOTS_BASELINE_PAD        19
#define CT_SLOTS_LABEL_INSET         4
#define CT_SLOTS_LABEL_DY            4

#define CT_CARD_TOP                  144
#define CT_CARD_HEIGHT               84
#define CT_CARD_PAD                  6
#define CT_CARD_MAX                  2
#define CT_CARD_H_ONE                32
#define CT_CARD_H_TWO                56
#define CT_CARD_GAP                  4
#define CT_CARD_TITLE_DY             8
#define CT_CARD_BODY_DY              34
#define CT_CARD_MORE_H               16
#define CT_CARD_TEXT_INSET           30
#define CT_CARD_RAIL_W               3
#define CT_CARD_RAIL_INSET_ALERT     0
#define CT_CARD_RAIL_INSET_INFO      10
#define CT_CARD_RAIL_INSET_DONE      15
#define CT_CARD_MARK                 8
#define CT_CARD_MARK_STROKE          2
#define CT_CARD_MARK_RIGHT           8

#define CT_USAGE_ROW_H               40
#define CT_USAGE_GAP                 4
#define CT_USAGE_BAR_H               7
#define CT_USAGE_SESSION_WINDOW      18000
#define CT_USAGE_WEEKLY_WINDOW       604800
#define CT_USAGE_WARN_PCT            60
#define CT_USAGE_CRIT_PCT            85

#define CT_STROLL_SPEED_PX_S         34
#define CT_STROLL_WARN_SPEED_PX_S    25
#define CT_STROLL_CRIT_SPEED_PX_S    17
#define CT_STROLL_PAUSE_S            2.5f
#define CT_STROLL_PAD_PX             96

#define CT_SKY_HORIZON               120
#define CT_SKY_DAWN_HOUR             5
#define CT_SKY_DAY_HOUR              7
#define CT_SKY_DUSK_HOUR             17
#define CT_SKY_NIGHT_HOUR            19
#define CT_SKY_DISC_R                7
#define CT_SKY_ARC_PEAK              89
#define CT_SKY_ARC_PAD               0
#define CT_SKY_TWINKLE_N             4
#define CT_SKY_STAR_PX               1
#define CT_SKY_STAR_PEAK_PX          3
#define CT_SKY_LOW_STAR_N            6
#define CT_SKY_CLOUD_SPEED_PX_S      4
#define CT_SKY_CLOUD_PAD             60
#define CT_SKY_DECK_BOTTOM           34
#define CT_SKY_RAIN_W                2
#define CT_SKY_RAIN_SPEED_PX_S       64
#define CT_SKY_SNOW_SPEED_PX_S       16

#define CT_SKY_STARS_COUNT           16
static const int16_t ct_sky_stars[CT_SKY_STARS_COUNT][2] = {
    { 33,  63},
    {128,  67},
    {205,  88},
    {289,  48},
    { 76,  27},
    {247,  71},
    { 14,  33},
    { 58,  82},
    {103,  44},
    {171,  90},
    { 92,  99},
    {149,  37},
    {186,  52},
    {229,  30},
    {268, 101},
    {307,  80},
};

#define CT_SKY_CLOUDS_COUNT          3
static const int16_t ct_sky_clouds[CT_SKY_CLOUDS_COUNT][3] = {
    { 40,  44,  44},
    {150,  74,  36},
    {250,  32,  52},
};

#define CT_SKY_GRASS_X_COUNT         24
static const int16_t ct_sky_grass_x[CT_SKY_GRASS_X_COUNT] = {
      4,  13,  35,  41,  55,  62,  88,  99, 104, 123, 131, 155,
    168, 174, 191, 200, 228, 235, 247, 268, 273, 288, 298, 310,
};

#define CT_SKY_DECK_LUMPS_COUNT      22
static const int16_t ct_sky_deck_lumps[CT_SKY_DECK_LUMPS_COUNT][3] = {
    {-15,  18,   9},
    { -2,  26,  13},
    { 21,  16,   8},
    { 35,  20,  10},
    { 49,  20,  10},
    { 62,  22,  11},
    { 76,  26,  13},
    { 92,  26,  13},
    {113,  20,  10},
    {125,  26,  13},
    {145,  16,   8},
    {157,  28,  14},
    {179,  20,  10},
    {193,  22,  11},
    {214,  16,   8},
    {227,  20,  10},
    {237,  28,  14},
    {257,  18,   9},
    {267,  26,  13},
    {286,  16,   8},
    {301,  16,   8},
    {313,  22,  11},
};

#define CT_SKY_RAIN_COUNT            14
static const int16_t ct_sky_rain[CT_SKY_RAIN_COUNT][3] = {
    { 22,  44,  14},
    { 54,  66,  10},
    { 78,  40,  18},
    {108,  78,  12},
    {140,  52,  15},
    {166,  88,  11},
    {196,  46,  17},
    {222,  70,  13},
    {252,  56,  16},
    {284,  92,  10},
    { 38, 104,   9},
    {126, 112,   8},
    {208,  98,   9},
    {300,  84,   8},
};

#define CT_SKY_SNOW_COUNT            14
static const int16_t ct_sky_snow[CT_SKY_SNOW_COUNT][3] = {
    { 26,  42,   5},
    { 58,  64,   3},
    { 84,  48,   4},
    {112,  80,   3},
    {138,  54,   5},
    {162,  92,   3},
    {192,  44,   4},
    {216,  74,   3},
    {248,  58,   5},
    {278, 100,   4},
    { 42, 108,   3},
    {130, 116,   4},
    {204,  96,   3},
    {296,  68,   4},
};

#define CT_SKY_FOG_BANDS_COUNT       4
static const int16_t ct_sky_fog_bands[CT_SKY_FOG_BANDS_COUNT][2] = {
    { 64,   4},
    { 80,   5},
    { 98,   7},
    {112,   8},
};

#define CT_SKY_BOLT_COUNT            4
static const int16_t ct_sky_bolt[CT_SKY_BOLT_COUNT][4] = {
    { 58,  34,   9,  12},
    { 49,  45,   9,  11},
    { 55,  55,   8,  10},
    { 46,  64,   8,   9},
};

#define CT_ROTATION_SECONDS          20
#define CT_ROTATION_HOLD_SECONDS     300
#define CT_ROTATION_AUTO             1

#define CT_TOUCH_Z_MIN               200
#define CT_TOUCH_Z_MAX               3000
#define CT_TOUCH_PRESS_SAMPLES       2
#define CT_TOUCH_COUNTS_PER_PX_H     11
#define CT_TOUCH_COUNTS_PER_PX_V     14
#define CT_TOUCH_SWIPE_MIN_PX        60
#define CT_TOUCH_SWIPE_MAX_MS        1500
#define CT_TOUCH_POLL_MS             50
#define CT_TOUCH_H_ORIGIN            206
#define CT_TOUCH_V_ORIGIN            325
#define CT_TOUCH_TAP_MAX_PX          12
#define CT_TOUCH_TAP_MAX_MS          700

#define CT_PAGE_STALE_FACTOR         10

#define CT_FOOTER_HEIGHT             22
#define CT_FOOTER_RULE_H             1
#define CT_FOOTER_AGE_X              12
#define CT_FOOTER_AGE_Y              223
#define CT_FOOTER_PIP_H              4
#define CT_FOOTER_PIP_DOT_W          4
#define CT_FOOTER_PIP_CUR_W          14
#define CT_FOOTER_PIP_GAP            6
#define CT_FOOTER_PIP_RIGHT_GAP      12
#define CT_FOOTER_MINI_UNIT_PX       1.5f
#define CT_FOOTER_MINI_RIGHT         10
#define CT_FOOTER_MINI_BOTTOM_Y      236
#define CT_FOOTER_PLINTH_H           2
#define CT_FOOTER_PLINTH_PAD         2

#define CT_WEATHER_PLACE_X           12
#define CT_WEATHER_PLACE_Y           28
#define CT_WEATHER_TEMP_X            14
#define CT_WEATHER_TEMP_Y            52
#define CT_WEATHER_HILO_X            16
#define CT_WEATHER_HILO_Y            108
#define CT_WEATHER_TEMP_FONT         48
#define CT_WEATHER_TEMP_FONT_PIL     46
#define CT_WEATHER_ICON_X            236
#define CT_WEATHER_ICON_Y            60
#define CT_WEATHER_ICON_PX           5
#define CT_WEATHER_ICON_GRID         10
#define CT_WEATHER_EMPTY_Y           80
#define CT_WEATHER_EMPTY_SUB_Y       104
#define CT_WEATHER_HORIZON           148
#define CT_WEATHER_DECK_CLOUD_Y      56
#define CT_WEATHER_DECK_WET_Y        62
#define CT_WEATHER_DECK_STORM_Y      108
#define CT_WEATHER_RAIN_W            2
#define CT_WEATHER_BOLT_PERIOD_S     4
#define CT_WEATHER_FC_COLS           5
#define CT_WEATHER_FC_COL_W          64
#define CT_WEATHER_FC_HOUR_Y         150
#define CT_WEATHER_FC_ICON_Y         166
#define CT_WEATHER_FC_ICON_PX        2
#define CT_WEATHER_FC_TEMP_Y         190
#define CT_WEATHER_FC_HOUR_FONT      12
#define CT_WEATHER_FC_TEMP_FONT      14
#define CT_WEATHER_REFRESH_S         900

#define CT_WEATHER_DECK_LUMPS_COUNT  10
static const int16_t ct_weather_deck_lumps[CT_WEATHER_DECK_LUMPS_COUNT][3] = {
    { -6,  46,  16},
    { 30,  34,  11},
    { 60,  42,  18},
    { 96,  30,  10},
    {124,  40,  15},
    {158,  34,  12},
    {188,  44,  17},
    {226,  32,  10},
    {252,  42,  16},
    {290,  44,  13},
};

#define CT_WEATHER_RAIN_COUNT        14
static const int16_t ct_weather_rain[CT_WEATHER_RAIN_COUNT][3] = {
    { 22,  76,  14},
    { 54,  92,  10},
    { 78,  70,  18},
    {108,  98,  12},
    {140,  74,  15},
    {166,  96,  11},
    {196,  72,  17},
    {222,  90,  13},
    {252,  78,  16},
    {284,  94,  10},
    { 38, 112,   9},
    {126, 118,   8},
    {208, 116,   9},
    {300, 110,   8},
};

#define CT_WEATHER_SNOW_COUNT        14
static const int16_t ct_weather_snow[CT_WEATHER_SNOW_COUNT][3] = {
    { 26,  74,   5},
    { 58,  96,   3},
    { 84,  78,   4},
    {112, 104,   3},
    {138,  82,   5},
    {162, 100,   3},
    {192,  76,   4},
    {216,  98,   3},
    {248,  84,   5},
    {278, 102,   4},
    { 42, 120,   3},
    {130, 124,   4},
    {204, 122,   3},
    {296, 118,   4},
};

#define CT_WEATHER_FOG_BANDS_COUNT   4
static const int16_t ct_weather_fog_bands[CT_WEATHER_FOG_BANDS_COUNT][2] = {
    { 68,   5},
    { 86,   7},
    {106,   9},
    {128,  12},
};

#define CT_WEATHER_BOLT_COUNT        4
static const int16_t ct_weather_bolt[CT_WEATHER_BOLT_COUNT][4] = {
    {168, 102,  11,  14},
    {157, 114,  11,  13},
    {164, 126,  10,  12},
    {153, 136,  10,  10},
};

#define CT_CRYPTO_ROWS               5
#define CT_CRYPTO_CARD_X             8
#define CT_CRYPTO_CARD_Y             26
#define CT_CRYPTO_CARD_W             304
#define CT_CRYPTO_CARD_H             93
#define CT_CRYPTO_ICON_X             18
#define CT_CRYPTO_ICON_Y             30
#define CT_CRYPTO_ICON_PX            32
#define CT_CRYPTO_SYM_X              58
#define CT_CRYPTO_SYM_BASE_Y         54
#define CT_CRYPTO_SYM_W              100
#define CT_CRYPTO_SYM_FONT           24
#define CT_CRYPTO_SYM_FONT_PIL       22
#define CT_CRYPTO_PCT_X              298
#define CT_CRYPTO_PCT_BASE_Y         52
#define CT_CRYPTO_PCT_FONT           24
#define CT_CRYPTO_PCT_FONT_PIL       22
#define CT_CRYPTO_WIN_FONT           12
#define CT_CRYPTO_WIN_FONT_PIL       10
#define CT_CRYPTO_WIN_GAP            8
#define CT_CRYPTO_WIN_Y              34
#define CT_CRYPTO_WIN_H              18
#define CT_CRYPTO_WIN_R              9
#define CT_CRYPTO_WIN_W              34
#define CT_CRYPTO_ARROW_GAP          8
#define CT_CRYPTO_ARROW_PX           5
#define CT_CRYPTO_ARROW_GRID         4
#define CT_CRYPTO_ARROW_Y            32
#define CT_CRYPTO_PRICE_X            18
#define CT_CRYPTO_PRICE_BASE_Y       102
#define CT_CRYPTO_PRICE_W            192
#define CT_CRYPTO_INT_FONT           36
#define CT_CRYPTO_INT_FONT_PIL       34
#define CT_CRYPTO_FRAC_FONT          18
#define CT_CRYPTO_FRAC_FONT_PIL      16
#define CT_CRYPTO_INT_DIGITS_MAX     6
#define CT_CRYPTO_SPARK_X            218
#define CT_CRYPTO_SPARK_Y            60
#define CT_CRYPTO_SPARK_W            80
#define CT_CRYPTO_SPARK_H            48
#define CT_CRYPTO_SPARK_COLS         16
#define CT_CRYPTO_SPARK_PITCH        5
#define CT_CRYPTO_SPARK_BAR          4
#define CT_CRYPTO_ROW_Y              122
#define CT_CRYPTO_ROW_H              21
#define CT_CRYPTO_ROW_FONT           14
#define CT_CRYPTO_ROW_FONT_PIL       12
#define CT_CRYPTO_ROW_TEXT_DY        4
#define CT_CRYPTO_ROW_ICON_X         18
#define CT_CRYPTO_ROW_ICON_DY        4
#define CT_CRYPTO_ROW_ICON_PX        16
#define CT_CRYPTO_ROW_SYM_X          38
#define CT_CRYPTO_ROW_SYM_W          52
#define CT_CRYPTO_ROW_PRICE_X        176
#define CT_CRYPTO_ROW_SPARK_X        186
#define CT_CRYPTO_ROW_SPARK_DY       3
#define CT_CRYPTO_ROW_SPARK_W        48
#define CT_CRYPTO_ROW_SPARK_H        15
#define CT_CRYPTO_ROW_SPARK_COLS     8
#define CT_CRYPTO_ROW_SPARK_PITCH    6
#define CT_CRYPTO_ROW_SPARK_BAR      5
#define CT_CRYPTO_ROW_ARROW_GAP      6
#define CT_CRYPTO_ROW_ARROW_PX       4
#define CT_CRYPTO_ROW_ARROW_GRID     4
#define CT_CRYPTO_ROW_ARROW_DY       5
#define CT_CRYPTO_ROW_PCT_X          302
#define CT_CRYPTO_HINT_X             18
#define CT_CRYPTO_HINT_Y             132
#define CT_CRYPTO_EMPTY_X            18
#define CT_CRYPTO_EMPTY_Y            80
#define CT_CRYPTO_EMPTY_SUB_Y        104
#define CT_CRYPTO_REFRESH_S          60

#define CT_STOCKS_ROWS               5
#define CT_STOCKS_CARD_X             8
#define CT_STOCKS_CARD_Y             26
#define CT_STOCKS_CARD_W             304
#define CT_STOCKS_CARD_H             93
#define CT_STOCKS_ICON_X             18
#define CT_STOCKS_ICON_Y             30
#define CT_STOCKS_ICON_PX            32
#define CT_STOCKS_SYM_X              58
#define CT_STOCKS_SYM_BASE_Y         54
#define CT_STOCKS_SYM_W              100
#define CT_STOCKS_SYM_FONT           24
#define CT_STOCKS_SYM_FONT_PIL       22
#define CT_STOCKS_PCT_X              298
#define CT_STOCKS_PCT_BASE_Y         52
#define CT_STOCKS_PCT_FONT           24
#define CT_STOCKS_PCT_FONT_PIL       22
#define CT_STOCKS_WIN_FONT           12
#define CT_STOCKS_WIN_FONT_PIL       10
#define CT_STOCKS_WIN_GAP            8
#define CT_STOCKS_WIN_Y              34
#define CT_STOCKS_WIN_H              18
#define CT_STOCKS_WIN_R              9
#define CT_STOCKS_WIN_W              46
#define CT_STOCKS_ARROW_GAP          8
#define CT_STOCKS_ARROW_PX           5
#define CT_STOCKS_ARROW_GRID         4
#define CT_STOCKS_ARROW_Y            32
#define CT_STOCKS_PRICE_X            18
#define CT_STOCKS_PRICE_BASE_Y       102
#define CT_STOCKS_PRICE_W            192
#define CT_STOCKS_INT_FONT           36
#define CT_STOCKS_INT_FONT_PIL       34
#define CT_STOCKS_FRAC_FONT          18
#define CT_STOCKS_FRAC_FONT_PIL      16
#define CT_STOCKS_INT_DIGITS_MAX     6
#define CT_STOCKS_RANGE_X            218
#define CT_STOCKS_RANGE_W            84
#define CT_STOCKS_RANGE_HI_Y         58
#define CT_STOCKS_RANGE_LO_Y         95
#define CT_STOCKS_RANGE_CAP_X        218
#define CT_STOCKS_RANGE_CAP_FONT     12
#define CT_STOCKS_RANGE_CAP_FONT_PIL 10
#define CT_STOCKS_RANGE_CAP_DY       2
#define CT_STOCKS_RANGE_VAL_X        302
#define CT_STOCKS_RANGE_VAL_FONT     14
#define CT_STOCKS_RANGE_VAL_FONT_PIL 12
#define CT_STOCKS_RANGE_RAIL_Y       83
#define CT_STOCKS_RANGE_RAIL_H       3
#define CT_STOCKS_RANGE_MARK_Y       78
#define CT_STOCKS_RANGE_MARK_W       5
#define CT_STOCKS_RANGE_MARK_H       13
#define CT_STOCKS_ROW_Y              122
#define CT_STOCKS_ROW_H              21
#define CT_STOCKS_ROW_FONT           14
#define CT_STOCKS_ROW_FONT_PIL       12
#define CT_STOCKS_ROW_TEXT_DY        4
#define CT_STOCKS_ROW_ICON_X         18
#define CT_STOCKS_ROW_ICON_DY        4
#define CT_STOCKS_ROW_ICON_PX        16
#define CT_STOCKS_ROW_SYM_X          38
#define CT_STOCKS_ROW_SYM_W          52
#define CT_STOCKS_ROW_PRICE_X        176
#define CT_STOCKS_ROW_ARROW_GAP      6
#define CT_STOCKS_ROW_ARROW_PX       4
#define CT_STOCKS_ROW_ARROW_GRID     4
#define CT_STOCKS_ROW_ARROW_DY       5
#define CT_STOCKS_ROW_PCT_X          302
#define CT_STOCKS_HINT_X             18
#define CT_STOCKS_HINT_Y             132
#define CT_STOCKS_EMPTY_X            18
#define CT_STOCKS_EMPTY_Y            80
#define CT_STOCKS_EMPTY_SUB_Y        104
#define CT_STOCKS_REFRESH_S          60

#define CT_CALENDAR_ROWS             3
#define CT_CALENDAR_LIST_ROWS        2
#define CT_CALENDAR_HERO_PAD         8
#define CT_CALENDAR_HERO_Y           26
#define CT_CALENDAR_HERO_H           96
#define CT_CALENDAR_HERO_TIME_X      20
#define CT_CALENDAR_HERO_TIME_DY     6
#define CT_CALENDAR_HERO_TIME_FONT   24
#define CT_CALENDAR_HERO_TIME_FONT_PIL 20
#define CT_CALENDAR_HERO_DAY_X       96
#define CT_CALENDAR_HERO_DAY_W       80
#define CT_CALENDAR_HERO_SUB_RIGHT   20
#define CT_CALENDAR_HERO_SUB_DY      13
#define CT_CALENDAR_HERO_SUB_W       120
#define CT_CALENDAR_HERO_TITLE_X     20
#define CT_CALENDAR_HERO_TITLE_DY    38
#define CT_CALENDAR_HERO_TITLE_W     280
#define CT_CALENDAR_HERO_TITLE_LINES 2
#define CT_CALENDAR_HERO_TITLE_LINE_H 22
#define CT_CALENDAR_CARD_DISC_X      268
#define CT_CALENDAR_CARD_DISC_Y      2
#define CT_CALENDAR_CARD_DISC_R      11
#define CT_CALENDAR_CARD_STAR_PX     2
#define CT_CALENDAR_CARD_CLOUD_H     7
#define CT_CALENDAR_CARD_CLOUD_R     3
#define CT_CALENDAR_SPINE_X          88
#define CT_CALENDAR_SPINE_W          2
#define CT_CALENDAR_SPINE_TOP        126
#define CT_CALENDAR_SPINE_PAD        4
#define CT_CALENDAR_COUNTDOWN_MAX_MIN 720
#define CT_CALENDAR_ROW_Y            128
#define CT_CALENDAR_ROW_H            37
#define CT_CALENDAR_TIME_X           12
#define CT_CALENDAR_TIME_W           68
#define CT_CALENDAR_DAY_X            12
#define CT_CALENDAR_DAY_W            68
#define CT_CALENDAR_TITLE_X          96
#define CT_CALENDAR_TITLE_W          216
#define CT_CALENDAR_TIME_FONT        14
#define CT_CALENDAR_DAY_FONT         12
#define CT_CALENDAR_TITLE_FONT       12
#define CT_CALENDAR_TIME_DY          4
#define CT_CALENDAR_DAY_DY           21
#define CT_CALENDAR_TITLE_DY         12
#define CT_CALENDAR_EMPTY_Y          80
#define CT_CALENDAR_EMPTY_SUB_Y      104
#define CT_CALENDAR_DATE_Y           76
#define CT_CALENDAR_DATE_SUB_Y       136
#define CT_CALENDAR_DATE_FONT        48
#define CT_CALENDAR_DATE_FONT_PIL    46
#define CT_CALENDAR_REFRESH_S        300

#define CT_CALENDAR_CARD_STARS_COUNT 7
static const int16_t ct_calendar_card_stars[CT_CALENDAR_CARD_STARS_COUNT][2] = {
    { 34,  16},
    {108,  10},
    {166,  24},
    {244,  12},
    {292,  40},
    { 58,  44},
    {214,  52},
};

#define CT_CALENDAR_CARD_CLOUDS_COUNT 2
static const int16_t ct_calendar_card_clouds[CT_CALENDAR_CARD_CLOUDS_COUNT][3] = {
    { 40,  36,  38},
    {196,  38,  30},
};

#define CT_MASCOT_GRID_W             16.5f
#define CT_MASCOT_GRID_H             12
#define CT_MASCOT_CORNER             0.25f

// กรอบวาดมาสคอตรวม prop (หน่วย unit) — มาจาก tools/gen/props.py
#define CT_BOX_X0                    -0.5f
#define CT_BOX_X1                    23.4f
#define CT_BOX_Y0                    -5.6f
#define CT_BOX_Y1                    12.0f

// จานสีเป็น RGB565 ตามที่แผงจอกินจริง
#define CT_COL_BG                    0x1081
#define CT_COL_BG_SLOT               0x18A2
#define CT_COL_BG_CARD_ALERT         0x28E3
#define CT_COL_BG_CARD_DONE          0x1081
#define CT_COL_BG_CARD_UP            0x1103
#define CT_COL_BG_CARD_DOWN          0x28E3
#define CT_COL_CARD_EDGE_UP          0x3B48
#define CT_COL_CARD_EDGE_DOWN        0x8A48
#define CT_COL_CLAY                  0xDBAA
#define CT_COL_CLAY_DARK             0xAAA7
#define CT_COL_CLAY_SLEEP            0x7A26
#define CT_COL_GRAY                  0x5AEB
#define CT_COL_GRAY_DARK             0x39E7
#define CT_COL_INK                   0x10A1
#define CT_COL_INK_DIM               0x3AAD
#define CT_COL_OUTLINE               0xFFFF
#define CT_COL_TEXT                  0xEF1B
#define CT_COL_TEXT_DIM              0x8C0F
#define CT_COL_ACCENT                0xEDC9
#define CT_COL_ACCENT_WARM           0xDCA4
#define CT_COL_GLASS                 0xAEDD
#define CT_COL_STEEL                 0x53B1
#define CT_COL_ALERT                 0xDAA9
#define CT_COL_GOOD                  0x5D4B
#define CT_COL_SKY_NIGHT             0x0863
#define CT_COL_SKY_DAWN              0x3A4F
#define CT_COL_SKY_DAY               0xBEFE
#define CT_COL_SKY_DUSK              0x696F
#define CT_COL_GROUND_NIGHT          0x10C3
#define CT_COL_GROUND_DAWN           0x1926
#define CT_COL_GROUND_DAY            0x29C5
#define CT_COL_GROUND_DUSK           0x20C5
#define CT_COL_SUN                   0xF525
#define CT_COL_SUN_LOW               0xEC09
#define CT_COL_MOON                  0xCE7B
#define CT_COL_STAR                  0xEF1B
#define CT_COL_STAR_MID              0xA577
#define CT_COL_STAR_DIM              0x6BD1
#define CT_COL_CLOUD_DAY             0xF7DF
#define CT_COL_CLOUD_DAWN            0x5B73
#define CT_COL_CLOUD_DUSK            0xC352
#define CT_COL_GRASS_NIGHT           0x3248
#define CT_COL_GRASS_DAWN            0x10E3
#define CT_COL_GRASS_DAY             0x6D4B
#define CT_COL_GRASS_DUSK            0x10A2
#define CT_COL_SHADOW_NIGHT          0x0862
#define CT_COL_SHADOW_DAWN           0x10A3
#define CT_COL_SHADOW_DAY            0x1923
#define CT_COL_SHADOW_DUSK           0x1884
#define CT_COL_CAL_SKY_NIGHT         0x1907
#define CT_COL_CAL_SKY_DAWN          0x3A4F
#define CT_COL_CAL_SKY_DAY           0xBEFE
#define CT_COL_CAL_SKY_DUSK          0x696F
#define CT_COL_CAL_EDGE_NIGHT        0x4AD1
#define CT_COL_CAL_EDGE_DAWN         0x7438
#define CT_COL_CAL_EDGE_DAY          0x5C97
#define CT_COL_CAL_EDGE_DUSK         0xAB17
#define CT_COL_CAL_DIM               0xBE1B
#define CT_COL_CAL_ACCENT            0xFE14
#define CT_COL_CAL_INK_ACCENT        0x8A04
#define CT_COL_CAL_LO_NIGHT          0x2169
#define CT_COL_CAL_LO_DAWN           0x4AD1
#define CT_COL_CAL_LO_DAY            0xD75F
#define CT_COL_CAL_LO_DUSK           0x79D1
#define CT_COL_CAL_GLOW_NIGHT        0x3A4E
#define CT_COL_CAL_GLOW_DAWN         0x63B5
#define CT_COL_CAL_GLOW_DAY          0xE79F
#define CT_COL_CAL_GLOW_DUSK         0x9293
#define CT_COL_CAL_DISC_NIGHT        0x5B51
#define CT_COL_CAL_DISC_DAWN         0x6BD5
#define CT_COL_CAL_DISC_DAY          0xE54B
#define CT_COL_CAL_DISC_DUSK         0x9293
#define CT_COL_WX_DECK_NIGHT         0x2147
#define CT_COL_WX_DECK_DAWN          0x5333
#define CT_COL_WX_DECK_DAY           0x8D16
#define CT_COL_WX_DECK_DUSK          0x8A53
#define CT_COL_WX_STORM_SKY          0x29A8
#define CT_COL_WX_STORM_DECK         0x10C4
#define CT_COL_WX_DULL_SKY           0xAE3A
#define CT_COL_WX_DULL_DECK          0x7C73
#define CT_COL_WX_DULL_GRASS         0x6CA9
#define CT_COL_WX_FLAKE              0xF7DF
#define CT_COL_WX_FLAKE_INK          0x6C74

// visual state — daemon ส่งค่าพวกนี้มาบน BLE ห้ามเรียงใหม่
typedef enum {
    CT_STATE_IDLE         = 0,
    CT_STATE_READING      = 1,
    CT_STATE_WRITING      = 2,
    CT_STATE_BUILDING     = 3,
    CT_STATE_SEARCHING    = 4,
    CT_STATE_THINKING     = 5,
    CT_STATE_WAITING      = 6,
    CT_STATE_SLEEPING     = 7,
    CT_STATE_ALERT        = 8,
    CT_STATE_CELEBRATE    = 9,
    CT_STATE_ERROR        = 10,
    CT_STATE_ENTERING     = 11,
    CT_STATE_LEAVING      = 12,
    CT_STATE_CONDUCTING   = 13,
    CT_STATE_BEACON       = 14,
    CT_STATE_COUNT             = 15,
} ct_state_t;

static const char *const ct_state_names[CT_STATE_COUNT] = {
    "idle",
    "reading",
    "writing",
    "building",
    "searching",
    "thinking",
    "waiting",
    "sleeping",
    "alert",
    "celebrate",
    "error",
    "entering",
    "leaving",
    "conducting",
    "beacon",
};

// ชนิดของ page — ตัวเลขเดินทางบนสาย ห้ามเรียงใหม่ (ADR-0004)
// ฝั่ง Swift คือ `PageKind` (host/Sources/TamaCore/Pages.swift) ซึ่ง tamatest
// อ่านไฟล์นี้มาเทียบ ตารางจึงมีต้นทางเดียวจริงๆ ไม่ใช่สามสำเนาที่บังเอิญตรงกัน
typedef enum {
    CT_PAGE_MASCOT       = 0,
    CT_PAGE_WEATHER      = 1,
    CT_PAGE_CRYPTO       = 2,
    CT_PAGE_CALENDAR     = 3,
    CT_PAGE_STOCKS       = 4,
    CT_PAGE_KIND_COUNT         = 5,
} ct_page_kind_t;

// ชื่อที่ขึ้นบนแถบบน — ผูกกับ enum ไม่ใช่ข้อมูลที่เดินทางมากับเฟรม หน้าที่ยังไม่เคย
// ได้ข้อมูลจึงมีชื่ออยู่แล้ว และไม่มีไบต์ไหนถูกจ่ายบนสายให้สตริงที่ไม่เคยเปลี่ยน
static const char *const ct_page_labels[CT_PAGE_KIND_COUNT] = {
    "tamaclaude",
    "weather",
    "crypto",
    "calendar",
    "stocks",
};
