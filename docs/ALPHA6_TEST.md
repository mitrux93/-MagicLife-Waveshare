# Magic Knob OS 2.0 — Alpha 6: test Commander e cronologia

**Stato:** firmware sperimentale. Il successo della compilazione Arduino CI non equivale al collaudo del touch o della ghiera sul Waveshare.

## Perché questa modifica
Regole ufficiali MTG Commander: una singola creatura comandante che infligge **21+ danni da combattimento** a uno stesso giocatore ne provoca la sconfitta. Due comandanti Partner sono due fonti diverse: **non sommare** 10 danni del comandante A e 11 del comandante B ai fini della sconfitta.

Fonte: https://magic.wizards.com/en/formats/commander
Fonte esplicita Partner: https://magic.wizards.com/en/news/feature/commander-2016-mechanics-2016-10-24

## Navigazione
- Durante una partita: **MENU > COMMANDER**.
- Selezionare in alto **P1–P4** = proprietario del comandante (fonte).
- Selezionare **CMD A**, oppure abilitare **PARTNER SI** per passare fra **CMD A/B**. Partner è configurato per giocatore.
- Selezionare in basso **P1–P4** = giocatore che subisce i danni, compreso il proprietario nei casi insoliti.
- Il numero centrale indica danni da combattimento totali ricevuti **da quella singola fonte**. Usare +/- oppure la ghiera.
- **VITA AUTO SI/NO**: quando SI, una variazione di +N danni sottrae N vite al bersaglio e la correzione di -N ne restituisce N. Quando NO, non modifica le vite (utile se il danno era già stato sottratto). Il cambio SI/NO vale sulle modifiche successive.
- A 21+ compare KO CMD. Nella partita a 2 il bersaglio riceve un badge KO; nel riepilogo partita appare l'avviso.
- Non è consentito disattivare Partner se esiste danno registrato del comandante B: evita che un record attivo diventi invisibile.

## Salvataggi e retrocompatibilità
- La matrice `commanderMatrix[4][2][4]` viene salvata nella Preferences NVS come blob `cmdMatrix` di 64 byte, separato dal dato di vita.
- Opzione `cmdAutoLife` e maschera `cmdPartners` salvate.
- Il vecchio contatore aggregato `cmd0..3` Alpha 5 **non può essere convertito** in danni per fonte: è conservato nella NVS legacy ma non è più presentato come se fosse regolare per Commander.
- Nuova partita azzera la matrice e i Partner, non la preferenza automatica delle vite.
- **ANNULLA** ripristina l'ultima modifica a vita, segnalino o danno da comandante; in quest'ultimo caso ripristina anche il precedente valore vite se la variazione era automatica.
- **STORICO:** 32 eventi recenti in RAM (vite, contatori, Commander, turni, Monarca/Iniziativa, inizio, undo). Scorrimento con ghiera e pulsanti. **Non persiste al riavvio**, per evitare scritture eccessive sulla flash; il tabellone e i danni Commander invece persistono.
- Gli eventi vengono registrati per modifica; la cronologia sostituisce i più vecchi una volta raggiunto il limite.

## Collaudo guidato (2–4 giocatori)
1. Avvia nuova partita Commander da 40 vite con 4 giocatori. Menu > Commander.
2. Seleziona P1 / CMD A / bersaglio P2. Con VITA AUTO SI, aggiungi 5. Atteso P1A→P2 = 5 e vita P2 = 35.
3. Attiva Partner per P1. Seleziona CMD B, bersaglio P2; aggiungi 16. Atteso P1B→P2 = 16, P1A→P2 resta 5; nessun KO da comandante perché 5+16 provengono da fonti diverse. Se VITA AUTO SI, vita P2 scende a 19.
4. Torna su CMD A e aggiungi 16: P1A→P2 = 21. Atteso avviso KO CMD pur con vita residua 3.
5. Premi MENU > ANNULLA dopo una singola modifica a Commander. Verifica che ripristini sia danno che vita. (L'annullamento riguarda **l'ultima modifica**, non l'intera serie di pressioni.)
6. Prova VITA AUTO NO: aggiungi 1 danno e verifica che le vite non cambino; correggi -1 e controlla che restino inalterate.
7. Cambia fonte P3 e bersaglio P4; il totale deve essere indipendente dai valori P1.
8. Vai in MENU > STORICO. Verifica righe leggibili e ordine più recente → meno recente; scorri con la ghiera e coi pulsanti.
9. Spegni e riaccendi: verifica danni Commander, Partner, vite e preferenza auto. La cronologia deve ripartire vuota.
10. Test regressione Alpha 5: 1/2/3/4 giocatori, contatori, ghiera CW/CCW, D4–D100, cronometro, Monarca/Iniziativa, swipe, monitor linea 5V.

## Installazione
- Usare soltanto la porta che espone **ESP32-S3** (USB VID/PID 303a:1001), non il microcontrollore secondario ESP32-U4WDH.
- Immagine unificata `MagicKnobOS_v2alpha6_ESP32S3_FLASH_0x0.bin` all'offset **0x0**; APP `*_APP_0x10000.bin` solo se il resto della flash è coerente.
- Non eseguire erase completo senza backup del firmware originale.
- Se il display non parte dopo flash via RTS, spegnere/riaccendere fisicamente e riferire il risultato.

## Ancora da realizzare (non incluso in Alpha 6)
- Nome delle carte comandante e import di mazzi.
- Commander damage non-combat **non** deve contare verso 21: l'utente inserisce soltanto danni da combattimento.
- Combo comandi rapidi, lifelink e blocchi multipli.
- Cronologia persistente/esportazione.
- Connessione **ESP-NOW** fra più dispositivi, pairing e sincronizzazione robusta; non implementata né simulata.
- Recupero factory di SD, Bluetooth su chip secondario, audio e altro; Alpha 6 NON è firmware di fabbrica.

## Roadmap sync senza comprometterne l'affidabilità
Ogni dispositivo dovrà identificare partita, autore, numero progressivo e stato completo; messaggi univoci con ACK e snapshot, gestione riconnessione e protezione dal doppio conteggio. Non attivare la sincronizzazione durante partite reali finché non è testata con almeno due schede.
