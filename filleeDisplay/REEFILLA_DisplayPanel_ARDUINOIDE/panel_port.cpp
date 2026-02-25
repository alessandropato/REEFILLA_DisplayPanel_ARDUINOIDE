#include <Arduino.h>
#include "driver/i2c.h"
#include "panel_port.h"

// Oggetti statici visibili solo in questo file
static ESP_Panel        *s_panel      = nullptr;
static ESP_PanelLcd     *s_lcd        = nullptr;
static ESP_PanelTouch   *s_touch      = nullptr;
static ESP_PanelBacklight *s_backlight = nullptr;

static void v4_touch_board_prepare()
{
  // Waveshare ESP32-S3-Touch-LCD-4 V4: vendor examples always do these
  // writes to I2C device 0x24 before GT911 init.
  const int touch_sda = 15;
  const int touch_scl = 7;
  const i2c_port_t port = I2C_NUM_0;

  i2c_config_t cfg = {};
  cfg.mode = I2C_MODE_MASTER;
  cfg.sda_io_num = (gpio_num_t)touch_sda;
  cfg.scl_io_num = (gpio_num_t)touch_scl;
  cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
  cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
  cfg.master.clk_speed = 400000;

  esp_err_t err = i2c_param_config(port, &cfg);
  if (err != ESP_OK) {
    Serial.printf("[panel_port] WARN: i2c_param_config failed (%d)\n", (int)err);
    return;
  }

  err = i2c_driver_install(port, cfg.mode, 0, 0, 0);
  if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
    Serial.printf("[panel_port] WARN: i2c_driver_install failed (%d)\n", (int)err);
    return;
  }

  uint8_t cfg1[2] = {0x02, 0xFF};
  uint8_t cfg2[2] = {0x03, 0x3A};

  err = i2c_master_write_to_device(port, 0x24, cfg1, sizeof(cfg1), pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    Serial.printf("[panel_port] WARN: write 0x24 reg 0x02 failed (%d)\n", (int)err);
  }

  err = i2c_master_write_to_device(port, 0x24, cfg2, sizeof(cfg2), pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    Serial.printf("[panel_port] WARN: write 0x24 reg 0x03 failed (%d)\n", (int)err);
  }

  i2c_driver_delete(port);
}

bool panel_port_init()
{
  bool panel_begin_ok = false;

  v4_touch_board_prepare();

  Serial.println("[panel_port] Creo oggetto ESP_Panel");
  s_panel = new ESP_Panel();

  Serial.println("[panel_port] panel->init()...");
  if (!s_panel->init()) {
    Serial.println("[panel_port] ERRORE: panel->init() ha fallito.");
    return false;
  }

  Serial.println("[panel_port] panel->begin()...");
  panel_begin_ok = s_panel->begin();
  if (!panel_begin_ok) {
    Serial.println("[panel_port] ATTENZIONE: panel->begin() ha fallito.");
    Serial.println("[panel_port] Provo a continuare senza touch (solo display).");
  }

  s_lcd       = s_panel->getLcd();
  s_touch     = panel_begin_ok ? s_panel->getTouch() : nullptr;
  s_backlight = s_panel->getBacklight();

  if (!s_lcd) {
    Serial.println("[panel_port] ERRORE: lcd == nullptr (config errata).");
    return false;
  }

  if (s_backlight) {
    Serial.println("[panel_port] Accendo retroilluminazione.");
    s_backlight->on();
  }

  uint16_t w = s_panel->getLcdWidth();
  uint16_t h = s_panel->getLcdHeight();
  Serial.printf("[panel_port] Pannello %u x %u\n", w, h);

  if (s_touch) {
    Serial.println("[panel_port] Touch presente.");
  } else {
    Serial.println("[panel_port] Touch NON presente: continuo solo con LCD.");
  }

  return true;
}

// Getter
ESP_PanelLcd* panel_port_get_lcd()
{
  return s_lcd;
}

ESP_PanelTouch* panel_port_get_touch()
{
  return s_touch;
}

ESP_PanelBacklight* panel_port_get_backlight()
{
  return s_backlight;
}

ESP_Panel* panel_port_get_panel()
{
  return s_panel;
}
