#include <SPI.h>
#include <Wire.h>
#include <Arduino.h>
#include <oled.h>

#include "Adafruit_CCS811.h"

// SH1106
const int DISPLAY_BREITE = 128;
const int DISPLAY_HOEHE = 64;

OLED display = OLED(D4, D3, NO_RESET_PIN, 0x3C, DISPLAY_BREITE, DISPLAY_HOEHE, true);

Adafruit_CCS811 ccs;

// Display Logik Steuerung

int x = 0;
int y = 0;

int y_delta = 0;
float y_float = 0.0;

void setup() {
  Serial.begin(115200);

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

  Serial.println("Starte Display");
  display.begin();
  // hinterhertreten, CCS811 arbeitet mit I2C clock stretching
  Wire.setClockStretchLimit(500);
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

  co2();
  tvoc();

  display_x_erhoehen();
  
  // ungefähre Temperatur
  float temp = ccs.calculateTemperature();
  Serial.print(" ppb   Temp:");
  Serial.println(temp);

  delay(500);
}

/**
 * CO2
 * 400 ppm -> y 0 
 * 1000 ppm -> y 63 (Max)
 */
void co2()
{
  // 
  Serial.print("eCO2: ");
  float eCO2 = ccs.geteCO2();
  Serial.print(eCO2);
  y_delta = abs(eCO2) - 400;
  y_float = (float) y_delta / 600.0;
  y = abs(y_float * (DISPLAY_HOEHE - 1)) % (DISPLAY_HOEHE - 1);
  
  display.draw_pixel(x, y);
  
  display.setCursor(0, 50);
  display.print("CO2: " + (String) eCO2 + " ppm");
  
  display.display();
  
}

/**
 * TVOC -  Total Volatile Organic Compounds  - Gesamt flüchtige organische Verbindungen
 */
void tvoc()
{
  Serial.print(" ppm, TVOC: ");
  float TVOC = ccs.getTVOC();
  Serial.print(TVOC);
}


void display_x_erhoehen()
{
  x++;
  if (x == 128) {
    display.clear();
    x = 0;
  }
}
