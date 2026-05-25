#include "dbc_decoder.h"
#include <limits.h>

static const uint32_t ID_MSG1 = 0x1088A0F1UL;
static const uint32_t ID_MSG2 = 0x1088A1F1UL;
static const uint32_t ID_MSG3 = 0x1088A2F1UL;

static DbcState g_state;

static uint16_t read_le_u16(const uint8_t *d)
{
  return (uint16_t)d[0] | ((uint16_t)d[1] << 8);
}

static int16_t read_le_i16(const uint8_t *d)
{
  return (int16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
}

// ── VCU_DISPLAY_STATUS_MSG1 ───────────────────────────────────────────────────
static void decode_msg1(const CanFrame &frame)
{
  if (frame.dlc < 7) {
    Serial.println("[DBC] MSG1: DLC < 7, ignorato");
    return;
  }
  const uint8_t *d = frame.data;

  g_state.batt_soc_tot    = (d[0] == 0xFFU) ? -1 : (int16_t)d[0];
  g_state.batt_soc_active = (d[1] == 0xFFU) ? -1 : (int16_t)d[1];
  g_state.time_to_full_min  = read_le_u16(&d[2]);   // 0 = non calcolabile
  g_state.time_to_empty_min = read_le_u16(&d[4]);   // 0 = non calcolabile
  g_state.main_state        = (d[6] == 0xFFU) ? -1 : (int16_t)d[6];
  g_state.status_lastUpdate_ms = frame.timestamp_ms;

  Serial.printf("[DBC] MSG1: SOC_TOT=%d%% SOC_ACT=%d%% TTF=%uminutes TTE=%uminutes STATE=%d\n",
      (int)g_state.batt_soc_tot, (int)g_state.batt_soc_active,
      (unsigned)g_state.time_to_full_min, (unsigned)g_state.time_to_empty_min,
      (int)g_state.main_state);
}

// ── VCU_DISPLAY_STATUS_MSG2 ───────────────────────────────────────────────────
static void decode_msg2(const CanFrame &frame)
{
  if (frame.dlc < 7) {
    Serial.println("[DBC] MSG2: DLC < 7, ignorato");
    return;
  }
  const uint8_t *d = frame.data;

  uint8_t cnt = d[6];
  if (cnt > 3) cnt = 3;   // sanity
  g_state.inv_count = cnt;

  for (int i = 0; i < 3; ++i)
    g_state.inv_p_ac_w[i] = (i < cnt) ? read_le_i16(&d[i * 2]) : 0;

  g_state.status2_lastUpdate_ms = frame.timestamp_ms;

  Serial.printf("[DBC] MSG2: INV_COUNT=%u  P_AC=[%dW, %dW, %dW]\n",
      (unsigned)cnt,
      (int)g_state.inv_p_ac_w[0],
      (int)g_state.inv_p_ac_w[1],
      (int)g_state.inv_p_ac_w[2]);
}

// ── VCU_DISPLAY_STATUS_MSG3 ───────────────────────────────────────────────────
static void decode_msg3(const CanFrame &frame)
{
  if (frame.dlc < 5) {
    Serial.println("[DBC] MSG3: DLC < 5, ignorato");
    return;
  }
  const uint8_t *d = frame.data;
  for (int i = 0; i < 4; ++i) g_state.wifi_ip[i] = d[i];
  g_state.mqtt_is_connected     = (d[4] == 1);
  g_state.status3_lastUpdate_ms = frame.timestamp_ms;

  const bool wifi_ok = d[0] || d[1] || d[2] || d[3];
  Serial.printf("[DBC] MSG3: %s  MQTT=%s\n",
      wifi_ok ? "IP=" : "WiFi non connesso",
      g_state.mqtt_is_connected ? "connesso" : "disconnesso");
  if (wifi_ok)
    Serial.printf("  IP=%u.%u.%u.%u\n", d[0], d[1], d[2], d[3]);
}

// ── Entry point ───────────────────────────────────────────────────────────────
void dbc_handle_frame(const CanFrame &frame)
{
  if (frame.rtr) return;

  if (frame.extended && frame.id == ID_MSG1) { decode_msg1(frame); return; }
  if (frame.extended && frame.id == ID_MSG2) { decode_msg2(frame); return; }
  if (frame.extended && frame.id == ID_MSG3) { decode_msg3(frame); return; }
}

const DbcState &dbc_get_state()
{
  return g_state;
}
