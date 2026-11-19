# Alpha 4.2 — uno scatto mancato sul verso verso destra

L'utente segnala che, anche in Alpha4.1, la ghiera salta talvolta **uno scatto nel verso sinistra→destra**. La decodifica ESP32-S3 è differenziata per direzione: GPIO8 (destra, impulsi +) e GPIO7 (sinistra, impulsi −).

## Modifica sperimentale
- Il canale destra, GPIO8, ora usa di default il **fronte di risalita** (RISING) anziché il fronte di discesa (FALLING).
- Il canale sinistra, GPIO7, mantiene il **fronte di discesa** che funziona correttamente.
- Entrambi i pin vengono osservati su **CHANGE** per contare separatamente i fronti fisici alti (SU) e bassi (GIÙ).
- **Impostazioni → Ghiera → Scatto verso destra** permette di alternare `FRONTE SALITA` / `FRONTE DISCESA` e salva la preferenza su flash. La sensibilità 1...4 resta invariata.
- **Impostazioni → Diagnostica** mostra:
  - DESTRA e SINISTRA: eventi considerati validi dal contatore;
  - per ogni verso, `GIU` e `SU`: numero totale dei rispettivi fronti osservati.
  - Pulsante `AZZERA` per azzerare i conteggi e fare prove riproducibili.
- Non cambia la grafica dual-player, l'animazione KO, il flusso touch o la UI della Alpha4.1.

## Test su hardware (necessario)
1. Avvia la scheda; vai a **Impostazioni → Ghiera**. Metti sensibilità=**1** e **FRONTE SALITA** (default sulla nuova installazione). Nota: se si era salvato un diverso valore in una build successiva, permane.
2. Vai a **Impostazioni → Diagnostica** → **AZZERA**.
3. Ruota a destra **lentamente per 20 scatti fisici**; attendi, prendi nota di **DESTRA**, **GIÙ** e **SU**.
4. Premi **AZZERA**; ripeti 20 scatti a sinistra, annotando **SINISTRA**, **GIÙ** e **SU**.
5. Se a destra manca ancora uno scatto, passa da **FRONTE SALITA** a **FRONTE DISCESA** e ripeti.
6. Se **SU** e **GIÙ** si fermano entrambi a 19 su 20, il problema è prima del conteggio software, nel segnale di GPIO8 o nella sua connessione. Se **SU=20** e **GIÙ=19**, e con fronte salita il valore conta 20, il software era il responsabile.
7. Verifica anche il conteggio delle vite in gioco e l'animazione KO per regressioni.

### Note importanti
- Il comando diagnostico resetta soltanto i contatori di diagnostica, **non la partita**.
- I contatori dei fronti grezzi possono differire temporaneamente di 1 se la linea è ancora bassa; attendere un istante prima di leggere.
- Non è garantito che ogni scatto meccanico produca un impulso. Un confronto 20 scatti fisici/20 fronti consente di individuare eventuali mancati impulsi elettrici.
- Solo ESP32-S3, offset flash 0x0. **Non flashare il secondo ESP32.**

Riferimenti:
- https://github.com/juggernautwins608/Waveshare-1.8-Magic-the-Gathering-Life-Counter/blob/main/MTG_Life_Counter/MTG_Life_Counter.ino (rileva RISING)
- https://github.com/teetotum-rs/firmware/blob/main/docs/hardware/input.md (segnali distinti per verso)
