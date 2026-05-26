#pragma once

// Flusso OTA al boot: mostra schermata di avvio, tenta connessione WiFi,
// controlla aggiornamenti su GitHub Pages.
// Se trova una versione più recente la installa e riavvia il dispositivo.
// Altrimenti pulisce lo schermo e ritorna, pronto per ui_main_init().
void ota_run();
