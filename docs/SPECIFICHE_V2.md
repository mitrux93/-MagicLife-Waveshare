# Magic Knob OS v2 — Specifica funzionale e criteri di rilascio

**Stato: progetto di sviluppo, NON un firmware installabile completo.**
La versione attuale v1.x sulla branch `main` è soltanto un segnapunti sperimentale; non sostituisce tutte le funzioni Waveshare.
Questa branch serve per progettare, realizzare e testare un firmware completo. Non generare né promuovere un BIN "completo" finché non sono soddisfatti i criteri sotto.

## Obiettivo
Il dispositivo Waveshare ESP32-S3-Knob-Touch-LCD-1.8 deve diventare un **multi-tool** indipendente con launcher, Magic: The Gathering e tutte le funzioni quotidiane disponibili nel firmware originale, oppure segnalare esplicitamente quelle non disponibili. **Conservare il firmware originale in parallelo non è automatico**: la scrittura sul chip S3 lo sostituisce. Prima di installare, acquisire backup e procedura di ripristino di *entrambi* i chip.

## Home / launcher
- Magic (app principale)
- Orologio / timer
- Multimedia (musica da microSD, foto, video se supportato, TXT)
- Bluetooth musica e controllo volume/HID
- Strumenti (microfono/analizzatore audio, dado e moneta)
- Impostazioni dispositivo
- Pressione centrale / gesto indietro; sfioramento per selezione, ghiera per navigazione
- Stato: batteria reale, alimentazione USB, ora, Wi-Fi/Bluetooth, memoria SD

## Magic — partita
- Modalità 1 e 2 giocatori sul medesimo display circolare, selezione iniziale
- Profili Classic (20), Commander (40), Personalizzato (vite configurabili)
- Punti vita indipendenti, guadagna/perdi +1/-1, +5/-5 opzionali
- Segnalazione di sconfitta con 0 o meno vite; **non** bloccare il punteggio a zero (si possono avere vite negative)
- Veleno (soglia 10), energia, esperienza; tassa comandante, danni da un singolo comandante (soglia 21) con tracciamento fonte separato
- Monarch, Initiative, day/night e storm come contatori/stati opzionali
- D6/D20, lancio moneta, selezione primo giocatore
- Timer di partita, timer di turno, eventuale timer per giocatore
- Undo/redo, reset partita con conferma, log delle variazioni
- Salvataggio atomico delle partite: scritture differite per ridurre l'usura flash, ripristino dopo riavvio
- Aspetto *Magic minimal elegante*: nero/blu notte, oro tenue, P1 blu e P2 ambra; testo ad alto contrasto, leggibile nella geometria circolare

## Input / resa grafica
- **Antiflicker**: disegno statico una volta; aggiornamento solo dei widget modificati, preferibilmente LVGL con flush a riquadri su PSRAM
- **Ghiera a impulsi separati**: GPIO8 e GPIO7 forniscono impulsi di rotazione distinti nei due versi secondo il progetto di riferimento; campionamento o interrupt con debounce per ogni ingresso; prova diagnostica dei conteggi reali
- Direzione ghiera configurabile; moltiplicatore `1 vita/scatto` di default; passi per scatto calibrabili; filtraggio contro i rimbalzi; nessuna accelerazione di default
- Click centrale (GPIO0) = seleziona; pressione prolungata = indietro/menu (da validare su hardware)
- Feedback vibrazione opzionale sullo scatto, con forza configurabile; controlli touch +/-
- Selezione del giocatore tramite tocco; la ghiera agisce solo sul giocatore selezionato

## Impostazioni dispositivo
- Luminosità manuale e spegnimento/dim schermo (durate configurabili)
- Vibrazione: acceso/spento e intensità
- Ghiera: verso, sensibilità, velocità/accelerazione (opzionale), ruolo nel launcher e nella partita
- Wi-Fi e impostazione orario/timezone NTP, Bluetooth/HID (stato reale, non solo etichetta grafica)
- Audio: sorgente, volume, microSD e uscita 3.5mm
- Batteria: lettura tensione e stato caricamento con indicatore affidabile (solo se batteria presente); sospensione/risveglio; se dispositivo senza batteria, avviso esplicito
- Lingua, informazioni versione, diagnostica display/touch/encoder
- Backup/importazione preferenze, ripristino valori, firmware e recovery

## Confronto con firmware originale
Funzioni documentate da Waveshare: musica da SD, Bluetooth Music Player (secondo ESP32), Bluetooth HID volume, galleria JPEG, video/MJPEG, lettore testi TXT, orologio/timer, time zone e Wi-Fi sincronizzazione, analizzatore di spettro MIC, impostazioni di sistema, vibrazione.
Fonte: https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8

| Modulo | v1.x main | v2 obiettivo |
| --- | --- | --- |
| Magic 1/2 giocatori | parziale | completo |
| Velocità ghiera bidirezionale | difettosa | impulsi separati e debounce verificati |
| Flicker | presente | eliminato |
| Timer/orologio | assente | da implementare |
| Foto/SD/TXT | assente | da implementare |
| Bluetooth audio/HID | assente | da implementare e integrare con ESP32 secondario |
| Microfono/spettro | assente | da implementare |
| Gestione energia/batteria | assente | da implementare su hardware |
| Firmware factory ripristinabile | non garantito | backup e ripristino verificati |

## Hardware e compatibilità
- S3 USB VID:PID `303a:1001`, **non** il seriale `1a86:7523` del chip ESP32 secondario
- LCD QSPI 360×360: documentazione talvolta cita ST77916; alcuni BSP funzionanti usano SH8601. Verificare sul dispositivo prima di consolidare il driver
- Touch CST816(S) I²C, encoder A=8 B=7, touch SDA=11 SCL=12, BL=47
- Audio/Bluetooth distribuiti fra i due chip; il firmware del chip secondario già sovrascritto in un precedente tentativo potrebbe richiedere ripristino
- **Non fare alcuna cancellazione né flash automatico sul chip dell'utente.**

## Criteri di accettazione
1. Test reale S3: 1 giocatore e 2 giocatori; 20/40 vite; + e - su touch.
2. 50 scatti in senso orario e 50 antiorario, conteggi esatti con diverse velocità.
3. Cambio vite senza schermo nero o sfarfallio; 200 aggiornamenti consecutivi.
4. Dati Magic salvati a riavvio e separati dalle impostazioni dispositivo.
5. Batteria/alimentazione verificata su variante con e senza cella.
6. App originali equivalenti funzionanti *oppure* ogni funzione mancante chiaramente indicata; solo dopo i test dichiarare firmware completo.
7. Immagine factory e procedura recovery salvate/verificate per S3 e chip secondario.
8. Compilazione GitHub Actions, firmware unico con offset e checksum documentati.

## Stato dei lavori
- [x] Spec e architettura v2 isolate in branch dedicata, senza toccare la main
- [x] Nucleo v2 alpha (launcher, Magic 1/2, contatori, dadi, timer e impostazioni) aggiunto nel ramo
- [ ] Porting/validazione BSP LCD/encoder/touch su esemplare
- [ ] Engine Magic avanzato e test
- [ ] UI LVGL completa
- [ ] App multimediali e Bluetooth
- [ ] Sistema alimentazione e impostazioni
- [ ] Test di regressione e recupero firmware originali
- [ ] Release v2 verificata su hardware
