#include <lvgl.h>
#include <Arduino.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "ui_main.h"
#include "dbc_decoder.h"

LV_FONT_DECLARE(font_orbitron_14);
LV_FONT_DECLARE(font_orbitron_20);
LV_FONT_DECLARE(font_orbitron_28);
LV_FONT_DECLARE(font_orbitron_48);

// ── Palette ────────────────────────────────────────────────────────────────
#define CLR_BG          0x0A0F1A
#define CLR_ACCENT      0x22C5C8  // oklch(0.75 0.18 193) ≈ teal
#define CLR_TRACK       0x1C212C  // rgba(255,255,255,0.07) on BG
#define CLR_BORDER      0x14181F  // rgba(255,255,255,0.05) on BG
#define CLR_DIVIDER     0x1A2030  // rgba(255,255,255,0.08) on BG
#define CLR_GREEN       0x4ADE80  // oklch(0.75 0.2 145)
#define CLR_YELLOW      0xF0B429  // oklch(0.78 0.18 60)
#define CLR_RED         0xEF4444  // oklch(0.65 0.22 25)
#define CLR_TEAL        0x22D3EE  // discharging teal
#define CLR_WHITE_88    0xE0E0E0  // rgba(255,255,255,0.88)
#define CLR_TEXT_DIM    0x636873  // rgba(255,255,255,0.35)
#define CLR_TEXT_VDIM   0x3D424D  // rgba(255,255,255,0.20)
#define CLR_WARM        0xC4A460  // oklch(0.75 0.12 60) — temperature

// ── LVGL objects ───────────────────────────────────────────────────────────

// Top bar
static lv_obj_t *label_brand        = nullptr;
static lv_obj_t *label_auto_title   = nullptr;
static lv_obj_t *label_time_value   = nullptr;
static lv_obj_t *dot_status         = nullptr;
static lv_obj_t *label_status_text  = nullptr;

// Arc gauge
static lv_obj_t *soc_arc_bg         = nullptr;
static lv_obj_t *soc_arc_fg         = nullptr;

// Center text
static lv_obj_t *label_soc_value    = nullptr;
static lv_obj_t *label_soc_sub      = nullptr;  // "PERCENTUALE"

// Arc edge labels
static lv_obj_t *label_arc_start    = nullptr;  // "0%"
static lv_obj_t *label_arc_end      = nullptr;  // "100%"

// Power lines (3 columns)
static lv_obj_t *label_line_title[3] = {};
static lv_obj_t *label_line_value[3] = {};
static lv_obj_t *label_line_unit[3]  = {};
static lv_obj_t *col_obj[3]          = {};   // column containers (for show/hide)
static lv_obj_t *vsep_obj[2]         = {};   // vertical separators between columns

// Bottom bar
static lv_obj_t *label_wifi          = nullptr;

// ── Sentinel values ─────────────────────────────────────────────────────────
static constexpr uint16_t TIME_INVALID = 0;   // 0 = non calcolabile (spec DBC)
static const char        *DASHES       = "---";

// ── Helpers ──────────────────────────────────────────────────────────────────

static lv_color_t soc_color(uint8_t pct)
{
    if (pct > 60) return lv_color_hex(CLR_GREEN);
    if (pct > 25) return lv_color_hex(CLR_YELLOW);
    return lv_color_hex(CLR_RED);
}

struct StatusInfo { lv_color_t color; const char *label; };
static StatusInfo state_info(int16_t state)
{
    switch (state) {
        case 0:  return { lv_color_hex(CLR_TEXT_DIM), "INIT"     };
        case 1:  return { lv_color_hex(CLR_TEXT_DIM), "ALIGN"    };
        case 2:  return { lv_color_hex(CLR_TEAL),     "STANDBY"  };
        case 3:  return { lv_color_hex(CLR_GREEN),    "CARICA"   };
        case 4:  return { lv_color_hex(CLR_TEAL),     "SCARICA"  };
        case 5:  return { lv_color_hex(CLR_TEAL),     "SCARICA ON-GRID" };
        case 90: return { lv_color_hex(CLR_YELLOW),   "WARNING"  };
        case 99: return { lv_color_hex(CLR_RED),      "ERRORE"   };
        default: return { lv_color_hex(CLR_TEXT_DIM), "---"      };
    }
}

static uint16_t pick_time_min(const DbcState &s, bool status_valid)
{
    if (!status_valid) return TIME_INVALID;
    const bool ttf = s.time_to_full_min  != 0;
    const bool tte = s.time_to_empty_min != 0;
    if (s.main_state == 3 && ttf) return s.time_to_full_min;   // RUN_CHARGING
    if ((s.main_state == 4 || s.main_state == 5) && tte) return s.time_to_empty_min; // RUN_DISCHARGING / ONGRID
    if (tte) return s.time_to_empty_min;
    if (ttf) return s.time_to_full_min;
    return TIME_INVALID;
}

// ── Update ───────────────────────────────────────────────────────────────────

void ui_main_update()
{
    const DbcState &s = dbc_get_state();
    const bool status_valid  = s.status_lastUpdate_ms  != 0;
    const bool status2_valid = s.status2_lastUpdate_ms != 0;

    // ── SOC ──
    uint8_t soc = 0;
    bool soc_valid = status_valid && s.batt_soc_active >= 0;
    if (soc_valid) {
        int16_t v = s.batt_soc_active;
        soc = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    }

    lv_color_t arc_col = soc_valid ? soc_color(soc) : lv_color_hex(CLR_ACCENT);

    if (soc_arc_fg) {
        lv_arc_set_value(soc_arc_fg, soc_valid ? soc : 0);
        lv_obj_set_style_arc_color(soc_arc_fg, arc_col, LV_PART_INDICATOR);
    }

    if (label_soc_value) {
        if (!soc_valid) {
            lv_label_set_text(label_soc_value, DASHES);
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%u", (unsigned)soc);
            lv_label_set_text(label_soc_value, buf);
        }
        lv_obj_set_style_text_color(label_soc_value, arc_col, 0);
    }

    // ── Status dot + label ──
    if (status_valid && s.main_state >= 0) {
        StatusInfo si = state_info(s.main_state);
        if (dot_status)        lv_obj_set_style_bg_color(dot_status, si.color, 0);
        if (label_status_text) {
            lv_label_set_text(label_status_text, si.label);
            lv_obj_set_style_text_color(label_status_text, si.color, 0);
        }
    } else {
        if (dot_status)        lv_obj_set_style_bg_color(dot_status, lv_color_hex(CLR_TEXT_DIM), 0);
        if (label_status_text) lv_label_set_text(label_status_text, DASHES);
    }

    // ── Time remaining ──
    uint16_t time_min = pick_time_min(s, status_valid);
    if (label_time_value) {
        if (time_min == TIME_INVALID) {
            lv_label_set_text(label_time_value, DASHES);
        } else if (time_min > (16U * 60U)) {
            lv_label_set_text(label_time_value, ">16h");
        } else {
            char buf[16];
            uint16_t h = time_min / 60U;
            uint16_t m = time_min % 60U;
            if (h > 0) snprintf(buf, sizeof(buf), "%uh %02um", (unsigned)h, (unsigned)m);
            else       snprintf(buf, sizeof(buf), "%um", (unsigned)m);
            lv_label_set_text(label_time_value, buf);
        }
    }

    // ── Power lines ──
    // Determina quali colonne hanno dati validi: solo i < inv_count
    bool col_valid[3];
    for (int i = 0; i < 3; ++i)
        col_valid[i] = status2_valid && i < s.inv_count;

    // Mostra/nascondi colonne — LVGL flex salta gli oggetti hidden
    for (int i = 0; i < 3; ++i) {
        if (!col_obj[i]) continue;
        if (col_valid[i]) lv_obj_clear_flag(col_obj[i], LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag  (col_obj[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Separatore 0 (tra col 0 e col 1): visibile solo se entrambe presenti
    if (vsep_obj[0]) {
        if (col_valid[0] && col_valid[1]) lv_obj_clear_flag(vsep_obj[0], LV_OBJ_FLAG_HIDDEN);
        else                              lv_obj_add_flag  (vsep_obj[0], LV_OBJ_FLAG_HIDDEN);
    }
    // Separatore 1 (tra col 1 e col 2): visibile se col 2 ha dati e c'è almeno una colonna a sinistra
    if (vsep_obj[1]) {
        if (col_valid[2] && (col_valid[0] || col_valid[1])) lv_obj_clear_flag(vsep_obj[1], LV_OBJ_FLAG_HIDDEN);
        else                                                 lv_obj_add_flag  (vsep_obj[1], LV_OBJ_FLAG_HIDDEN);
    }

    // Aggiorna valori nelle colonne
    for (int i = 0; i < 3; ++i) {
        if (!label_line_value[i]) continue;
        if (!col_valid[i]) continue;
        int32_t val = s.inv_p_ac_w[i];
        char buf[16];
        if (val >= 0) snprintf(buf, sizeof(buf), "+%ld", (long)val);
        else          snprintf(buf, sizeof(buf), "%ld",  (long)val);
        lv_label_set_text(label_line_value[i], buf);
        lv_obj_set_style_text_color(
            label_line_value[i],
            val < 0 ? lv_color_hex(CLR_TEAL) : lv_color_hex(CLR_GREEN),
            0);
    }

    // ── WiFi / IP ──
    if (label_wifi) {
        const bool msg3_received = s.status3_lastUpdate_ms != 0;
        if (!msg3_received) {
            lv_label_set_text(label_wifi, "WiFi: ---");
            lv_obj_set_style_text_color(label_wifi, lv_color_hex(CLR_TEXT_VDIM), 0);
        } else {
            const uint8_t *ip = s.wifi_ip;
            const bool connected = ip[0] || ip[1] || ip[2] || ip[3];
            if (!connected) {
                lv_label_set_text(label_wifi, "WiFi: non connesso");
                lv_obj_set_style_text_color(label_wifi, lv_color_hex(CLR_TEXT_DIM), 0);
            } else {
                char buf[40];
                snprintf(buf, sizeof(buf), "Connesso al WiFi  |  %u.%u.%u.%u",
                         (unsigned)ip[0], (unsigned)ip[1],
                         (unsigned)ip[2], (unsigned)ip[3]);
                lv_label_set_text(label_wifi, buf);
                lv_obj_set_style_text_color(label_wifi, lv_color_hex(CLR_ACCENT), 0);
            }
        }
    }
}

// ── Init helpers ─────────────────────────────────────────────────────────────

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

// ── Init ─────────────────────────────────────────────────────────────────────

void ui_main_init()
{
    Serial.println("[ui_main] Nuova UI REEFILLA Dashboard");

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_color_t col_accent  = lv_color_hex(CLR_ACCENT);
    lv_color_t col_white88 = lv_color_hex(CLR_WHITE_88);
    lv_color_t col_dim     = lv_color_hex(CLR_TEXT_DIM);
    lv_color_t col_vdim    = lv_color_hex(CLR_TEXT_VDIM);

    // ── TOP BAR ─────────────────────────────────────────────────────────────
    // height=58, bottom border
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, 480, 58);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_border_color(top_bar, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_side(top_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(top_bar, LV_DIR_NONE);

    // Left: "REEFILLA" + "POWER"
    lv_obj_t *brand_col = lv_obj_create(top_bar);
    lv_obj_remove_style_all(brand_col);
    lv_obj_set_size(brand_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(brand_col, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_flex_flow(brand_col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(brand_col, 5, 0);

    label_brand = make_label(brand_col, "REEFILLA", &font_orbitron_14, col_accent);
    make_label(brand_col, "POWER", &font_orbitron_14, col_vdim);

    // Center: "AUTONOMIA" title + time value
    lv_obj_t *time_col = lv_obj_create(top_bar);
    lv_obj_remove_style_all(time_col);
    lv_obj_set_size(time_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(time_col, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(time_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(time_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(time_col, 1, 0);

    label_auto_title = make_label(time_col, "AUTONOMIA", &font_orbitron_14, col_dim, LV_TEXT_ALIGN_CENTER);
    label_time_value = make_label(time_col, DASHES, &font_orbitron_20, col_white88, LV_TEXT_ALIGN_CENTER);

    // Right: status dot + label
    lv_obj_t *status_row = lv_obj_create(top_bar);
    lv_obj_remove_style_all(status_row);
    lv_obj_set_size(status_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(status_row, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_row, 6, 0);

    dot_status = lv_obj_create(status_row);
    lv_obj_remove_style_all(dot_status);
    lv_obj_set_size(dot_status, 10, 10);
    lv_obj_set_style_radius(dot_status, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot_status, col_dim, 0);
    lv_obj_set_style_bg_opa(dot_status, LV_OPA_COVER, 0);

    label_status_text = make_label(status_row, DASHES, &font_orbitron_14, col_dim);

    // ── ARC GAUGE ────────────────────────────────────────────────────────────
    // Center on screen: (240, 236) → top-left of 260×260 widget: (110, 106)
    // Rotation 120° + bg_angles(0, 300) → 300° arc, gap at bottom 60°
    const lv_coord_t ARC_SZ = 260;
    const lv_coord_t ARC_W  = 14;
    const lv_coord_t arc_x  = 240 - ARC_SZ / 2;   // 110
    const lv_coord_t arc_y  = 236 - ARC_SZ / 2;   // 106

    // Background track
    soc_arc_bg = lv_arc_create(scr);
    lv_obj_set_size(soc_arc_bg, ARC_SZ, ARC_SZ);
    lv_obj_set_pos(soc_arc_bg, arc_x, arc_y);
    lv_arc_set_rotation(soc_arc_bg, 120);
    lv_arc_set_bg_angles(soc_arc_bg, 0, 300);
    lv_arc_set_range(soc_arc_bg, 0, 100);
    lv_arc_set_value(soc_arc_bg, 100);
    lv_obj_clear_flag(soc_arc_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(soc_arc_bg, ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(soc_arc_bg, lv_color_hex(CLR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(soc_arc_bg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(soc_arc_bg, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(soc_arc_bg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(soc_arc_bg, LV_OPA_TRANSP, LV_PART_KNOB);

    // Fill arc
    soc_arc_fg = lv_arc_create(scr);
    lv_obj_set_size(soc_arc_fg, ARC_SZ, ARC_SZ);
    lv_obj_set_pos(soc_arc_fg, arc_x, arc_y);
    lv_arc_set_rotation(soc_arc_fg, 120);
    lv_arc_set_bg_angles(soc_arc_fg, 0, 300);
    lv_arc_set_range(soc_arc_fg, 0, 100);
    lv_arc_set_value(soc_arc_fg, 0);
    lv_obj_clear_flag(soc_arc_fg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(soc_arc_fg, ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(soc_arc_fg, lv_color_hex(CLR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(soc_arc_fg, ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(soc_arc_fg, col_accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(soc_arc_fg, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(soc_arc_fg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(soc_arc_fg, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_move_foreground(soc_arc_fg);

    // ── CENTER TEXT ──────────────────────────────────────────────────────────
    // Positioned over arc center (240, 236)
    lv_obj_t *center_col = lv_obj_create(scr);
    lv_obj_remove_style_all(center_col);
    lv_obj_set_size(center_col, 200, LV_SIZE_CONTENT);
    lv_obj_align(center_col, LV_ALIGN_CENTER, 0, -4);     // arc center at y=236, screen center 240
    lv_obj_set_flex_flow(center_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(center_col, 4, 0);
    lv_obj_move_foreground(center_col);

    label_soc_value = make_label(center_col, DASHES, &font_orbitron_48, col_accent, LV_TEXT_ALIGN_CENTER);

    label_soc_sub = make_label(center_col, "%", &font_orbitron_28, col_dim, LV_TEXT_ALIGN_CENTER);

    // ── ARC EDGE LABELS ──────────────────────────────────────────────────────
    // "0%"   at arc start: angle 120° from center (240,236), R=130 + gap offset
    // In SVG: 120° → x=cx+R*cos(120°)=240-65=175, y=cy+R*sin(120°)=236+112=348
    // Add a bit of offset outward to clear the arc
    // Positions from HTML: cx-118=122 and cx+118=358, y = SVG(316)+40 = 356
    // text-anchor="middle" → top-left = center_x - half_label_width, top_y = y - font_height
    label_arc_start = make_label(scr, "0%", &font_orbitron_14, col_vdim, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(label_arc_start, 113, 342);   // center x≈122, baseline y≈356

    label_arc_end = make_label(scr, "100%", &font_orbitron_14, col_vdim, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_pos(label_arc_end, 340, 342);     // center x≈358, baseline y≈356

    // ── HORIZONTAL DIVIDER ────────────────────────────────────────────────────
    lv_obj_t *divider = lv_obj_create(scr);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, 440, 1);
    lv_obj_align(divider, LV_ALIGN_BOTTOM_MID, 0, -110);
    lv_obj_set_style_bg_color(divider, lv_color_hex(CLR_DIVIDER), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    // ── POWER LINES ──────────────────────────────────────────────────────────
    // y=370 to y=460, height=90 (lascia 20px in basso per la WiFi bar)
    lv_obj_t *power_row = lv_obj_create(scr);
    lv_obj_remove_style_all(power_row);
    lv_obj_set_size(power_row, 480, 90);
    lv_obj_align(power_row, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_flex_flow(power_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(power_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(power_row, 16, 0);
    lv_obj_set_style_pad_right(power_row, 16, 0);
    lv_obj_set_scroll_dir(power_row, LV_DIR_NONE);

    const char *line_titles[3] = { "LINEA 1", "LINEA 2", "LINEA 3" };
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            lv_obj_t *vsep = lv_obj_create(power_row);
            lv_obj_remove_style_all(vsep);
            lv_obj_set_size(vsep, 1, 50);
            lv_obj_set_style_bg_color(vsep, lv_color_hex(CLR_DIVIDER), 0);
            lv_obj_set_style_bg_opa(vsep, LV_OPA_COVER, 0);
            lv_obj_set_flex_grow(vsep, 0);
            vsep_obj[i - 1] = vsep;   // vsep_obj[0] tra col0–col1, vsep_obj[1] tra col1–col2
        }

        lv_obj_t *col = lv_obj_create(power_row);
        lv_obj_remove_style_all(col);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, 90);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 2, 0);
        lv_obj_set_style_text_align(col, LV_TEXT_ALIGN_CENTER, 0);
        col_obj[i] = col;

        label_line_title[i] = make_label(col, line_titles[i], &font_orbitron_14, col_dim, LV_TEXT_ALIGN_CENTER);
        label_line_value[i] = make_label(col, DASHES, &font_orbitron_20, lv_color_hex(CLR_TEAL), LV_TEXT_ALIGN_CENTER);
        label_line_unit[i]  = make_label(col, "W", &font_orbitron_14, col_vdim, LV_TEXT_ALIGN_CENTER);
    }

    // ── WIFI BAR ─────────────────────────────────────────────────────────────
    lv_obj_t *wifi_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(wifi_bar);
    lv_obj_set_size(wifi_bar, 480, 20);
    lv_obj_align(wifi_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(wifi_bar, 1, 0);
    lv_obj_set_style_border_color(wifi_bar, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_side(wifi_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_bg_opa(wifi_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(wifi_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(wifi_bar, LV_DIR_NONE);

    label_wifi = make_label(wifi_bar, "WiFi: ---", &font_orbitron_14, col_vdim, LV_TEXT_ALIGN_CENTER);

    Serial.println("[ui_main] UI REEFILLA Dashboard pronta");
}
