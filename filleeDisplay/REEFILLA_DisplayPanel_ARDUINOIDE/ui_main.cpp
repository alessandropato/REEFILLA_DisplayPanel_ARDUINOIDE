#include <lvgl.h>
#include <Arduino.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <esp_heap_caps.h>

#include "ui_main.h"
#include "dbc_decoder.h"

LV_FONT_DECLARE(font_orbitron_14);
LV_FONT_DECLARE(font_orbitron_20);
LV_FONT_DECLARE(font_orbitron_28);
LV_FONT_DECLARE(font_orbitron_48);

// ── Palette ──────────────────────────────────────────────────────────────────
#define CLR_BG       0x0A0F1A
#define CLR_DIM      0x252C3B
#define CLR_PURPLE   0x6666C5
#define CLR_LIME     0xD2F68C
#define CLR_WHITE    0xFDFBF2
#define CLR_YELLOW   0xF0B429
#define CLR_RED      0xEF4444
#define CLR_TEAL     0x22D3EE
#define CLR_TEXT_DIM 0x636873

// ── Layout ───────────────────────────────────────────────────────────────────
// WiFi bar :  y=  0, h=20
// Divider  :  y= 20, h= 1
// Top bar  :  y= 21, h=57  (→ y=78)
// Divider  :  y= 78, h= 1
// Stage    :  y= 79, h=311 (→ y=390)
// Power row:  y=390, h=90  (→ y=480)
static constexpr lv_coord_t Y_TOPBAR   =  21;
static constexpr lv_coord_t H_TOPBAR   =  57;
static constexpr lv_coord_t Y_STAGE    =  79;
static constexpr lv_coord_t H_STAGE    = 311;
static constexpr lv_coord_t Y_POWERROW = 390;
static constexpr lv_coord_t H_POWERROW =  90;

// ── V monogram – geometria in canvas coords ───────────────────────────────────
// Coordinate derivate dal file REEFILLA_Display_Preview.html (viewBox 0 0 480 311.5).
// Gradiente verticale: top della V = y_V_TOP, bottom = y_V_BOT
static constexpr lv_coord_t y_V_TOP =  46;
static constexpr lv_coord_t y_V_BOT = 260;

// ── Oggetti LVGL ─────────────────────────────────────────────────────────────
static lv_obj_t *label_wifi          = nullptr;
static lv_obj_t *label_auto_title    = nullptr;
static lv_obj_t *label_time_value    = nullptr;
static lv_obj_t *dot_status          = nullptr;
static lv_obj_t *label_status_text   = nullptr;
static lv_obj_t *mono_canvas         = nullptr;
static uint8_t  *canvas_buf          = nullptr;
static lv_obj_t *label_soc_value     = nullptr;
static lv_obj_t *label_soc_pct       = nullptr;
static lv_obj_t *label_line_title[3] = {};
static lv_obj_t *label_line_value[3] = {};
static lv_obj_t *label_line_unit[3]  = {};
static lv_obj_t *col_obj[3]          = {};
static lv_obj_t *vsep_obj[2]         = {};

// SOC dell'ultimo disegno del canvas (255 = mai disegnato)
static uint8_t g_last_drawn_soc = 255;

// ── Sentinelle ───────────────────────────────────────────────────────────────
static constexpr uint16_t TIME_INVALID = 0;
static const char        *DASHES       = "---";

// ── Helpers logica ───────────────────────────────────────────────────────────

static lv_color_t soc_color(uint8_t pct)
{
    return (pct > 20) ? lv_color_hex(CLR_PURPLE) : lv_color_hex(CLR_RED);
}

// StatusInfo con colori separati per dot e label (come nel design HTML V3.1)
struct StatusInfo {
    lv_color_t dot_color;
    lv_color_t label_color;
    const char *label;
};

static StatusInfo state_info(int16_t state)
{
    // Mapping dal design HTML:
    //   carica  → dot=accent2(purple), label=accent(lime)
    //   scarica → dot=accent2(purple), label=accent2(purple)
    //   standby → dot=dim,             label=dim
    //   warning → dot=yellow,          label=yellow
    //   errore  → dot=red,             label=red
    switch (state) {
        case 0:  return { lv_color_hex(CLR_TEXT_DIM), lv_color_hex(CLR_TEXT_DIM), "INIT"            };
        case 1:  return { lv_color_hex(CLR_TEXT_DIM), lv_color_hex(CLR_TEXT_DIM), "ALIGN"           };
        case 2:  return { lv_color_hex(CLR_TEXT_DIM), lv_color_hex(CLR_TEXT_DIM), "STANDBY"         };
        case 3:  return { lv_color_hex(CLR_PURPLE),   lv_color_hex(CLR_LIME),     "CARICA"          };
        case 4:  return { lv_color_hex(CLR_PURPLE),   lv_color_hex(CLR_PURPLE),   "SCARICA"         };
        case 5:  return { lv_color_hex(CLR_PURPLE),   lv_color_hex(CLR_PURPLE),   "SCARICA ON-GRID" };
        case 90: return { lv_color_hex(CLR_YELLOW),   lv_color_hex(CLR_YELLOW),   "WARNING"         };
        case 99: return { lv_color_hex(CLR_RED),      lv_color_hex(CLR_RED),      "ERRORE"          };
        default: return { lv_color_hex(CLR_TEXT_DIM), lv_color_hex(CLR_TEXT_DIM), "---"             };
    }
}

static uint16_t pick_time_min(const DbcState &s, bool status_valid)
{
    if (!status_valid) return TIME_INVALID;
    const bool ttf = s.time_to_full_min  != 0;
    const bool tte = s.time_to_empty_min != 0;
    if (s.main_state == 3 && ttf) return s.time_to_full_min;
    if ((s.main_state == 4 || s.main_state == 5) && tte) return s.time_to_empty_min;
    if (tte) return s.time_to_empty_min;
    if (ttf) return s.time_to_full_min;
    return TIME_INVALID;
}

struct Span {
    lv_coord_t x1;
    lv_coord_t x2;
};

static lv_color_t mix_hex(uint32_t a, uint32_t b, uint16_t num, uint16_t den)
{
    if (den == 0) return lv_color_hex(b);
    if (num > den) num = den;

    const uint8_t ar = (a >> 16) & 0xFF;
    const uint8_t ag = (a >>  8) & 0xFF;
    const uint8_t ab =  a        & 0xFF;
    const uint8_t br = (b >> 16) & 0xFF;
    const uint8_t bg = (b >>  8) & 0xFF;
    const uint8_t bb =  b        & 0xFF;

    const uint8_t r = ar + ((int16_t)br - ar) * (int32_t)num / den;
    const uint8_t g = ag + ((int16_t)bg - ag) * (int32_t)num / den;
    const uint8_t bl = ab + ((int16_t)bb - ab) * (int32_t)num / den;
    return lv_color_make(r, g, bl);
}

static lv_color_t charge_edge_color(lv_coord_t y, lv_coord_t y_cut, bool low_soc)
{
    const uint32_t lit = low_soc ? CLR_RED : CLR_LIME;
    static constexpr lv_coord_t EDGE_FADE_H = 28;

    if (y <= y_cut) return lv_color_hex(CLR_DIM);
    if (y >= y_cut + EDGE_FADE_H) return lv_color_hex(lit);
    return mix_hex(CLR_DIM, lit, (uint16_t)(y - y_cut), EDGE_FADE_H);
}

static void sort_i32(int32_t *v, int n)
{
    for (int i = 1; i < n; ++i) {
        const int32_t key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) {
            v[j + 1] = v[j];
            --j;
        }
        v[j + 1] = key;
    }
}

static void add_span(Span *spans, int &span_cnt, lv_coord_t x1, lv_coord_t x2)
{
    if (x2 <= x1) return;
    if (x1 < 0) x1 = 0;
    if (x2 > 480) x2 = 480;
    if (span_cnt >= 12) return;

    spans[span_cnt++] = { x1, x2 };
}

static void add_poly_spans(const lv_point_t *pts, int point_cnt,
                           lv_coord_t y, Span *spans, int &span_cnt)
{
    int32_t xs[12];
    int x_cnt = 0;
    const int32_t scan_y = (int32_t)y * 256 + 128;

    for (int i = 0; i < point_cnt; ++i) {
        const lv_point_t a = pts[i];
        const lv_point_t b = pts[(i + 1) % point_cnt];
        const int32_t ay = (int32_t)a.y * 256;
        const int32_t by = (int32_t)b.y * 256;
        if ((ay <= scan_y && by > scan_y) || (by <= scan_y && ay > scan_y)) {
            const int32_t dy = (int32_t)b.y - a.y;
            if (dy != 0 && x_cnt < 12) {
                xs[x_cnt++] = (int32_t)a.x * 256 +
                              ((scan_y - ay) * ((int32_t)b.x - a.x)) / dy;
            }
        }
    }

    sort_i32(xs, x_cnt);
    for (int i = 0; i + 1 < x_cnt; i += 2) {
        const lv_coord_t x1 = (lv_coord_t)(xs[i] / 256);
        const lv_coord_t x2 = (lv_coord_t)((xs[i + 1] + 255) / 256);
        add_span(spans, span_cnt, x1, x2);
    }
}

static void sort_spans(Span *spans, int n)
{
    for (int i = 1; i < n; ++i) {
        const Span key = spans[i];
        int j = i - 1;
        while (j >= 0 && spans[j].x1 > key.x1) {
            spans[j + 1] = spans[j];
            --j;
        }
        spans[j + 1] = key;
    }
}

static void draw_span_row(lv_obj_t *canvas, lv_coord_t y, lv_color_t color,
                          Span *spans, int span_cnt)
{
    if (span_cnt <= 0) return;
    sort_spans(spans, span_cnt);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color     = color;
    dsc.border_width = 0;
    dsc.radius       = 0;

    lv_coord_t x1 = spans[0].x1;
    lv_coord_t x2 = spans[0].x2;
    for (int i = 1; i < span_cnt; ++i) {
        if (spans[i].x1 <= x2) {
            if (spans[i].x2 > x2) x2 = spans[i].x2;
        } else {
            lv_canvas_draw_rect(canvas, x1, y, x2 - x1, 1, &dsc);
            x1 = spans[i].x1;
            x2 = spans[i].x2;
        }
    }
    lv_canvas_draw_rect(canvas, x1, y, x2 - x1, 1, &dsc);
}

static void draw_monogram(lv_obj_t *canvas, uint8_t soc_pct)
{
    lv_canvas_fill_bg(canvas, lv_color_hex(CLR_BG), LV_OPA_COVER);

    const lv_coord_t y_cut = (soc_pct >= 100)
        ? y_V_TOP
        : y_V_TOP + (lv_coord_t)((uint32_t)(100 - soc_pct) *
                                  (uint32_t)(y_V_BOT - y_V_TOP) / 100);
    const bool low_soc = soc_pct <= 20;

    static const lv_point_t left_arm[] = {
        {143, 46}, { 96, 46}, {221,260}, {257,260}, {266,260}, {243,220}
    };
    static const lv_point_t right_bolt[] = {
        {337, 46}, {286,135}, {303,135}, {254,221}, {360,126}, {338,126}, {384,46}
    };
    static const lv_point_t right_fill[] = {
        {304,135}, {348,137}, {254,221}
    };
    static const lv_point_t bottom_fill[] = {
        {207,157}, {161,157}, {192,210}, {221,259}, {235,259}, {240,259}, {266,259}, {253,237}
    };

    for (lv_coord_t y = y_V_TOP; y < y_V_BOT; ++y) {
        Span spans[12];
        int span_cnt = 0;

        add_poly_spans(left_arm,   6, y, spans, span_cnt);
        add_poly_spans(right_bolt, 7, y, spans, span_cnt);
        add_poly_spans(right_fill, 3, y, spans, span_cnt);
        add_poly_spans(bottom_fill, 8, y, spans, span_cnt);

        const lv_color_t row_color = (y >= y_cut)
            ? charge_edge_color(y, y_cut, low_soc)
            : lv_color_hex(CLR_DIM);
        draw_span_row(canvas, y, row_color, spans, span_cnt);
    }
}

// ── Helper label ─────────────────────────────────────────────────────────────
static lv_obj_t *make_label(lv_obj_t *parent,
                              const char *text,
                              const lv_font_t *font,
                              lv_color_t color,
                              lv_text_align_t align = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_align(lbl, align, 0);
    return lbl;
}

// ── Update ───────────────────────────────────────────────────────────────────

void ui_main_update()
{
    const DbcState &s = dbc_get_state();
    const bool status_valid  = s.status_lastUpdate_ms  != 0;
    const bool status2_valid = s.status2_lastUpdate_ms != 0;

    // ── SOC ──
    uint8_t soc = 0;
    const bool soc_valid = status_valid && s.batt_soc_active >= 0;
    if (soc_valid) {
        int16_t v = s.batt_soc_active;
        soc = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    }

    // Ridisegna il monogram solo quando il SOC cambia
    const uint8_t draw_soc = soc_valid ? soc : 0;
    if (mono_canvas && draw_soc != g_last_drawn_soc) {
        g_last_drawn_soc = draw_soc;
        draw_monogram(mono_canvas, draw_soc);
        lv_obj_invalidate(mono_canvas);
    }

    // Etichetta SOC
    if (label_soc_value) {
        const lv_color_t col = soc_valid ? soc_color(soc) : lv_color_hex(CLR_TEXT_DIM);
        if (!soc_valid) {
            lv_label_set_text(label_soc_value, DASHES);
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%u", (unsigned)soc);
            lv_label_set_text(label_soc_value, buf);
        }
        lv_obj_set_style_text_color(label_soc_value, col, 0);
        if (label_soc_pct)
            lv_obj_set_style_text_color(label_soc_pct, col, 0);
    }

    // ── Stato (dot + etichetta) ──
    if (status_valid && s.main_state >= 0) {
        const StatusInfo si = state_info(s.main_state);
        if (dot_status)        lv_obj_set_style_bg_color(dot_status, si.dot_color, 0);
        if (label_status_text) {
            lv_label_set_text(label_status_text, si.label);
            lv_obj_set_style_text_color(label_status_text, si.label_color, 0);
        }
    } else {
        if (dot_status)        lv_obj_set_style_bg_color(dot_status, lv_color_hex(CLR_TEXT_DIM), 0);
        if (label_status_text) lv_label_set_text(label_status_text, DASHES);
    }

    // ── Autonomia ──
    const uint16_t time_min = pick_time_min(s, status_valid);
    if (label_time_value) {
        if (time_min == TIME_INVALID) {
            lv_label_set_text(label_time_value, DASHES);
        } else if (time_min > (16U * 60U)) {
            lv_label_set_text(label_time_value, ">16h");
        } else {
            char buf[16];
            const uint16_t h = time_min / 60U;
            const uint16_t m = time_min % 60U;
            if (h > 0) snprintf(buf, sizeof(buf), "%uh %02um", (unsigned)h, (unsigned)m);
            else       snprintf(buf, sizeof(buf), "%um", (unsigned)m);
            lv_label_set_text(label_time_value, buf);
        }
    }

    // ── Potenza per linea ──
    bool col_valid[3];
    for (int i = 0; i < 3; ++i)
        col_valid[i] = status2_valid && i < s.inv_count;

    for (int i = 0; i < 3; ++i) {
        if (!col_obj[i]) continue;
        if (col_valid[i]) lv_obj_clear_flag(col_obj[i], LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag  (col_obj[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (vsep_obj[0]) {
        if (col_valid[0] && col_valid[1]) lv_obj_clear_flag(vsep_obj[0], LV_OBJ_FLAG_HIDDEN);
        else                              lv_obj_add_flag  (vsep_obj[0], LV_OBJ_FLAG_HIDDEN);
    }
    if (vsep_obj[1]) {
        if (col_valid[2] && (col_valid[0] || col_valid[1])) lv_obj_clear_flag(vsep_obj[1], LV_OBJ_FLAG_HIDDEN);
        else                                                 lv_obj_add_flag  (vsep_obj[1], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 3; ++i) {
        if (!label_line_value[i] || !col_valid[i]) continue;
        const int32_t val = s.inv_p_ac_w[i];
        char buf[16];
        if (val >= 0) snprintf(buf, sizeof(buf), "+%ld", (long)val);
        else          snprintf(buf, sizeof(buf), "%ld",  (long)val);
        lv_label_set_text(label_line_value[i], buf);
    }

    // ── WiFi / IP ──
    if (label_wifi) {
        const bool msg3 = s.status3_lastUpdate_ms != 0;
        if (!msg3) {
            lv_label_set_text(label_wifi, "WiFi: ---");
            lv_obj_set_style_text_color(label_wifi, lv_color_hex(CLR_TEXT_DIM), 0);
        } else {
            const uint8_t *ip = s.wifi_ip;
            const bool connected = ip[0] || ip[1] || ip[2] || ip[3];
            if (!connected) {
                lv_label_set_text(label_wifi, "WiFi: non connesso");
                lv_obj_set_style_text_color(label_wifi, lv_color_hex(CLR_TEXT_DIM), 0);
            } else {
                char buf[48];
                snprintf(buf, sizeof(buf), "Connesso al WiFi  |  %u.%u.%u.%u",
                         (unsigned)ip[0], (unsigned)ip[1],
                         (unsigned)ip[2], (unsigned)ip[3]);
                lv_label_set_text(label_wifi, buf);
                lv_obj_set_style_text_color(label_wifi, lv_color_hex(CLR_LIME), 0);
            }
        }
    }
}

// ── Init ─────────────────────────────────────────────────────────────────────
void ui_main_init()
{
    Serial.println("[ui_main] Init VOLTAB V3.1 (1)");

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    const lv_color_t col_white    = lv_color_hex(CLR_WHITE);
    const lv_color_t col_lime     = lv_color_hex(CLR_LIME);
    const lv_color_t col_purple   = lv_color_hex(CLR_PURPLE);
    const lv_color_t col_dim      = lv_color_hex(CLR_DIM);
    const lv_color_t col_text_dim = lv_color_hex(CLR_TEXT_DIM);

    // ── CANVAS monogram (primo in Z-order: dietro tutto) ─────────────────────
    // 480×311 px @ (0, 79) — buffer in PSRAM (~293 KB RGB565)
    size_t buf_sz = (size_t)480 * H_STAGE * sizeof(lv_color_t);
    canvas_buf = (uint8_t *)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    if (canvas_buf) {
        mono_canvas = lv_canvas_create(scr);
        lv_canvas_set_buffer(mono_canvas, canvas_buf, 480, H_STAGE, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_pos(mono_canvas, 0, Y_STAGE);
        lv_obj_clear_flag(mono_canvas, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        // Disegno iniziale con SOC=0 (V spenta)
        draw_monogram(mono_canvas, 0);
        g_last_drawn_soc = 0;
    } else {
        Serial.println("[ui_main] WARN: PSRAM insufficiente per il monogram canvas");
    }

    // ── WIFI BAR – in cima (y=0, h=20) ──────────────────────────────────────
    {
        lv_obj_t *wb = lv_obj_create(scr);
        lv_obj_remove_style_all(wb);
        lv_obj_set_size(wb, 480, 20);
        lv_obj_set_pos(wb, 0, 0);
        lv_obj_set_style_bg_opa(wb, LV_OPA_TRANSP, 0);
        lv_obj_set_scroll_dir(wb, LV_DIR_NONE);
        lv_obj_set_flex_flow(wb, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(wb, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label_wifi = make_label(wb, "WiFi: ---", &font_orbitron_14, col_text_dim, LV_TEXT_ALIGN_CENTER);
    }

    // ── DIVISORI orizzontali (y=20 e y=78) ───────────────────────────────────
    for (lv_coord_t y : { (lv_coord_t)20, (lv_coord_t)78 }) {
        lv_obj_t *d = lv_obj_create(scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 480, 1);
        lv_obj_set_pos(d, 0, y);
        lv_obj_set_style_bg_color(d, col_dim, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }

    // ── TOP BAR (y=21, h=57) ─────────────────────────────────────────────────

    // Sinistra: "VOLTAB" / "POWER" — centrato verticalmente nel topbar
    // Contenuto: ~20px (VOLTAB) + 9px gap + ~14px (POWER) = 43px su 57px disponibili
    {
        lv_obj_t *bc = lv_obj_create(scr);
        lv_obj_remove_style_all(bc);
        lv_obj_set_size(bc, 88, H_TOPBAR);
        lv_obj_set_pos(bc, 24, Y_TOPBAR);
        lv_obj_set_flex_flow(bc, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(bc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(bc, 9, 0);
        lv_obj_set_scroll_dir(bc, LV_DIR_NONE);
        make_label(bc, "VOLTAB", &font_orbitron_14, col_lime, LV_TEXT_ALIGN_CENTER);
        make_label(bc, "POWER",  &font_orbitron_14, col_dim,  LV_TEXT_ALIGN_CENTER);
    }

    // Centro: "AUTONOMIA" + tempo — centrato verticalmente nel topbar
    // Contenuto: ~14px (AUTONOMIA) + 7px gap + ~20px (valore) = 41px su 57px
    {
        lv_obj_t *tc = lv_obj_create(scr);
        lv_obj_remove_style_all(tc);
        lv_obj_set_size(tc, 200, H_TOPBAR);
        lv_obj_set_pos(tc, (480 - 200) / 2, Y_TOPBAR);
        lv_obj_set_flex_flow(tc, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tc, 7, 0);
        lv_obj_set_scroll_dir(tc, LV_DIR_NONE);
        label_auto_title = make_label(tc, "AUTONOMIA", &font_orbitron_14, col_white, LV_TEXT_ALIGN_CENTER);
        label_time_value = make_label(tc, DASHES,      &font_orbitron_20, col_white, LV_TEXT_ALIGN_CENTER);
    }

    // Destra: dot + etichetta stato
    // HTML V3.1(1): top:0; height:58px; align-items:center → centrato verticalmente nel topbar
    {
        lv_obj_t *sr = lv_obj_create(scr);
        lv_obj_remove_style_all(sr);
        lv_obj_set_size(sr, 180, 20);
        // Centro verticale: Y_TOPBAR + (H_TOPBAR - 20) / 2
        lv_obj_set_pos(sr, 480 - 22 - 180, Y_TOPBAR + (H_TOPBAR - 20) / 2);
        lv_obj_set_flex_flow(sr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(sr, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(sr, 6, 0);
        lv_obj_set_scroll_dir(sr, LV_DIR_NONE);

        dot_status = lv_obj_create(sr);
        lv_obj_remove_style_all(dot_status);
        lv_obj_set_size(dot_status, 10, 10);
        lv_obj_set_style_radius(dot_status, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot_status, col_text_dim, 0);
        lv_obj_set_style_bg_opa(dot_status, LV_OPA_COVER, 0);

        label_status_text = make_label(sr, DASHES, &font_orbitron_14, col_text_dim);
    }

    // ── ETICHETTE SCALA (sopra il canvas) ────────────────────────────────────
    // HTML: .scale-100 { left:226px; top:30px } e .scale-0 { left:232px; top:268.5px }
    // relative to stage (y=79)
    {
        lv_obj_t *l = make_label(scr, "100%", &font_orbitron_14, col_dim);
        lv_obj_set_pos(l, 226, Y_STAGE + 30);
    }
    {
        lv_obj_t *l = make_label(scr, "0%", &font_orbitron_14, col_dim);
        lv_obj_set_pos(l, 232, Y_STAGE + 268);
    }

    // ── TESTO SOC CENTRALE (sopra il canvas) ─────────────────────────────────
    // HTML: .center-text { top:80px } relative to stage → abs y = 79+80 = 159
    {
        lv_obj_t *sc = lv_obj_create(scr);
        lv_obj_remove_style_all(sc);
        lv_obj_set_size(sc, 200, LV_SIZE_CONTENT);
        lv_obj_set_pos(sc, (480 - 200) / 2, Y_STAGE + 80);
        lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(sc, 12, 0);
        lv_obj_set_scroll_dir(sc, LV_DIR_NONE);
        label_soc_value = make_label(sc, DASHES, &font_orbitron_48, col_purple, LV_TEXT_ALIGN_CENTER);
        label_soc_pct   = make_label(sc, "%",    &font_orbitron_28, col_purple, LV_TEXT_ALIGN_CENTER);
    }

    // ── POWER ROW (y=390, h=90) ──────────────────────────────────────────────
    {
        lv_obj_t *pr = lv_obj_create(scr);
        lv_obj_remove_style_all(pr);
        lv_obj_set_size(pr, 480, H_POWERROW);
        lv_obj_set_pos(pr, 0, Y_POWERROW);
        lv_obj_set_flex_flow(pr, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(pr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(pr, 16, 0);
        lv_obj_set_style_pad_right(pr, 16, 0);
        lv_obj_set_scroll_dir(pr, LV_DIR_NONE);

        const char *titles[3] = { "Linea 1", "Linea 2", "Linea 3" };
        for (int i = 0; i < 3; ++i) {
            if (i > 0) {
                lv_obj_t *vs = lv_obj_create(pr);
                lv_obj_remove_style_all(vs);
                lv_obj_set_size(vs, 1, 50);
                lv_obj_set_style_bg_color(vs, col_dim, 0);
                lv_obj_set_style_bg_opa(vs, LV_OPA_COVER, 0);
                lv_obj_set_flex_grow(vs, 0);
                vsep_obj[i - 1] = vs;
            }

            lv_obj_t *col = lv_obj_create(pr);
            lv_obj_remove_style_all(col);
            lv_obj_set_flex_grow(col, 1);
            lv_obj_set_height(col, H_POWERROW);
            lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_row(col, 4, 0);
            lv_obj_set_scroll_dir(col, LV_DIR_NONE);
            col_obj[i] = col;

            label_line_title[i] = make_label(col, titles[i], &font_orbitron_14, col_white,  LV_TEXT_ALIGN_CENTER);

            // Barra orizzontale 78×1 px
            {
                lv_obj_t *bar = lv_obj_create(col);
                lv_obj_remove_style_all(bar);
                lv_obj_set_size(bar, 78, 1);
                lv_obj_set_style_bg_color(bar, col_dim, 0);
                lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            }

            label_line_value[i] = make_label(col, DASHES, &font_orbitron_20, col_purple, LV_TEXT_ALIGN_CENTER);
            label_line_unit[i]  = make_label(col, "W",    &font_orbitron_14, col_dim,    LV_TEXT_ALIGN_CENTER);
        }
    }

    Serial.println("[ui_main] VOLTAB V3.1 (1) pronta");
}
