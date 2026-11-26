#include "display.h"
#include "devicePins.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <LovyanGFX.hpp>
#include "bitmaps.h"

static constexpr uint16_t CERNA = 0x0000;
static constexpr uint16_t BILA = 0xFFFF;
static constexpr uint16_t SEDA = 0x7BEF;
static constexpr uint16_t TYRKYSOVA = 0x07FF;
static constexpr uint16_t CERVENA = 0xF800;
static constexpr float PI = 3.14159265358979323846f;
static constexpr int POLOMER_ZNACKY = 3;

class Displej : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel;
  lgfx::Bus_SPI bus;
public:
  Displej() {
    auto b = bus.config();
    b.spi_host = SPI2_HOST;
    b.freq_write = 27000000;
    b.pin_sclk = DevicePins::DISPLAY_SCLK;
    b.pin_mosi = DevicePins::DISPLAY_MOSI;
    b.pin_miso = -1;
    b.pin_dc = DevicePins::DISPLAY_DC;
    bus.config(b);
    panel.setBus(&bus);

    auto p = panel.config();
    p.pin_cs = DevicePins::DISPLAY_CS;
    p.pin_rst = DevicePins::DISPLAY_RST;
    p.panel_width = 240;
    p.panel_height = 240;
    p.invert = true;
    panel.config(p);
    setPanel(&panel);
  }
};

static Displej tft;
static int predchoziUhel = 0;

struct Znacka {
  int x;
  int y;
  bool platna;
};
static Znacka znacky[2] = {};

static void drawUsek(int zacatek, int konec, uint16_t barva) {
  if (zacatek > konec) std::swap(zacatek, konec);
  if (zacatek != konec) tft.fillArc(120, 120, 115, 108, 270 + zacatek, 270 + konec, barva);
}

esp_err_t displayInit() {
  if (!tft.init()) return ESP_FAIL;
  tft.setRotation(2);
  tft.fillScreen(CERNA);
  tft.setTextColor(BILA, CERNA);
  return ESP_OK;
}

static void drawZakladStupnice() {
  tft.fillArc(120, 120, 115, 108, 0, 360, SEDA);
  tft.drawCircle(120, 120, 117, SEDA);
  for (int naklon = -60; naklon <= 60; naklon += 5) {
    float uhel = naklon * PI / 180.0f;
    int vnitrniPolomer = naklon % 10 == 0 ? 94 : 98;
    int x1 = lroundf(120 + sinf(uhel) * vnitrniPolomer);
    int y1 = lroundf(120 - cosf(uhel) * vnitrniPolomer);
    int x2 = lroundf(120 + sinf(uhel) * 102);
    int y2 = lroundf(120 - cosf(uhel) * 102);
    tft.drawLine(x1, y1, x2, y2, naklon % 10 == 0 ? BILA : SEDA);
  }
}

void drawScale() {
  drawZakladStupnice();
  predchoziUhel = 0;
  for (auto &znacka : znacky) znacka.platna = false;
}

static void drawZnacky() {
  for (const auto &znacka : znacky) {
    if (znacka.platna) tft.fillCircle(znacka.x, znacka.y, POLOMER_ZNACKY, CERVENA);
  }
}

static void obnovPodZnackou(const Znacka &znacka) {
  if (!znacka.platna) return;
  int prumer = 2 * POLOMER_ZNACKY + 1;
  tft.setClipRect(znacka.x - POLOMER_ZNACKY, znacka.y - POLOMER_ZNACKY, prumer, prumer);
  tft.fillRect(znacka.x - POLOMER_ZNACKY, znacka.y - POLOMER_ZNACKY, prumer, prumer, CERNA);
  drawZakladStupnice();
  drawUsek(0, predchoziUhel, TYRKYSOVA);
  tft.clearClipRect();
}

void updateMaxScale(float zaporny, float kladny) {
  const float hodnoty[] = {zaporny, kladny};
  Znacka nove[2] = {};
  bool zmena = false;
  for (int i = 0; i < 2; i++) {
    if (std::isfinite(hodnoty[i])) {
      float uhel = std::max(-60.0f, std::min(60.0f, hodnoty[i])) * PI / 180.0f;
      nove[i] = {static_cast<int>(lroundf(120 + sinf(uhel) * 111)),
                 static_cast<int>(lroundf(120 - cosf(uhel) * 111)), true};
    }
    if (nove[i].platna != znacky[i].platna ||
        (nove[i].platna && (nove[i].x != znacky[i].x || nove[i].y != znacky[i].y))) {
      obnovPodZnackou(znacky[i]);
      zmena = true;
    }
  }
  if (!zmena) return;
  for (int i = 0; i < 2; i++) znacky[i] = nove[i];
  drawZnacky();
}

void updateScale(float naklon) {
  int uhel = std::isfinite(naklon) ? lroundf(std::max(-60.0f, std::min(60.0f, naklon))) : 0;
  if (uhel == predchoziUhel) return;
  if ((uhel >= 0 && predchoziUhel >= 0) || (uhel <= 0 && predchoziUhel <= 0)) {
    if (abs(uhel) > abs(predchoziUhel)) {
      drawUsek(predchoziUhel, uhel, TYRKYSOVA);
    } else {
      drawUsek(predchoziUhel, uhel, SEDA);
      if (uhel > 0) drawUsek(uhel - 1, uhel, TYRKYSOVA);
      if (uhel < 0) drawUsek(uhel, uhel + 1, TYRKYSOVA);
    }
  } else {
    drawUsek(predchoziUhel, 0, SEDA);
    drawUsek(0, uhel, TYRKYSOVA);
  }
  predchoziUhel = uhel;
  drawZnacky();
}

void drawCelsius() {
  tft.setTextColor(0x001F, CERNA);
  tft.drawCentreString("C", 124, 183, &lgfx::fonts::Font4);
  tft.drawCentreString("O", 110, 180, &lgfx::fonts::Font2);
}

void drawTeplota(float teplota) {
  char text[24];
  if (std::isfinite(teplota)) snprintf(text, sizeof(text), "%.1f", double(teplota));
  else snprintf(text, sizeof(text), "--.-");
  tft.setTextColor(BILA, CERNA);
  tft.setTextPadding(194);
  tft.drawCentreString(text, 121, 90, &lgfx::fonts::Font8);
  tft.setTextPadding(0);
}

void drawBtStatus(bool online) {
  tft.setSwapBytes(true);
  tft.pushImage((240 - BT_width) / 2, 45, BT_width, BT_height, online ? BT : BT_OFF, BILA);
}

void drawResetScreen(int faze) {
  static constexpr uint16_t TMAVA = 0x212B;
  static constexpr uint16_t ZLATA = 0xFD20;
  static constexpr uint16_t ZELENA = 0x07B2;
  uint16_t barva = faze == 3 ? CERVENA : (faze == 2 ? ZELENA : ZLATA);

  tft.startWrite();
  for (int y = 0; y < 240; y++) {
    uint16_t barvaPozadi = tft.color565(8 + y * 48 / 239, 8 + y * 80 / 239, 24 + y * 110 / 239);
    tft.drawFastHLine(0, y, 240, barvaPozadi);
  }
  tft.endWrite();
  tft.fillArc(120, 120, 115, 107, 0, 360, TMAVA);
  tft.drawCircle(120, 120, 118, TYRKYSOVA);
  if (faze == 1) tft.fillArc(120, 120, 115, 107, 270, 450, barva);
  if (faze >= 2) tft.fillArc(120, 120, 115, 107, 0, 360, barva);

  tft.setTextPadding(0);
  tft.setTextSize(1);
  tft.setTextColor(TYRKYSOVA);
  tft.drawCentreString("MAX", 120, 40, &lgfx::fonts::Font4);
  tft.setTextSize(2);
  tft.setTextColor(BILA);
  tft.drawCentreString("RESET", 120, 81, &lgfx::fonts::Font4);
  tft.setTextSize(1.5f);
  tft.setTextColor(barva);
  const char *stav = faze == 0 ? "2" : faze == 1 ? "1" : faze == 2 ? "OK" : "CHYBA";
  tft.drawCentreString(stav, 120, 145, &lgfx::fonts::Font4);
  tft.setTextSize(1);
}

void drawMainScreen(float naklon, float zaporny, float kladny, float teplota, bool bt) {
  tft.fillScreen(CERNA);
  drawScale();
  updateScale(naklon);
  updateMaxScale(zaporny, kladny);
  drawCelsius();
  drawTeplota(teplota);
  drawBtStatus(bt);
}
