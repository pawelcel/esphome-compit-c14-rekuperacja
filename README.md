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

## Z czym to działa

Sprawdzone tylko na jednej instalacji: sterownik **Compit AERO 6** z panelem **NANO XS**. Adresy
na magistrali i numery parametrów mogą być inne w innych zestawach.

W innym zestawie zacznij od samego odczytu:

1. Sprawdź, czy ramki się dekodują i czy temperatury zgadzają się z rzeczywistością.
2. Odczytaj wszystkie parametry panelu poleceniem `0x52`.
3. Zmień coś na panelu przyciskiem i odczytaj je ponownie. Różnica pokaże numer parametru.
4. Dopiero potem zapisuj, i tylko parametry, których znaczenie już znasz.

## Gdy ESP nie działa

Przewody A i B są rozcięte, więc **bez działającego i zasilanego ESP panel nie ma łączności z
AERO**. Panel może wtedy pikać. Nie sprawdzano, jak AERO zachowuje się przez dłuższy czas bez
komunikacji z panelem.

Powrót do stanu pierwotnego: wyłącz zasilanie rekuperacji, odłącz moduły RS485 i połącz ze sobą
przewody A z panelu i z AERO oraz B z panelu i z AERO. G i U pozostają bez zmian. Panel i AERO
działają wtedy jak przed instalacją.

## Lista części (BOM)

| Ilość | Część | Uwagi |
|---|---|---|
| 1 | Płytka ESP32-S3 DevKit (użyta: ESP32-S3-N16R8) | konfiguracja: `esp32-s3-devkitc-1`, flash 4 MB |
| 2 | Moduł TTL–RS485 „RS485 V2.05” z wejściem EN | zasilany z 3,3 V; trzeba usunąć rezystor 120 Ω „121” |
| 1 | Przewody połączeniowe | do ESP i do zacisków A, B, G |
| 1 | Złączki do rozcięcia A i B | ułatwiają powrót do oryginalnego połączenia |
| 1 | Obudowa | opcjonalnie |

Do tego wybierz **jeden** ze sposobów zasilania ESP:

| Wariant | Części | Uwagi |
|---|---|---|
| **A. Ładowarka USB** | ładowarka 5 V, kabel USB | ładowarka musi być dobrej jakości: tania wprowadzała zakłócenia na magistralę |
| **B. Z linii U z AERO (17 V)** | **dodatkowo** przetwornica obniżająca Pololu **D24V5F5** (5 V, 0,5 A, wejście do 36 V) | nie trzeba gniazdka ani ładowarki przy centrali; **sprawdzone w działaniu** na AERO 6 z NANO XS |

Podłączenie wariantu B:

| D24V5F5 | Gdzie |
|---|---|
| VIN | przewód U magistrali (17 V) |
| GND | przewód G magistrali, wspólna masa z ESP |
| VOUT | pin **5V** ESP32-S3, **nie** 3V3 |
| SHDN | niepodłączony |

W wariancie B:

- Przed podłączeniem do ESP zmierz VOUT miernikiem: powinno być ok. 5 V.
- Nie podłączaj jednocześnie kabla USB. Do aktualizacji wystarczy Wi-Fi, a przy wgrywaniu przez
  USB najpierw odłącz przetwornicę.
- Linia U zasila też panel, a jej wydajność prądowa nie jest podana. Na AERO 6 z NANO XS wystarcza.
  W innym zestawie sprawdź po podłączeniu, czy panel działa normalnie i czy ESP nie restartuje się
  samo. ESP z modułami pobiera szacunkowo 0,1–0,25 A przy 5 V.

Narzędzia: lutownica do usunięcia rezystora, miernik, kabel USB do pierwszego wgrania firmware.

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

## Instalacja w Home Assistant

1. Zainstaluj dodatek **ESPHome Device Builder**.
2. Skopiuj `rekuperacja.yaml` i `c14_relay.h` do `/config/esphome`.
3. Dopisz do `/config/esphome/secrets.yaml` wartości z `secrets.yaml.example`. Klucz API
   wygenerujesz poleceniem `openssl rand -base64 32`.
4. W `rekuperacja.yaml` ustaw własne `manual_ip` i `use_address` w sekcji `wifi`.
5. Pierwszy raz wgraj firmware przez USB, kolejne razy przez Wi-Fi:
   `esphome run rekuperacja.yaml --device 192.168.20.18`
6. W HA wejdź w Ustawienia → Urządzenia i usługi → Dodaj integrację → **ESPHome**. Podaj adres
   IP urządzenia, port 6053 i klucz API z `c14_api_key`.

Jeśli ESP jest w innej sieci niż HA, nie zostanie wykryte automatycznie, dlatego dodaje się je po
adresie IP. Zapora musi przepuszczać ruch z HA do ESP na porcie TCP 6053.

ESP stoi na drodze sygnału, więc przy restarcie panel na chwilę traci połączenie z AERO i może
zapikać.

## Encje w Home Assistant

Urządzenie nazywa się „Rekuperacja”, więc identyfikatory mają przedrostek `rekuperacja_`.

**Sterowanie**

| Encja | Znaczenie |
|---|---|
| `switch.rekuperacja_praca_ha` | włączenie i wyłączenie rekuperacji |
| `select.rekuperacja_tryb_ha` | tryb: Dzień lub Noc |
| `number.rekuperacja_predkosc_dzien_ha` | prędkość dzienna, 30–100 % co 10 % |
| `number.rekuperacja_predkosc_noc_ha` | prędkość nocna, 30–100 % co 10 % |
| `switch.rekuperacja_sterowanie_z_ha` | podmiana ramek ustawień w drodze do AERO, **powinna być wyłączona** |

Zmiana na panelu jest przepisywana do tych encji, więc obowiązuje ostatnia zmiana, z HA albo z
panelu. Przy włączonym `sterowanie_z_ha` ESP przestaje przekazywać ramki ustawień bez zmian.

**Odczyty**

| Encja | Znaczenie |
|---|---|
| `sensor.rekuperacja_temperatura_nawiewu` | powietrze nawiewane do domu, °C |
| `sensor.rekuperacja_temperatura_wywiewu` | powietrze wyciągane z domu, °C |
| `sensor.rekuperacja_temperatura_czerpni` | powietrze z zewnątrz, °C |
| `sensor.rekuperacja_temperatura_wyrzutni` | powietrze wyrzucane na zewnątrz, °C |
| `sensor.rekuperacja_wentylatory` | rzeczywista prędkość wentylatorów, % |
| `sensor.rekuperacja_predkosc_zadana` | prędkość zadana w tej chwili, % |
| `sensor.rekuperacja_predkosc_dzien` | prędkość dzienna z panelu, % |
| `sensor.rekuperacja_predkosc_noc` | prędkość nocna z panelu, % |
| `sensor.rekuperacja_tryb` | tryb z panelu |
| `sensor.rekuperacja_sprawnosc_odzysku` | sprawność odzysku liczona na ESP, % |
| `binary_sensor.rekuperacja_praca` | czy rekuperacja pracuje |

Sprawność to `(nawiew − czerpnia) / (wywiew − czerpnia) × 100`, liczona co 60 s. Nie jest
wyliczana, gdy wentylatory stoją albo różnica temperatur jest mniejsza niż 5 °C.

**Diagnostyka**

| Encja | Znaczenie |
|---|---|
| `binary_sensor.rekuperacja_awaria` | AERO zgłasza awarię |
| `sensor.rekuperacja_kod_awarii` | kod awarii (bajt [6] stanu), 0 = brak |
| `sensor.rekuperacja_nieznana_zmiana_c14` | ostatnia zmiana niezdekodowanego bajtu lub nowy typ ramki |
| `sensor.rekuperacja_ramki_od_panelu` | licznik ramek od panelu |
| `sensor.rekuperacja_ramki_od_rekuperacji` | licznik ramek od AERO |
| `sensor.rekuperacja_bledne_ramki_c14` | licznik błędnych ramek |
| `sensor.rekuperacja_sygnal_wi_fi` | sygnał Wi-Fi, dBm |

Firmware udostępnia też akcje diagnostyczne `rozmowa_z_panelem`, `wyslij_do_panelu`,
`zmien_stan_dla_panelu` i `wstrzymaj_adopcje`. Służą do badania protokołu i mogą wysyłać dowolne
ramki do panelu, więc nie należy używać ich w automatyzacjach.

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

## Rozwiązywanie problemów

| Objaw | Co sprawdzić |
|---|---|
| Brak ramek, liczniki stoją | Opisy pinów modułu są z jego punktu widzenia: dane wychodzą na **TXD**. Zamień A i B, jeśli nadal nic nie ma. |
| Dużo błędnych ramek | Usuń z modułów rezystor 120 Ω „121”. Wymień ładowarkę zasilającą ESP: zaszumiona dawała zakłócenia ok. 16 kHz. |
| ESP nie nadaje | EN musi być w stanie wysokim podczas nadawania. |
| Panel pika, `kod_awarii` różny od 0 | AERO zgłasza awarię, sprawdź kod na panelu. |
| Panel pika i milczy ok. 1–1,5 min | Po poleceniu `0x45` lub `0x46` wraca sam. Restart ESP tego nie przyspiesza. |
| Panel przestał odpytywać magistralę | Prawdopodobnie zapisano 0 do 00:02. Zapisz 1 akcją `wyslij_do_panelu`, bo wysyła ona ramkę od razu, bez czekania na odpytanie. |
| Zmiana z HA nie pojawia się na panelu | Sprawdź, czy rosną oba liczniki ramek. Zapis idzie dopiero po odpytaniu `81:44` z cyklu trwającego ok. 4 s. |
| Urządzenie niedostępne w HA | Sprawdź adres IP, klucz API i port TCP 6053 na zaporze. Słaby sygnał przy centrali też może rozłączać. |

## Czego jeszcze nie wiadomo

- Jak na magistrali wygląda odmrażanie. Nie wystąpiło jeszcze przy −7,2 °C na czerpni.
- Co robi polecenie `0x54`.
- Znaczenie bitu 0x02 w bajcie [28]: przełącza się przy czerpni ok. −3,3 °C i nie zmienia pracy
  wentylatorów, a firmware pomija go w „Nieznanej zmianie C14”.
- Bit 0x08 bajtu [28] stanu ustawia w ramce panelu [11] = `0x20`. Możliwe, że to by-pass.
- Większość parametrów panelu 00:00–00:28 i pozostałe bajty ramki stanu.

Nowe wartości pokazuje `sensor.rekuperacja_nieznana_zmiana_c14`.
