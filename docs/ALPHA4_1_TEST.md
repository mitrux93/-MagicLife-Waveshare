# Magic Knob OS Alpha 4.1 — correzione reattività ghiera

## Risultato riportato dall'utilizzatore
La ghiera in Alpha 4 risponde peggio rispetto alla Alpha 3: il decoder a due ingressi con debounce 2400 µs è rimasto identico, ma il nuovo disegno della schermata rendeva più costoso ogni aggiornamento.

## Interventi
1. **Nessun cambio a GPIO, ISR, verso o debounce**: preservato il comportamento hardware della Alpha 3 verificato dall'utilizzatore.
2. **No redraw focus a ogni impulso**: il codice prima assegnava `selectionDirty=true` per *ogni* variazione vita, richiamando `paintSelection()` che ridisegnava due cornici e due etichette P1/P2 anche senza cambio di selezione. Alpha4.1 lo fa soltanto quando cambia il giocatore selezionato.
3. **Dirty numeric coalescing**: aggiorna i numeri non più spesso di ogni ~25 ms (~40 Hz), accumulando sempre gli impulsi indipendentemente dalla frequenza degli aggiornamenti del display.
4. **Area numerica ridotta**: utilizza `fillRect()` sul box dei caratteri, anziché `fillRoundRect()` dell'intero riquadro vita.
5. **Nessuna perdita artificiale di impulsi accumulati**: rimosso `constrain(step,-30,30)`; il valore finale è comunque limitato in `adjustLife()` all'intervallo [-99,999].
6. **KO mantenuto**: animazione solo attraversamento da vita positiva a 0 o meno; piccolo bordo e badge aggiornati senza riempire l'intera schermata.

## Verifiche da fare sul Knob
- **Impostazioni > Ghiera > Sensibilità = 1**; un valore più alto richiede più impulsi per modificare un punto vita.
- In modalità 2 giocatori ruota lentamente 20 tacche a destra, poi 20 a sinistra e confronta il risultato; ripeti rapidamente.
- Controlla se gli scatti vengono registrati ma il numero compare in ritardo oppure se *mancano* punti vita in modo permanente.
- Controlla tocco su P1/P2, pulsanti +/- , KO da 1 a 0, reset e swipe a Home.
- Compilazione CI non sostituisce il test sul dispositivo.

Build: workflow `build-v2.yml`, pacchetto `MagicKnobOS-v2alpha4-1-ESP32S3-BIN`. L'immagine `MagicKnobOS_v2alpha4_1_ESP32S3_FLASH_0x0.bin` è destinata esclusivamente all'ESP32-S3 a offset 0x0, non al chip U4WDH.
