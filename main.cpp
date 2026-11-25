// CYD Chess  -  ESP32-2432S028 ("Cheap Yellow Display", 2.8" ILI9341 + XPT2046 touch)
// Pass & Play + built-in engine (4 levels). Full chess rules incl. castling, en passant,
// promotion, check/mate/stalemate, 50-move, repetition, insufficient material.
#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include "chess_engine.h"

typedef ch::Move Move;
typedef ch::Position Position;

// ----------------------------------------------------------------- hardware pins (CYD)
#define PIN_BL         21
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_CLK  25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(PIN_TOUCH_CS);
Preferences prefs;

// ----------------------------------------------------------------- colours (chess.com-ish)
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
static const uint16_t C_BG        = RGB(38, 37, 34);
static const uint16_t C_LIGHT     = RGB(238, 238, 210);
static const uint16_t C_DARK      = RGB(118, 150, 86);
static const uint16_t C_HL_LIGHT  = RGB(246, 246, 130);
static const uint16_t C_HL_DARK   = RGB(186, 202, 68);
static const uint16_t C_SEL_LIGHT = RGB(246, 235, 90);
static const uint16_t C_SEL_DARK  = RGB(210, 200, 50);
static const uint16_t C_CHECK     = RGB(230, 90, 80);
static const uint16_t C_TEXT      = RGB(235, 235, 235);
static const uint16_t C_DIM       = RGB(150, 150, 150);
static const uint16_t C_BTN       = RGB(70, 68, 64);
static const uint16_t C_BTN_SEL   = RGB(118, 150, 86);
static const uint16_t C_BTN_EDGE  = RGB(110, 108, 104);
static const uint16_t C_WPIECE    = RGB(250, 250, 250);
static const uint16_t C_BPIECE    = RGB(45, 45, 45);

// ----------------------------------------------------------------- touch calibration
struct Cal { bool swap; float ax, bx, ay, by; };
static Cal cal = { false, 320.0f / 3500.0f, -200.0f * (320.0f / 3500.0f),
                          240.0f / 3560.0f, -240.0f * (240.0f / 3560.0f) };

static void saveCal() {
  prefs.begin("cydchess", false);
  prefs.putUChar("ok", 1);
  prefs.putUChar("sw", cal.swap ? 1 : 0);
  prefs.putFloat("ax", cal.ax); prefs.putFloat("bx", cal.bx);
  prefs.putFloat("ay", cal.ay); prefs.putFloat("by", cal.by);
  prefs.end();
}
static bool loadCal() {
  prefs.begin("cydchess", false);
  bool ok = prefs.getUChar("ok", 0) == 1;
  if (ok) {
    cal.swap = prefs.getUChar("sw", 0) == 1;
    cal.ax = prefs.getFloat("ax", cal.ax); cal.bx = prefs.getFloat("bx", cal.bx);
    cal.ay = prefs.getFloat("ay", cal.ay); cal.by = prefs.getFloat("by", cal.by);
  }
  prefs.end();
  return ok;
}

static void mapTouch(const TS_Point& p, int& sx, int& sy) {
  float rx = cal.swap ? (float)p.y : (float)p.x;
  float ry = cal.swap ? (float)p.x : (float)p.y;
  int x = (int)(cal.ax * rx + cal.bx + 0.5f);
  int y = (int)(cal.ay * ry + cal.by + 0.5f);
  if (x < 0) x = 0;
  if (x > 319) x = 319;
  if (y < 0) y = 0;
  if (y > 239) y = 239;
  sx = x; sy = y;
}

// One debounced tap per press. Returns true once, with screen coordinates.
static bool pollTap(int& x, int& y) {
  static bool down = false, fired = false;
  static uint32_t tDown = 0, tLast = 0;
  static long sumX = 0, sumY = 0;
  static int cnt = 0;
  uint32_t now = millis();
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int sx, sy; mapTouch(p, sx, sy);
    tLast = now;
    if (!down) { down = true; fired = false; tDown = now; sumX = 0; sumY = 0; cnt = 0; }
    if (!fired) {
      sumX += sx; sumY += sy; cnt++;
      if (now - tDown >= 40 && cnt >= 2) { fired = true; x = (int)(sumX / cnt); y = (int)(sumY / cnt); return true; }
    }
  } else if (down && (now - tLast) > 60) {
    down = false;
  }
  return false;
}

// Blocks until a press+release, returns averaged RAW touch values.
static bool readRawStable(int& rx, int& ry) {
  while (!ts.touched()) delay(10);
  delay(60);
  long sx = 0, sy = 0; int cnt = 0; uint32_t t0 = millis();
  while (ts.touched() && millis() - t0 < 400) {
    TS_Point p = ts.getPoint(); sx += p.x; sy += p.y; cnt++; delay(8);
  }
  while (ts.touched()) delay(10);
  delay(150);
  if (cnt < 3) return false;
  rx = (int)(sx / cnt); ry = (int)(sy / cnt);
  return true;
}

static void drawCross(int x, int y, uint16_t c) {
  tft.drawLine(x - 12, y, x + 12, y, c);
  tft.drawLine(x, y - 12, x, y + 12, c);
  tft.drawCircle(x, y, 6, c);
}

static void runCalibration() {
  const int tx[3] = { 20, 300, 20 };
  const int ty[3] = { 20, 20, 220 };
  for (;;) {
    int rx[3] = { 0, 0, 0 }, ry[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; ++i) {
      tft.fillScreen(C_BG);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_TEXT);
      tft.drawString("Touch calibration", 160, 100, 4);
      char buf[24]; snprintf(buf, sizeof(buf), "Tap the cross (%d/3)", i + 1);
      tft.drawString(buf, 160, 135, 2);
      drawCross(tx[i], ty[i], C_CHECK);
      int a = 0, b = 0;
      while (!readRawStable(a, b)) { }
      rx[i] = a; ry[i] = b;
    }
    int dx = abs(rx[1] - rx[0]), dy = abs(ry[1] - ry[0]);
    bool sw = dy > dx;                                     // raw Y is the screen X axis
    float x0 = (float)(sw ? ry[0] : rx[0]), x1 = (float)(sw ? ry[1] : rx[1]);
    float y0 = (float)(sw ? rx[0] : ry[0]), y2 = (float)(sw ? rx[2] : ry[2]);
    if (fabsf(x1 - x0) < 600.0f || fabsf(y2 - y0) < 600.0f) {
      tft.fillScreen(C_BG);
      tft.setTextDatum(MC_DATUM); tft.setTextColor(C_CHECK);
      tft.drawString("Calibration failed - try again", 160, 120, 2);
      delay(1500);
      continue;
    }
    cal.swap = sw;
    cal.ax = (float)(tx[1] - tx[0]) / (x1 - x0);
    cal.bx = (float)tx[0] - cal.ax * x0;
    cal.ay = (float)(ty[2] - ty[0]) / (y2 - y0);
    cal.by = (float)ty[0] - cal.ay * y0;
    saveCal();
    break;
  }
}

// ----------------------------------------------------------------- widgets
struct Btn { int16_t x, y, w, h; const char* label; };
static bool hit(const Btn& b, int px, int py) {
  return px >= b.x && px < b.x + b.w && py >= b.y && py < b.y + b.h;
}
static void drawBtn(const Btn& b, uint16_t fill, uint16_t fg) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, fill);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 5, C_BTN_EDGE);
  tft.setTextColor(fg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

// ----------------------------------------------------------------- game state
enum UiState { UI_MENU, UI_SETUP, UI_GAME, UI_PROMO };
static UiState ui = UI_MENU;

static const int MAXGAME = 600;
static int  mode = 0;               // 0 = pass & play, 1 = vs computer
static int  humanSide = ch::SIDE_W;
static int  aiSide = ch::SIDE_B;
static int  level = 2;              // 1..4
static bool flipped = false;
static bool autoRot = false;

static Position cur;
static Move     gameMoves[MAXGAME];
static int      nMoves = 0;
static uint64_t hist[MAXGAME + 2];
static int      nHist = 0;
static Move     legal[ch::MAX_MOVES];
static int      nLegal = 0;
static int      status = ch::GS_PLAY;
static int      selSq = -1, lastFrom = -1, lastTo = -1;
static int      promoFrom = -1, promoTo = -1;

// setup-screen choices
static int setupSide = 0;           // 0 white, 1 black
static int setupLevel = 2;

// AI worker (runs on core 0 so the UI stays responsive)
static ch::Engine engine;
static volatile int aiState = 0;    // 0 idle, 1 requested, 2 thinking, 3 done
static volatile bool aiOk = false;
static Position aiPos;
static Move aiMove;
static int aiLevel = 2;

static void aiTask(void*) {
  for (;;) {
    if (aiState == 1) {
      aiState = 2;
      bool ok = engine.findBest(aiPos, aiLevel, hist, nHist, aiMove);
      aiOk = ok;
      __sync_synchronize();
      aiState = 3;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void refreshState() {
  nLegal = ch::legalMoves(cur, legal);
  status = ch::gameStatus(cur, nLegal, hist, nHist);
  if (status == ch::GS_PLAY && nMoves >= MAXGAME - 2) status = ch::GS_DRAW_50;   // safety cap
  if (mode == 0 && autoRot) flipped = (cur.side == ch::SIDE_B);
}

static void rebuild() {
  ch::setStart(cur);
  nHist = 0;
  hist[nHist++] = ch::hashPos(cur);
  for (int i = 0; i < nMoves; ++i) {
    ch::makeMove(cur, gameMoves[i]);
    hist[nHist++] = ch::hashPos(cur);
  }
  if (nMoves > 0) { lastFrom = gameMoves[nMoves - 1].from; lastTo = gameMoves[nMoves - 1].to; }
  else { lastFrom = -1; lastTo = -1; }
  refreshState();
}

static void applyMove(const Move& m) {
  if (nMoves >= MAXGAME - 1) return;
  ch::makeMove(cur, m);
  gameMoves[nMoves++] = m;
  hist[nHist++] = ch::hashPos(cur);
  lastFrom = m.from; lastTo = m.to;
  refreshState();
}

// ----------------------------------------------------------------- board drawing
static const int SQ = 28, BX = 4, BY = 8;

static void sqXY(int sq, int& x, int& y) {
  int r = sq >> 3, f = sq & 7;
  int col = flipped ? 7 - f : f;
  int row = flipped ? r : 7 - r;
  x = BX + col * SQ; y = BY + row * SQ;
}
static int xyToSq(int px, int py) {
  if (px < BX || py < BY || px >= BX + 8 * SQ || py >= BY + 8 * SQ) return -1;
  int col = (px - BX) / SQ, row = (py - BY) / SQ;
  int f = flipped ? 7 - col : col;
  int r = flipped ? row : 7 - row;
  return r * 8 + f;
}

static void drawPieceAt(int x, int y, int piece) {    // x,y = top-left of a 28x28 cell
  if (piece == 0) return;
  bool white = piece > 0;
  int t = white ? piece : -piece;
  int cx = x + SQ / 2, cy = y + SQ / 2;
  uint16_t edge = white ? C_BPIECE : RGB(225, 225, 225);
  uint16_t fill = white ? C_WPIECE : C_BPIECE;
  uint16_t fg   = white ? C_BPIECE : C_WPIECE;
  tft.fillCircle(cx, cy, 12, edge);
  tft.fillCircle(cx, cy, 10, fill);
  char s[2] = { "?PNBRQK"[t], 0 };
  tft.setTextColor(fg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(s, cx, cy + 1, 2);
}

static void drawBoard() {
  int kingChk = -1;
  if ((status == ch::GS_PLAY || status == ch::GS_CHECKMATE) && ch::inCheck(cur)) kingChk = cur.kingSq[cur.side];
  for (int sq = 0; sq < 64; ++sq) {
    int x, y; sqXY(sq, x, y);
    int r = sq >> 3, f = sq & 7;
    bool light = ((r + f) & 1) == 1;                     // a1 is dark
    uint16_t col = light ? C_LIGHT : C_DARK;
    if (sq == lastFrom || sq == lastTo) col = light ? C_HL_LIGHT : C_HL_DARK;
    if (sq == selSq) col = light ? C_SEL_LIGHT : C_SEL_DARK;
    if (sq == kingChk) col = C_CHECK;
    tft.fillRect(x, y, SQ, SQ, col);
    drawPieceAt(x, y, cur.b[sq]);
  }
  if (selSq >= 0) {
    for (int i = 0; i < nLegal; ++i) {
      if (legal[i].from != selSq) continue;
      int to = legal[i].to, x, y; sqXY(to, x, y);
      int cx = x + SQ / 2, cy = y + SQ / 2;
      bool capture = cur.b[to] != 0 || (cur.b[selSq] != 0 && (cur.b[selSq] == 1 || cur.b[selSq] == -1) && (to & 7) != (selSq & 7));
      if (capture) { tft.drawCircle(cx, cy, 13, RGB(200, 50, 50)); tft.drawCircle(cx, cy, 12, RGB(200, 50, 50)); }
      else tft.fillCircle(cx, cy, 4, RGB(90, 90, 90));
    }
  }
}

// ----------------------------------------------------------------- panel
static Btn gUndo = { 236, 100, 80, 30, "UNDO" };
static Btn gFlip = { 236, 134, 80, 30, "FLIP" };
static Btn gRot  = { 236, 168, 80, 30, "ROT OFF" };
static Btn gMenu = { 236, 202, 80, 30, "MENU" };

static void drawPanel() {
  tft.fillRect(230, 0, 90, 240, C_BG);
  // turn box
  bool whiteTurn = cur.side == ch::SIDE_W;
  tft.fillRoundRect(236, 8, 80, 28, 5, whiteTurn ? C_WPIECE : C_BPIECE);
  tft.drawRoundRect(236, 8, 80, 28, 5, whiteTurn ? C_BPIECE : RGB(225, 225, 225));
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(whiteTurn ? C_BPIECE : C_WPIECE);
  tft.drawString(status != ch::GS_PLAY ? "GAME OVER" : (whiteTurn ? "WHITE" : "BLACK"), 276, 22, 2);

  const char* l1 = ""; const char* l2 = "";
  if (aiState != 0) l1 = "Thinking...";
  else {
    switch (status) {
      case ch::GS_CHECKMATE:     l1 = "Checkmate!"; l2 = whiteTurn ? "Black wins" : "White wins"; break;
      case ch::GS_STALEMATE:     l1 = "Stalemate";  l2 = "Draw"; break;
      case ch::GS_DRAW_50:       l1 = "Draw";       l2 = "50-move rule"; break;
      case ch::GS_DRAW_MATERIAL: l1 = "Draw";       l2 = "no material"; break;
      case ch::GS_DRAW_REPEAT:   l1 = "Draw";       l2 = "repetition"; break;
      default:
        if (ch::inCheck(cur)) l1 = "Check!";
        else if (mode == 1) l1 = (cur.side == humanSide) ? "Your move" : "Computer";
        break;
    }
  }
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(status == ch::GS_PLAY && aiState == 0 && ch::inCheck(cur) ? C_CHECK : C_TEXT);
  tft.drawString(l1, 238, 42, 2);
  tft.setTextColor(C_TEXT);
  tft.drawString(l2, 238, 60, 2);

  if (nMoves > 0) {
    char mv[8], line[16];
    ch::moveToStr(gameMoves[nMoves - 1], mv);
    snprintf(line, sizeof(line), "Last %s", mv);
    tft.setTextColor(C_DIM);
    tft.drawString(line, 238, 80, 2);
  }

  drawBtn(gUndo, C_BTN, C_TEXT);
  drawBtn(gFlip, C_BTN, C_TEXT);
  if (mode == 0) {
    gRot.label = autoRot ? "ROT ON" : "ROT OFF";
    drawBtn(gRot, autoRot ? C_BTN_SEL : C_BTN, C_TEXT);
  }
  drawBtn(gMenu, C_BTN, C_TEXT);
}

static void drawGame() { drawBoard(); drawPanel(); }
static void drawGameFull() { tft.fillScreen(C_BG); drawGame(); }

// ----------------------------------------------------------------- promotion popup
static const int PB_X = 24, PB_Y = 84, PB_W = 184, PB_H = 64;
static const int promoOrder[4] = { ch::QUEEN, ch::ROOK, ch::BISHOP, ch::KNIGHT };

static void drawPromo() {
  tft.fillRoundRect(PB_X, PB_Y, PB_W, PB_H, 6, C_BG);
  tft.drawRoundRect(PB_X, PB_Y, PB_W, PB_H, 6, C_TEXT);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(C_TEXT);
  tft.drawString("Promote to:", PB_X + PB_W / 2, PB_Y + 11, 2);
  int sign = (cur.side == ch::SIDE_W) ? 1 : -1;
  for (int i = 0; i < 4; ++i) {
    int x = PB_X + 8 + i * 44, y = PB_Y + 22;
    tft.fillRoundRect(x, y, 40, 34, 4, C_LIGHT);
    drawPieceAt(x + 6, y + 3, sign * promoOrder[i]);
  }
}

// ----------------------------------------------------------------- menus
static Btn mPP  = { 60, 70, 200, 40, "PASS & PLAY" };
static Btn mAI  = { 60, 120, 200, 40, "VS COMPUTER" };
static Btn mCal = { 60, 170, 200, 40, "CALIBRATE TOUCH" };

static void drawMenu() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_BTN_SEL);
  tft.drawString("CYD CHESS", 160, 34, 4);
  drawBtn(mPP, C_BTN, C_TEXT);
  drawBtn(mAI, C_BTN, C_TEXT);
  drawBtn(mCal, C_BTN, C_TEXT);
  tft.setTextColor(C_DIM);
  tft.drawString("2 players on one screen, or play the engine", 160, 224, 1);
}

static Btn sWhite = { 20, 60, 130, 36, "WHITE" };
static Btn sBlack = { 170, 60, 130, 36, "BLACK" };
static Btn sLvl[4] = { { 20, 128, 68, 36, "1" }, { 96, 128, 68, 36, "2" },
                       { 172, 128, 68, 36, "3" }, { 248, 128, 68, 36, "4" } };
static Btn sStart = { 20, 192, 130, 38, "START" };
static Btn sBack  = { 170, 192, 130, 38, "BACK" };
static const char* levelNames[4] = { "Beginner", "Easy", "Medium", "Hard" };

static void drawSetup() {
  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_BTN_SEL);
  tft.drawString("VS COMPUTER", 160, 20, 4);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_TEXT);
  tft.drawString("You play:", 20, 40, 2);
  tft.drawString("Level:", 20, 108, 2);
  drawBtn(sWhite, setupSide == 0 ? C_BTN_SEL : C_BTN, C_TEXT);
  drawBtn(sBlack, setupSide == 1 ? C_BTN_SEL : C_BTN, C_TEXT);
  for (int i = 0; i < 4; ++i) drawBtn(sLvl[i], setupLevel == i + 1 ? C_BTN_SEL : C_BTN, C_TEXT);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM);
  tft.drawString(levelNames[setupLevel - 1], 160, 176, 2);
  drawBtn(sStart, C_BTN_SEL, C_TEXT);
  drawBtn(sBack, C_BTN, C_TEXT);
}

// ----------------------------------------------------------------- game flow
static void newGame() {
  nMoves = 0; selSq = -1; promoFrom = promoTo = -1; autoRot = false;
  aiState = 0;
  if (mode == 1) {
    humanSide = (setupSide == 0) ? ch::SIDE_W : ch::SIDE_B;
    aiSide = humanSide ^ 1;
    level = setupLevel;
    flipped = (humanSide == ch::SIDE_B);
  } else {
    flipped = false;
  }
  rebuild();
  ui = UI_GAME;
  drawGameFull();
}

static void playHuman(const Move& m) {
  applyMove(m);
  selSq = -1;
  drawGame();
}

static void doUndo() {
  if (nMoves == 0) return;
  if (mode == 1) {
    int humanMoves = nMoves - (humanSide == ch::SIDE_B ? 1 : 0);
    if (humanMoves <= 0) return;                    // nothing of the player's to take back
    nMoves--;
    int sideToMove = (nMoves & 1) ? ch::SIDE_B : ch::SIDE_W;
    if (sideToMove != humanSide && nMoves > 0) nMoves--;
  } else {
    nMoves--;
  }
  selSq = -1;
  rebuild();
  drawGame();
}

static void onGameTap(int x, int y) {
  if (aiState != 0) return;                         // engine is thinking
  if (hit(gUndo, x, y)) { doUndo(); return; }
  if (hit(gFlip, x, y)) { flipped = !flipped; drawGame(); return; }
  if (mode == 0 && hit(gRot, x, y)) {
    autoRot = !autoRot;
    if (autoRot) flipped = (cur.side == ch::SIDE_B);
    drawGame();
    return;
  }
  if (hit(gMenu, x, y)) { ui = UI_MENU; drawMenu(); return; }
  if (status != ch::GS_PLAY) return;
  if (mode == 1 && cur.side != humanSide) return;

  int sq = xyToSq(x, y);
  if (sq < 0) return;
  int pc = cur.b[sq];
  bool own = pc != 0 && ((pc > 0) == (cur.side == ch::SIDE_W));

  if (selSq >= 0) {
    int cnt = 0; Move cand = ch::mk(0, 0);
    for (int i = 0; i < nLegal; ++i)
      if (legal[i].from == selSq && legal[i].to == sq) { if (cnt == 0) cand = legal[i]; cnt++; }
    if (cnt == 1) { playHuman(cand); return; }
    if (cnt > 1) {                                  // promotion: ask which piece
      promoFrom = selSq; promoTo = sq;
      ui = UI_PROMO; drawPromo();
      return;
    }
    if (own) { selSq = (sq == selSq) ? -1 : sq; drawBoard(); return; }
    selSq = -1; drawBoard();
    return;
  }
  if (own) { selSq = sq; drawBoard(); }
}

static void onPromoTap(int x, int y) {
  for (int i = 0; i < 4; ++i) {
    Btn b = { (int16_t)(PB_X + 8 + i * 44), (int16_t)(PB_Y + 22), 40, 34, "" };
    if (hit(b, x, y)) {
      for (int k = 0; k < nLegal; ++k) {
        if (legal[k].from == promoFrom && legal[k].to == promoTo && legal[k].promo == promoOrder[i]) {
          ui = UI_GAME;
          playHuman(legal[k]);
          return;
        }
      }
      return;
    }
  }
  bool inside = x >= PB_X && x < PB_X + PB_W && y >= PB_Y && y < PB_Y + PB_H;
  if (!inside) { ui = UI_GAME; selSq = -1; drawGame(); }     // tap outside cancels
}

static void gameTick() {
  if (mode != 1) return;
  if (aiState == 3) {                                         // engine finished
    Move m = aiMove;
    bool valid = false;
    if (aiOk) for (int i = 0; i < nLegal; ++i) if (ch::sameMove(legal[i], m)) valid = true;
    if (!valid && nLegal > 0) m = legal[0];                   // never play anything illegal
    aiState = 0;
    if (nLegal > 0) applyMove(m);
    selSq = -1;
    drawGame();
    return;
  }
  if (aiState == 0 && status == ch::GS_PLAY && cur.side == aiSide && nLegal > 0) {
    aiPos = cur;
    aiLevel = level;
    aiState = 1;
    drawPanel();                                              // shows "Thinking..."
  }
}

static void onMenuTap(int x, int y) {
  if (hit(mPP, x, y))  { mode = 0; newGame(); }
  else if (hit(mAI, x, y)) { ui = UI_SETUP; drawSetup(); }
  else if (hit(mCal, x, y)) { runCalibration(); drawMenu(); }
}

static void onSetupTap(int x, int y) {
  if (hit(sWhite, x, y)) { setupSide = 0; drawSetup(); return; }
  if (hit(sBlack, x, y)) { setupSide = 1; drawSetup(); return; }
  for (int i = 0; i < 4; ++i) if (hit(sLvl[i], x, y)) { setupLevel = i + 1; drawSetup(); return; }
  if (hit(sStart, x, y)) { mode = 1; newGame(); return; }
  if (hit(sBack, x, y)) { ui = UI_MENU; drawMenu(); }
}

// ----------------------------------------------------------------- arduino entry points
void setup() {
  Serial.begin(115200);
  // CYD has an RGB LED (active low): make sure it is off
  pinMode(4, OUTPUT);  digitalWrite(4, HIGH);
  pinMode(16, OUTPUT); digitalWrite(16, HIGH);
  pinMode(17, OUTPUT); digitalWrite(17, HIGH);
  pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, HIGH);

  tft.init();
#ifdef CYD_INVERT_DISPLAY
  tft.invertDisplay(true);
#endif
  tft.setRotation(1);                                         // landscape, 320x240
  tft.fillScreen(C_BG);

  touchSPI.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  if (!loadCal()) runCalibration();                           // first boot after a flash

  xTaskCreatePinnedToCore(aiTask, "chess-ai", 24576, NULL, 1, NULL, 0);

  ch::setStart(cur);
  nHist = 0; hist[nHist++] = ch::hashPos(cur);
  refreshState();
  drawMenu();
}

void loop() {
  int tx = 0, ty = 0;
  bool tap = pollTap(tx, ty);
  switch (ui) {
    case UI_MENU:  if (tap) onMenuTap(tx, ty);  break;
    case UI_SETUP: if (tap) onSetupTap(tx, ty); break;
    case UI_GAME:  if (tap) onGameTap(tx, ty); if (ui == UI_GAME) gameTick(); break;
    case UI_PROMO: if (tap) onPromoTap(tx, ty); break;
  }
  delay(5);
}
