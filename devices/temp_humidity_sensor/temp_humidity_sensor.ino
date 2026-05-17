/**
 * ESP8266 温湿度传感器设备 (MQTT 版)
 * 功能：通过 UDP 广播发现 MQTT Broker，连接后注册设备并定时发布传感器数据
 * 硬件：ESP8266 + DHT11 温湿度传感器 + SSD1306 OLED 显示屏
 * 依赖：ESP8266WiFi, PubSubClient, ArduinoJson, Adafruit_SSD1306, DHT
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ==================== OLED 配置 ====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== DHT 传感器配置 ====================
#define DHTPIN 2       // GPIO2 (D4)
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ==================== WiFi 配置 ====================
const char* ssid     = "十香の携帯電話";
const char* password = "00000000";

// ==================== 设备信息 ====================
const char* DEVICE_ID   = "SENSOR_BEDROOM_001";
const char* DEVICE_NAME = "卧室温湿度传感器";
const char* DEVICE_TYPE = "sensor_device";

// ==================== 网络配置 ====================
WiFiUDP udp;
WiFiClient espClient;
PubSubClient mqtt(espClient);

const int UDP_PORT = 8888;
const int MQTT_PORT = 1883;
const char* TOPIC_PREFIX = "homemind";

const unsigned long STATE_PUBLISH_INTERVAL = 10000;
const unsigned long RECONNECT_INTERVAL = 5000;
const unsigned long BROKER_DISCOVERY_INTERVAL = 3000;
const unsigned long SENSOR_READ_INTERVAL = 2000;
const unsigned long OLED_UPDATE_INTERVAL = 1000;

// ==================== MQTT 主题 ====================
String topicRegister;
String topicState;
String topicCmd;
String topicOffline;
String topicScan;

// ==================== Broker 发现 ====================
bool brokerFound = false;
String brokerIp;
unsigned long lastDiscoverySend = 0;

// ==================== 传感器数据 ====================
float temperature = 0.0;
float humidity = 0.0;
int lightLevel = 0;
bool motionDetected = false;
const int LIGHT_PIN = A0;
const int MOTION_PIN = 14;

// ==================== 时间控制 ====================
unsigned long lastSensorRead = 0;
unsigned long lastStatePublish = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastOLEDUpdate = 0;

// ==================== MQTT 主题构建 ====================
void buildTopics() {
  topicRegister = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/register";
  topicState    = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/state";
  topicCmd      = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/cmd";
  topicOffline  = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/offline";
  topicScan     = String(TOPIC_PREFIX) + "/scan";
}

// ==================== 注册 JSON ====================
String generateRegisterJson() {
  StaticJsonDocument<256> doc;
  doc["device_type"] = DEVICE_TYPE;
  doc["device_name"] = DEVICE_NAME;
  doc["ip"] = WiFi.localIP().toString();
  doc["tcp_port"] = 0;
  String out;
  serializeJson(doc, out);
  return out;
}

// ==================== 状态 JSON ====================
String generateStateJson() {
  StaticJsonDocument<256> doc;
  doc["device_type"] = DEVICE_TYPE;
  doc["device_name"] = DEVICE_NAME;
  doc["ip"] = WiFi.localIP().toString();
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["light"] = lightLevel;
  doc["motion"] = motionDetected;
  String out;
  serializeJson(doc, out);
  return out;
}

// ==================== MQTT 回调 ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("[MQTT] ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(msg);

  String topicStr = String(topic);

  if (topicStr == topicCmd) {
    handleCommand(msg);
  } else if (topicStr == topicScan) {
    publishRegister();
  }
}

// ==================== 处理命令 ====================
void handleCommand(const String& msg) {
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) return;

  String cmdType = doc["type"].as<String>();
  if (cmdType == "get_sensor_data" || cmdType == "query") {
    publishState();
  }
}

// ==================== MQTT 发布 ====================
void publishRegister() {
  if (!mqtt.connected()) return;
  String json = generateRegisterJson();
  mqtt.publish(topicRegister.c_str(), json.c_str(), true);
  Serial.println("[MQTT] Register sent: " + json);
}

void publishState() {
  if (!mqtt.connected()) return;
  String json = generateStateJson();
  mqtt.publish(topicState.c_str(), json.c_str(), false);
  Serial.println("[MQTT] State sent: " + json);
}

// ==================== MQTT 连接 ====================
void connectMqtt() {
  if (!mqtt.connected() && millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
    lastReconnectAttempt = millis();
    Serial.print("[MQTT] Connecting to broker at ");
    Serial.print(brokerIp);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    String willTopic = topicOffline;
    String willMsg = "{\"device_id\":\"" + String(DEVICE_ID) + "\"}";

    if (mqtt.connect(DEVICE_ID, NULL, NULL, willTopic.c_str(), 0, true, willMsg.c_str())) {
      Serial.println("[MQTT] Connected!");

      mqtt.subscribe(topicCmd.c_str());
      mqtt.subscribe(topicScan.c_str());
      Serial.print("[MQTT] Subscribed: ");
      Serial.println(topicCmd);
      Serial.print("[MQTT] Subscribed: ");
      Serial.println(topicScan);

      publishRegister();
      publishState();
    } else {
      Serial.print("[MQTT] Failed, rc=");
      Serial.println(mqtt.state());
    }
  }
}

// ==================== UDP Broker 发现 ====================
void discoverBroker() {
  if (brokerFound) return;

  int packetSize = udp.parsePacket();
  if (packetSize) {
    char buf[256];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len > 0) {
      buf[len] = 0;
      String packet = String(buf);

      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, packet);
      if (!err && doc["type"].as<String>() == "mqtt_discover") {
        String ip = doc["broker_ip"].as<String>();
        if (ip.length() > 0) {
          brokerIp = ip;
          brokerFound = true;
          Serial.print("[Discovery] Broker found at ");
          Serial.println(brokerIp);
          mqtt.setServer(brokerIp.c_str(), MQTT_PORT);
        }
      }
    }
  }

  if (!brokerFound && millis() - lastDiscoverySend > BROKER_DISCOVERY_INTERVAL) {
    lastDiscoverySend = millis();
    Serial.println("[Discovery] Waiting for broker broadcast...");
  }
}

// ==================== 读取传感器 ====================
void readSensors() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    humidity = h;
    temperature = t;
  }

  int lightRaw = analogRead(LIGHT_PIN);
  lightLevel = map(lightRaw, 0, 1023, 0, 100);
  motionDetected = digitalRead(MOTION_PIN) == HIGH;

  Serial.printf("Sensor: T=%.1f H=%.1f L=%d M=%s\n",
                temperature, humidity, lightLevel, motionDetected ? "Y" : "N");
}

// ==================== OLED 更新 ====================
void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("温湿度传感器");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setCursor(0, 15);
  display.print("温度:");
  display.setTextSize(2);
  display.setCursor(40, 14);
  display.print(temperature, 1);
  display.setTextSize(1);
  display.setCursor(90, 14);
  display.print("C");

  display.setCursor(0, 36);
  display.print("湿度:");
  display.setTextSize(2);
  display.setCursor(40, 35);
  display.print(humidity, 1);
  display.setTextSize(1);
  display.setCursor(90, 35);
  display.print("%");

  display.setCursor(0, 56);
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi" : "NoWiFi");
  display.setCursor(45, 56);
  display.print(mqtt.connected() ? "MQTT" : "----");

  display.display();
}

void showOLEDMessage(String line1, String line2) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line1);
  display.setCursor(0, 35);
  display.println(line2);
  display.display();
}

// ==================== WiFi 连接 ====================
void connectWiFi() {
  showOLEDMessage("连接WiFi", ssid);
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    showOLEDMessage("WiFi已连接", WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi failed!");
    showOLEDMessage("WiFi连接失败", "请检查配置");
  }
}

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP8266 Sensor (MQTT) ===");

  pinMode(MOTION_PIN, INPUT);
  pinMode(LIGHT_PIN, INPUT);

  Wire.begin(4, 5);
  Wire.setClock(50000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("OLED not found!");
    }
  }

  showOLEDMessage("温湿度传感器", "正在启动...");
  delay(1000);

  dht.begin();

  connectWiFi();

  buildTopics();

  if (WiFi.status() == WL_CONNECTED) {
    udp.begin(UDP_PORT);
    Serial.printf("UDP listening on port %d\n", UDP_PORT);

    mqtt.setServer("0.0.0.0", MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setKeepAlive(30);
  }
}

// ==================== loop ====================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      udp.begin(UDP_PORT);
    }
  }

  if (!brokerFound) {
    discoverBroker();
  } else {
    if (!mqtt.connected()) {
      connectMqtt();
    }
    mqtt.loop();

    if (mqtt.connected() && millis() - lastStatePublish > STATE_PUBLISH_INTERVAL) {
      lastStatePublish = millis();
      publishState();
    }
  }

  if (millis() - lastSensorRead >= SENSOR_READ_INTERVAL) {
    readSensors();
    lastSensorRead = millis();
  }

  if (millis() - lastOLEDUpdate >= OLED_UPDATE_INTERVAL) {
    updateOLED();
    lastOLEDUpdate = millis();
  }

  delay(10);
}
