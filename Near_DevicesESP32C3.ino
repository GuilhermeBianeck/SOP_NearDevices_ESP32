#include <WiFi.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <ArduinoJson.h>

#define WIFI_SSID "STONKS"
#define WIFI_PASSWORD "STONKS"
#define MQTT_SERVER "192.168.31.124"
#define MQTT_PORT 1883
#define DEVICE_ID "ESP32C3_Device"
#define JSON_BUFFER_SIZE 2048
#define MQTT_RECONNECT_DELAY_MS 500
#define WIFI_CONNECT_DELAY_MS 500
#define MQTT_PUBLISH_DELAY_MS 500
#define SCAN_INTERVAL_MS 500

WiFiClient espClient;
PubSubClient client(espClient);
BLEScan* pBLEScan;

DynamicJsonDocument doc(JSON_BUFFER_SIZE);
JsonArray mainDeviceArray = doc.createNestedArray(DEVICE_ID);

unsigned long previousMQTTConnectAttempt = 0;

void attemptMQTTConnect() {
  if (millis() - previousMQTTConnectAttempt > MQTT_RECONNECT_DELAY_MS) {
    previousMQTTConnectAttempt = millis();
    Serial.println("Attempting MQTT connection...");
    if (client.connect(DEVICE_ID)) {
      Serial.println("Connected to MQTT");
    } else {
      Serial.println("Failed to connect. Retrying...");
    }
  }
}

class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    String deviceName = advertisedDevice.getAddress().toString().c_str();
    if (!deviceName.isEmpty()) {
      if (!client.connected()) {
        attemptMQTTConnect();
      }
      Serial.println("Found device: " + deviceName);
      JsonObject device = mainDeviceArray.createNestedObject();
      device["id"] = deviceName;
      device["rssi"] = advertisedDevice.getRSSI();
      Serial.println("Device data added to JSON document");
    }
  }
};

AdvertisedDeviceCallbacks myAdvertisedDeviceCallbacks;

void publishScannedDevicesData() {
  if (mainDeviceArray.size() > 0 && client.connected()) {
    String jsonString;
    serializeJson(doc, jsonString);
    String topic = "/ble/scannedDevices";
    bool isPublished = client.publish(topic.c_str(), jsonString.c_str(), true);
    client.loop();
    if (isPublished) {
      Serial.println("Published data: " + jsonString);
      doc.clear();
      mainDeviceArray = doc.createNestedArray(DEVICE_ID);
    } else {
      Serial.println("Publish failed! - Buffer Size" + String(jsonString.length()));
    }
    delay(MQTT_PUBLISH_DELAY_MS);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing serial communication...");
  client.setServer(MQTT_SERVER, MQTT_PORT);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting to WiFi...");
    delay(WIFI_CONNECT_DELAY_MS);
  }
  Serial.println("Connected to WiFi.");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(&myAdvertisedDeviceCallbacks);
  pBLEScan->setActiveScan(true);
  Serial.println("BLE scanning initiated.");
}

unsigned long previousScan = 0;

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousScan > SCAN_INTERVAL_MS) {
    previousScan = currentMillis;
    if (!client.connected()) {
      attemptMQTTConnect();
    }
    client.loop();
    pBLEScan->start(1, false);
    publishScannedDevicesData();
  }
}
