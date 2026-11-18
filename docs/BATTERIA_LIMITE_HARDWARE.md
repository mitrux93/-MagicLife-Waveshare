# Batteria e percentuale — analisi circuito (ESP32-S3-Knob-Touch-LCD-1.8)

## Correzione importante alle Alpha 3

**Non usare il valore analogico GPIO1 come percentuale della carica della batteria.**

Dallo schema elettrico 4_OTHER del modello Waveshare:
- net etichettata `BATT_ADC` = punto medio di **R62 10 kΩ** verso **5V** e **R63 10 kΩ** verso **GND**;
- ESP32-S3 **GPIO1** è connesso a `BATT_ADC` (schema 2_ESP32S3-R8);
- ne deriva `V(GPIO1) ≈ V(5V rail) / 2`.
- `analogReadMilliVolts(GPIO1) × 2` misura dunque la **linea di alimentazione 5V**, non una misura diretta della cella Li-ion da 3.7 V.

**Conseguenza:** una percentuale derivata da 5V rail non è attendibile: il power path può stabilizzare la linea nonostante la scarica della batteria. Non associare 5.0V a 100% e non fare scale 0–100% della tensione 5V.

### Comportamento firmware Alpha 3
La pagina BATTERIA misura GPIO1 x2; mostra la % stimata soltanto se il risultato ricade in 2.7–4.35V, ma questa logica **non garantisce una percentuale di carica**, perché il punto analogico non è la cella. Occorre modificare la UI per chiamare la misura **Tensione alimentazione**, disabilitare le percentuali non validate, e utilizzare soltanto eventuale lettura realistica da sensore della cella dedicato.

### Per mostrare percentuali affidabili
Opzioni hardware da valutare con cablaggio e spazio disponibili:
1. Fuel gauge Li-ion (es. MAX17048) collegato **ai terminali della cella** e a un bus I²C con indirizzo compatibile, rilevato dal firmware.
2. Partitore sul vero VBAT, collegato a un ADC libero e verificato nello schema; convertire tensione in una stima non lineare soltanto a batteria, dopo calibrazione empirica.
3. Verificare se un componente del sistema di ricarica espone già una misura stato di carica via protocollo accessibile; **non assumere** che esista.

### Diagnosi preliminare
- Verificare **se la versione possiede fisicamente una batteria**: la variante -EN può esserne priva.
- Se si spegne appena scollegata la USB, prima valutare la batteria/connettore/interruttore. Il firmware non può misurare lo stato di carica di una batteria assente.
- Rilevare GPIO1 con USB e senza, quando possibile, per confermare la misura 5V.
- Non considerare la linea 5V della scheda un indicatore di ricarica.

Fonti schemi e demo:
- https://github.com/KrX3D/WaveShare-Knob-Esp32S3/blob/main/ESP32-S3-Knob-Touch-LCD-1.8-schematic/4_OTHER.png
- https://github.com/KrX3D/WaveShare-Knob-Esp32S3/blob/main/ESP32-S3-Knob-Touch-LCD-1.8-schematic/2_ESP32S3-R8.png
- https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8
