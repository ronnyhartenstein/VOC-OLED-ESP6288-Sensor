#include <SPI.h>
#include <Wire.h>
#include <Arduino.h>
#include <oled.h>
#include <ESP8266WiFi.h>

#include "Adafruit_CCS811.h"
#include "Secrets.h"


// SH1106
const int DISPLAY_BREITE = 128;
const int DISPLAY_HOEHE = 64;

OLED display = OLED(D4, D3, NO_RESET_PIN, 0x3C, DISPLAY_BREITE, DISPLAY_HOEHE, true);

Adafruit_CCS811 ccs;

// Display Logik Steuerung

int y[DISPLAY_BREITE];
int x = 0;
int y_curr = 0;

int y_delta = 0;
float y_float = 0.0;

int eCO2_curr = 0;
int TVOC_curr = 0;

void setup() {
  Serial.begin(115200);
  delay(10);

  setup_voc();
  setup_display();
  setup_wifi();
}

void setup_voc() {
  Serial.println("Starte VOC");
  if (!ccs.begin()) {
    Serial.println("Failed to start sensor! Please check your wiring.");
    while (1);
  }

  //calibrate temperature sensor
  Serial.println("Auf VOC warten");
  while (!ccs.available());

  Serial.println("Temperatur prüfen");
  float temp = ccs.calculateTemperature();
  ccs.setTempOffset(temp - 25.0);
}

void setup_display() {
  Serial.println("Starte Display");
  display.begin();
  // hinterhertreten, CCS811 arbeitet mit I2C clock stretching
  Wire.setClockStretchLimit(500);
}

void setup_wifi() {
  Serial.print("Verbinde mit WiFi SSID ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


void loop() {
  if (!ccs.available()) {
    delay(500);
    return;
  }

  if (!!ccs.readData()) {
    Serial.println("FEHLER!");
    while (1);
  }

  lese_co2();
  lese_tvoc();
  lese_temp();

  display_rendern();

  x_erhoehen();

  delay(500);
}

/**
   CO2
   400 ppm -> y 0
   1000 ppm -> y 63 (Max)
*/
void lese_co2()
{
  Serial.print("eCO2: ");
  float eCO2 = ccs.geteCO2();
  eCO2_curr = abs(eCO2);
  Serial.print(eCO2);
  int y_delta = abs(eCO2) - 400;
  float y_float = (float) y_delta / 600.0;
  int y_curr = abs(y_float * (DISPLAY_HOEHE - 1));
  if (y_curr > (DISPLAY_HOEHE - 1)) {
    y_curr = DISPLAY_HOEHE - 1;
  }
  y[x] = y_curr;
}

/**
   TVOC -  Total Volatile Organic Compounds  - Gesamt flüchtige organische Verbindungen
*/
void lese_tvoc()
{
  Serial.print(" ppm, TVOC: ");
  float TVOC = ccs.getTVOC();
  TVOC_curr = abs(TVOC);
  Serial.print(TVOC);
}

/**
   ungefähre Temperatur
*/
void lese_temp()
{
  float temp = ccs.calculateTemperature();
  Serial.print(" ppb   Temp:");
  Serial.println(temp);
}


void x_erhoehen()
{
  x++;
  if (x == 128) {
    x = 0;
  }
}

/**
   Gesammelte Werte auf OLED Display darstellen
   Screen komplett neu aufbauen
*/
void display_rendern()
{
  display.clear();

  // Kurve malen
  int tmp_x = 0;
  while (tmp_x < 128) {
    // invers
    display.draw_pixel(tmp_x, 63 - y[tmp_x]);
    tmp_x++;
  }

  // Werte ausschreiben
  display.setCursor(1, 1);
  display.print("CO2: " + (String) eCO2_curr + " ppm");

  display.display();

}
