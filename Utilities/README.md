# Setup ambiente Arduino – Waveshare ESP32-S3 4" Touch LCD (480x480)
Impostazioni fondamentali Arduino:
USB CDC On Boot        → Enabled
CPU Frequency          → 240MHz (WiFi)
Flash Mode             → QIO
Flash Frequency        → 80MHz
Flash Size             → 16MB (128Mb)
Partition Scheme       → Huge APP (3MB No OTA / 1MB SPIFFS)
PSRAM                  → OPI PSRAM

USB Mode               → Hardware CDC and JTAG
USB CDC On Boot        → Enabled


**Waveshare ESP32-S3-Touch-LCD-4** con:

- driver RGB `ESP32_Display_Panel` (Espressif, v0.1.x)
- IO-expander `ESP32_IO_Expander`
- LVGL (solo libreria, configurata ma non ancora usata nello sketch)
- file di configurazione specifici per questa board.

> Ambiente di riferimento: **macOS**, Arduino IDE 2.x.  
> Su Windows i percorsi sono analoghi (cambia solo la cartella utente).

---

## 1. Requisiti

- Arduino IDE 2.x
- Core **ESP32** by Espressif installato via Board Manager  
  (URL aggiuntiva nelle Preferenze, come da documentazione Espressif)
- Board disponibile in Arduino come:  
  `Tools → Board → ESP32 Arduino → Waveshare ESP32-S3-Touch-LCD-4`

---

## 2. Installazione librerie via Library Manager

Aprire **Arduino IDE** e:

1. Menu `Sketch → Include Library → Manage Libraries…`

2. Cercare e installare:

   ### 2.1 ESP32_Display_Panel

   - Nome: **ESP32_Display_Panel**
   - Autore: *Espressif Systems*
   - Versione consigliata: **0.1.8** (serie 0.1.x, NON le 1.x)   

   ### 2.2 ESP32_IO_Expander

   - Nome: **ESP32_IO_Expander**
   - Autore: *Espressif Systems*
   - Versione consigliata: **0.0.4** (compatibile con ESP32_Display_Panel 0.1.x).   

Per ora **NON** installare LVGL dal Library Manager; la libreria LVGL + `lv_conf.h`
verranno dalla ZIP di Waveshare.

---

## 3. Copia dei file Waveshare nella cartella `libraries`

Scaricare dal sito Waveshare il pacchetto relativo alla board
ESP32-S3-Touch-LCD-4 (Arduino demo / libraries).

All’interno della ZIP, nella parte Arduino, si trovano:

- cartella `lvgl/`
- file `lv_conf.h`
- file `ESP_Panel_Conf.h`
- (eventuali) `ESP_Panel_Board_Supported.h` e `ESP_Panel_Board_Custom.h`

Secondo le linee guida Espressif / vendor, questi file vanno copiati direttamente
a livello della cartella `libraries` di Arduino.   

Su macOS, il percorso tipico è:

```text
~/Documents/Arduino/libraries
