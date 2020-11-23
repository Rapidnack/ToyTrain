#include <Wire.h>
#include "SSD1306Wire.h"
#include <WiFi.h>

const char* ssid = "--ssid--"; // 要変更
const char* password = "--password--"; // 要変更
const char* scratchAddr = "192.168.10.7"; // 要変更（Scratchを実行しているPCのIPアドレス）
const int scratchPort = 42001;

void mySetup(); // このスケッチ固有のsetup()
void myLoop(); // このスケッチ固有のloop()
void process(String str); // 受信したメッセージを処理

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

WiFiClient client;
int messageLength = 0;

void sendScratch(String str) {
  if (client.connected()) {
    uint8_t sizebuf[4] = {0};
    sizebuf[3] = (uint8_t)str.length();
    client.write(sizebuf, sizeof(sizebuf));
    client.write(str.c_str(), str.length());
  }
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

void loop() {
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
        messageLength = (buf[2] << 8) + buf[3];
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

// 以下、このスケッチ固有の記述

#include "Ticker.h"

Ticker timer1;
volatile bool updateRequest = false;
void showMessage(String s, float seconds) {
  displayBuffer[3] = s;
  updateDisplay();
  timer1.once(seconds, hideMessage);  
}
void hideMessage() {
  displayBuffer[3] = "";
  updateRequest = true;
}

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

byte switchPins[] = { 14, 27, 26, 25, 33, 32, 15, 4, 16, 17, 5, 18, 19 };
byte servoPins[] = { 13, 12 };

void rotateServo(int chan, float angle) { // 0(0.5ms) ~ 180(2.5ms)
  double ms = 0.5 + angle / 90.0;
  int duty = round(ms / 20.0 * 65536.0); // 20ms full scale
  ledcWrite(chan, duty);
}

void mySetup() {
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

  displayBuffer[2] = "Scratch_SP";
  updateDisplay();
}

void myLoop() {
  if (updateRequest) {
    updateRequest = false;
    updateDisplay();
  }

  for (int i = 0; i < 40; i++) {
    if (interruptList[i].count > 0) {
      interruptList[i].count = 0;
      String s = "broadcast \"catch " + String(i) + "\"";
      sendScratch(s);

      showMessage("catch " + String(i), 0.5);
    }
  }
}

void process(String str) {
  //Serial.println(str);

  if (str.startsWith("sensor-update ")) {
    str = str.substring(str.indexOf(' ') + 1); // 変数名の先頭の"以降
    while (str.startsWith("\"")) {
      //Serial.println(str);
      bool match = false;
      // Servo + ピン番号
      for (int i = 0; i < sizeof(servoPins); i++) {
        int pin = servoPins[i];
        String pat = "\"Servo" + String(pin) + "\" ";
        if (str.startsWith(pat)) {
          match = true;
          showMessage(str, 0.5);
          str = str.substring(str.indexOf(' ') + 1); // 値の先頭以降
          float angle = str.toFloat();
          rotateServo(i, angle);
        }
      }
      if (!match) {
          str = str.substring(str.indexOf(' ') + 1); // 値の先頭以降
      }
      if (str.indexOf(' ') >= 0) {
        str = str.substring(str.indexOf(' ') + 1); // 変数名の先頭の"以降
      }
    }
  } else if (str.startsWith("broadcast ")) {
    str = str.substring(str.indexOf(' ') + 1); // 文字列の先頭の"以降
    showMessage(str, 0.5);
  }
}
