# Magic Knob OS Alpha 7 — layout split e filtro ghiera (test hardware)

**Stato**: implementato sul ramo `feature/firmware-completo-v2` per Waveshare **ESP32-S3-Knob-Touch-LCD-1.8**. La compilazione CI non prova il comportamento fisico: testare prima di usare in partita.

## Layout 3 giocatori — tutti visibili
Tre pannelli:
- P1 al centro in alto (x97–263, y72–163)
- P2 in basso a sinistra (x47–171, y196–294)
- P3 in basso a destra (x189–313, y196–294)
- Pulsante circolare MENU al centro (180,184), in area non occupata dai numeri.

## Layout 4 giocatori — quattro quadranti
- P1 alto sinistra, P2 alto destra
- P3 basso sinistra, P4 basso destra
- Ogni pannello: numero grande, Pn e asterisco per chi ha il turno, indicatori sintetici V (veleno), E (energia), T (tassa), bordo oro sul giocatore selezionato.
- Cornice/tinta rossa nei casi di KO Commander.
- Touch `-` e `+` ai lati del numero cambia immediatamente le vite **del pannello toccato**; tocco sul corpo pannello seleziona il giocatore da modificare con la ghiera.
- MENU circolare centrale, turno visibile nella fascia superiore; fascia inferiore Monarca/Iniziativa.

## Filtro anti-cross-talk della ghiera
Il Knob usa due linee impulsive indipendenti GPIO8/GPIO7, NON codifica quadratura.
- A ogni impulso attivo, debounce per canale di 2,4 ms.
- Se l'ultimo **impulso accettato** era della direzione opposta ed è trascorso meno di `crossLockMs`, il nuovo impulso opposto viene rifiutato. Gli impulsi dello stesso verso sono sempre eleggibili dopo il debounce.
- Default: **16 ms**. Nel menu Impostazioni > Ghiera, toccare FILTRO INCROCI per passare **0 → 8 → 16 → 32 → 0 ms**; 0 disattiva il filtro. La preferenza è salvata.
- **Controindicazione:** una rapidissima inversione volontaria della rotazione entro la finestra scelta può essere ignorata. In tal caso usare 8 ms o 0 ms.
- **Nessuna certezza hardware che 16 ms bastino**: misurare diagnostica prima/dopo.

## Diagnostica: interpretare i valori
- `DESTRA OK`, `SINISTRA OK`: impulsi **realmente conteggiati** (dopo debounce e filtro direzionale).
- `su` / `giu`: fronti **grezzi** rilevati su ciascun GPIO.
- `X`: impulsi candidati **scartati per incrocio opposto**.
- `R`: impulsi **scartati come rimbalzi del medesimo canale**.
- `AZZERA` resetta i contatori e lo storico temporale dei filtri, senza modificare punti vita.
- Il filtro non rende per definizione pulito il segnale fisico: il contatore dei fronti grezzi può comunque crescere nel verso opposto, ma **non deve crescere il contatore OK opposto**.

## Sequenza test ghiera (ripetibile e misurabile)
1. Impostazioni > Ghiera: seleziona **0 MS**. Vai in DIAGNOSTICA e premi AZZERA.
2. Ruota **esattamente 20 scatti a destra**, a ritmo regolare. Annota `DESTRA OK`, `SINISTRA OK`, `X` e `R`, separatamente. Se il sensore genera 1 falso sinistra ogni 2 destra, ci aspettiamo circa 10 falsi solo nella situazione senza filtro (misura, non risultato garantito).
3. Ripeti il test dopo AZZERA con **8 MS**, **16 MS**, **32 MS**. Il livello consigliato è il più basso che porti `SINISTRA OK=0` senza perdere scatti veri.
4. Ripeti in senso sinistro: 20 scatti a sinistra, verifica che `DESTRA OK` non aumenti.
5. Prova **10 rapide inversioni** destra/sinistra: se il filtro ignora movimenti legittimi, riduci la finestra; non promettere piena reattività con 32 MS.
6. Durante una partita, cambiare vite con ghiera CW/CCW rapidamente e verificare che differenza reale corrisponda agli scatti.
7. Reimposta le preferenze a 16 MS per la prima prova in partita e riferisci i contatori ottenuti.

## Sequenza test UI
1. Nuova partita con **3 giocatori**, 40 vite. I tre valori devono essere visibili insieme.
2. Seleziona P2 toccando il corpo del pannello. Ruota 3 scatti a destra: solo P2 deve passare 40→43, P1/P3 invariati.
3. Usa -/+ sui pannelli P1 e P3: devono cambiare i giocatori **corrispondenti**, non quello precedente.
4. MENU al centro → TURNO: cambiano asterisco e cronometro turno, ma nessuna vita viene modificata.
5. Nuova partita con **4 giocatori**: tutti i quattro punteggi devono apparire, con V/E/T sotto i numeri.
6. Modifica velocemente le vite di due pannelli diversi, senza tremolio del resto dello schermo.
7. Colora il KO Commander portando a 21 i danni di una singola fonte. Verifica che il pannello del difensore abbia la cornice rossa e non venga confuso con il colore del focus.
8. Controlla menu, segnalini, cronologia, Partner e annullamento dell'ultima modifica. Verifica salvataggi al riavvio.

## Installazione
- Selezionare **solo ESP32-S3 principale** (tipicamente USB VID:PID 303a:1001); non scrivere sul microcontrollore secondario.
- Il file `MagicKnobOS_v2alpha7_ESP32S3_FLASH_0x0.bin` è l'immagine completa da flashare a indirizzo `0x0`.
- L'APP-only `*_APP_0x10000.bin` è alternativa e richiede bootloader/partizioni già compatibili, **non** va caricato assieme all'unificato.
- Non cancellare l'intera flash senza avere un backup.

## Limitazioni ancora aperte
- Solo firmware sperimentale: non ripristina le funzioni originali SD/audio/BT e non fornisce percentuale batteria affidabile.
- I 3 giocatori sono disposti a triangolo con tre pannelli arrotondati, non tre spicchi matematici con testi ruotati; questa scelta rende i numeri dritti e leggibili senza font personalizzati.
- Nessuna sincronizzazione ESP-NOW attiva e collaudata.
- Confermare il filtro della ghiera con misure sul dispositivo, non solo con compilazione CI.
