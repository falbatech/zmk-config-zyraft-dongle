# zmk-config-zyraft-dongle

Konfiguracja ZMK dla **Zyra FT** (Cradio/Sweep, 34 klawisze) **z donglem BT**.

Bazowane na sprawdzonej strukturze codex (kwmlodozeniec/ferris-sweep-zmk-config).

## Architektura

- **Lewa połówka** - peripheral, BLE → dongle
- **Prawa połówka** - peripheral, BLE → dongle  
- **Dongle** - USB → komputer, BLE central dla obu połówek, ZMK Studio

## Hardware

- 2× nice!nano v2 (lewa, prawa połówka)
- 1× nice!nano v2 (dongle, USB do komputera)
- Wszystkie 3 z bootloaderem ZMK

## Pinout (kopia stock cradio/sweep)

GPIO direct, 17 input-gpios per stronę:
- Pro Micro pins: 7, 18, 19, 20, 21, 15, 14, 16, 10, 1, 2, 3, 4, 5, 6, 8, 9

## Warstwy (4)

| # | Nazwa | Funkcja |
|---|-------|---------|
| 0 | Base | QWERTY + home-row mods |
| 1 | Right | Cyfry, nawigacja (lewy thumb) |
| 2 | Left | Symbole (prawy thumb) |
| 3 | Tri | **System** + BT controls (oba thumby) |

## Bluetooth - 5 urządzeń + 2 połówki = 7 connections

W warstwie Tri:

| Klawisz | Funkcja |
|---------|---------|
| `Z` | Profil BT 0 |
| `X` | Profil BT 1 |
| `C` | Profil BT 2 |
| `V` | Profil BT 3 |
| `B` | Profil BT 4 |
| `N` | Wyczyść aktywny profil |
| `M` | Wyczyść wszystkie profile |
| `,` | Tryb USB |
| `.` | Tryb Bluetooth |

## ZMK Studio

Aktywne na donglu (dwa buildy: `zyraft_dongle_studio` z Studio i `zyraft_dongle` bez).

Procedura unlock: trzymaj oba thumby aktywujące Tri (TAB + BSPC) - wciśnij `Q`.

## Build artifacts (5 plików UF2)

| Plik | Co flashuje |
|------|------|
| `settings_reset` | OBOWIĄZKOWY - czysci BLE bondings |
| `zyraft_dongle_studio` | Dongle z ZMK Studio (default) |
| `zyraft_dongle` | Dongle bez Studio (fallback) |
| `zyraft_left` | Lewa peripheral |
| `zyraft_right` | Prawa peripheral |

## Procedura flashowania

**KROK 1: Settings reset na WSZYSTKICH 3 nice!nano**

Każdy: USB → 2× reset → przeciągnij `settings_reset-*.uf2` → odczekaj 10 sek.

**KROK 2: Wgraj docelowy firmware**

W kolejności:
1. Dongle: `zyraft_dongle_studio-*.uf2`
2. Lewa: `zyraft_left-*.uf2`
3. Prawa: `zyraft_right-*.uf2`

**KROK 3: Test**

1. Włącz baterie w połówkach
2. Podłącz dongle przez USB
3. Czekaj 30 sekund - peripherals same się sparują z donglem
4. Komputer widzi "Zyra FT DGL" jako USB HID
5. Klawisze piszą

## Różnica względem `zmk-config-zyraft` (bez dongla)

| | Bez dongla | Z donglem |
|---|---|---|
| USB | Lewa połówka | Dongle |
| Central | Lewa | Dongle |
| BLE profiles | 5 | 5 + 2 peripherals |
| Bateria lewa | krótka (BT host) | długa (peripheral) |
| Bateria prawa | długa (peripheral) | długa (peripheral) |

## Wsparcie

FalbaTech - [falbatech.click](https://falbatech.click)
