#pragma once

#include <Arduino.h>
#include <limits.h>
#include "can_port.h"   // per la struct CanFrame

// Stato globale decodificato dal "DBC"
struct DbcState
{
  // ================== VCU_Display_Status (0x1088A0F1) ==================
  int16_t  soc_tot_percent       = -11; // SOC_TOT [%]
  int16_t  soc_active_percent    = -11; // SOC_ACTIVE [%]
  // TimeToFull/Empty come ricevuti (min, uint16), 0xFFFF=invalid
  uint16_t time_to_full_deci_min  = 0xFFFFU;
  uint16_t time_to_empty_deci_min = 0xFFFFU;
  int16_t  main_state            = -11; // Main state machine enum
  uint32_t status_lastUpdate_ms  = 0;   // millis ultima ricezione valida

  // ================== VCU_Display_Status_2 (0x1088A1F1) ==================
  // INV_P_AC_VECT[*] come ricevuto (unità DBC: 0.1 kW, int16)
  // Nota: usiamo INT16_MIN come "invalid" (come da DBC), per non collidere con valori negativi leciti.
  int16_t  inv_p_ac_deci_kw[3]   = { INT16_MIN, INT16_MIN, INT16_MIN };
  uint32_t status2_lastUpdate_ms = 0;   // millis ultima ricezione valida
};

// Gestisce un frame CAN secondo il nostro "DBC"
// - se il messaggio è riconosciuto, lo decodifica, aggiorna lo stato e stampa sulla seriale
// - se non è riconosciuto, lo ignora (silenzio)
void dbc_handle_frame(const CanFrame &frame);

// Accesso in sola lettura allo stato decodificato
const DbcState &dbc_get_state();
