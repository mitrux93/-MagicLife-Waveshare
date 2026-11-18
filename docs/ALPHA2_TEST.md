# Magic Knob OS 2.0 Alpha 2 — test su ESP32-S3

**Versione sperimentale:** non conserva ancora tutte le funzioni del firmware Waveshare di fabbrica. Non flashare sul chip secondario ESP32-U4WDH.

## Modifiche rispetto all'Alpha 1

- **Ghiera**: GPIO8 orario, GPIO7 antiorario, due impulsi indipendenti con interrupt FALLING e debounce per canale (2,4 ms), anziché decodifica a quadratura. In Impostazioni puoi invertire il verso e scegliere 1–4 impulsi per punto vita. Usa Diagnostica per confrontare i contatori A e B ruotando nei due sensi.
- **Riavvio USB**: forzato reset fisico LCD; prima schermata disegnata con retroilluminazione spenta, poi retroilluminazione accesa. Questo riduce la possibilità di schermo nero dopo Hard resetting via RTS; la validazione sul dispositivo resta necessaria.
- **Touch**: tap interpretato al rilascio, breve filtro di falsi release e swipe **da sinistra a destra partendo entro i primi 115 pixel**, almeno 100 px in orizzontale, per tornare alla Home da ogni pagina diversa dalla Home. Lo swipe non deve variare vite o impostazioni.
- **Dadi e moneta**: schermata di lancio dedicata con numero D6/D20 gigante o TESTA/CROCE grande; tocco sul risultato o su RILANCIA per lanciare di nuovo.
- **UI**: piccoli ritocchi cornici e tipografia, etichette Home e versione Alpha 2.

## Test di regressione richiesti

1. Scollega e ricollega USB, spegni/accendi: Home sempre visibile. Dopo upload RTS osserva il display.
2. Home > Magic > Nuova: verifica 1/2 giocatori, 20/40, tocco +/-, salvataggio al riavvio.
3. Diagnostica: gira lentamente 20 scatti orari (annota A/B), poi 20 antiorari. Ogni verso dovrebbe incrementare soprattutto uno dei due contatori. Se uno non risponde, registrare i due valori: occorrerà analizzare GPIO e polarità.
4. Verifica che la ghiera non modifichi il conteggio quando non gira e che 20 scatti producano circa 20 variazioni con sensibilità = 1.
5. D6/D20/Moneta: tap rilancia, risultato leggibile. Swipe da sinistra verso destra torna in Home senza attivare bottoni.
6. Cambia 100 volte le vite: niente lampeggio del display intero.
7. Testa funzionalità USB, batteria e sospensione *prima* di considerare risolto il power issue.

## Compilazione

In GitHub Actions, esegui il workflow `build-v2.yml` sulla branch `feature/firmware-completo-v2`, scarica l'artefatto `MagicKnobOS-v2alpha-ESP32S3-BIN`.

- File `*_FLASH_0x0.bin` > flash S3 offset `0x0`.
- File `*_APP_0x10000.bin` > flash S3 offset `0x10000` *solo se bootloader e tabella partizioni coincidono*.
- Non cancellare la flash senza backup verificato. Non scrivere sul chip seriale USB `1a86:7523` (ESP32-U4WDH).
- Il dispositivo S3 deve risultare USB `303a:1001`.

## Stato reale

Compilazione GitHub Actions ≠ firmware hardware testato. Mancano ancora Bluetooth audio/HID, galleria, musica da microSD, viewer TXT, audio spectrum, gestione batteria e molte opzioni Magic evolute. Queste funzionalità sono previste ma non disponibili in Alpha 2.
