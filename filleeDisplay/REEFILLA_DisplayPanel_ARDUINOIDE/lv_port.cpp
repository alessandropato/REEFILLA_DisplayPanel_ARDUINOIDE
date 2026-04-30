#include <Arduino.h>
#include "lv_port.h"

// Risoluzione del display
#define LVGL_HOR_RES       480
#define LVGL_VER_RES       480
#define LVGL_BUF_LINES     40   // righe nel draw buffer (480x40)

// Buffer LVGL
static lv_color_t lvgl_buf1[LVGL_HOR_RES * LVGL_BUF_LINES];
static lv_disp_draw_buf_t lvgl_draw_buf;
static lv_disp_drv_t lvgl_disp_drv;
static lv_indev_drv_t lvgl_indev_drv;
static lv_indev_t *lvgl_indev = nullptr;

// Puntatori ai driver hardware
static ESP_PanelLcd   *s_lcd   = nullptr;
static ESP_PanelTouch *s_touch = nullptr;

// FLUSH CALLBACK -> usa lcd->drawBitmap
static void my_lvgl_flush_cb(lv_disp_drv_t *disp_drv,
                             const lv_area_t *area,
                             lv_color_t *color_p)
{
  if (!s_lcd) {
    lv_disp_flush_ready(disp_drv);
    return;
  }

  int32_t x1 = area->x1;
  int32_t y1 = area->y1;
  int32_t w  = area->x2 - area->x1 + 1;
  int32_t h  = area->y2 - area->y1 + 1;

  s_lcd->drawBitmap(x1, y1, w, h, (const uint8_t *)color_p);

  lv_disp_flush_ready(disp_drv);
}

// TOUCH CALLBACK -> usa ESP_PanelTouch
static void my_lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  (void)drv;

  if (!s_touch) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  ESP_PanelTouchPoint points[5];
  int n = s_touch->readPoints(points, 5, 0);   // timeout 0 ms: non blocca
  
  if (n > 0) {
    int16_t x = points[0].x;
    int16_t y = points[0].y;

    x = LVGL_HOR_RES - 1 - x;
    y = LVGL_VER_RES - 1 - y;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= LVGL_HOR_RES) x = LVGL_HOR_RES - 1;
    if (y >= LVGL_VER_RES) y = LVGL_VER_RES - 1;

    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void lv_port_init(ESP_PanelLcd *lcd, ESP_PanelTouch *touch)
{
  Serial.println("[lv_port] lv_init()");
  lv_init();

  s_lcd   = lcd;
  s_touch = touch;

  Serial.println("[lv_port] lv_disp_draw_buf_init()");
  lv_disp_draw_buf_init(&lvgl_draw_buf,
                        lvgl_buf1,
                        NULL,
                        LVGL_HOR_RES * LVGL_BUF_LINES);

  Serial.println("[lv_port] lv_disp_drv_init()");
  lv_disp_drv_init(&lvgl_disp_drv);
  lvgl_disp_drv.hor_res  = LVGL_HOR_RES;
  lvgl_disp_drv.ver_res  = LVGL_VER_RES;
  lvgl_disp_drv.flush_cb = my_lvgl_flush_cb;
  lvgl_disp_drv.draw_buf = &lvgl_draw_buf;
  lvgl_disp_drv.sw_rotate = 1;
  lvgl_disp_drv.rotated   = LV_DISP_ROT_180;

  Serial.println("[lv_port] lv_disp_drv_register()");
  lv_disp_t *disp = lv_disp_drv_register(&lvgl_disp_drv);
  (void)disp;

  Serial.println("[lv_port] lv_indev_drv_init()");
  lv_indev_drv_init(&lvgl_indev_drv);
  lvgl_indev_drv.type    = LV_INDEV_TYPE_POINTER;
  lvgl_indev_drv.read_cb = my_lvgl_touch_read_cb;

  Serial.println("[lv_port] lv_indev_drv_register()");
  lvgl_indev = lv_indev_drv_register(&lvgl_indev_drv);
  (void)lvgl_indev;
}
