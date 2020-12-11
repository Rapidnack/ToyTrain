#include <Wire.h>
#include "SSD1306Wire.h"
#include <WiFi.h>
#include "Ticker.h"

const char* ssid = "--ssid--"; // 要変更
const char* password = "--password--"; // 要変更
const char* scratchAddr = "--ip address--"; // 要変更（Scratchを実行しているPCのIPアドレス）
const int scratchPort = 42001;

void mySetup(); // このスケッチ固有のsetup()
void myLoop(); // このスケッチ固有のloop()
void processSensorUpdate(String sensor, String value); // 受信したsensor-updateを処理
void processBroadcast(String message); // 受信したbroadcastを処理

SSD1306Wire display(0x3c, 21, 22);
String displayBuffer[4];
void updateDisplay() {
  display.clear();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);
  for (int i = 0; i < 4; i++) {
    display.drawString(0, i * 16, displayBuffer[i]);
  }
  display.display();
}

Ticker messageTimer;
volatile bool updateRequest = false;
void showMessage(String s, float seconds) {
  displayBuffer[3] = s;
  updateDisplay();
  messageTimer.once(seconds, hideMessage);
}
void hideMessage() {
  displayBuffer[3] = "";
  updateRequest = true;
}

WiFiClient client;
int messageLength = 0;

void sendScratch(String str) {
  if (client.connected()) {
    uint8_t sizebuf[4] = {0};
    unsigned int n = str.length();
    sizebuf[0] = (uint8_t)(n >> 24) & 0xff;
    sizebuf[1] = (uint8_t)(n >> 16) & 0xff;
    sizebuf[2] = (uint8_t)(n >> 8) & 0xff;
    sizebuf[3] = (uint8_t)n & 0xff;
    client.write(sizebuf, sizeof(sizebuf));
    client.write(str.c_str(), str.length());
  }
}

void sensorUpdate(String sensor, String value) {
  sendScratch("sensor-update \"" + sensor + "\" " + value + " ");
}

void broadcast(String str) {
  sendScratch("broadcast \"" + str + "\"");
}

void setup() {
  Serial.begin(57600);

  display.init();

  // このスケッチ固有のsetup()を実行
  mySetup();

  // アクセスポイントに接続
  Serial.println("WiFi Connecting");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi Connected");

  Serial.println("ESP32: " + WiFi.localIP().toString());
  Serial.println("Scratch: " + String(scratchAddr));

  displayBuffer[0] = "E " + WiFi.localIP().toString();
  displayBuffer[1] = "S " + String(scratchAddr);
  updateDisplay();
}

void process(String str); // 受信したメッセージを処理

void loop() {
  if (updateRequest) {
    updateRequest = false;
    updateDisplay();
  }

  // このスケッチ固有のloop()を実行
  myLoop();

  if (!client.connected()) {
    displayBuffer[1] = "S?" + String(scratchAddr);
    updateDisplay();
    client.connect(scratchAddr, scratchPort);
    if (client.connected()) {
      Serial.println("Scratch connected");
      displayBuffer[1] = "S " + String(scratchAddr);
      updateDisplay();
    }
  } else {
    if (messageLength == 0) { // メッセージの長さを読み込む
      if (client.available() >= 4) {
        uint8_t buf[4];
        client.readBytes(buf, 4);
        messageLength = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
      }
    } else { // メッセージ本体を読み込む
      if (client.available() >= messageLength) {
        char buf[messageLength + 1] = {0};
        client.readBytes(buf, messageLength);
        String inputString = String(buf);
        process(inputString); // メッセージを処理
        messageLength = 0;
      }
    }
  }
}

void process(String str) {
  if (str.startsWith("sensor-update ")) {
    str = str.substring(str.indexOf(' ') + 1); // センサー名の先頭の"以降
    while (str.startsWith("\"")) {
      String sensor = str.substring(1, str.indexOf(' ') - 1); // ダブルクォートの内側
      str = str.substring(str.indexOf(' ') + 1); // 値の先頭以降
      if (str.indexOf(' ') >= 0) {
        String value = str.substring(0, str.indexOf(' ')); // スペースの直前まで
        processSensorUpdate(sensor, value);
        str = str.substring(str.indexOf(' ') + 1); // センサー名の先頭の"以降
      } else {
        processSensorUpdate(sensor, str);
        break;
      }
    }
  } else if (str.startsWith("broadcast ")) {
    str = str.substring(str.indexOf(' ') + 1); // 文字列の先頭の"以降
    String message = str.substring(1, str.length() - 1); // ダブルクォートの内側
    processBroadcast(message);
  }
}

// 以下、このスケッチ固有の記述

struct Interrupt {
  volatile unsigned int count = 0;
  volatile unsigned long fired = 0;
};
Interrupt interruptList[40];
void IRAM_ATTR isr(void* arg) {
  Interrupt* intr = static_cast<Interrupt*>(arg);
  unsigned long now = millis();
  if (intr->fired + 2000 < now) {
    intr->count++;
  }
  intr->fired = now;
}

byte switchPins[] = { 14, 27, 26, 25, 33, 32, 15, 4, /*16, 17,*/ 5, 18, 19 };
byte servoPins[] = { 13, 12 };

void rotateServo(int chan, float angle) { // 0(0.5ms) ~ 180(2.5ms)
  double ms = 0.5 + angle / 90.0;
  int duty = round(ms / 20.0 * 65536.0); // 20ms full scale
  ledcWrite(chan, duty);
}

const int MAX_BATTERY_ID = 63;
byte bytes[] = { 0x55, 0x00, 0x00 };
void sendCommand(byte id, float pct) {
  if (id > 0x3f) {
    return;
  }
  byte sign = 0;
  if (pct < 0) {
    sign = 1;
  }
  byte absPct = round(abs(pct));
  if (absPct > 0x7f) {
    absPct = 0x7f;
  }
  bytes[1] = 0x80 + (sign << 6) + id; // 1siiiiii
  bytes[2] = 0x80 + absPct; // 1ppppppp
  Serial2.write(bytes, sizeof(bytes));
}

void mySetup() {
  pinMode(2, OUTPUT);

  for (int i = 0; i < sizeof(switchPins); i++) {
    int pin = switchPins[i];
    interruptList[pin].count = 0;
    pinMode(pin, INPUT_PULLUP);
    attachInterruptArg(pin, (void (*)(void*))isr, &interruptList[pin], FALLING);
  }

  for (int i = 0; i < sizeof(servoPins); i++) {
    int pin = servoPins[i];
    ledcSetup(i, 50, 16);
    ledcAttachPin(pin, i);
    rotateServo(i, 45);
  }

  Serial2.begin(1200); // 赤外線LED用
  int chan = sizeof(servoPins);
  ledcSetup(chan, 38000, 6); // 38kHz
  ledcAttachPin(23, chan);
  ledcWrite(chan, 32); // 32/64 デューティー比50%

  displayBuffer[2] = "Scratch_SPT";
  updateDisplay();
}

void myLoop() {
  for (int i = 0; i < 40; i++) {
    if (interruptList[i].count > 0) {
      interruptList[i].count = 0;
      broadcast("catch " + String(i));
      showMessage("catch " + String(i), 0.5);
    }
  }
}

void processSensorUpdate(String sensor, String str) {
  showMessage(sensor + " " + str, 0.5);

  if (sensor == "D2") {
    digitalWrite(2, str.toInt());
  }

  // Servo + ピン番号
  for (int i = 0; i < sizeof(servoPins); i++) {
    int pin = servoPins[i];
    if (sensor == "Servo" + String(pin)) {
      rotateServo(i, str.toFloat());
    }
  }

  // Battery + ID(0 ~ MAX_BATTERY_ID)
  for (int id = 0; id <= MAX_BATTERY_ID; id++) {
    if (sensor == "Battery" + String(id)) {
      sendCommand(id, str.toFloat());
    }
  }
}

void processBroadcast(String str) {
  showMessage(str, 0.5);
}
