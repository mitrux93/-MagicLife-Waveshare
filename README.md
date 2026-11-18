# Magic Life v1.1 — segnapunti Magic: The Gathering

Firmware sperimentale, compilato per ESP32-S3 con display **ST77916 360 × 360** e collegamenti di riferimento **JC3636W518 V2**.

## Funzioni
- Schermata iniziale: scelta di 1 o 2 giocatori
- Punti vita iniziali: 20 o 40
- Punteggi indipendenti, regolabili con touch
- Ghiera: cambia le vite del giocatore selezionato
- Salvataggio locale dei punti vita
- Menu per iniziare una nuova partita

## ⚠️ Avvertenze hardware
Il firmware è **sperimentale**, compilare non significa che display, touch e ghiera funzionino fisicamente. La denominazione del prodotto commerciale Waveshare non basta per garantire la coincidenza della revisione del controller LCD.

**Non flashare questo BIN nel chip ESP32-U4WDH (ESP32 classico, 4MB flash).** Il dispositivo contiene un ESP32 secondario separato dall'ESP32-S3 principale. Nell'app di flashing devi controllare il rilevamento automatico del modello, non solo il valore del menu "Select Chip".

Eseguire il backup del firmware originale prima di scrivere sull'ESP32-S3. Il firmware può essere inadatto a varianti SH8601; confermare il controller leggendo le specifiche esatte della revisione.

## Build
La GitHub Action `Compila Magic Life ESP32-S3` compila il codice e pubblica un archivio ZIP con:
- `MagicLife_ESP32S3_FLASH_0x0.bin` — immagine unificata per offset `0x0`
- `MagicLife_ESP32S3_APP_0x10000.bin` — sola applicazione per offset `0x10000`
- `SHA256SUMS.txt`

La variante ST77916 è basata sulla [libreria grafica adattata](https://github.com/juggernautwins608/Waveshare-1.8-Magic-the-Gathering-Life-Counter).

## Recupero
Se il menu originale è ancora visibile, non è una prova che il chip secondario ESP32 sia intatto: significa solo che la parte del dispositivo necessaria al menu originale sta funzionando. Non cancellare altre memorie senza prima identificare il chip.

