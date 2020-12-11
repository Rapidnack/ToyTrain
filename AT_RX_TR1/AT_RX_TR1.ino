#include <Wire.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);
String displayBuffer[4];
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  for (int i = 0; i < 4; i++) {
    display.println(displayBuffer[i]);
  }
  display.display();
}

// 以下、このスケッチ固有の記述

const int PIN_ID0 = 8;
const int PIN_ID1 = 9;
const int PIN_ID2 = 10;
const int PIN_PWM = 5;
const int PIN_ADC = A3;

int myId; // 0..7 (~PIN_ID2,~PIN_ID1,~PIN_ID0)
const byte SYNC = 0x55;
int sync = 0;
byte b0; // 1バイト目
float reference = 1.5; // 1.5V時と同じ速度になるようにPWMを補正する

void process(byte b0, byte b1);

void setup() {
  Serial.begin(1200);

  pinMode(PIN_ID0, INPUT_PULLUP);
  pinMode(PIN_ID1, INPUT_PULLUP);
  pinMode(PIN_ID2, INPUT_PULLUP);
  myId = 7;
  myId -= digitalRead(PIN_ID0);
  myId -= digitalRead(PIN_ID1) << 1;
  myId -= digitalRead(PIN_ID2) << 2;
  pinMode(PIN_ID0, INPUT);
  pinMode(PIN_ID1, INPUT);
  pinMode(PIN_ID2, INPUT);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  displayBuffer[0] = "AT_RX_TR1";
  updateDisplay();

  TCCR0B = TCCR0B & B11111000 | B00000001; // D5, D6: 62500Hz
  pinMode(PIN_PWM, OUTPUT);
  process(0x80 + myId, 0x80);
}

void loop() {
  if (Serial.available() > 0) {
    int b = Serial.read();
    if (sync > 0) {
      sync--;
      if (sync > 0) {
        b0 = b;
      } else {
        process(b0, b);
      }
    } else {
      if (b == SYNC) {
        sync = 2;
      }
    }
  }
}

void process(byte b0, byte b1) {
  int id = b0 & 0x3f;
  int pct = b1 & 0x7f;
  String s = "CMD ";
  if (b0 < 0x10) {
    s += "0";
  }
  s += String(b0, HEX);
  s += " ";
  if (b1 < 0x10) {
    s += "0";
  }
  s += String(b1, HEX);
  displayBuffer[2] = s;
  
  if (id != myId) {
    updateDisplay();
    return;
  }

  float battery = (analogRead(PIN_ADC) * 3.3) / 1024;
  int pwm = 0;
  if (pct > 0) {
    pwm = round((((pct / 100.0) * 255.0) * reference) / battery);
    if (pwm > 255) {
      pwm = 255;
    }
  }
  analogWrite(PIN_PWM, pwm);
  
  displayBuffer[1] = String(myId) + " BAT " + String(battery);
  displayBuffer[3] = "PWM " + String(pwm);
  updateDisplay();
}
