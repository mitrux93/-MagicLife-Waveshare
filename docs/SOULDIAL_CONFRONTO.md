# Magic Knob OS — funzioni ispirate all'esperienza d'uso di Soul Dial

Fonte pubblica consultata 2026-11-19: https://souldial.net/pages/welcome-to-soul-dial

Questo è un progetto indipendente per Waveshare ESP32-S3-Knob-Touch-LCD-1.8; non contiene firmware, marchi grafici o codice proprietario Soul Dial. Si tratta di un confronto di funzionalità e priorità di sviluppo, non di compatibilità tra dispositivi.

| Funzione | Soul Dial | Magic Knob OS Alpha 5 |
|---|---|---|
| Vita MTG Standard/Commander | Sì | Sì, 20/40 |
| Giocatori | 1–6 | 1–4, modalità 2 giocatori a schermo diviso, 1/3/4 con giocatore focalizzato e riepilogo |
| Ghiera / touch | Sì | Sì; diagnostica bordi e sensibilità configurabile |
| Chi inizia | Sorteggio | Sorteggio e conferma, anche tramite ghiera |
| Turni | Sì, timer | Giro, giocatore corrente e durata turno visualizzati |
| Annulla | Sì | Ultima modifica della vita o di un segnalino |
| Dadi | d4-d100, moneta, combinazioni | d4, d6, d8, d10, d12, d20, d100 e moneta; un dado per volta |
| Segnalini | Ampia libreria e personalizzati | Veleno, energia, tassa, danni comandante aggregati, tesori, esperienza |
| Monarca / Iniziativa | Sì, proprietà esclusiva | Sì, assegnabili a un giocatore per volta |
| Danni comandante per attaccante, partner | Sì | **NO**: attualmente conteggio aggregato per difensore |
| Attacchi drag, multi-target / lifelink | Sì | NO |
| Storico / grafico / mazzi | Sì | NO |
| Gioco sincronizzato wireless | Sì | NO |
| Giocatori 5-6 e Arena radiale | Sì | NO |
| D&D / altre 90+ modalità | Sì | NO |
| Percentuale batteria accurata | Sì | NO: GPIO1 legge linea 5V, non cella |

## Vincoli tecnici
- Display rotondo 360x360: la vista da 3–4 giocatori privilegia leggibilità del giocatore selezionato rispetto a quattro vite grandi contemporaneamente.
- La ghiera Waveshare usa due contatti direzionali separati (GPIO8/7), non quadratura classica. Non modificare la logica degli interrupt senza verifica hardware.
- Il chip secondario ESP32-U4WDH non deve essere flashato con il firmware del chip principale.
- L'attuale lettura "alimentazione" GPIO1 non può essere convertita in una percentuale di batteria valida.
- Compilazione CI non equivale a collaudo del dispositivo.

## Prossimi obiettivi, in ordine
1. Danni da comandante per fonte **e** difensore, con eventuali comandanti partner; applicazione a vita per danni confermati.
2. UI multigiocatore migliorata: turn ring, feedback selezione, undo sulla schermata primaria.
3. Preset di partite, cronologia azioni e statistiche in memoria senza usura eccessiva flash.
4. Connessione ESP-NOW fra più Knob e gestione riconciliazione degli stati.
5. Estensione 5-6 giocatori e schermata Arena radiale con attenzione all'area visibile.
6. Recupero funzioni originali hardware (SD, audio, Wi-Fi, aptica) con relativo firmware e circuiti verificati.
