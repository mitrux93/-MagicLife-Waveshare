# ⚠️ STOP: firmware non adatto al Waveshare Knob 1.8

**NON CARICARE il file `MagicLife_ESP32S3_FLASH_0x0.bin` generato dalla workflow di novembre 2026 sul Waveshare ESP32-S3-Knob-Touch-LCD-1.8.**

Dopo la compilazione è emerso un errore di identificazione dell'hardware: il codice attuale usa il driver ST77916 e pin di una diversa scheda JC3636W518 V2. Il Waveshare Knob 1.8 usa invece un pannello SH8601 con specifica sequenza di inizializzazione, una mappatura diversa dei GPIO e un controllo touch diverso. Anche se il codice Arduino compila, **non è stato validato sul Knob** e rischia di mostrare schermo nero e funzionamento errato.

La workflow GitHub Actions deve essere considerata solo come test di compilazione per la revisione sbagliata; non come rilascio installabile. Il firmware per Knob va riscritto con il BSP/hardware esatto e testato prima del flash.

Consulta la [scheda ufficiale Waveshare](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8) e [hardware reference SH8601](https://github.com/amitn/knobcaster/blob/main/docs/01-hardware.md).

---

# Magic Life

Segnapunti sperimentale Magic: The Gathering per Waveshare ESP32-S3 Knob Touch LCD 1.8.

Funzioni: modalità 1 e 2 giocatori, 20/40 vite, touch e ghiera. Verificare modello esatto e salvare firmware originale prima del flash.
