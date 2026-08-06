#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_213_B74.h>

static const int EPD_RST  = 8;
static const int EPD_DC   = 9;
static const int EPD_CS   = 10;
static const int EPD_BUSY = 11;
static const int EINK_SCK  = 13;
static const int EINK_MOSI = 12;
static const int EINK_MISO = -1;

SPIClass hspi(HSPI);
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("GxEPD2 + HSPI test");

  hspi.begin(EINK_SCK, EINK_MISO, EINK_MOSI, -1);
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));

  display.init(115200, true, 2, true);
  display.setRotation(1);
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawRect(0, 0, display.width(), display.height(), GxEPD_BLACK);
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(2);
    display.setCursor(10, 28);
    display.print("HELLO");
    display.setTextSize(1);
    display.setCursor(10, 55);
    display.print("PlatformIO works!");
  } while (display.nextPage());

  Serial.println("Done.");
}

void loop() {}
