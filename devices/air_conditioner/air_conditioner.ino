/*
 * ESP32-WROOM-32 空调控制器 (MQTT 版)
 * 功能：
 *   1. 通过 UDP 广播发现 MQTT Broker IP
 *   2. 连接 MQTT Broker，注册设备，订阅命令
 *   3. 接收 MQTT 命令控制空调状态
 *   4. OLED 显示空调状态（温度、模式、风速等）
 *
 * 硬件：ESP32-WROOM-32 + SSD1306 OLED (I2C, 128x64)
 * 依赖：U8g2lib, PubSubClient, ArduinoJson
 */

#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ==================== 配置区 ====================
const char* ssid     = "十香の携帯電話";
const char* password = "00000000";
const char* DEVICE_ID   = "AC_LIVING_001";
const char* DEVICE_NAME = "客厅空调";
const char* DEVICE_TYPE = "ac_device";

const int   UDP_PORT = 8888;
const int   MQTT_PORT = 1883;
const char* TOPIC_PREFIX = "homemind";
const int   STATE_PUBLISH_INTERVAL = 5000;
const int   RECONNECT_INTERVAL = 5000;
const int   BROKER_DISCOVERY_INTERVAL = 3000;
const int   BROKER_TIMEOUT = 15000;
// ================================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

WiFiUDP udp;
WiFiClient espClient;
PubSubClient mqtt(espClient);

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
unsigned long brokerFoundTime = 0;

// ==================== 空调状态 ====================
struct ACState {
  bool power = false;
  int temperature = 26;
  int mode = 0;       // 0:制冷 1:制热 2:除湿 3:送风 4:自动
  int fanSpeed = 0;   // 0:自动 1:低 2:中 3:高
  bool swing = false;
  int timerMinutes = 0;
  bool display = true;
} ac;

const char* modeNames[] = {"制冷", "制热", "除湿", "送风", "自动"};
const char* fanNames[]  = {"自动", "低", "中", "高"};

unsigned long lastOledUpdate = 0;
const int OLED_UPDATE_INTERVAL = 500;
bool oledNeedsRefresh = true;

unsigned long lastStatePublish = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastTimerUpdate = 0;

// ==================== MQTT 主题构建 ====================
void buildTopics() {
  topicRegister = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/register";
  topicState    = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/state";
  topicCmd      = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/cmd";
  topicOffline  = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/offline";
  topicScan     = String(TOPIC_PREFIX) + "/scan";
}

// ==================== 状态 JSON 生成 ====================
String generateStateJson() {
  StaticJsonDocument<256> doc;
  doc["device_type"] = DEVICE_TYPE;
  doc["device_name"] = DEVICE_NAME;
  doc["ip"] = WiFi.localIP().toString();
  doc["power"] = ac.power;
  doc["temperature"] = ac.temperature;
  doc["mode"] = ac.mode;
  doc["fanSpeed"] = ac.fanSpeed;
  doc["swing"] = ac.swing;
  doc["timerMinutes"] = ac.timerMinutes;
  doc["modeName"] = modeNames[ac.mode];
  doc["fanName"] = fanNames[ac.fanSpeed];
  String out;
  serializeJson(doc, out);
  return out;
}

// ==================== 注册 JSON 生成 ====================
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
    handleMqttCommand(msg);
  } else if (topicStr == topicScan) {
    publishRegister();
  }
}

// ==================== 处理 MQTT 命令 ====================
void handleMqttCommand(const String& msg) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  String cmdType = doc["type"].as<String>();
  bool stateChanged = false;

  if (cmdType == "power") {
    String val = doc["value"].as<String>();
    if (val == "true") {
      ac.power = true;
    } else if (val == "false") {
      ac.power = false;
    } else {
      ac.power = !ac.power;
    }
    stateChanged = true;
  }
  else if (cmdType == "temperature") {
    int temp = doc["value"].as<int>();
    if (temp >= 16 && temp <= 30) {
      ac.temperature = temp;
      stateChanged = true;
    }
  }
  else if (cmdType == "temp_up") {
    if (ac.temperature < 30) {
      ac.temperature++;
      stateChanged = true;
    }
  }
  else if (cmdType == "temp_down") {
    if (ac.temperature > 16) {
      ac.temperature--;
      stateChanged = true;
    }
  }
  else if (cmdType == "mode") {
    int mode = doc["value"].as<int>();
    if (mode >= 0 && mode <= 4) {
      ac.mode = mode;
      stateChanged = true;
    }
  }
  else if (cmdType == "mode_next") {
    ac.mode = (ac.mode + 1) % 5;
    stateChanged = true;
  }
  else if (cmdType == "fanSpeed" || cmdType == "fan_speed") {
    int fan = doc["value"].as<int>();
    if (fan >= 0 && fan <= 3) {
      ac.fanSpeed = fan;
      stateChanged = true;
    }
  }
  else if (cmdType == "swing") {
    String val = doc["value"].as<String>();
    if (val == "true") {
      ac.swing = true;
    } else if (val == "false") {
      ac.swing = false;
    } else {
      ac.swing = !ac.swing;
    }
    stateChanged = true;
  }
  else if (cmdType == "timer") {
    ac.timerMinutes = doc["value"].as<int>();
    stateChanged = true;
  }
  else if (cmdType == "display") {
    ac.display = !ac.display;
    stateChanged = true;
  }

  if (stateChanged) {
    oledNeedsRefresh = true;
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
          brokerFoundTime = millis();
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

// ==================== OLED 绘制 ====================
void drawFanIndicator(int x, int y) {
  int bars = ac.fanSpeed == 0 ? 3 : ac.fanSpeed;
  for(int i = 0; i < 3; i++) {
    int h = (i + 1) * 2;
    int yPos = y - h;
    if(i < bars) {
      u8g2.drawBox(x + i * 4, yPos, 3, h);
    } else {
      u8g2.drawFrame(x + i * 4, yPos, 3, h);
    }
  }
}

void drawModeIcon(int x, int y) {
  switch(ac.mode) {
    case 0:
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawGlyph(x, y, 68);
      break;
    case 1:
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawGlyph(x, y, 229);
      break;
    case 2:
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawGlyph(x, y, 223);
      break;
    case 3:
      u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
      u8g2.drawGlyph(x, y, 228);
      break;
    case 4:
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.setCursor(x, y);
      u8g2.print("A");
      break;
  }
}

void drawACDisplay() {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 64);
  u8g2.setDrawColor(0);

  if (!ac.display) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(25, 35);
    u8g2.print("SCREEN OFF");
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.setCursor(18, 50);
    u8g2.print("AC is running");
    u8g2.sendBuffer();
    return;
  }

  // 顶部状态栏
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.setCursor(2, 7);
  u8g2.print(WiFi.localIP().toString());

  int rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 3 : (rssi > -70) ? 2 : 1;
  u8g2.setCursor(85, 7);
  u8g2.print("WiFi");
  for(int i = 0; i < 3; i++) {
    int h = 3 + i*2;
    int yPos = 6 - h;
    if(i < bars) {
      u8g2.drawBox(108 + i*4, yPos, 3, h);
    } else {
      u8g2.drawFrame(108 + i*4, yPos, 3, h);
    }
  }

  // MQTT 状态指示
  u8g2.setCursor(65, 7);
  u8g2.print(mqtt.connected() ? "MQTT" : "----");

  u8g2.drawHLine(0, 9, 128);

  if (!ac.power) {
    u8g2.setFont(u8g2_font_fur20_tf);
    u8g2.setCursor(30, 42);
    u8g2.print("OFF");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(15, 55);
    u8g2.print("Press POWER to start");
  } else {
    drawModeIcon(4, 32);

    u8g2.setFont(u8g2_font_fub30_tf);
    String tempStr = String(ac.temperature);
    int16_t tempWidth = u8g2.getUTF8Width(tempStr.c_str());
    u8g2.setCursor((128 - tempWidth - 12) / 2 + 8, 44);
    u8g2.print(tempStr);

    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.print(" C");

    u8g2.drawHLine(0, 48, 128);

    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.setCursor(2, 58);
    u8g2.print(modeNames[ac.mode]);

    u8g2.setCursor(40, 58);
    u8g2.print("Fan:");
    drawFanIndicator(62, 58);

    if (ac.swing) {
      u8g2.setCursor(2, 64);
      u8g2.print("~Swing~");
    }

    if (ac.timerMinutes > 0) {
      int startX = ac.swing ? 70 : 85;
      u8g2.setCursor(startX, 64);
      u8g2.print("T:");
      int h = ac.timerMinutes / 60;
      int m = ac.timerMinutes % 60;
      if (h > 0) u8g2.print(h);
      u8g2.print(":");
      if (m < 10) u8g2.print("0");
      u8g2.print(m);
    }
  }

  u8g2.sendBuffer();
}

void oledRefresh() {
  if (millis() - lastOledUpdate < OLED_UPDATE_INTERVAL) return;
  if (!oledNeedsRefresh) return;
  lastOledUpdate = millis();
  oledNeedsRefresh = false;
  drawACDisplay();
}

// ==================== WiFi 连接 ====================
void connectWiFi() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(5, 30);
  u8g2.print("Connecting WiFi...");
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed!");
  }
}

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 AC Controller (MQTT) ===");

  u8g2.begin();
  connectWiFi();

  buildTopics();

  if (WiFi.status() == WL_CONNECTED) {
    udp.begin(UDP_PORT);
    Serial.printf("UDP listening on port %d\n", UDP_PORT);

    mqtt.setServer("0.0.0.0", MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setKeepAlive(30);

    Serial.println("Waiting for broker discovery...");
  }

  oledNeedsRefresh = true;
  oledRefresh();
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

  // 定时器逻辑
  if (ac.timerMinutes > 0 && millis() - lastTimerUpdate >= 60000) {
    lastTimerUpdate = millis();
    ac.timerMinutes--;
    if (ac.timerMinutes == 0) {
      ac.power = false;
    }
    oledNeedsRefresh = true;
    publishState();
  }

  oledRefresh();
  delay(10);
}
