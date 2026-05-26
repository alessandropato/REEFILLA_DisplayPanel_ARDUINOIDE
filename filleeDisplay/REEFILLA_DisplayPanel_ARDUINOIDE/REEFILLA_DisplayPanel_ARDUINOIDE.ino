#include <Arduino.h>
#include "config.h"
#include "panel_port.h"
#include "lv_port.h"
#include "ota_manager.h"
#include "ui_main.h"
#include "can_port.h"

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("===== ESP32-S3 4\" PANEL - LVGL + CAN =====");
  Serial.println("Display FW v" DISPLAY_FIRMWARE_VERSION);

  if (!panel_port_init()) {
    Serial.println("ERRORE: panel_port_init() fallita, mi fermo.");
    while (true) {
      delay(1000);
    }
  }

  lv_port_init(panel_port_get_lcd(), panel_port_get_touch());

  // Schermata di boot + check aggiornamenti OTA
  // Se trova una versione più recente la installa e riavvia.
  // Altrimenti pulisce lo schermo e ritorna.
  ota_run();

  ui_main_init();

  // ---- CAN ----
  if (!can_port_init()) {
    Serial.println("ERRORE: can_port_init() fallita");
  } else {
    if (!can_port_start()) {
      Serial.println("ERRORE: can_port_start() fallita");
    } else {
      Serial.println("CAN avviata (mode NORMAL)");
    }
  }

  Serial.println("Setup completato: UI + CAN pronte.");
}

void loop()
{
  static uint32_t last_ms       = 0;
  static uint32_t last_ui_ms    = 0;

  uint32_t now = millis();

  // Tick LVGL (se in lv_conf.h hai LV_TICK_CUSTOM == 0)
  uint32_t diff = now - last_ms;
  last_ms = now;
  lv_tick_inc(diff);

  // Aggiorna UI ogni 1000 ms
  if (now - last_ui_ms >= 1000) {
    last_ui_ms = now;
    ui_main_update();
  }

  // LVGL gestisce rendering, input, animazioni, ecc.
  lv_timer_handler();

  delay(5);
}