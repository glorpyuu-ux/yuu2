// Host simulation of the UI state machine: compile with stubs (see README "development").
#include <thread>
#include <chrono>
#include <atomic>
#include <string>
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

// ---- stub implementations
static std::chrono::steady_clock::time_point T0 = std::chrono::steady_clock::now();
unsigned long millis() { return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - T0).count(); }
unsigned long micros() { return (unsigned long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - T0).count(); }
void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
void pinMode(int,int) {} void digitalWrite(int,int) {}
void vTaskDelay(TickType_t t) { std::this_thread::sleep_for(std::chrono::milliseconds(t < 1 ? 1 : 10)); }
int xTaskCreatePinnedToCore(TaskFunction_t f, const char*, uint32_t, void* a, int, void*, int) { std::thread(f, a).detach(); return 1; }
void SerialStub::begin(long) {} SerialStub Serial;
SPIClass::SPIClass(int) {} void SPIClass::begin(int,int,int,int) {}
TFT_eSPI::TFT_eSPI() {} void TFT_eSPI::init() {} void TFT_eSPI::setRotation(uint8_t) {} void TFT_eSPI::invertDisplay(bool) {}
void TFT_eSPI::fillScreen(uint32_t) {} void TFT_eSPI::fillRect(int32_t,int32_t,int32_t,int32_t,uint32_t) {}
void TFT_eSPI::fillRoundRect(int32_t,int32_t,int32_t,int32_t,int32_t,uint32_t) {} void TFT_eSPI::drawRoundRect(int32_t,int32_t,int32_t,int32_t,int32_t,uint32_t) {}
void TFT_eSPI::drawLine(int32_t,int32_t,int32_t,int32_t,uint32_t) {} void TFT_eSPI::drawCircle(int32_t,int32_t,int32_t,uint32_t) {}
void TFT_eSPI::fillCircle(int32_t,int32_t,int32_t,uint32_t) {} void TFT_eSPI::setTextColor(uint16_t) {} void TFT_eSPI::setTextDatum(uint8_t) {}
int16_t TFT_eSPI::drawString(const char*,int32_t,int32_t,uint8_t) { return 0; }
bool Preferences::begin(const char*, bool) { return true; } void Preferences::end() {}
uint8_t Preferences::getUChar(const char* k, uint8_t d) { return (!strcmp(k, "ok")) ? 1 : d; }   // pretend calibrated, no axis swap
size_t Preferences::putUChar(const char*, uint8_t) { return 1; }
float Preferences::getFloat(const char*, float d) { return d; } size_t Preferences::putFloat(const char*, float) { return 1; }

static bool g_pressing = false; static int g_rawx = 0, g_rawy = 0;
XPT2046_Touchscreen::XPT2046_Touchscreen(uint8_t) {} bool XPT2046_Touchscreen::begin(SPIClass&) { return true; }
void XPT2046_Touchscreen::setRotation(uint8_t) {}
bool XPT2046_Touchscreen::touched() { return g_pressing; }
TS_Point XPT2046_Touchscreen::getPoint() { TS_Point p; p.x = (int16_t)g_rawx; p.y = (int16_t)g_rawy; p.z = 800; return p; }

#include "../../src/main.cpp"      // pull in the firmware itself so we can inspect its state

static int fails = 0;
#define EXPECT(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } else printf("ok:   %s\n", msg); } while (0)

static void tapAt(int sx, int sy) {                         // inverse of the default calibration
  g_rawx = (int)(((float)sx - cal.bx) / cal.ax); g_rawy = (int)(((float)sy - cal.by) / cal.ay);
  g_pressing = true;  for (int i = 0; i < 14; ++i) loop();
  g_pressing = false; for (int i = 0; i < 20; ++i) loop();
}
static void tapSq(int sq) { int x, y; sqXY(sq, x, y); tapAt(x + 14, y + 14); }
static void tapBtn(const Btn& b) { tapAt(b.x + b.w / 2, b.y + b.h / 2); }
static void waitAI() { uint32_t t0 = millis(); while ((aiState != 0 || (mode == 1 && status == ch::GS_PLAY && cur.side == aiSide)) && millis() - t0 < 20000) loop(); }
static int sq(const char* s) { return (s[1] - '1') * 8 + (s[0] - 'a'); }

int main() {
  setup();
  EXPECT(ui == UI_MENU, "boots into menu");
  tapBtn(mPP);
  EXPECT(ui == UI_GAME && mode == 0, "Pass & Play starts");
  tapSq(sq("e2")); EXPECT(selSq == sq("e2"), "select e2");
  tapSq(sq("e4")); EXPECT(cur.b[sq("e4")] == ch::PAWN && cur.side == ch::SIDE_B, "e2-e4 played, black to move");
  tapSq(sq("e7")); tapSq(sq("e5")); EXPECT(cur.b[sq("e5")] == -ch::PAWN, "black e7-e5 played");
  tapSq(sq("e4")); tapSq(sq("e6"));                       // illegal for a blocked pawn -> just deselects
  EXPECT(cur.b[sq("e4")] == ch::PAWN && cur.side == ch::SIDE_W, "illegal target ignored");
  tapBtn(gUndo); EXPECT(nMoves == 1 && cur.side == ch::SIDE_B, "undo takes back one ply in pass&play");
  tapBtn(gFlip); EXPECT(flipped, "flip works");
  tapSq(sq("e7")); tapSq(sq("e5")); EXPECT(cur.b[sq("e5")] == -ch::PAWN, "moves work on the flipped board");
  tapBtn(gRot); EXPECT(autoRot && flipped == false, "auto-rotate on, white to move -> white at bottom");

  // promotion popup
  ch::setFen(cur, "8/P6k/8/8/8/8/8/K7 w - - 0 1"); nMoves = 0; nHist = 0; hist[nHist++] = ch::hashPos(cur);
  refreshState(); selSq = -1; autoRot = false; flipped = false; drawGame();
  tapSq(sq("a7")); tapSq(sq("a8"));
  EXPECT(ui == UI_PROMO, "promotion popup shown");
  tapAt(PB_X + 8 + 3 * 44 + 20, PB_Y + 22 + 17);          // knight button
  EXPECT(ui == UI_GAME && cur.b[sq("a8")] == ch::KNIGHT, "promoted to knight");

  // back to menu, vs computer as black, level 1
  tapBtn(gMenu); EXPECT(ui == UI_MENU, "menu button");
  tapBtn(mAI); EXPECT(ui == UI_SETUP, "setup screen");
  tapBtn(sBlack); tapBtn(sLvl[0]); tapBtn(sStart);
  EXPECT(ui == UI_GAME && mode == 1 && humanSide == ch::SIDE_B && flipped, "vs computer, human = black, board flipped");
  waitAI(); EXPECT(nMoves == 1 && cur.side == ch::SIDE_B, "computer (white) opened");
  tapBtn(gUndo); EXPECT(nMoves == 1, "undo refused when player has no move yet");
  // play a few moves, always the first legal move for the human, and make sure engine replies legally
  for (int i = 0; i < 6 && status == ch::GS_PLAY; ++i) {
    int from = -1, to = -1;
    for (int k = 0; k < nLegal; ++k) if (legal[k].promo == 0 || legal[k].promo == ch::QUEEN) { from = legal[k].from; to = legal[k].to; break; }
    tapSq(from); tapSq(to);
    if (ui == UI_PROMO) { tapAt(PB_X + 8 + 20, PB_Y + 22 + 17); }
    waitAI();
  }
  EXPECT(nMoves >= 12 || status != ch::GS_PLAY, "7 plies each side without error");
  int before = nMoves; tapBtn(gUndo);
  EXPECT(nMoves == before - 2 && cur.side == ch::SIDE_B, "undo in AI mode removes player's move + engine reply");

  printf("\n%s (%d failures)\n", fails ? "UI SIM FAILED" : "UI SIM PASSED", fails);
  return fails ? 1 : 0;
}
