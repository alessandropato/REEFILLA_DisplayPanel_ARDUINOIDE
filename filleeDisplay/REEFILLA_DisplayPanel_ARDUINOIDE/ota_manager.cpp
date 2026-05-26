#include "ota_manager.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <lvgl.h>

LV_FONT_DECLARE(font_orbitron_14);
LV_FONT_DECLARE(font_orbitron_28);

// ── Palette (stessa di ui_main) ───────────────────────────────────────────────
#define OTA_CLR_BG       0x0A0F1A
#define OTA_CLR_DIM      0x252C3B
#define OTA_CLR_LIME     0xD2F68C
#define OTA_CLR_WHITE    0xFDFBF2
#define OTA_CLR_TEXT_DIM 0x636873
#define OTA_CLR_YELLOW   0xF0B429
#define OTA_CLR_RED      0xEF4444
#define OTA_CLR_GREEN    0x4ADE80

// ── Oggetti UI della schermata di avvio ──────────────────────────────────────
static lv_obj_t *s_lbl_status   = nullptr;
static lv_obj_t *s_lbl_progress = nullptr;
static lv_obj_t *s_bar_progress = nullptr;

// ── Helpers LVGL ─────────────────────────────────────────────────────────────

static void lv_pump(uint32_t ms)
{
    lv_tick_inc(ms);
    lv_timer_handler();
}

static void set_status(const char *msg, lv_color_t color)
{
    if (!s_lbl_status) return;
    lv_label_set_text(s_lbl_status, msg);
    lv_obj_set_style_text_color(s_lbl_status, color, 0);
    lv_pump(5);
}

// ── Progress callback per HTTPUpdate ─────────────────────────────────────────

static void ota_progress_cb(int cur, int total)
{
    if (!s_bar_progress || !s_lbl_progress) return;
    const int pct = (total > 0) ? (int)((long)cur * 100 / total) : 0;

    static int last_pct = -1;
    if (pct == last_pct) return;
    last_pct = pct;

    // Solo aggiornamento memoria: nessun flush SPI durante il download OTA.
    // Durante esp_ota_write() le flash write bloccano il bus QSPI e possono
    // interrompere i trasferimenti SPI del display → display corrotto.
    // Il refresh avviene una volta sola dopo che httpUpdate termina e il
    // WiFi è spento.
    lv_bar_set_value(s_bar_progress, pct, LV_ANIM_OFF);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(s_lbl_progress, buf);
}

// ── Parsing versione dal JSON {"version":"X.Y.Z"} ────────────────────────────

static bool parse_remote_version(const String &json, char *out, size_t max)
{
    int idx = json.indexOf("\"version\"");
    if (idx < 0) return false;
    idx = json.indexOf('"', idx + 9);
    if (idx < 0) return false;
    idx++;
    const int end = json.indexOf('"', idx);
    if (end < 0 || end - idx >= (int)max) return false;
    json.substring(idx, end).toCharArray(out, max);
    return true;
}

static bool version_newer(const char *remote)
{
    int rm, ri, rp, lm, li, lp;
    if (sscanf(remote,                    "%d.%d.%d", &rm, &ri, &rp) != 3) return false;
    if (sscanf(DISPLAY_FIRMWARE_VERSION,  "%d.%d.%d", &lm, &li, &lp) != 3) return false;
    if (rm != lm) return rm > lm;
    if (ri != li) return ri > li;
    return rp > lp;
}

// ── Costruzione schermata boot OTA ────────────────────────────────────────────

static void build_boot_screen()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(OTA_CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Versione firmware – top-left
    lv_obj_t *lv = lv_label_create(scr);
    lv_label_set_text(lv, "v" DISPLAY_FIRMWARE_VERSION);
    lv_obj_set_style_text_font(lv, &font_orbitron_14, 0);
    lv_obj_set_style_text_color(lv, lv_color_hex(OTA_CLR_TEXT_DIM), 0);
    lv_obj_set_pos(lv, 6, 3);

    // Contenitore centrale (colonna flex)
    lv_obj_t *cnt = lv_obj_create(scr);
    lv_obj_remove_style_all(cnt);
    lv_obj_set_size(cnt, 400, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cnt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cnt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cnt, 18, 0);
    lv_obj_set_scroll_dir(cnt, LV_DIR_NONE);
    lv_obj_align(cnt, LV_ALIGN_CENTER, 0, 20);

    // Titolo
    lv_obj_t *title = lv_label_create(cnt);
    lv_label_set_text(title, "REEFILLA");
    lv_obj_set_style_text_font(title, &font_orbitron_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(OTA_CLR_LIME), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    // Sottotitolo
    lv_obj_t *sub = lv_label_create(cnt);
    lv_label_set_text(sub, "DISPLAY PANEL");
    lv_obj_set_style_text_font(sub, &font_orbitron_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(OTA_CLR_TEXT_DIM), 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);

    // Stato
    s_lbl_status = lv_label_create(cnt);
    lv_label_set_text(s_lbl_status, "Avvio...");
    lv_obj_set_style_text_font(s_lbl_status, &font_orbitron_14, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(OTA_CLR_WHITE), 0);
    lv_obj_set_style_text_align(s_lbl_status, LV_TEXT_ALIGN_CENTER, 0);

    // Barra progresso – posizione assoluta su scr (fuori dal flex)
    // Larghezza fissa: evita re-layout cascade quando il valore cambia
    s_bar_progress = lv_bar_create(scr);
    lv_obj_set_size(s_bar_progress, 300, 10);
    lv_obj_set_pos(s_bar_progress, (480 - 300) / 2, 400);
    lv_obj_set_style_bg_color(s_bar_progress, lv_color_hex(OTA_CLR_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar_progress, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_progress, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_progress, lv_color_hex(OTA_CLR_LIME), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar_progress, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_progress, 5, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar_progress, 0, 100);
    lv_bar_set_value(s_bar_progress, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_bar_progress, LV_OBJ_FLAG_HIDDEN);

    // Label percentuale – larghezza fissa (evita resize quando "0%"→"100%")
    s_lbl_progress = lv_label_create(scr);
    lv_label_set_text(s_lbl_progress, "");
    lv_obj_set_size(s_lbl_progress, 80, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_lbl_progress, (480 - 80) / 2, 416);
    lv_obj_set_style_text_font(s_lbl_progress, &font_orbitron_14, 0);
    lv_obj_set_style_text_color(s_lbl_progress, lv_color_hex(OTA_CLR_LIME), 0);
    lv_obj_set_style_text_align(s_lbl_progress, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_lbl_progress, LV_OBJ_FLAG_HIDDEN);
}

// ── Flusso principale OTA ─────────────────────────────────────────────────────

void ota_run()
{
    build_boot_screen();
    lv_pump(30);

    // Se nessuna credenziale WiFi configurata, salta subito
    if (strlen(OTA_WIFI_SSID) == 0) {
        set_status("WiFi non configurato", lv_color_hex(OTA_CLR_TEXT_DIM));
        delay(1200);
        lv_pump(5);
        lv_obj_clean(lv_scr_act());
        return;
    }

    // Connessione WiFi
    set_status("Connessione WiFi...", lv_color_hex(OTA_CLR_WHITE));
    WiFi.mode(WIFI_STA);
    WiFi.begin(OTA_WIFI_SSID, OTA_WIFI_PASSWORD);

    const uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
        lv_pump(100);
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        set_status("WiFi non disponibile", lv_color_hex(OTA_CLR_TEXT_DIM));
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(1000);
        lv_pump(5);
        lv_obj_clean(lv_scr_act());
        return;
    }

    set_status("Controllo aggiornamenti...", lv_color_hex(OTA_CLR_WHITE));
    lv_pump(5);

    // Leggi version.json da GitHub Pages
    WiFiClientSecure client;
    client.setInsecure();

    char remote_version[32] = {};
    bool got_version = false;

    {
        HTTPClient http;
        if (http.begin(client, OTA_VERSION_URL)) {
            const int code = http.GET();
            if (code == 200) {
                got_version = parse_remote_version(http.getString(), remote_version, sizeof(remote_version));
            }
            http.end();
        }
    }
    lv_pump(5);

    if (!got_version) {
        set_status("Controllo aggiornamenti fallito", lv_color_hex(OTA_CLR_YELLOW));
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(1500);
        lv_pump(5);
        lv_obj_clean(lv_scr_act());
        return;
    }

    if (!version_newer(remote_version)) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Firmware aggiornato (v%s)", DISPLAY_FIRMWARE_VERSION);
        set_status(msg, lv_color_hex(OTA_CLR_GREEN));
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(1500);
        lv_pump(5);
        lv_obj_clean(lv_scr_act());
        return;
    }

    // Nuova versione disponibile – avvia aggiornamento
    char msg[64];
    snprintf(msg, sizeof(msg), "Aggiornamento v%s...", remote_version);
    set_status(msg, lv_color_hex(OTA_CLR_LIME));
    lv_obj_clear_flag(s_bar_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_progress, LV_OBJ_FLAG_HIDDEN);
    lv_pump(20);

    httpUpdate.onProgress(ota_progress_cb);
    httpUpdate.rebootOnUpdate(false);

    WiFiClientSecure client2;
    client2.setInsecure();
    const t_httpUpdate_return ret = httpUpdate.update(client2, OTA_BIN_URL);

    // WiFi off prima di qualsiasi refresh display: evita conflitti SPI/flash
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    switch (ret) {
        case HTTP_UPDATE_OK:
            lv_bar_set_value(s_bar_progress, 100, LV_ANIM_OFF);
            lv_label_set_text(s_lbl_progress, "100%");
            set_status("Aggiornamento completato! Riavvio...", lv_color_hex(OTA_CLR_LIME));
            lv_pump(50);
            delay(1000);
            ESP.restart();
            break;

        case HTTP_UPDATE_FAILED:
            snprintf(msg, sizeof(msg), "Errore: %s", httpUpdate.getLastErrorString().c_str());
            set_status(msg, lv_color_hex(OTA_CLR_RED));
            delay(2000);
            lv_pump(5);
            lv_obj_clean(lv_scr_act());
            break;

        case HTTP_UPDATE_NO_UPDATES:
        default:
            set_status("Nessun aggiornamento disponibile", lv_color_hex(OTA_CLR_TEXT_DIM));
            delay(1500);
            lv_pump(5);
            lv_obj_clean(lv_scr_act());
            break;
    }
}
