#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

// OLED配置
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 软串口配置
#define SOFT_RX D4  // ESP8266的D4接TX
#define SOFT_TX D3  // ESP8266的D3接RX
SoftwareSerial softSerial(SOFT_RX, SOFT_TX);

// WiFi配置
const char* ssid = "荣耀Magic6";
const char* pswd = "1111IIll";

// 云平台配置
const char* server = "ndp.nlecloud.com";
const int port = 8600;
const char* deviceId = "seedStorge";
const char* deviceKey = "33b641356c3942cc936e9ac5f4db7372";
const char* websocketPath = "/ws/device/1290009";  // 修正WebSocket路径

// QT TCP服务器配置
WiFiServer qtServer(8080);  // 为QT上位机创建TCP服务器
WiFiClient qtClient;        // 用于处理QT连接

// Web服务器配置
ESP8266WebServer webServer(80);

// 全局变量
float temperature = 0;
float humidity = 0;
unsigned long concentration = 0;
String lastRFID = "";
String lastAction = "";
String ipAddress = "";
WebSocketsClient sock;

void setup() {
  Serial.begin(115200);
  softSerial.begin(115200);
  
  // 初始化OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED初始化失败");
    while(1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("等待数据...");
  display.display();
  
  setup_wifi();
  setup_webserver();

  // 启动QT TCP服务器
  qtServer.begin();
  Serial.println("QT TCP服务器已启动，端口: 8080");

  // WebSocket初始化
  sock.begin(server, port, websocketPath);
  sock.onEvent([](WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
      case WStype_CONNECTED:
        Serial.println("[WS] Connected");
        sendRegistration();
        break;
      case WStype_TEXT:
        handleServerMessage((char*)payload);
        break;
      case WStype_DISCONNECTED:
        Serial.println("[WS] Disconnected");
        break;
      case WStype_ERROR:
        Serial.println("[WS] Error");
        break;
    }
  });
  sock.setReconnectInterval(5000);
}

void loop() {
  sock.loop();
  webServer.handleClient();
  readSerialData();
  handleQtClient();
  
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 5000) {
    sendSensorData();
    lastSend = millis();
  }
}

void readSerialData() {
  if (softSerial.available()) {
    String rawData = softSerial.readStringUntil('\n');
    rawData.trim();
    
    Serial.print("Raw Data: ");
    Serial.println(rawData);

    // 检查并提取RFID数据
    if(rawData.startsWith("Mifare_One(S50),ID:")) {
      // 提取ID部分
      int idStart = rawData.indexOf("ID:") + 3;
      lastRFID = rawData.substring(idStart);
      lastRFID.trim(); // 去除可能的空白字符
      
      // 确定操作类型
      if(lastRFID == "E600B701") {
        lastAction = "In";
      } else if(lastRFID == "C5AA3F02") {
        lastAction = "OUT";
      } else {
        lastAction = "No Idea";
      }
      
      Serial.println("提取的RFID: " + lastRFID + " - " + lastAction);
      updateOLED();
      
      // 发送给QT客户端
      if (qtClient && qtClient.connected()) {
        String rfidJson = String("{\"rfid\":\"") + lastRFID + "\",\"action\":\"" + lastAction + "\"}";
        qtClient.println(rfidJson);
        Serial.println("发送给QT: " + rfidJson);
      }
      return;
    }

    // 解析传感器数据
    int tempStart = rawData.indexOf("Temperature:");
    int humiStart = rawData.indexOf("Humidity:");
    int concStart = rawData.indexOf("Concentration:");
    
    if (tempStart != -1 && humiStart != -1 && concStart != -1) {
      temperature = rawData.substring(tempStart + 12, humiStart).toFloat();
      humidity = rawData.substring(humiStart + 9, concStart).toFloat();
      concentration = rawData.substring(concStart + 14).toInt();
      
      Serial.printf("Parsed: Temp=%.1f, Humi=%.1f, Conc=%lu\n", 
                   temperature, humidity, concentration);
      updateOLED();
      
      // 发送传感器数据给QT客户端
      if (qtClient && qtClient.connected()) {
        String sensorJson = String("{\"temp\":") + temperature + 
                          ",\"humi\":" + humidity + 
                          ",\"conc\":" + concentration + "}";
        qtClient.println(sensorJson);
      }
    }
  }
}

void handleQtClient() {
  if (qtServer.hasClient()) {
    if (!qtClient || !qtClient.connected()) {
      if (qtClient) qtClient.stop();
      qtClient = qtServer.available();
      Serial.println("新的QT客户端已连接");
      qtClient.println("{\"status\":\"connected\"}");
    }
  }
  if (qtClient && qtClient.connected() && qtClient.available()) {
    String command = qtClient.readStringUntil('\n');
    command.trim();
    Serial.println("收到QT命令: " + command);
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setCursor(0,0);
  
  // 显示IP地址
  display.print("IP:");
  display.println(WiFi.localIP().toString());
  
  // 显示传感器数据
  display.print("Temp:");
  display.print(temperature);
  display.println("C");
  
  display.print("Humi:");
  display.print(humidity);
  display.println("%");
  
  display.print("Conc:");
  display.print(concentration);
  display.println("ppm");
  
  // 显示RFID信息
  if(lastRFID.length() > 0) {
    display.println();
    display.print("RFID:");
    display.println(lastRFID);
    display.print("Action:");
    display.println(lastAction);
  }
  
  display.display();
}

void sendSensorData() {
  if(!sock.isConnected()) {
    Serial.println("WebSocket未连接，尝试重连...");
    return;
  }
  
  DynamicJsonDocument doc(256);
  doc["t"] = "3";  // 数据类型标识
  doc["datatype"] = "1";
  
  JsonObject datas = doc.createNestedObject("datas");
  datas["Temperature"] = temperature;
  datas["Humidity"] = humidity;
  datas["Concentration"] = concentration;
  
  doc["msgid"] = String(millis());

  String jsonStr;
  serializeJson(doc, jsonStr);
  sock.sendTXT(jsonStr);
  
  Serial.println("Sent to cloud: " + jsonStr);
}

void setup_webserver() {
  webServer.on("/", HTTP_GET, [](){
    String response = "{\"status\":\"ok\",\"rfid\":\"" + lastRFID + "\",\"action\":\"" + lastAction + "\"}";
    webServer.send(200, "application/json", response);
  });
  
  webServer.begin();
  Serial.println("HTTP server started");
}

void setup_wifi() {
  WiFi.begin(ssid, pswd);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  ipAddress = WiFi.localIP().toString();
}

void sendRegistration() {
  String regMsg = R"({"t":1,"device":")" + String(deviceId) + 
                  R"(","key":")" + String(deviceKey) + R"(","ver":"v1.1"})";
  sock.sendTXT(regMsg);
  Serial.println("Sent registration: " + regMsg);
}

void handleServerMessage(String msg) {
  Serial.println("Server message: " + msg);
}