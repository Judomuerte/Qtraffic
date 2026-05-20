/*
  sketch/sketch.ino  —  LED matrix + Modulino Vibro (MCU side)

  Receives scaled RX/TX values (0-4) from Python via Router Bridge.
  Renders split waveform on 8x13 LED matrix and buzzes the Vibro on level 4.

  Level 4 = alarm: 10x above baseline traffic — sustained spike.
  RX spike = 2 short buzzes (unexpected inbound)
  TX spike  = 3 short buzzes (something phoning home)
*/

#include <Arduino_RouterBridge.h>
#include <Arduino_LED_Matrix.h>
#include <Modulino.h>

ArduinoLEDMatrix matrix;
ModulinoVibro vibro;

const uint8_t ROWS = 8;
const uint8_t COLS = 13;
uint8_t frame[ROWS * COLS];

uint8_t rxHistory[COLS] = {0};
uint8_t txHistory[COLS] = {0};

volatile bool    newData   = false;
volatile uint8_t pendingRx = 0;
volatile uint8_t pendingTx = 0;

bool  hasData   = false;
float idlePhase = 0.0f;
unsigned long lastRender = 0;

// ── Bridge RPC handler ───────────────────────────────────────────────────────
bool updateTraffic(int rx, int tx) {
  pendingRx = (uint8_t)constrain(rx, 0, 4);
  pendingTx = (uint8_t)constrain(tx, 0, 4);
  newData = true;
  return true;
}

// ── Helpers ──────────────────────────────────────────────────────────────────
inline void setPixel(uint8_t row, uint8_t col, uint8_t val) {
  if (row < ROWS && col < COLS)
    frame[row * COLS + col] = val;
}

void scrollAndPush(uint8_t rxVal, uint8_t txVal) {
  for (int i = 0; i < COLS - 1; i++) {
    rxHistory[i] = rxHistory[i + 1];
    txHistory[i] = txHistory[i + 1];
  }
  rxHistory[COLS - 1] = rxVal;
  txHistory[COLS - 1] = txVal;
}

void renderTraffic() {
  memset(frame, 0, sizeof(frame));
  for (int col = 0; col < COLS; col++) {
    uint8_t txH = txHistory[col];
    for (uint8_t r = 0; r < txH; r++) setPixel(r, col, 255);
    uint8_t rxH = rxHistory[col];
    for (uint8_t r = 7; r > (7u - rxH); r--) setPixel(r, col, 255);
  }
  matrix.draw(frame);
}

void renderIdle() {
  idlePhase += 0.04f;
  uint8_t b = (uint8_t)((sinf(idlePhase) * 0.5f + 0.5f) * 80.0f);
  memset(frame, 0, sizeof(frame));
  for (int col = 0; col < COLS; col++) {
    setPixel(3, col, b);
    setPixel(4, col, b);
  }
  matrix.draw(frame);
}

// ── Vibro alert patterns ─────────────────────────────────────────────────────
void buzzRxAlarm() {
  // 2 short buzzes — unexpected inbound spike
  vibro.on(150, true);
  delay(100);
  vibro.on(150, true);
}

void buzzTxAlarm() {
  // 3 short buzzes — something phoning home
  vibro.on(150, true);
  delay(100);
  vibro.on(150, true);
  delay(100);
  vibro.on(150, true);
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────
void setup() {
  Monitor.begin();
  Modulino.begin();
  matrix.begin();
  matrix.setGrayscaleBits(8);
  vibro.begin();

  Bridge.provide("updateTraffic", updateTraffic);

  // Quick confirmation buzz on boot
  vibro.on(200, true);

  Monitor.println("[wave_matrix] ready");
}

void loop() {
  Bridge.update();

  if (newData) {
    newData = false;
    hasData = true;

    bool rxAlarm = (pendingRx == 4);
    bool txAlarm = (pendingTx == 4);

    scrollAndPush(pendingRx, pendingTx);

    if (rxAlarm) buzzRxAlarm();
    else if (txAlarm) buzzTxAlarm();
  }

  unsigned long now = millis();
  if (now - lastRender > 55) {
    lastRender = now;
    if (hasData) renderTraffic();
    else         renderIdle();
  }
}
