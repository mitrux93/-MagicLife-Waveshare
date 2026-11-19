# Magic Knob OS 2.0 Alpha 8 — ESP-NOW host e visore (sperimentale)

Il firmware **ESP32-S3-Knob-Touch-LCD-1.8** contiene una prima sincronizzazione fra **due** dispositivi tramite ESP-NOW (2,4 GHz, canale radio fisso 1). Il Knob principale è **l'unico che modifica la partita**; il secondo è un **visore di sola lettura**. Non è ancora editing bidirezionale, non è uno schema cloud, e non necessita di router o Internet.

## Presupposti e avvertenze
- Occorrono **due Waveshare** con **Alpha 8** sul microcontrollore **ESP32-S3 principale**. Non flashare il chip secondario ESP32-U4WDH.
- Entrambi devono essere alimentati, vicini e nello stesso ambiente radio; non promettere distanze operative.
- L'abbinamento usa **quattro cifre in chiaro** nei pacchetti di scoperta. **NON** è un sistema di autenticazione forte, cifratura né protezione dei dati; usare solo per collaudi e partite senza dati riservati.
- Il dispositivo visore **non deve poter modificare vite o segnalini locali** durante la sincronizzazione.
- Il dispositivo principale resta operativo con la radio spenta. Nella prima Alpha 8 non c'è sincronizzazione multi-host e non c'è supporto a più di un visore per volta.
- Il firmware **non** ripristina le funzioni factory (audio, SD, Bluetooth del secondo chip); la batteria percentuale non è attendibile con l'ADC GPIO1.

## Accensione e abbinamento

### Primo Knob — host
1. Aprire **Impostazioni > RETE ESP-NOW**.
2. Selezionare **HOST**, premere **AVVIA**.
3. Comparirà un codice **di quattro cifre** generato all'avvio.
4. Tenere la pagina visibile mentre si imposta il visore; in seguito premere **PARTITA** per tornare al contatore oppure fermare con **FERMA**.

### Secondo Knob — visore
1. Aprire **Impostazioni > RETE ESP-NOW**, selezionare **VISORE**.
2. Toccare una delle quattro caselle del codice per selezionare la cifra; modificare la cifra con i pulsanti **- / +** oppure con la ghiera.
3. Ripetere fino a riprodurre esattamente il codice mostrato dall'host.
4. Premere **AVVIA**, attendere **COLLEGATO**. Premere **PARTITA** per vedere i punti vita replicati.
5. Nel visore, toccare la schermata di partita per tornare alla pagina di rete. **FERMA** spegne la radio e ripristina in RAM la partita locale precedente.

**Importante**: il principale e il visore devono usare entrambi Alpha 8. Il visore non possiede una procedura per ricostruire autonomamente una partita se manca il principale.

## Che cosa viene sincronizzato
- Numero di giocatori, 20/40 iniziali, stato partita, selezione e turno corrente
- Punti vita, veleno, energia, tassa comandante, tesori, esperienza per ciascuno dei quattro posti
- Matrice completa danni Commander separati per proprietario, comandante A/B (partner) e bersaglio
- Partner abilitati, Monarca, Iniziativa, opzione vita automatica e timer del turno

**Non sincronizzati in Alpha 8**: cronologia azioni, nomi delle carte, impostazioni locali di schermo/ghiera, randomizzazione, file/audio e altri dati del firmware originale. Il visore non salva la partita remota nella propria NVS e riparte con radio spenta dopo riavvio.

## Protocollo e comportamento in caso di errori
- ESP-NOW su canale Wi-Fi fisso 1, modalità station (nessun router necessario)
- Pacchetti tipizzati con version, session, sequence e pairing code, dimensione controllata in compilazione (<=250 byte)
- Il principale invia **snapshot completi** su modifica e periodicamente, circa ogni 1,1 s, invece di semplici delta: un duplicato non deve applicare due volte il danno
- Il visore accetta solo sequence crescente dalla stessa sessione e stesso indirizzo host
- Il visore invia ACK di conferma; l'host visualizza l'ultimo sequence confermato
- L'host mantiene il collegamento finché riceve ACK; dopo ~8 s di silenzio libera il visore. Il visore mostra RICERCA dopo ~4,5 s senza snapshot e riprova l'abbinamento
- **Limitazione:** l'ACK e lo stato sono solo a scopo sperimentale, non sono ancora un sistema affidabile di sincronizzazione multi-master e non garantiscono disponibilità radio in tutte le condizioni. I pacchetti non sono cifrati.

## Test hardware essenziali
1. Accendere due Knob con Alpha 8, aprire RETE ESP-NOW sul principale e sul visore; abbinare.
2. Con il visore sul contatore 4 giocatori, modificare vite P1, P2, P3, P4 **solo sul principale**. Devono aggiornarsi i pannelli corrispondenti sul visore, senza perdere valori.
3. Ruotare la ghiera sul visore: **non deve modificare** il principale né il visore. Toccare +/- sul visore: deve aprire la pagina di rete, non alterare vite.
4. Sul principale aggiungere +7 danni del comandante P1A a P2 con vita automatica attiva; sul visore controllare che **cambino una sola volta** il danno e la vita di P2.
5. Ripetere usando un secondo comandante Partner e un altro bersaglio; verificare che le fonti restino separate.
6. Cambiare Monarca e Iniziativa, poi avanzare due turni. Confrontare i due schermi.
7. Allontanare/spegnere il principale: il visore dovrebbe mostrare RICERCA dopo qualche secondo mantenendo l'ultima visualizzazione momentaneamente. Riaccendere/riavviare HOST e usare **nuovo codice** sul visore se il principale ha cambiato sessione.
8. Spegnere il visore e poi tornare al principale: la partita sul principale deve restare invariata e giocabile.
9. FERMA sul visore: la precedente partita locale deve essere recuperata dalla copia in RAM; dopo riavvio restano i salvataggi locali precedenti.
10. Verificare che la ghiera sulla pagina RETE possa modificare solo la cifra selezionata quando VISORE è spento, lasciando inalterato il filtro di 16 ms.

## Ghiera: segnalazione dell'Alpha 7
È stato riscontrato un piccolo ritardo nel cambio **destra → sinistra** nella schermata diagnostica ma non evidente in partita. Questo è compatibile con la finestra di blocco direzionale. La Alpha 8 **mantiene invariata la logica collaudata**. La finestra è modificabile in **Impostazioni > Ghiera > FILTRO INCROCI**: 0 / 8 / 16 / 32 ms. Se si prova 8 ms, confrontare nuovamente i falsi impulsi; tornare a 16 ms se ricompare il disturbo.

## Installazione
- `MagicKnobOS_v2alpha8_ESP32S3_FLASH_0x0.bin` all'offset **0x0** del solo ESP32-S3 (USB generalmente VID:PID `303a:1001`), **senza Erase All** se non si possiede un backup.
- `APP_0x10000.bin` è una modalità alternativa che richiede bootloader/partizioni compatibili, non una seconda immagine da installare assieme al firmware unificato.
- Dopo il flash, se schermo nero, spegnere e riaccendere fisicamente e annotare l'esito.

## Prossima fase dopo il collaudo con due Knob
Supportare modifiche bidirezionali tramite **un unico host autorevole** che riceva richieste con ID univoci, le elabori una sola volta e rimandi snapshot e ACK. Soltanto dopo test con perdita pacchetti e riconnessioni estendere il sistema a più visori, registri eventi sincronizzati e pairing sicuro.
