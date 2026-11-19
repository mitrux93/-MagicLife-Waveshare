# Magic Knob OS 2.0 Alpha 5 — Guida test su Waveshare

**Alpha sperimentale, NON sostituto delle applicazioni factory.** Compilazione corretta non garantisce touch/ghiera corretti sul dispositivo; conservare backup firmware originale.

## Installazione
- Collegare e selezionare ESCLUSIVAMENTE il chip **ESP32-S3** (USB VID:PID 303a:1001), **non** il secondario ESP32-U4WDH.
- Scrivere il file `MagicKnobOS_v2alpha5_ESP32S3_FLASH_0x0.bin` all'offset **0x0**; per l'APP separata usare offset **0x10000** soltanto con bootloader/partition coerenti.
- Non selezionare "erase all flash" senza backup; dopo il flash, se lo schermo rimane nero, spegnere e riaccendere fisicamente, riferendo il risultato.

## Verifiche principali
1. Nuova partita: selezionare 1, 2, 3 e 4 giocatori; controllare start 20/40 e ritorno dalle schermate.
2. Estrazione primo giocatore: premere LANCIA più volte, ruotare la ghiera, confermare; verificare vita e giocatore evidenziato.
3. 2 giocatori: ruotare entrambe le direzioni, modificare P1 e P2; verificare che contatti e schermata non perdano scatti.
4. 3-4 giocatori: cambiare il giocatore selezionato tramite riquadri superiori, ruotare la ghiera, usare +/−, controllare che i riquadri aggiornino la vita.
5. Turno: premere TURNO (2 giocatori) o Menu > TURNO; verificare alternanza Pn, GIRO e cronometro.
6. Menu > ANNULLA: cambiare vita con un singolo scatto e annullare; idem con un segnalino, tornando dalla pagina COUNTERS.
7. Segnalini: controllare 6 categorie e selezione da P1 a P4. Confermare il posizionamento di 0, 9, 10 e 100.
8. Menu > STATO: assegnare e togliere Monarca e Iniziativa, spostarli tra giocatori; verificarne etichette.
9. Strumenti: d4, d6, d8, d10, d12, d20, d100, moneta e cronometro; verificare che i risultati restino nel range.
10. Home > Impostaz. > Alimentazione: la schermata deve mostrare tensione linea 5V e **nessuna percentuale** falsamente derivata.
11. Spegnimento e riaccensione: verificare vite, contatori e titolari delle condizioni salvati.
12. Riavvio software via RTS/USB: verificare display; indicare se serve riavvio tramite interruttore.

## Limitazioni note
- Undo: soltanto ultima modifica a vita o contatore, non undo multiplo né turni.
- Danni comandante ancora **aggregati** per bersaglio; nessuna attribuzione alla sorgente. Non usarlo per rilevare la regola dei 21 danni da un singolo comandante.
- Modalità multigiocatore 3–4: vista focus, non Arena a spicchi.
- Nessuna sincronizzazione wireless o telemetria SoC batteria in questa alpha.
- Manca ancora collaudo reale su dispositivo Waveshare.

Segnalare: fase test, giocatori, posizione schermo, comportamento atteso e comportamento osservato.
