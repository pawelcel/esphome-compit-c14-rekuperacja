#pragma once
// Przekaznik (man-in-the-middle) magistrali C14 miedzy panelem NANO XS a sterownikiem Compit AERO 6.
// Dwa moduly RS485, kazdy na wlasnym odcinku magistrali; ESP32 przepisuje ramki z jednej strony na druga.
// Dziala w osobnych zadaniach FreeRTOS na sterowniku UART z ESP-IDF, zeby petla ESPHome (Wi-Fi, API)
// nie opozniala przekazywania. Do petli ESPHome trafiaja tylko kopie ramek (kolejka) - do logu i czujnikow.

#include <atomic>
#include <cstring>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace c14 {

enum Strona : uint8_t { OD_PANELU = 0, OD_STEROWNIKA = 1 };

struct Ramka {
  uint8_t strona;
  uint8_t dl;
  bool zmieniona;  // przepisana przez sterowanie z HA
  uint8_t b[64];
};

struct Port {
  uart_port_t uart;
  int tx, rx, en;
  // Modul "RS485 V2.05" ma odbiornik stale wlaczony, wiec slyszy wlasne nadawanie. Do tej chwili (us)
  // wszystko, co przyjdzie na ten port, to echo naszej ramki i jest odrzucane.
  std::atomic<int64_t> echo_do{0};
};

inline Port PANEL{UART_NUM_1, 9, 8, 10};       // modul przy panelu: RXD<-GPIO9, TXD->GPIO8, EN<-GPIO10
inline Port STEROWNIK{UART_NUM_2, 5, 4, 6};    // modul przy rekuperacji: RXD<-GPIO5, TXD->GPIO4, EN<-GPIO6

// Sterowanie z HA: gdy aktywne, ramka ustawien od panelu (E4:21:xx:3E) jest przepisywana przed wyslaniem
// do sterownika. Bajty: [16] predkosc zadana, [17] dzien %, [18] noc %, [19] bit0 praca, bit1 tryb dzien.
inline std::atomic<bool> nadpisz{false};
inline std::atomic<uint8_t> nadpisz_dzien{40};
inline std::atomic<uint8_t> nadpisz_noc{30};
inline std::atomic<bool> nadpisz_praca{true};
inline std::atomic<bool> nadpisz_tryb_dzien{true};

inline std::atomic<uint32_t> licznik[2] = {0, 0};
inline std::atomic<uint32_t> echo_odrzucone{0};
inline QueueHandle_t kolejka = nullptr;

inline void suma_kontrolna(uint8_t *b, size_t n) {
  uint32_t s = 0;
  for (size_t k = 0; k < n; k++)
    if (k != 2) s += b[k];
  b[2] = s & 0x7F;
}

// Ustawiona, gdy panel zmienil ustawienia przy wlaczonym sterowaniu z HA (petla ESPHome odswieza wtedy encje).
inline std::atomic<bool> zmiana_z_panelu{false};
// Na czas prob z panelem: zmiany na panelu nie sa przenoszone do rekuperacji.
inline std::atomic<bool> adopcja_wstrzymana{false};
// Surowe ustawienia z ostatniej ramki panelu (przed podmiana): [17] << 16 | [18] << 8 | [19].
inline std::atomic<int> panel_surowe{-1};

// Proby: podmiana jednego bajtu w odpowiedzi rekuperacji (E4:21:xx:63) wysylanej do PANELU,
// przez stan_ile kolejnych ramek. Rekuperacja tego nie widzi.
inline std::atomic<int> stan_bajt{-1};
inline std::atomic<int> stan_wartosc{0};
inline std::atomic<int> stan_ile{0};

inline bool przepisz_stan(uint8_t *b, size_t n) {
  if (stan_ile.load() <= 0 || n != 30 || b[0] != 0xE4 || b[1] != 0x21 || b[3] != 0x63) return false;
  int k = stan_bajt.load();
  if (k < 3 || k > 28) return false;
  b[k] = stan_wartosc.load() & 0x7F;
  suma_kontrolna(b, n);
  stan_ile--;
  return true;
}

inline bool przepisz_ustawienia(uint8_t *b, size_t n) {
  if (n != 30 || b[0] != 0xE4 || b[1] != 0x21 || b[3] != 0x3E || b[29] != 0x23) return false;
  // "Ostatnia zmiana wygrywa": jesli ktos zmienil cos na panelu, przejmujemy wartosci panelu do sterowania z HA.
  static int ost17 = -1, ost18 = -1, ost19 = -1;
  bool panel_zmienil = ost17 >= 0 && (b[17] != ost17 || b[18] != ost18 || (b[19] & 0x03) != (ost19 & 0x03));
  ost17 = b[17];
  ost18 = b[18];
  ost19 = b[19];
  panel_surowe = (b[17] << 16) | (b[18] << 8) | b[19];
  // Przejmowanie zmian z panelu dziala zawsze (takze bez podmiany), bo HA zapisuje ustawienia do panelu.
  if (panel_zmienil && !adopcja_wstrzymana.load()) {
    nadpisz_dzien = b[17];
    nadpisz_noc = b[18];
    nadpisz_praca = b[19] & 0x01;
    nadpisz_tryb_dzien = b[19] & 0x02;
    zmiana_z_panelu = true;
  }
  // Podmiana (sterowanie z HA "na sile") - od 2026-11-02 wylaczona: panel sam dostaje ustawienia z HA, a ramka
  // panelu ma isc do rekuperacji nietknieta (np. na wypadek, gdyby odmrazanie zmienialo w niej predkosc).
  if (!nadpisz.load()) return false;
  bool praca = nadpisz_praca.load(), dzien = nadpisz_tryb_dzien.load();
  b[17] = nadpisz_dzien.load();
  b[18] = nadpisz_noc.load();
  b[16] = praca ? (dzien ? b[17] : b[18]) : 0;
  b[19] = (b[19] & ~0x03) | (praca ? 0x01 : 0) | (dzien ? 0x02 : 0);
  suma_kontrolna(b, n);
  return true;
}

inline void nadaj(Port &p, const uint8_t *b, size_t n) {
  p.echo_do.store(INT64_MAX);
  gpio_set_level((gpio_num_t) p.en, 1);
  esp_rom_delay_us(100);
  uart_write_bytes(p.uart, b, n);
  uart_wait_tx_done(p.uart, pdMS_TO_TICKS(200));
  esp_rom_delay_us(200);
  gpio_set_level((gpio_num_t) p.en, 0);
  // Echo ostatnich bajtow moze dotrzec ze sterownika UART z opoznieniem (timeout odbioru) - 6 ms zapasu.
  p.echo_do.store(esp_timer_get_time() + 6000);
}

// Proby odpowiadania panelowi na zapytania bez odpowiedzi (F0/A9/81 - prawdopodobnie do nieobecnych urzadzen,
// np. modulu internetowego Compit). Jedna ramka, wysylana tylko na odcinek panelu, opoznienie_ms po zapytaniu.
// Kolejka kilku krokow rozmowy: krok k czeka na ramke od panelu o bajtach [0],[1] = wyzwalacz (b1 = -1: dowolny),
// po czym (po opoznieniu) wysyla swoja ramke do panelu. Kroki sa wykonywane po kolei.
struct Krok {
  int b0, b1, opoznienie_ms, dl;
  uint8_t ramka[64];
};
inline Krok kroki[6];
inline std::atomic<int> kroki_ile{0};
inline std::atomic<int> krok_nr{0};
inline uint8_t odp_ramka[64];
inline std::atomic<int> odp_dl{0};
inline std::atomic<bool> odp_wyslana{false};

// Zapis parametrow PANELU (ustalone 2026-11-02): ramka BE:57:cs:01:7E + 6 wpisow [nr hi][nr lo][wart hi][wart lo],
// wartosc = liczba + 2000 zapisana 7-bitowo. Panel odpowiada 81:77 (echo) i sam zmienia swoje ustawienia.
// Parametry: 00:06 praca (1 wl, 0 wyl), 00:07 tryb (1 dzien, 0 noc), 00:08 predkosc dzien, 00:09 predkosc noc
// (w krokach po 10 %).
// NIE zapisywac 00:02 - to rola panelu na magistrali (0 = panel przestaje odpytywac).
// Wysylane jako krok rozmowy po zapytaniu panelu 81:44 (wtedy panel czeka na odpowiedz adresu 1).
inline void zapisz_panel(int dzien_pct, int noc_pct, bool tryb_dzien, bool praca) {
  struct { int nr, wart; } w[4] = {{0x07, tryb_dzien ? 1 : 0}, {0x08, dzien_pct / 10}, {0x09, noc_pct / 10},
                                   {0x06, praca ? 1 : 0}};
  Krok &kr = kroki[0];
  uint8_t *b = kr.ramka;
  b[0] = 0xBE; b[1] = 0x57; b[2] = 0; b[3] = 0x01; b[4] = 0x7E;
  for (int k = 0; k < 6; k++) {
    auto &e = w[k < 4 ? k : 3];
    int v = e.wart + 2000;
    b[5 + 4 * k] = 0x00;
    b[6 + 4 * k] = e.nr;
    b[7 + 4 * k] = (v >> 7) & 0x7F;
    b[8 + 4 * k] = v & 0x7F;
  }
  b[29] = 0x23;
  suma_kontrolna(b, 30);
  kr.b0 = 0x81;
  kr.b1 = 0x44;
  kr.opoznienie_ms = 50;
  kr.dl = 30;
  krok_nr = 0;
  kroki_ile = 1;
}

struct Kierunek {
  Port *z;
  Port *do_;
  Strona strona;
};

inline void zadanie(void *arg) {
  auto *k = (Kierunek *) arg;
  Ramka r;
  for (;;) {
    int n = uart_read_bytes(k->z->uart, r.b, 1, portMAX_DELAY);
    if (n != 1) continue;
    int64_t poczatek = esp_timer_get_time();
    // Bajty w ramce leca co ~1,04 ms; 4 ms ciszy = koniec ramki (min. 1 tick, gdyby tick byl dluzszy).
    const TickType_t cisza = pdMS_TO_TICKS(4) ? pdMS_TO_TICKS(4) : 1;
    while (n < (int) sizeof(r.b) && uart_read_bytes(k->z->uart, r.b + n, 1, cisza) == 1) n++;
    if (poczatek < k->z->echo_do.load()) {
      echo_odrzucone++;
      continue;
    }
    r.dl = n;
    r.strona = k->strona;
    r.zmieniona = k->strona == OD_PANELU ? przepisz_ustawienia(r.b, n) : przepisz_stan(r.b, n);
    nadaj(*k->do_, r.b, n);
    licznik[k->strona]++;
    int nr = krok_nr.load();
    if (k->strona == OD_PANELU && n >= 2 && nr < kroki_ile.load()) {
      Krok &kr = kroki[nr];
      if (r.b[0] == kr.b0 && (kr.b1 < 0 || r.b[1] == kr.b1)) {
        krok_nr = nr + 1;
        vTaskDelay(pdMS_TO_TICKS(kr.opoznienie_ms));
        nadaj(PANEL, kr.ramka, kr.dl);
        memcpy(odp_ramka, kr.ramka, kr.dl);
        odp_dl = kr.dl;
        odp_wyslana = true;
        // Kolejne kroki z wyzwalaczem "++" (b0 == -2) leca od razu po poprzednim, bez czekania na panel
        // (np. gdy panel oddal nam role nadzorcy i sam milczy).
        while (krok_nr.load() < kroki_ile.load() && kroki[krok_nr.load()].b0 == -2) {
          Krok &nx = kroki[krok_nr.load()];
          krok_nr = krok_nr.load() + 1;
          vTaskDelay(pdMS_TO_TICKS(nx.opoznienie_ms > 0 ? nx.opoznienie_ms : 300));
          nadaj(PANEL, nx.ramka, nx.dl);
          while (odp_wyslana.load()) vTaskDelay(pdMS_TO_TICKS(5));
          memcpy(odp_ramka, nx.ramka, nx.dl);
          odp_dl = nx.dl;
          odp_wyslana = true;
        }
      }
    }
    if (kolejka) xQueueSend(kolejka, &r, 0);
  }
}

inline void przygotuj_port(Port &p) {
  gpio_reset_pin((gpio_num_t) p.en);
  gpio_set_direction((gpio_num_t) p.en, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t) p.en, 0);
  uart_config_t cfg = {};
  cfg.baud_rate = 9600;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  uart_driver_install(p.uart, 1024, 1024, 0, nullptr, 0);
  uart_param_config(p.uart, &cfg);
  uart_set_pin(p.uart, p.tx, p.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  // Domyslnie sterownik oddaje bajty dopiero po 10 znakach ciszy - skracamy do 2, zeby nie opozniac ramek.
  uart_set_rx_timeout(p.uart, 2);
}

inline void start() {
  static bool uruchomiony = false;
  if (uruchomiony) return;
  uruchomiony = true;
  kolejka = xQueueCreate(16, sizeof(Ramka));
  przygotuj_port(PANEL);
  przygotuj_port(STEROWNIK);
  static Kierunek p2s{&PANEL, &STEROWNIK, OD_PANELU};
  static Kierunek s2p{&STEROWNIK, &PANEL, OD_STEROWNIKA};
  // Priorytet wyzszy niz petla ESPHome (1), zeby przekazywanie nie czekalo na Wi-Fi/API.
  xTaskCreatePinnedToCore(zadanie, "c14_p2s", 4096, &p2s, 10, nullptr, 1);
  xTaskCreatePinnedToCore(zadanie, "c14_s2p", 4096, &s2p, 10, nullptr, 1);
}

}  // namespace c14
