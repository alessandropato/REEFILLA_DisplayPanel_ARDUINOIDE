#pragma once

#include <Arduino.h>
#include <limits.h>
#include "can_port.h"

struct DbcState
{
  // ================== VCU_DISPLAY_STATUS_MSG1 (0x1088A0F1) ==================
  int16_t  batt_soc_tot          = -1;      // BATT_SOC_TOT   [%], -1 = non ricevuto
  int16_t  batt_soc_active       = -1;      // BATT_SOC_ACTIVE [%], -1 = non ricevuto
  uint16_t time_to_full_min      = 0;       // TIME_TO_FULL  [min], 0 = non calcolabile
  uint16_t time_to_empty_min     = 0;       // TIME_TO_EMPTY [min], 0 = non calcolabile
  int16_t  main_state            = -1;      // MAIN_STATE enum (vedi sotto), -1 = non ricevuto
  uint32_t status_lastUpdate_ms  = 0;

  // ================== VCU_DISPLAY_STATUS_MSG2 (0x1088A1F1) ==================
  int16_t  inv_p_ac_w[3]         = {0, 0, 0}; // INV_P_AC [W], valido per i < inv_count
  uint8_t  inv_count             = 0;          // INV_COUNT: inverter validi (0–3)
  uint32_t status2_lastUpdate_ms = 0;

  // ================== VCU_DISPLAY_STATUS_MSG3 (0x1088A2F1) ==================
  uint8_t  wifi_ip[4]            = {0, 0, 0, 0}; // IP address; tutti 0x00 = WiFi non connesso
  uint32_t status3_lastUpdate_ms = 0;
};

// MAIN_STATE:
//  0  INIT
//  1  ALIGN
//  2  RUN_STANDBY
//  3  RUN_CHARGING
//  4  RUN_DISCHARGING
//  5  RUN_DISCHARGING_ONGRID
// 90  WARNING
// 99  ERROR

void dbc_handle_frame(const CanFrame &frame);
const DbcState &dbc_get_state();
