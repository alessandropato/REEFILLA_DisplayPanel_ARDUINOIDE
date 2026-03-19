#include <lvgl.h>
#include <Arduino.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>

#include "ui_main.h"
#include "dbc_decoder.h"   // per dbc_get_state()

// -----------------------
// Oggetti LVGL
// -----------------------

// SoC gauge
static lv_obj_t *soc_arc_bg        = nullptr;
static lv_obj_t *soc_arc_fg        = nullptr;
static lv_obj_t *label_soc_value   = nullptr;
static lv_obj_t *label_soc_percent = nullptr;
static lv_obj_t *label_soc_state   = nullptr;

// Time remaining
static lv_obj_t *label_time_title = nullptr;
static lv_obj_t *label_time_value = nullptr;
static lv_obj_t *label_time_unit  = nullptr;

// Icone stato (placeholder)
static lv_obj_t *icon_warn = nullptr;
static lv_obj_t *icon_stop = nullptr;

// Line info (3 colonne in basso)
static lv_obj_t *label_line_name[3]  = { nullptr, nullptr, nullptr };
static lv_obj_t *label_line_value[3] = { nullptr, nullptr, nullptr };
static lv_obj_t *label_line_unit[3]  = { nullptr, nullptr, nullptr };

static constexpr int32_t DATA_UNAVAILABLE = -11;
static const char *UNAVAILABLE_TEXT = "-11";
static constexpr int16_t INV_P_AC_INVALID = INT16_MIN;
static constexpr uint16_t TIME_INVALID = 0xFFFFU;

// ----------------------------------------------------
// Helper: stringa stato principale
// ----------------------------------------------------
static const char *state_to_str(int16_t state)
{
  switch (state)
  {
    case 0:  return "TURN ON";
    case 1:  return "WAKE BMS";
    case 2:  return "RECOVERY";
    case 3:  return "RUN CHARGE";
    case 4:  return "RUN DISCH";
    case 5:  return "RUN STBY";
    case 6:  return "RUN DISCH OG";
    case 99: return "ERROR";
    default: return "UNKNOWN";
  }
}

// ----------------------------------------------------
// FUNZIONE PUBBLICA: aggiorna la UI dai dati DBC
// ----------------------------------------------------
void ui_main_update()
{
  const DbcState &s = dbc_get_state();
  const bool status_valid  = s.status_lastUpdate_ms  != 0;
  const bool status2_valid = s.status2_lastUpdate_ms != 0;

  // ---------- SOC centrale ----------
  auto clamp_soc = [](int16_t value) -> uint8_t {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return static_cast<uint8_t>(value);
  };

  // Usa sempre SOC_ACTIVE come valore principale.
  const bool soc_active_valid = status_valid && s.soc_active_percent >= 0;
  const bool soc_valid = soc_active_valid;

  uint8_t soc = 0;
  if (soc_active_valid) {
    soc = clamp_soc(s.soc_active_percent);
  }

  lv_color_t accent_col;
  if (!soc_valid) {
    accent_col = lv_palette_main(LV_PALETTE_BLUE);
  } else if (soc < 10) {
    accent_col = lv_palette_main(LV_PALETTE_RED);
  } else if (soc < 40) {
    accent_col = lv_palette_main(LV_PALETTE_YELLOW);
  } else {
    accent_col = lv_palette_main(LV_PALETTE_BLUE);
  }

  if (soc_arc_fg) {
    lv_arc_set_value(soc_arc_fg, soc);
    lv_obj_set_style_arc_color(soc_arc_fg, accent_col, LV_PART_INDICATOR);
  }

  if (label_soc_value) {
    if (!soc_valid) {
      lv_label_set_text(label_soc_value, UNAVAILABLE_TEXT);
    } else {
      char buf[16];
      snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(soc));
      lv_label_set_text(label_soc_value, buf);
    }
  }

  if (label_soc_percent) {
    lv_label_set_text(label_soc_percent, "%");
  }

  if (label_soc_state) {
    if (!status_valid || s.main_state < 0) {
      lv_label_set_text(label_soc_state, UNAVAILABLE_TEXT);
    } else {
      const char *mode = state_to_str(s.main_state);
      char buf[32];
      size_t len = strlen(mode);
      if (len >= sizeof(buf)) len = sizeof(buf) - 1;
      for (size_t i = 0; i < len; ++i) {
        buf[i] = toupper((unsigned char)mode[i]);
      }
      buf[len] = '\0';
      lv_label_set_text(label_soc_state, buf);
    }
  }

  auto set_icon_visible = [](lv_obj_t *obj, bool visible) {
    if (!obj) return;
    if (visible) {
      lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  };

  const bool warn_visible = status_valid && (s.main_state == 2);
  const bool stop_visible = status_valid && (s.main_state == 99);

  set_icon_visible(icon_warn, warn_visible);
  set_icon_visible(icon_stop, stop_visible);

  // ---------- Tempo rimanente ----------
  // Il tempo ricevuto è in minuti (uint16), 0xFFFF=invalid.
  auto pick_time_min = [&]() -> uint16_t {
    if (!status_valid) return TIME_INVALID;

    const bool ttf_valid = s.time_to_full_deci_min  != TIME_INVALID;
    const bool tte_valid = s.time_to_empty_deci_min != TIME_INVALID;

    if (s.main_state == 3 && ttf_valid) return s.time_to_full_deci_min;
    if ((s.main_state == 4 || s.main_state == 6) && tte_valid) return s.time_to_empty_deci_min;
    if (tte_valid) return s.time_to_empty_deci_min;
    if (ttf_valid) return s.time_to_full_deci_min;
    return TIME_INVALID;
  };

  uint16_t time_min = pick_time_min();

  if (label_time_value) {
    if (time_min == TIME_INVALID) {
      lv_label_set_text(label_time_value, UNAVAILABLE_TEXT);
    } else {
      if (time_min > (16U * 60U)) {
        lv_label_set_text(label_time_value, ">16h");
      } else {
        char buf[16];
        uint16_t hours = static_cast<uint16_t>(time_min / 60U);
        uint16_t mins  = static_cast<uint16_t>(time_min % 60U);
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)hours, (unsigned)mins);
        lv_label_set_text(label_time_value, buf);
      }
    }
  }

  if (label_time_unit) {
    lv_label_set_text(label_time_unit, "");
  }

  // ---------- Linee inferiori (placeholder: valori attuali) ----------
  auto set_line_value = [&](int idx, bool valid, int32_t value) {
    if (!label_line_value[idx]) return;
    if (!valid) {
      lv_label_set_text(label_line_value[idx], UNAVAILABLE_TEXT);
      return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    lv_label_set_text(label_line_value[idx], buf);
  };

  for (int i = 0; i < 3; ++i) {
    bool valid = status2_valid && s.inv_p_ac_deci_kw[i] != INV_P_AC_INVALID;
    set_line_value(i, valid, s.inv_p_ac_deci_kw[i]);
  }

  for (int i = 0; i < 3; ++i) {
    if (label_line_unit[i]) {
      lv_label_set_text(label_line_unit[i], "W");
    }
  }
}

// ----------------------------------------------------
// Inizializzazione UI principale
// ----------------------------------------------------
void ui_main_init()
{
  Serial.println("[ui_main] Creo UI principale stile single-panel");

  lv_obj_t *scr = lv_scr_act();

  // Sfondo teal Pantone 326C
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x00B2A9), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // --------- Container scrollabile solo orizzontalmente ---------
  lv_obj_t *pages = lv_obj_create(scr);
  lv_obj_remove_style_all(pages);
  lv_obj_set_size(pages, lv_pct(100), lv_pct(100));
  lv_obj_center(pages);
  lv_obj_set_scroll_dir(pages, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(pages, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(pages, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_flex_flow(pages, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pages,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pages, 0, 0);

  lv_obj_t *page_main = lv_obj_create(pages);
  lv_obj_remove_style_all(page_main);
  lv_obj_set_size(page_main, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(page_main, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(page_main, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(page_main, LV_DIR_NONE);

  lv_obj_t *page_secondary = lv_obj_create(pages);
  lv_obj_remove_style_all(page_secondary);
  lv_obj_set_size(page_secondary, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(page_secondary, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(page_secondary, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(page_secondary, LV_DIR_NONE);
  lv_obj_t *page2_label = lv_label_create(page_secondary);
  lv_label_set_text(page2_label, "COMING SOON");
  lv_obj_set_style_text_color(page2_label, lv_color_white(), 0);
  lv_obj_center(page2_label);

  // --------- Riga superiore: testo tempo, valore, icone ---------
  lv_obj_t *top_row = lv_obj_create(page_main);
  lv_obj_remove_style_all(top_row);
  lv_obj_set_size(top_row, lv_pct(100), 90);
  lv_obj_align(top_row, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top_row,
                        LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_left(top_row, 20, 0);
  lv_obj_set_style_pad_right(top_row, 20, 0);

  lv_obj_t *time_block = lv_obj_create(top_row);
  lv_obj_remove_style_all(time_block);
  lv_obj_set_size(time_block, 200, LV_SIZE_CONTENT);
  lv_obj_set_style_text_color(time_block, lv_color_white(), 0);
  lv_obj_set_style_text_align(time_block, LV_TEXT_ALIGN_LEFT, 0);

  label_time_title = lv_label_create(time_block);
  lv_label_set_text(label_time_title, "TIME\nREMAINING");
  lv_obj_set_style_text_font(label_time_title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(label_time_title, lv_color_white(), 0);
  lv_obj_set_style_text_align(label_time_title, LV_TEXT_ALIGN_LEFT, 0);

  lv_obj_t *icon_row = lv_obj_create(top_row);
  lv_obj_remove_style_all(icon_row);
  lv_obj_set_flex_flow(icon_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(icon_row,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(icon_row, 16, 0);

  // --------- Icone stato ---------
  icon_warn = lv_obj_create(icon_row);
  lv_obj_remove_style_all(icon_warn);
  lv_obj_set_size(icon_warn, 46, 46);
  lv_obj_set_style_radius(icon_warn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(icon_warn, lv_color_hex(0xFFC857), 0);
  lv_obj_set_style_bg_opa(icon_warn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(icon_warn, 12, 0);
  lv_obj_set_style_shadow_opa(icon_warn, LV_OPA_30, 0);
  lv_obj_t *warn_label = lv_label_create(icon_warn);
  lv_label_set_text(warn_label, "!");
  lv_obj_set_style_text_color(warn_label, lv_color_black(), 0);
  lv_obj_center(warn_label);

  icon_stop = lv_obj_create(icon_row);
  lv_obj_remove_style_all(icon_stop);
  lv_obj_set_size(icon_stop, 46, 46);
  lv_obj_set_style_radius(icon_stop, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(icon_stop, lv_color_hex(0xF05454), 0);
  lv_obj_set_style_bg_opa(icon_stop, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(icon_stop, 12, 0);
  lv_obj_set_style_shadow_opa(icon_stop, LV_OPA_30, 0);
  lv_obj_t *stop_label = lv_label_create(icon_stop);
  lv_label_set_text(stop_label, "X");
  lv_obj_set_style_text_color(stop_label, lv_color_white(), 0);
  lv_obj_center(stop_label);

  // --------- Valore tempo centrato orizzontalmente ---------
  lv_obj_t *time_value_block = lv_obj_create(page_main);
  lv_obj_remove_style_all(time_value_block);
  lv_obj_set_size(time_value_block, 140, 90);
  lv_obj_align(time_value_block, LV_ALIGN_TOP_MID, 0, 12);
  lv_obj_set_flex_flow(time_value_block, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(time_value_block,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_text_color(time_value_block, lv_color_white(), 0);
  lv_obj_move_foreground(time_value_block);

  label_time_value = lv_label_create(time_value_block);
  lv_label_set_text(label_time_value, UNAVAILABLE_TEXT);
  lv_obj_set_style_text_font(label_time_value, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_align(label_time_value, LV_TEXT_ALIGN_CENTER, 0);

  label_time_unit = lv_label_create(time_value_block);
  lv_label_set_text(label_time_unit, "min");
  lv_obj_set_style_text_font(label_time_unit, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(label_time_unit, LV_TEXT_ALIGN_CENTER, 0);

  // --------- Gauge circolare ---------
  const lv_coord_t arc_size = 260;

  soc_arc_bg = lv_arc_create(page_main);
  lv_obj_set_size(soc_arc_bg, arc_size, arc_size);
  lv_arc_set_range(soc_arc_bg, 0, 100);
  lv_arc_set_rotation(soc_arc_bg, 270);
  lv_arc_set_bg_angles(soc_arc_bg, 20, 340);
  lv_arc_set_mode(soc_arc_bg, LV_ARC_MODE_REVERSE);
  lv_arc_set_value(soc_arc_bg, 100);
  lv_obj_align(soc_arc_bg, LV_ALIGN_CENTER, 0, -10);
  lv_obj_clear_flag(soc_arc_bg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(soc_arc_bg, 26, LV_PART_MAIN);
  lv_obj_set_style_arc_color(soc_arc_bg, lv_color_hex(0x007E81), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(soc_arc_bg, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_opa(soc_arc_bg, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(soc_arc_bg, LV_OPA_TRANSP, LV_PART_KNOB);

  soc_arc_fg = lv_arc_create(page_main);
  lv_obj_set_size(soc_arc_fg, arc_size, arc_size);
  lv_arc_set_range(soc_arc_fg, 0, 100);
  lv_arc_set_rotation(soc_arc_fg, 270);
  lv_arc_set_bg_angles(soc_arc_fg, 20, 340);
  lv_arc_set_mode(soc_arc_fg, LV_ARC_MODE_REVERSE);
  lv_arc_set_value(soc_arc_fg, 0);
  lv_obj_align(soc_arc_fg, LV_ALIGN_CENTER, 0, -10);
  lv_obj_clear_flag(soc_arc_fg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(soc_arc_fg, 26, LV_PART_MAIN);
  lv_obj_set_style_arc_color(soc_arc_fg, lv_color_hex(0x00585B), LV_PART_MAIN);
  lv_obj_set_style_arc_width(soc_arc_fg, 26, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(soc_arc_fg, lv_palette_lighten(LV_PALETTE_CYAN, 2), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(soc_arc_fg, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(soc_arc_fg, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_move_foreground(soc_arc_fg);

  // --------- Testi centrali ---------
  lv_obj_t *soc_center = lv_obj_create(page_main);
  lv_obj_remove_style_all(soc_center);
  lv_obj_set_size(soc_center, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(soc_center, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_flex_flow(soc_center, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(soc_center,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(soc_center, 6, 0);
  lv_obj_set_style_text_align(soc_center, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(soc_center, lv_color_white(), 0);
  lv_obj_set_style_text_opa(soc_center, LV_OPA_COVER, 0);
  lv_obj_move_foreground(soc_center);

  label_soc_value = lv_label_create(soc_center);
  lv_label_set_text(label_soc_value, UNAVAILABLE_TEXT);
  lv_obj_set_style_text_font(label_soc_value, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(label_soc_value, lv_color_white(), 0);
  lv_obj_set_style_text_opa(label_soc_value, LV_OPA_COVER, 0);

  label_soc_percent = lv_label_create(soc_center);
  lv_label_set_text(label_soc_percent, "%");
  lv_obj_set_style_text_font(label_soc_percent, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(label_soc_percent, lv_color_white(), 0);
  lv_obj_set_style_text_opa(label_soc_percent, LV_OPA_COVER, 0);

  label_soc_state = lv_label_create(soc_center);
  lv_label_set_text(label_soc_state, UNAVAILABLE_TEXT);
  lv_obj_set_style_text_color(label_soc_state, lv_color_white(), 0);
  lv_obj_set_style_text_opa(label_soc_state, LV_OPA_COVER, 0);

  // --------- Fascia inferiore con 3 colonne ---------
  lv_obj_t *bottom_row = lv_obj_create(page_main);
  lv_obj_remove_style_all(bottom_row);
  lv_obj_set_size(bottom_row, lv_pct(100), 140);
  lv_obj_align(bottom_row, LV_ALIGN_BOTTOM_MID, 0, 8);
  lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bottom_row,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bottom_row, 30, 0);
  lv_obj_set_style_pad_left(bottom_row, 20, 0);
  lv_obj_set_style_pad_right(bottom_row, 20, 0);

  const char *line_titles[3] = { "LINE 1", "LINE 2", "LINE 3" };
  for (int i = 0; i < 3; ++i) {
    lv_obj_t *col = lv_obj_create(bottom_row);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_text_align(col, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(col, lv_color_white(), 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, lv_pct(100));

    label_line_name[i] = lv_label_create(col);
    lv_label_set_text(label_line_name[i], line_titles[i]);

    label_line_value[i] = lv_label_create(col);
    lv_label_set_text(label_line_value[i], UNAVAILABLE_TEXT);
    lv_obj_set_style_text_font(label_line_value[i], &lv_font_montserrat_32, 0);

    label_line_unit[i] = lv_label_create(col);
    lv_label_set_text(label_line_unit[i], "W");
  }

  Serial.println("[ui_main] UI stile gauge pronta");
}
