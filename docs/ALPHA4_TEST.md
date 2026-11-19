# Magic Knob OS 2.0 — Alpha 4 (UI Duel + KO)

Stato: firmware **sperimentale** per ESP32-S3, non un sostituto completo delle app originali Waveshare. Il firmware del secondo chip ESP32-U4WDH non deve essere modificato.

## Funzioni introdotte
- Nuova UI per **due giocatori**: pannelli simmetrici sul display circolare da 360×360, P1 azzurro, P2 ambra, decorazioni sottili color oro.
- Numeri di vita più grandi, centrati, colorati; in rosso se pari o inferiori a 0, in oro per vite basse (da 1 a 5).
- P1 e P2 selezionabili da touch o tramite ultimo segnapunti modificato, senza cambiare la lettura della ghiera già verificata dall'utente.
- Pulsanti touch + / − indipendenti per ogni giocatore.
- Tre pulsanti al centro: SEGN. (contatori), RESET, MENU.
- **RESET** apre conferma esplicita. ANNULLA conserva la partita; CONFERMA reimposta vite e segnalini.
- **Animazione KO**: quando il valore passa da positivo a 0 o meno, il bordo numerico e il badge KO pulsano in rosso/oro per circa 800 ms, senza usare delay bloccanti e senza ripitturare l'intero display. Le vite possono andare negative. Da 0 a -1 non si attiva di nuovo; se torna positivo e scende di nuovo a 0, l'effetto si ripete.
- Dirty mask dei giocatori: ogni cambio vita ridisegna soltanto il numero del giocatore aggiornato.
- La modalità un giocatore, dadi, moneta, cronometro, swipe verso Home, segnalini, impostazioni e ghiera restano disponibili.

## Dettagli tecnici
- KO è animato nel `loop()` con `millis()`, fotogrammi da 100 ms, 8 frame. Non sono usati ritardi di animazione.
- Lo stato KO persistente si deduce dai punti vita. Il breve effetto, invece, è transitorio e non viene salvato.
- La UI di reset è una pagina indipendente, quindi nessun reset può partire direttamente dal tasto principale.
- Vite limitate a -99…999, impostazione 20/40 conservata.
- Versione non convalida ancora Bluetooth, microSD, audio, batteria o altro firmware factory.

## Collaudo consigliato
1. Avvia una nuova partita a 2 giocatori, 20 o 40 punti. Verifica centratura del numero P1 e P2 per 5, 20, 40, -1 e 999.
2. Ruota la ghiera in entrambi i sensi: la sensibilità deve rimanere quella corretta in Alpha 3.
3. Tocco sui pulsanti ± di entrambi i giocatori, poi scelta di P1/P2 al centro del pannello.
4. P1 a 1: premi meno una volta. Deve comparire il KO breve su P1; P2 rimane invariato. Premi meno di nuovo: non deve ripartire l'animazione.
5. Torna a 1, poi 0: deve partire una **nuova** animazione. Ripeti con P2.
6. Durante l'animazione, ruota la ghiera: il programma deve rimanere reattivo.
7. Premi RESET, poi ANNULLA: i dati devono rimanere. Premi RESET, CONFERMA: la partita deve ripartire da 20/40 e tutti i segnalini da 0.
8. Apri segnalini e impostazioni, prova lo swipe per tornare alla Home. Verifica salvataggio allo spegnimento e riaccensione.
9. Se il display lampeggia quando cambiano i punti vita, segnala se riguarda tutta la schermata o soltanto il riquadro del numero.

## Scaricamento
Workflow GitHub Actions `.github/workflows/build-v2.yml` sulla branch `feature/firmware-completo-v2`: artifact `MagicKnobOS-v2alpha4-ESP32S3-BIN`, con un BIN unificato `MagicKnobOS_v2alpha4_ESP32S3_FLASH_0x0.bin` per offset **0x0** e checksum SHA-256.

**Attenzione:** scrivere solo sul chip ESP32-S3 identificato come VID:PID `303a:1001`. Non effettuare erase-all senza un backup. Compilazione automatica positiva non significa test hardware effettuato.
