# Magic Knob OS 2.0 — Alpha 3

**Stato:** codice sperimentale compilabile; NON conserva ancora tutte le app Waveshare. Non flashare sul secondo chip ESP32-U4WDH.

## Modifiche verificabili rispetto ad Alpha 2
- Segnalini: valore centrato in una finestra numerica di 84×73 px attorno al centro (180,234), dimensione font adattata a 1, 2 o 3 cifre; tipo segnalino, scelta giocatore, pulsanti touch +/−, selezione con ghiera.
- Ghiera: **decoder invariato e già verificato dall'utente**. La voce «Sensibilità ghiera» (1–4) è il numero di impulsi riconosciuti necessario a variare di uno il valore selezionato. 1 = più reattiva, 4 = più lenta. Non sono vite iniziali; in assenza di problemi lasciare **1**.
- Menu impostazioni riorganizzato in Home > Impostazioni: **Schermo**, **Ghiera**, **Batteria**, **Diagnostica**. È una struttura incrementale: menu Bluetooth, orologio, SD/multimedia e energia andranno integrati e testati.
- Schermo: luminosità 40–255; controllabile con touch o ghiera.
- Ghiera: direzione invertibile e sensibilità da 1 a 4, salvate in Preferences.
- Batteria: stima della tensione letta sul GPIO1 ADC, rapporto nominale partitore 2:1, media di 10 misure, filtro leggero, aggiornamento circa ogni 2.2 s; percentuale **indicativa**, non un fuel gauge.
- Batteria: se la tensione letta supera 4.35 V viene mostrato «LETTURA USB?» e **non** viene mostrata percentuale; se è inferiore a 2.7 V è considerata non valida/possibile batteria assente. I limiti si basano sul cablaggio documentato del pannello di riferimento: varianti e stato USB possono influire.
- Swipe sinistra→destra torna sempre alla Home dalle sottopagine; i pulsanti di ritorno restano disponibili.

## Checklist di prova sul dispositivo
1. Impostazioni > Ghiera: confermare che la sensibilità sia 1 e che i due versi restino affidabili.
2. Magic > Segnalini: cambiare tipo con frecce, aumentare/diminuire da touch, cambiare giocatore; controllare l'allineamento per 0, 9, 10, 99, 100, 999.
3. Impostazioni > Schermo: regolare la luminosità e verificare il salvataggio dopo OFF/ON.
4. Impostazioni > Batteria: annotare la tensione con USB, poi senza USB se il dispositivo ha una batteria funzionante. **Non dedurre batteria presente soltanto dalla lettura sotto USB.**
5. Controllare swipe, dadi, moneta, timer e avvio post-upload.
6. Non considerare l'indicazione della batteria una diagnosi di ricarica o di guasto: non esiste una misura software affidabile della corrente né un segnale di stato carica esposto in questi sorgenti.

## Installazione
La pipeline GitHub Actions produce `MagicKnobOS_v2alpha3_ESP32S3_FLASH_0x0.bin` e `MagicKnobOS_v2alpha3_ESP32S3_APP_0x10000.bin` per **ESP32-S3**. 
Il file FLASH va scritto all'offset `0x0`; quello APP all'offset `0x10000` soltanto con bootloader/partizioni corrispondenti.
**Non cancellare tutta la memoria senza backup.** Identificare sempre ESP32-S3 prima del flash.

## Stato del progetto e passi successivi
- [x] Home e app Magic alpha
- [x] Ghiera bidirezionale verificata dall'utente
- [x] Segnalini centrati e controlli touch
- [x] Impostazioni modulari
- [x] Lettura preliminare tensione GPIO1
- [ ] Conferma empirica lettura batteria su USB e a batteria, ricarica e sleep
- [ ] Driver vibrazione e impostazioni di intensità
- [ ] Orologio/NTP, timer multipli
- [ ] Bluetooth audio/HID: porting delle funzioni del chip secondario
- [ ] Gestione microSD, lettore TXT, foto/musica/analizzatore audio
- [ ] Danni da comandante per sorgente, undo/redo Magic e funzionalità avanzate
- [ ] Porting applicazioni fabbrica, test dell'intero hardware e procedura di ripristino firmware originale.

Fonti hardware: https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8 e https://github.com/juggernautwins608/Waveshare-1.8-Magic-the-Gathering-Life-Counter
