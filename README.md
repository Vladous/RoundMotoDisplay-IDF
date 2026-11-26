<div align="center">

# 🏍️ RoundMotoDisplay

**ESP32 · 240 × 240 · Bluetooth PAN · MQTT**

Motocyklový displej s teplotou a náklonem<br>
Motorcycle display with temperature and lean angle

[🇨🇿 Česky](#cesky) · [🇬🇧 English](#english) · [MIT License](LICENSE)

</div>

---

<a id="cesky"></a>

## 🇨🇿 Česky

RoundMotoDisplay je firmware pro **klasické ESP32** a kulatý displej. Zobrazuje
teplotu a náklon, pamatuje si největší náklon na obě strany a přes telefon
odesílá údaje do MQTT.

> **Projekt je experimentální.** Náklon počítaný z akcelerometru ovlivňuje
> zrychlování i vibrace. Není to přesný bezpečnostní přístroj a nespoléhej na
> něj při řízení.

### ✨ Co umí

| Funkce | Popis |
|---|---|
| 🌡️ Teplota | Termistor; údaj na displeji se obnovuje přibližně po 20 s. |
| ↔️ Náklon | Aktuální náklon na stupnici −60° až +60°. |
| 🔴 Rekordy | Červené tečky označují největší kladný a záporný náklon. |
| 💾 Paměť | Rekordy se ukládají do NVS ve flash a načtou se po zapnutí. |
| 🔘 Reset | Podržením tlačítka na 2 s se rekordy vynulují. |
| 📡 Bluetooth a MQTT | Telefon poskytuje Bluetooth tethering; ESP posílá JSON každou minutu. |

Hodnota mimo rozsah stupnice se označí na jejím konci. Před prvním rekordem
je maximum daného směru 0. Nový rekord se ukládá ihned; časté zápisy flash
opotřebovávají a výpadek napájení během zápisu může poslední hodnotu ztratit.

### 🧰 Co budeš potřebovat

| Díl | Poznámka |
|---|---|
| ESP32 s Bluetooth Classic | Např. ESP32-WROOM-32; alespoň 4 MB flash. Čipy bez Bluetooth Classic, např. ESP32-C3 nebo S3, nejsou náhradou pro tento firmware. |
| Kulatý SPI displej 240 × 240 | Firmware je nastavený pro GC9A01; skutečný řadič použitého displeje zatím není potvrzený. |
| MPU9250 nebo MPU6500 | Modul s I²C a logikou kompatibilní s 3,3 V. |
| NTC termistor a rezistor 10 kΩ | Termistor je ve vstupním děliči napětí. |
| Tlačítko | V klidu rozepnuté, při stisku spojí GPIO s GND. Externí pull-up není potřeba. |
| Android telefon | Bluetooth tethering a mobilní data nebo jiné dostupné připojení. |
| MQTT broker | Dostupný z připojení telefonu; pro sledování zpráv také MQTT klient. |
| Zdroj a vodiče | Pro motorku vhodný chráněný napájecí zdroj. |

### 🔌 Zapojení

Všechna zařízení musí mít **společnou zem GND**. Signály ESP32 jsou 3,3 V;
ověř napájení a logické úrovně konkrétních modulů podle jejich dokumentace.

| Součást | Vývod modulu | ESP32 |
|---|---|---|
| Displej SPI | SCL / CLK | GPIO13 |
| Displej SPI | SDA / MOSI | GPIO14 |
| Displej SPI | DC | GPIO25 |
| Displej SPI | CS | GPIO26 |
| Displej SPI | RST | GPIO27 |
| MPU9250 / MPU6500 | SDA | GPIO21 |
| MPU9250 / MPU6500 | SCL | GPIO22 |
| MPU9250 / MPU6500 | VCC a GND | 3,3 V a GND |
| Tlačítko resetu | jeden kontakt | GPIO32 |
| Tlačítko resetu | druhý kontakt | GND |

Displej připoj k napájení podle jeho modulu; použité zapojení počítá s 3,3 V.
**SCL a SDA na displeji zde znamenají SPI hodiny a data**, nikoliv společnou
I²C sběrnici senzoru. Senzor se zkouší na I²C adresách `0x68` a `0x69`.
Některé moduly potřebují pro I²C nastavit další vývody nebo mají vlastní
pull-up rezistory; řiď se dokumentací modulu.

Termistor zapoj jako dělič:

```text
3,3 V ── 10 kΩ ──┬── NTC termistor ── GND
                 │
               GPIO34
```

Program předpokládá pevný rezistor 10 kΩ, termistor 7,9 kΩ při 24,6 °C a
beta 3950. Pro jiný termistor je nutné upravit výpočet v
[`main/sensors.cpp`](main/sensors.cpp). Přehled pinů je v
[`main/devicePins.h`](main/devicePins.h).

> ⚠️ **Motorka:** Klíček má zapínat a vypínat napájení celého ESP i displeje.
> Palubní napětí ani signál klíčku **nepřipojuj přímo** k ESP nebo displeji.
> Použij vhodný chráněný napájecí zdroj pro vozidlo a správné jištění;
> palubní síť může mít nebezpečné napěťové špičky.

### 🚀 Sestavení a nahrání

1. Nainstaluj **ESP-IDF 6.2.x** podle
   [oficiálního návodu](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
   a otevři terminál s aktivovaným prostředím ESP-IDF. Ověřit jej můžeš pomocí
   `idf.py --version`. Nestačí jen běžný terminál, kde `idf.py` není v cestě.
2. Naklonuj repozitář a otevři jeho složku ve VS Code nebo terminálu.
   Je to samostatný ESP-IDF projekt, nikoliv Arduino sketch.
3. Vytvoř **`main/ip.h`** a dosaď adresu svého brokeru:

   ```cpp
   #pragma once
   static constexpr char secretIP[] = "mqtt://ADRESA_BROKERU:1883";
   ```

   Soubor je ignorovaný Gitem a musíš jej vytvořit i po novém naklonování.
   Skrývá adresu před běžným commitem, **nezabezpečuje ji**: je obsažena ve
   výsledném firmwaru.
4. Připoj ESP32 kabelem USB a sestav a nahraj firmware. `COM5` nahraď svým
   portem:

   ```powershell
   idf.py build
   idf.py -p COM5 flash monitor
   ```

   Závislosti spravuje ESP-IDF; při prvním sestavení je potřeba internet.
   Monitor ukončíš **Ctrl+]**. NVS nemaž, pokud nechceš přijít o uložené
   rekordy a párování.

### 📱 Připojení telefonu a MQTT

1. V Androidu zapni **Bluetooth tethering** a Bluetooth.
2. V telefonu spáruj zařízení **`RoundMotoDisplay`**.
3. ESP se pokusí připojit k jedinému spárovanému zařízení. Po navázání PAN
   uloží jeho adresu pro další připojení. Pokud je před prvním úspěšným
   připojením spárovaných zařízení více, ESP si telefon automaticky nevybere.
4. Modrá ikona Bluetooth znamená, že ESP získalo IP adresu přes PAN;
   **nepotvrzuje připojení MQTT**. Stav MQTT ověř v sériovém monitoru.
5. V MQTT klientovi se přihlas ke stejnému brokeru a odebírej topic:

   ```text
   roundmotodisplay/telemetrie
   ```

   Zpráva přichází přibližně jednou za minutu, například:

   ```json
   {"teplota":24.5,"max_naklon_plus":30.2,"max_naklon_minus":-25.1}
   ```

Teplota je ve °C, náklony ve stupních. Nedostupná teplota se odešle jako
`null`. Pokud MQTT není připojeno, po obnovení se odešlou aktuální hodnoty,
nikoliv historie výpadku. Používá se QoS 1, proto se zpráva může opakovat.

> 🔒 **Zabezpečení:** Bluetooth párování Just Works nechrání proti aktivnímu
> podvržení; páruj v důvěryhodném prostředí. MQTT je nyní bez hesla a TLS,
> takže data nejsou chráněna před odposlechem ani neoprávněným přístupem.
> Veřejně dostupný broker proto vyžaduje další zabezpečení.

### 🔘 Reset náklonu

Stiskni tlačítko mezi **GPIO32 a GND**. Na displeji se objeví odpočet. Při
uvolnění před dvěma sekundami se reset neprovede; po dvou sekundách se rekordy
vynulují. Pokud zápis selže, displej ukáže chybu a firmware zápis opakuje.
Další měření začnou okamžitě tvořit nové rekordy. Kalibrace MPU zatím není
implementovaná.

### 🔎 Když něco nefunguje

| Problém | Co zkontrolovat |
|---|---|
| `idf.py` nebyl nalezen | Otevři terminál s aktivovaným prostředím ESP-IDF. |
| Telefon nevidí ESP | Zapni Bluetooth, zkontroluj napájení a hledej `RoundMotoDisplay`. |
| Ikona Bluetooth je šedá | Zkontroluj tethering a párování; šedá znamená, že zatím není PAN IP. |
| MQTT zprávy nepřicházejí | Ověř v monitoru připojení MQTT, dostupnost brokeru a přesný topic výše. |
| Náklon chybí nebo je nepřesný | Zkontroluj napájení a I²C zapojení MPU; hodnoty ovlivňují zrychlení a vibrace. |
| Reset se spouští sám | Zkontroluj tlačítko a spojení GPIO32 s GND. |

### 📄 Licence a závislosti

Projekt je pod [MIT licencí](LICENSE). ESP-IDF, ESP-MQTT a LovyanGFX mají
vlastní licence; řiď se podmínkami jejich repozitářů. Projekt používá pouze
ESP32 SPI část LovyanGFX kvůli kompatibilitě s ESP-IDF 6.2.

---

<a id="english"></a>

## 🇬🇧 English

RoundMotoDisplay is firmware for the **classic ESP32** and a round display.
It shows temperature and lean angle, remembers the maximum lean in both
directions, and sends readings to MQTT through a phone.

> **This is an experimental project.** Acceleration and vibration affect the
> accelerometer-based lean angle. It is not a precision or safety instrument;
> do not rely on it while riding.

### ✨ Features

| Feature | Description |
|---|---|
| 🌡️ Temperature | Thermistor; the display refreshes approximately every 20 s. |
| ↔️ Lean angle | Current lean on a −60° to +60° scale. |
| 🔴 Records | Red dots mark the maximum positive and negative lean. |
| 💾 Storage | Records are saved in flash NVS and restored at startup. |
| 🔘 Reset | Hold the button for 2 s to clear the records. |
| 📡 Bluetooth and MQTT | The phone provides Bluetooth tethering; the ESP sends JSON every minute. |

Values outside the scale are shown at its end. Before a direction has a
record, its maximum is 0. New records are written immediately; frequent writes
wear flash, and a power cut during a write can lose the latest value.

### 🧰 Parts

| Part | Notes |
|---|---|
| ESP32 with Bluetooth Classic | For example ESP32-WROOM-32; at least 4 MB flash. Chips without Bluetooth Classic, such as ESP32-C3 or S3, are not drop-in replacements for this firmware. |
| Round 240 × 240 SPI display | The firmware is configured for GC9A01; the actual controller used has not yet been confirmed. |
| MPU9250 or MPU6500 | An I²C module with 3.3 V-compatible logic. |
| NTC thermistor and 10 kΩ resistor | The thermistor is part of an input voltage divider. |
| Push button | Normally open; connects GPIO to GND when pressed. No external pull-up is needed. |
| Android phone | Bluetooth tethering and mobile data or another available connection. |
| MQTT broker | Reachable from the phone's connection; an MQTT client is also useful for viewing messages. |
| Power supply and wires | Use a protected power supply suitable for a vehicle. |

### 🔌 Wiring

All devices must share **GND**. ESP32 signals use 3.3 V; check the power and
logic requirements of your particular modules.

| Device | Module pin | ESP32 |
|---|---|---|
| SPI display | SCL / CLK | GPIO13 |
| SPI display | SDA / MOSI | GPIO14 |
| SPI display | DC | GPIO25 |
| SPI display | CS | GPIO26 |
| SPI display | RST | GPIO27 |
| MPU9250 / MPU6500 | SDA | GPIO21 |
| MPU9250 / MPU6500 | SCL | GPIO22 |
| MPU9250 / MPU6500 | VCC and GND | 3.3 V and GND |
| Reset button | one contact | GPIO32 |
| Reset button | other contact | GND |

Power the display according to its module's specifications; the setup used
here assumes 3.3 V. **SCL and SDA on the display are SPI clock and data**,
not the sensor's I²C bus. The firmware tries sensor addresses `0x68` and
`0x69`. Some modules require additional pins to select I²C or have their own
pull-ups; check your module's documentation.

Connect the thermistor as a divider:

```text
3.3 V ── 10 kΩ ──┬── NTC thermistor ── GND
                 │
               GPIO34
```

The calculation assumes a 10 kΩ fixed resistor, a 7.9 kΩ thermistor at
24.6 °C, and beta 3950. Adapt [`main/sensors.cpp`](main/sensors.cpp) for a
different thermistor. Pin definitions are in
[`main/devicePins.h`](main/devicePins.h).

> ⚠️ **Motorcycle power:** The ignition should switch power to the entire ESP
> and display. **Do not connect** the vehicle supply or ignition signal directly
> to the ESP or display. Use a protected automotive-suitable power supply and
> appropriate fusing; the vehicle electrical system can produce hazardous
> voltage transients.

### 🚀 Build and flash

1. Install **ESP-IDF 6.2.x** using the
   [official guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
   and open a terminal with the ESP-IDF environment activated. Check it with
   `idf.py --version`; a regular terminal may not have `idf.py` on its path.
2. Clone the repository and open its folder in VS Code or a terminal. This is
   a standalone ESP-IDF project, not an Arduino sketch.
3. Create **`main/ip.h`** and enter your MQTT broker address:

   ```cpp
   #pragma once
   static constexpr char secretIP[] = "mqtt://BROKER_ADDRESS:1883";
   ```

   The file is Git-ignored and must also be created after a fresh clone.
   Ignoring it keeps the address out of ordinary commits but **does not make
   it secret**: the address is included in the compiled firmware.
4. Connect the ESP32 via USB, then build and flash. Replace `COM5` with your
   port:

   ```powershell
   idf.py build
   idf.py -p COM5 flash monitor
   ```

   ESP-IDF manages dependencies; the first build needs internet access.
   Exit the monitor with **Ctrl+]**. Do not erase NVS unless you want to
   remove saved records and pairing.

### 📱 Phone connection and MQTT

1. Enable **Bluetooth tethering** and Bluetooth on the Android phone.
2. Pair the phone with **`RoundMotoDisplay`**.
3. The ESP attempts to connect to the only paired device. After PAN connects,
   it saves that device's address for future connections. If multiple devices
   are paired before the first successful connection, it will not choose one
   automatically.
4. A blue Bluetooth icon means the ESP obtained an IP address over PAN;
   **it does not confirm MQTT connectivity**. Check MQTT status in the serial
   monitor.
5. Connect an MQTT client to the same broker and subscribe to:

   ```text
   roundmotodisplay/telemetrie
   ```

   A message arrives approximately once a minute, for example:

   ```json
   {"teplota":24.5,"max_naklon_plus":30.2,"max_naklon_minus":-25.1}
   ```

Temperature is in °C and angles in degrees. Unavailable temperature is sent
as `null`. When MQTT reconnects, current readings are sent rather than a
history of the outage. QoS 1 is used, so messages may be duplicated.

> 🔒 **Security:** Bluetooth Just Works pairing does not prevent active
> impersonation; pair in a trusted environment. MQTT currently has no password
> or TLS, so data is not protected against interception or unauthorized access.
> A publicly accessible broker therefore needs additional security.

### 🔘 Resetting lean records

Press the button between **GPIO32 and GND**. The display shows a countdown.
Releasing before two seconds cancels the reset; holding for two seconds clears
the records. If writing fails, the display reports an error and the firmware
retries. Subsequent readings immediately start new records. MPU calibration
is not implemented yet.

### 🔎 Troubleshooting

| Problem | Check |
|---|---|
| `idf.py` not found | Open a terminal with the ESP-IDF environment activated. |
| Phone cannot see the ESP | Enable Bluetooth, check power, and look for `RoundMotoDisplay`. |
| Bluetooth icon is grey | Check tethering and pairing; grey means there is no PAN IP yet. |
| No MQTT messages | Check MQTT connection in the monitor, broker reachability, and the exact topic above. |
| Lean reading is missing or inaccurate | Check MPU power and I²C wiring; acceleration and vibration affect readings. |
| Reset triggers by itself | Check the button and GPIO32-to-GND wiring. |

### 📄 License and dependencies

This project is released under the [MIT License](LICENSE). ESP-IDF, ESP-MQTT
and LovyanGFX have their own licenses; follow the terms in their respective
repositories. The project uses only the ESP32 SPI part of LovyanGFX for
compatibility with ESP-IDF 6.2.
