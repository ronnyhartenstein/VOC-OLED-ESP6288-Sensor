#include <SPI.h>
#include <Wire.h>
#include <Arduino.h>
#include <oled.h>
#include <ESP8266WiFi.h>

#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

#include <Adafruit_CCS811.h>

#include <DHT.h>

#include "Secrets.h"

#define I2C_SDA D4
#define I2C_SCL D3


// --------- VOC Sensor CCS811

Adafruit_CCS811 ccs;

// --------- Temperatur/Luftfeuchte DHD22

#define DHT22PIN D7
#define DHT22TYPE DHT22

DHT dht22(DHT22PIN, DHT22TYPE);


// --------- OLED Display SH1106

const int DISPLAY_BREITE = 128;
const int DISPLAY_HOEHE = 64;

// SDA, SCL, Reset PIN, I2C addr, width, height, isSH1106)
OLED display = OLED(I2C_SDA, I2C_SCL, NO_RESET_PIN, 0x3C, DISPLAY_BREITE, DISPLAY_HOEHE, true);

// --------- Display Render Steuerung

int y[DISPLAY_BREITE];
int x = 0;
int y_curr = 0;

int y_delta = 0;
float y_float = 0.0;

int eCO2_curr = 0;
int TVOC_curr = 0;

int loop_nr = 0;

#define ECO2_MIN 400
#define ECO2_MAX 5000

// -------- MQTT -------

WiFiClient client;

Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_PORT, MQTT_USER, MQTT_KEY);

Adafruit_MQTT_Publish pub_voc_eco2 = Adafruit_MQTT_Publish(&mqtt, MQTT_USER "arduino/voc/eco2");
Adafruit_MQTT_Publish pub_voc_tvoc = Adafruit_MQTT_Publish(&mqtt, MQTT_USER "arduino/voc/tvoc");
Adafruit_MQTT_Publish pub_voc_temp = Adafruit_MQTT_Publish(&mqtt, MQTT_USER "arduino/voc/temp");

Adafruit_MQTT_Publish pub_baro_temp = Adafruit_MQTT_Publish(&mqtt, MQTT_USER "arduino/baro/temp");
Adafruit_MQTT_Publish pub_baro_humi = Adafruit_MQTT_Publish(&mqtt, MQTT_USER "arduino/baro/humi");

bool wlan_aktiv = false;

// ---------- SETUP --------

void setup() {
  Serial.begin(115200);
  delay(100);

  setup_voc_ccs811();
  delay(100);
  setup_temp_dht22();
  delay(100);
  setup_display_sh1106();
  delay(100);
  setup_wifi();
  delay(100);
}

void setup_voc_ccs811() {
  Serial.println("Starte VOC");
  if (!ccs.begin()) {
    Serial.println("Failed to start VOC Sensor CCS811!");
    while (1);
  }

  //calibrate temperature sensor
  Serial.println("Auf VOC warten");
  while (!ccs.available());

  Serial.println("Temperatur prüfen");
  float temp = ccs.calculateTemperature();
  ccs.setTempOffset(temp - 25.0);

  // am Anfang alle Sek.
  Serial.println("--> CSS Mode: 1 Sek");
  ccs.setDriveMode(CCS811_DRIVE_MODE_1SEC);
}

void setup_display_sh1106() {
  Serial.println("Starte Display");
  display.begin();
  // hinterhertreten, CCS811 arbeitet mit I2C clock stretching
  Wire.setClockStretchLimit(500);
}

void setup_temp_dht22() {
  Serial.println("Starte DHD22");
  dht22.begin();
}

void setup_wifi() {
  Serial.println("Verbinde mit WiFi SSID " + String(WLAN_SSID));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WLAN_SSID, WLAN_PASSWORD);

  int versuch = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (versuch == 10) {
      Serial.println(F("WLAN Verbindung fehlgeschlagen, dann eben ohne MQTT Publish"));
      break;
    }
    versuch++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wlan_aktiv = true;
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

// --------- LOOP ---------

void loop() {
  if (wlan_aktiv) {
    MQTT_connect();
  }

  float temp = lese_dht22_temp();
  float humi = lese_dht22_humi();

  int ccs_warte_zyklen = 10;
  while (!ccs.available()) {
    Serial.println("Auf VOC warten (loop) .. ");
    delay(500);
    if (ccs_warte_zyklen > 0) {
      ccs_warte_zyklen--;
    } else {
      return;
    }
  }

  Serial.println("CCS811: Setze Korrekturwerte: " + String(humi) + " / " + String(temp));
  ccs.setEnvironmentalData(humi, temp);
  if (!!ccs.readData()) {
    Serial.println("FEHLER!");
    while (1);
  }

  lese_voc_co2();
  lese_voc_tvoc();
  //lese_voc_temp();

  display_rendern();

  x_erhoehen();

  // ersten 10 Sek. alle 1 Sek messen
  if (loop_nr < 10) {
    delay(1000);
  }
  // nächste 50 Sek. alle 10 Sek messen
  else if (loop_nr < 10 + 5) {
    if (loop_nr == 10) {
      Serial.println("--> CSS Mode: alle 10 Sek");
      ccs.setDriveMode(CCS811_DRIVE_MODE_10SEC);
    }
    delay(10000);
  }
  // dann alle 60 Sek
  else {
    if (loop_nr == 15) {
      Serial.println("--> CSS Mode: alle 60 Sek");
      ccs.setDriveMode(CCS811_DRIVE_MODE_60SEC);
    }
    delay(60000);
  }
  loop_nr++;
}

/**
   CO2
   400 ppm -> y 0
   1000 ppm -> y 63 (Max)
*/
void lese_voc_co2()
{
  float eCO2 = ccs.geteCO2();
  eCO2_curr = abs(eCO2);

  Serial.println("eCO2: " + String(eCO2_curr) + " ppm");
  publish_mqtt(pub_voc_eco2, eCO2_curr);

  int y_delta = abs(eCO2) - 400;
  float y_float = (float) y_delta / (ECO2_MAX - ECO2_MIN);
  int y_curr = abs(y_float * (DISPLAY_HOEHE - 1));
  if (y_curr > (DISPLAY_HOEHE - 1)) {
    y_curr = DISPLAY_HOEHE - 1;
  }
  y[x] = y_curr;

}

/**
   TVOC -  Total Volatile Organic Compounds  - Gesamt flüchtige organische Verbindungen
*/
void lese_voc_tvoc()
{
  float TVOC = ccs.getTVOC();
  TVOC_curr = abs(TVOC);

  Serial.println("TVOC: " + String(TVOC_curr)  + " ppb");
  publish_mqtt(pub_voc_tvoc, TVOC);
}

/**
   ungefähre Temperatur
*/
void lese_voc_temp()
{
  float temp = ccs.calculateTemperature();

  Serial.println("Temp: " + String(temp) + " °C");
  publish_mqtt(pub_voc_temp, temp);
}

float lese_dht22_temp()
{
  float temp = dht22.readTemperature();

  Serial.println("DHT22 Temp: " + String(temp) + " °C");
  publish_mqtt(pub_baro_temp, temp);

  return temp;
}

float lese_dht22_humi()
{
  float humi = dht22.readHumidity();

  Serial.println("DHT22 Humi:" + String(humi) + " %");
  publish_mqtt(pub_baro_humi, humi);

  return humi;
}

void publish_mqtt(Adafruit_MQTT_Publish pub, float val)
{
  if (!wlan_aktiv) {
    return;
  }
  //Serial.print(F("\nPublish "));
  //Serial.print("...");
  if (! pub.publish(val)) {
    Serial.println(F("MQTT Publish Failed"));
  } else {
    //Serial.println(F("OK!"));
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
