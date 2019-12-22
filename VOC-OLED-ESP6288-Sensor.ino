#include <SPI.h>
#include <Wire.h>
#include <Arduino.h>
#include <oled.h>
#include <ESP8266WiFi.h>

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include "Adafruit_CCS811.h"
#include "Secrets.h"


// --------- OLED Display SH1106

const int DISPLAY_BREITE = 128;
const int DISPLAY_HOEHE = 64;

OLED display = OLED(D4, D3, NO_RESET_PIN, 0x3C, DISPLAY_BREITE, DISPLAY_HOEHE, true);

Adafruit_CCS811 ccs;

// --------- Display Render Steuerung

int y[DISPLAY_BREITE];
int x = 0;
int y_curr = 0;

int y_delta = 0;
float y_float = 0.0;

int eCO2_curr = 0;
int TVOC_curr = 0;

int loop_nr = 0;

// -------- MQTT -------

WiFiClient client;

Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_PORT, MQTT_USER, MQTT_KEY);

Adafruit_MQTT_Publish pub_eco2 = Adafruit_MQTT_Publish(&mqtt, MQTT_USER "arduino/voc/eco2");
Adafruit_MQTT_Publish pub_tvoc = Adafruit_MQTT_Publish(&mqtt, MQTT_USER "arduino/voc/tvoc");
Adafruit_MQTT_Publish pub_temp = Adafruit_MQTT_Publish(&mqtt, MQTT_USER "arduino/voc/temp");


// ---------- SETUP --------

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
  Serial.println(WLAN_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// --------- LOOP ---------

void loop() {
  MQTT_connect();
  
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

  // erste Minute alle 0,5 Sek messen
  if (loop_nr < 120) {
    delay(500);
  } 
  // zweite Minute alle 10 Sek messen
  else if (loop_nr < 120 + 6) {
    delay(10000);
  }
  // dann alle 30 Sek
  else {
    delay(30000);
  }
  loop_nr++;
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

  Serial.print(F("\nPublish "));
  Serial.print("...");
  if (! pub_eco2.publish(eCO2_curr)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }

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

  Serial.print(F("\nPublish "));
  Serial.print("...");
  if (! pub_tvoc.publish(TVOC_curr)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }
}

/**
   ungefähre Temperatur
*/
void lese_temp()
{
  float temp = ccs.calculateTemperature();
  Serial.print(" ppb   Temp:");
  Serial.println(temp);

  Serial.print(F("\nPublish "));
  Serial.print("...");
  if (! pub_temp.publish(temp)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }
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


void MQTT_connect() {
  int8_t ret;

  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}
