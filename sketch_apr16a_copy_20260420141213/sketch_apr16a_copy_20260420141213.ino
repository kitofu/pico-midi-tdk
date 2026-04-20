#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===== Encoder pins =====
#define ENC_CLK 2
#define ENC_DT  3
#define ENC_SW  4

// ===== Serial =====
#define DEBUG_SERIAL Serial
#define PICO_SERIAL  Serial1

// ===== Parameters =====
int sValues[4] = {300, 200, 600, 180};
int selectedIndex = 0;

// selectedIndex
// 0:S1 1:S2 2:S3 3:S4 4:STEP
const int ITEM_COUNT = 5;

const int VALUE_MIN = 0;
const int VALUE_MAX = 1023;

// STEP候補
const int stepOptions[3] = {1, 5, 10};
int stepIndex = 0;   // 0->1, 1->5, 2->10

// ===== Encoder state =====
int lastClkState = HIGH;

// ===== Button state =====
bool lastButtonReading = HIGH;
bool buttonStableState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long buttonPressStart = 0;
bool longPressHandled = false;

const unsigned long BUTTON_DEBOUNCE_MS = 30;
const unsigned long BUTTON_LONG_PRESS_MS = 700;

// ===== UI / Response =====
String lastResponse = "IDLE";
unsigned long responseDisplayTime = 0;
const unsigned long RESPONSE_HOLD_MS = 1500;

// ------------------------------
// OLED drawing
// ------------------------------
void drawUI() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("Threshold Editor");

  for (int i = 0; i < 4; i++) {
    if (i == selectedIndex) {
      display.print(">");
    } else {
      display.print(" ");
    }

    display.print("S");
    display.print(i + 1);
    display.print(": ");
    display.println(sValues[i]);
  }

  // STEP表示（旧TX表示の場所）
  if (selectedIndex == 4) {
    display.print(">");
  } else {
    display.print(" ");
  }
  display.print("STEP: ");
  display.println(stepOptions[stepIndex]);

  display.print("Resp: ");
  display.println(lastResponse);

  display.display();
}

// ------------------------------
// Send current values to Pico
// ------------------------------
void sendThresholds() {
  PICO_SERIAL.print("S1=");
  PICO_SERIAL.print(sValues[0]);
  PICO_SERIAL.print(",S2=");
  PICO_SERIAL.print(sValues[1]);
  PICO_SERIAL.print(",S3=");
  PICO_SERIAL.print(sValues[2]);
  PICO_SERIAL.print(",S4=");
  PICO_SERIAL.println(sValues[3]);

  DEBUG_SERIAL.print("Send: S1=");
  DEBUG_SERIAL.print(sValues[0]);
  DEBUG_SERIAL.print(",S2=");
  DEBUG_SERIAL.print(sValues[1]);
  DEBUG_SERIAL.print(",S3=");
  DEBUG_SERIAL.print(sValues[2]);
  DEBUG_SERIAL.print(",S4=");
  DEBUG_SERIAL.println(sValues[3]);

  lastResponse = "WAIT ACK";
  responseDisplayTime = millis();
  drawUI();
}

// ------------------------------
// Read ACK/ERR from Pico
// ------------------------------
void readPicoResponse() {
  static String line = "";

  while (PICO_SERIAL.available()) {
    char c = PICO_SERIAL.read();

    if (c == '\r') continue;

    if (c == '\n') {
      if (line.length() > 0) {
        if (line == "ACK") {
          lastResponse = "ACK OK";
        } else if (line == "ERR") {
          lastResponse = "ERR";
        } else if (line == "OK") {
          lastResponse = "HELLO OK";
        } else {
          lastResponse = line;
        }

        responseDisplayTime = millis();

        DEBUG_SERIAL.print("Recv: ");
        DEBUG_SERIAL.println(line);

        line = "";
        drawUI();
      }
    } else {
      line += c;
    }
  }
}

// ------------------------------
// Auto clear response
// ------------------------------
void updateResponseTimeout() {
  if (lastResponse != "IDLE") {
    if (millis() - responseDisplayTime > RESPONSE_HOLD_MS) {
      lastResponse = "IDLE";
      drawUI();
    }
  }
}

// ------------------------------
// Encoder update
// S1-S4 は STEP刻み
// STEP 自体は 1 / 5 / 10 の候補切替のみ
// ------------------------------
void updateEncoder() {
  int clkState = digitalRead(ENC_CLK);
  int dtState  = digitalRead(ENC_DT);

  if (clkState != lastClkState) {
    if (clkState == LOW) {
      bool clockwise = (dtState != clkState);

      if (selectedIndex >= 0 && selectedIndex <= 3) {
        int stepValue = stepOptions[stepIndex];

        if (clockwise) {
          sValues[selectedIndex] += stepValue;
        } else {
          sValues[selectedIndex] -= stepValue;
        }

        if (sValues[selectedIndex] < VALUE_MIN) sValues[selectedIndex] = VALUE_MIN;
        if (sValues[selectedIndex] > VALUE_MAX) sValues[selectedIndex] = VALUE_MAX;
      }
      else if (selectedIndex == 4) {
        // STEP設定は候補の切替だけ。STEP値をSTEPで増減しない
        if (clockwise) {
          stepIndex++;
        } else {
          stepIndex--;
        }

        if (stepIndex < 0) stepIndex = 0;
        if (stepIndex > 2) stepIndex = 2;
      }

      drawUI();
    }
  }

  lastClkState = clkState;
}

// ------------------------------
// Button update
// short press: select item
// long press : send values
// ------------------------------
void updateButton() {
  bool reading = digitalRead(ENC_SW);

  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > BUTTON_DEBOUNCE_MS) {
    if (reading != buttonStableState) {
      buttonStableState = reading;

      // pressed
      if (buttonStableState == LOW) {
        buttonPressStart = millis();
        longPressHandled = false;
      }

      // released
      if (buttonStableState == HIGH) {
        if (!longPressHandled) {
          selectedIndex++;
          if (selectedIndex >= ITEM_COUNT) selectedIndex = 0;
          drawUI();
        }
      }
    }

    // long press while holding
    if (buttonStableState == LOW && !longPressHandled) {
      if (millis() - buttonPressStart >= BUTTON_LONG_PRESS_MS) {
        sendThresholds();
        longPressHandled = true;
      }
    }
  }

  lastButtonReading = reading;
}

// ------------------------------
// Setup
// ------------------------------
void setup() {
  DEBUG_SERIAL.begin(115200);
  PICO_SERIAL.begin(115200);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  lastClkState = digitalRead(ENC_CLK);

  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    DEBUG_SERIAL.println("OLED init failed");
    while (1) {
    }
  }

  display.clearDisplay();
  display.display();

  DEBUG_SERIAL.println("Due UI start");
  drawUI();
}

// ------------------------------
// Loop
// ------------------------------
void loop() {
  updateEncoder();
  updateButton();
  readPicoResponse();
  updateResponseTimeout();
}