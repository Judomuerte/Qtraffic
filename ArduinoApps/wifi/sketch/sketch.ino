/*
  sketch/sketch.ino  —  LED matrix renderer (MCU / STM32 side)

  Receives scaled RX and TX values (each 0-4) from the Python side via
  Router Bridge, pushes them into a 13-column scrolling history, and
  renders a split waveform on the 8x13 LED matrix:

    Rows 0-3  →  TX (outbound), bar grows DOWN from row 0
    Rows 4-7  →  RX (inbound),  bar grows UP   from row 7

  While waiting for the first data from Python, a slow idle pulse runs
  on the two centre rows so the matrix isn't blank during boot.
*/

#include <Arduino_RouterBridge.h>
#include <Arduino_LED_Matrix.h>

ArduinoLEDMatrix matrix;

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

// ── Bridge RPC handler ──────────────────────────────────────────────────────
// Python calls:  Bridge.call("updateTraffic", rx_int, tx_int)
bool updateTraffic(int rx, int tx) {
  pendingRx = (uint8_t)constrain(rx, 0, 4);
  pendingTx = (uint8_t)constrain(tx, 0, 4);
  newData = true;
  return true;
}

// ── Helpers ─────────────────────────────────────────────────────────────────
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

// ── Setup / Loop ─────────────────────────────────────────────────────────────
void setup() {
  Monitor.begin();
  matrix.begin();
  matrix.setGrayscaleBits(8);

  // provide_safe() fires the callback safely on the main thread
  // when update_safe() is called in loop()
  Bridge.provide("updateTraffic", updateTraffic);

  Monitor.println("[wave_matrix] ready");
}

void loop() {
  delay(10);

  if (newData) {
    newData = false;
    hasData = true;
    scrollAndPush(pendingRx, pendingTx);
  }

  unsigned long now = millis();
  if (now - lastRender > 55) {
    lastRender = now;
    if (hasData) renderTraffic();
    else         renderIdle();
  }
}
