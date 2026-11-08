# Rekuperacja: ESPHome na magistrali Compit C14

Firmware ESPHome dla ESP32-S3, które stoi między panelem NANO XS a sterownikiem Compit AERO 6
i daje Home Assistant odczyty oraz sterowanie rekuperacją. Opis protokołu poniżej powstał przez
obserwację magistrali we własnym urządzeniu; nie jest to dokumentacja producenta.

## Jak to działa

Przewody A i B między panelem a AERO są rozcięte. ESP z dwoma modułami RS485 przepisuje każdą
ramkę z jednej strony na drugą bez zmian, więc rozmrażanie i pozostałe funkcje AERO działają jak
bez niego. HA steruje przez zapis parametrów panelu, a panel sam przekazuje je do AERO i pokazuje
na diodach prawdziwy stan.

- `rekuperacja.yaml`: konfiguracja ESPHome (encje, dekodowanie, wykrywanie nieznanych zmian)
- `c14_relay.h`: przekaźnik ramek na UART1/UART2 w zadaniach FreeRTOS
- `secrets.yaml.example`: nazwy sekretów do uzupełnienia

## Sprzęt

ESP32-S3-N16R8 DevKit oraz dwa moduły „RS485 V2.05” z usuniętym rezystorem 120 Ω („121”).
Opisy pinów modułu są z jego punktu widzenia: dane wychodzą na TXD. EN w stanie wysokim oznacza
nadawanie.

| Moduł | TXD | RXD | EN | A/B |
|---|---|---|---|---|
| P (panel) | GPIO8 | GPIO9 | GPIO10 | przewody do panelu |
| S (rekuperacja) | GPIO4 | GPIO5 | GPIO6 | zaciski w AERO |

VCC modułów do 3V3, GND do G magistrali. G i U przechodzą między panelem a AERO bez zmian.
U to 17 V zasilania panelu; nie podłączać bezpośrednio do ESP.

## Wgrywanie

1. Skopiuj `secrets.yaml.example` jako `secrets.yaml` i uzupełnij.
2. `esphome run rekuperacja.yaml --device 192.168.20.18`

ESP stoi na drodze sygnału, więc przy restarcie panel na chwilę traci połączenie z AERO i może
zapikać.

## Protokół C14

9600 8N1. Ramki 30-bajtowe, zakończone `0x23`, dane 7-bitowe.

| Bajt | Znaczenie |
|---|---|
| [0] | `0x80 \| adres docelowy` (`E4` = rozgłoszenie) |
| [1] | typ |
| [2] | suma kontrolna: suma pozostałych bajtów `& 0x7F` |
| [3] | adres nadawcy |

Adresy: panel `0x3E`, AERO `0x63`. Panel jest masterem magistrali.

**Stan AERO** (`E4:21:..:63`, odpowiedź na `E3:44`)

| Bajt | Znaczenie |
|---|---|
| [6] | kod awarii (panel pika, gdy różny od zera) |
| [8] | rzeczywista prędkość wentylatorów w % |
| [12..19] | cztery temperatury `((hi<<7 \| lo) - 2048) / 10` °C: nawiew, czerpnia, wyrzutnia, wywiew |
| [28] bit 0x02 | znacznik progu czerpni ok. −3,3 °C, znaczenie niepotwierdzone |

**Ustawienia panelu** (`E4:21:..:3E`)

| Bajt | Znaczenie |
|---|---|
| [16] | prędkość zadana w % |
| [17], [18] | prędkość dzień, noc w % |
| [19] | `0x40 \| bit0 praca \| bit1 tryb dzień` |
| [25], [26], [27] | wyrzutnia, nawiew, czerpnia zaokrąglone do 1 °C, ok. T + 45 |

**Polecenia do panelu** (cel `BE`, odpowiedź ma typ `polecenie | 0x20`)

| Typ | Działanie |
|---|---|
| `0x44` | rozgłoszenie ustawień |
| `0x45`, `0x46` | przekazanie magistrali: panel milknie na 1–1,5 min |
| `0x49` | identyfikacja („NANO XS”) |
| `0x52` | odczyt parametrów |
| `0x54` | nieznane |
| `0x57` | zapis parametrów |

Treść odczytu i zapisu: `[4] = 7E`, potem 6 wpisów `[nr hi][nr lo][wart hi][wart lo]`.
Wartość to `n + 2000` w zapisie 7-bitowym; `5D:5F` oznacza brak parametru.

**Parametry panelu**

| Nr | Znaczenie |
|---|---|
| 00:01 | własny adres (62) |
| 00:02 | rola mastera, **nie zapisywać** |
| 00:06 | praca (1 wł., 0 wył.) |
| 00:07 | tryb (1 dzień, 0 noc) |
| 00:08, 00:09 | prędkość dzień, noc (w krokach co 10 %) |
| 00:15..00:17 | odpytywane adresy (112, 99, 41) |

## Uwagi

- **Zapis 0 do parametru 00:02 zatrzymuje odpytywanie magistrali** i panel zaczyna pikać.
- Sekwencje `0x45`/`0x46` wyciszają panel na ponad minutę, a watchdog restartuje ESP dopiero po
  150 s ciszy.
- Na kafelkach HA minimalna prędkość to 30 %. Panel ma pełny zakres, a ESP go nie ogranicza.
- Sposób działania odmrażania na magistrali nie jest jeszcze znany.
