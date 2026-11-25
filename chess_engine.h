// chess_engine.h  -  self-contained chess rules + search engine.
// No hardware dependencies: it is unit-tested on a PC (perft) and runs on the ESP32.
//
// Squares: 0 = a1, 7 = h1, 56 = a8, 63 = h8   (sq = rank*8 + file)
// Pieces : +1..+6 white P N B R Q K, -1..-6 black.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  static inline uint32_t eng_rand() {          // tiny xorshift, entropy from the microsecond timer
    static uint32_t s = 2463534242u;
    s ^= (uint32_t)micros(); s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
  }
  #define ENG_MILLIS() ((uint32_t)millis())
  #define ENG_YIELD()  vTaskDelay(1)          // keeps the FreeRTOS idle task / watchdog fed
  #define ENG_RAND()   eng_rand()
#else
  #include <chrono>
  static inline uint32_t eng_host_millis() {
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  #define ENG_MILLIS() eng_host_millis()
  #define ENG_YIELD()  ((void)0)
  #define ENG_RAND()   ((uint32_t)rand())
#endif

namespace ch {

enum { NOPIECE = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6 };
enum { SIDE_W = 0, SIDE_B = 1 };

static const int MAX_MOVES = 256;
static const int MAX_PLY   = 40;
static const int MATE      = 30000;
static const int INF       = 32000;

struct Move {
  uint8_t from, to, promo, pad;
  int16_t score;
};

static inline Move mk(int f, int t, int pr = 0) {
  Move m; m.from = (uint8_t)f; m.to = (uint8_t)t; m.promo = (uint8_t)pr; m.pad = 0; m.score = 0;
  return m;
}
static inline bool sameMove(const Move& a, const Move& b) {
  return a.from == b.from && a.to == b.to && a.promo == b.promo;
}

struct Position {
  int8_t   b[64];
  uint8_t  side;        // SIDE_W / SIDE_B  (side to move)
  uint8_t  castle;      // bit0 WK, bit1 WQ, bit2 BK, bit3 BQ
  int8_t   ep;          // en-passant target square or -1
  uint8_t  kingSq[2];
  uint16_t halfmove;
  uint16_t fullmove;
};

// ------------------------------------------------------------------ setup
static inline void setStart(Position& p) {
  memset(&p, 0, sizeof(p));
  static const int8_t back[8] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
  for (int f = 0; f < 8; ++f) {
    p.b[f]      = back[f];
    p.b[8 + f]  = PAWN;
    p.b[48 + f] = -PAWN;
    p.b[56 + f] = -back[f];
  }
  p.side = SIDE_W; p.castle = 15; p.ep = -1;
  p.kingSq[0] = 4; p.kingSq[1] = 60;
  p.halfmove = 0; p.fullmove = 1;
}

static inline bool setFen(Position& p, const char* fen) {
  memset(&p, 0, sizeof(p));
  p.ep = -1; p.fullmove = 1;
  int r = 7, f = 0;
  const char* s = fen;
  for (; *s && *s != ' '; ++s) {
    char c = *s;
    if (c == '/') { r--; f = 0; }
    else if (c >= '1' && c <= '8') { f += c - '0'; }
    else {
      char lc = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
      int pc = 0;
      switch (lc) {
        case 'p': pc = PAWN; break;   case 'n': pc = KNIGHT; break;
        case 'b': pc = BISHOP; break; case 'r': pc = ROOK; break;
        case 'q': pc = QUEEN; break;  case 'k': pc = KING; break;
        default: return false;
      }
      if (r < 0 || r > 7 || f < 0 || f > 7) return false;
      int sign = (c >= 'a' && c <= 'z') ? -1 : 1;
      p.b[r * 8 + f] = (int8_t)(sign * pc);
      if (pc == KING) p.kingSq[sign > 0 ? 0 : 1] = (uint8_t)(r * 8 + f);
      f++;
    }
  }
  if (*s == ' ') s++;
  p.side = (*s == 'b') ? SIDE_B : SIDE_W;
  if (*s) s++;
  if (*s == ' ') s++;
  for (; *s && *s != ' '; ++s) {
    if (*s == 'K') p.castle |= 1;
    else if (*s == 'Q') p.castle |= 2;
    else if (*s == 'k') p.castle |= 4;
    else if (*s == 'q') p.castle |= 8;
  }
  if (*s == ' ') s++;
  if (*s && *s != '-') {
    int ef = s[0] - 'a', er = s[1] - '1';
    if (ef >= 0 && ef < 8 && er >= 0 && er < 8) p.ep = (int8_t)(er * 8 + ef);
    s += 2;
  } else if (*s) s++;
  if (*s == ' ') s++;
  if (*s) { p.halfmove = (uint16_t)atoi(s); while (*s && *s != ' ') s++; }
  if (*s == ' ') s++;
  if (*s) { int fm = atoi(s); p.fullmove = (uint16_t)(fm > 0 ? fm : 1); }
  return true;
}

// ------------------------------------------------------------------ attacks
static const int8_t KN_DR[8]  = { 2, 2, -2, -2, 1, 1, -1, -1 };
static const int8_t KN_DF[8]  = { 1, -1, 1, -1, 2, -2, 2, -2 };
static const int8_t DIR_DR[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };   // 0..3 orthogonal, 4..7 diagonal
static const int8_t DIR_DF[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

// Is square `sq` attacked by any piece of colour `by`?
static inline bool isAttacked(const Position& p, int sq, int by) {
  const int r = sq >> 3, f = sq & 7;
  const int sign = (by == SIDE_W) ? 1 : -1;

  // pawns: a white pawn attacking sq stands one rank below it, a black one one rank above
  int pr = (by == SIDE_W) ? r - 1 : r + 1;
  if (pr >= 0 && pr < 8) {
    if (f > 0 && p.b[pr * 8 + f - 1] == sign * PAWN) return true;
    if (f < 7 && p.b[pr * 8 + f + 1] == sign * PAWN) return true;
  }
  // knights
  for (int i = 0; i < 8; ++i) {
    int nr = r + KN_DR[i], nf = f + KN_DF[i];
    if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
    if (p.b[nr * 8 + nf] == sign * KNIGHT) return true;
  }
  // king
  for (int i = 0; i < 8; ++i) {
    int nr = r + DIR_DR[i], nf = f + DIR_DF[i];
    if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
    if (p.b[nr * 8 + nf] == sign * KING) return true;
  }
  // sliders
  for (int d = 0; d < 8; ++d) {
    int nr = r + DIR_DR[d], nf = f + DIR_DF[d];
    while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
      int pc = p.b[nr * 8 + nf];
      if (pc != 0) {
        if (pc == sign * QUEEN) return true;
        if (d < 4 && pc == sign * ROOK) return true;
        if (d >= 4 && pc == sign * BISHOP) return true;
        break;
      }
      nr += DIR_DR[d]; nf += DIR_DF[d];
    }
  }
  return false;
}

static inline bool inCheck(const Position& p) {
  return isAttacked(p, p.kingSq[p.side], p.side ^ 1);
}

// ------------------------------------------------------------------ move generation
static inline void addPromos(Move* out, int& n, int from, int to) {
  out[n++] = mk(from, to, QUEEN);
  out[n++] = mk(from, to, ROOK);
  out[n++] = mk(from, to, BISHOP);
  out[n++] = mk(from, to, KNIGHT);
}

// Pseudo-legal moves (may leave own king in check). capsOnly = captures + promotions.
static inline int genMoves(const Position& p, Move* out, bool capsOnly) {
  int n = 0;
  const int us = p.side;
  const int sign = (us == SIDE_W) ? 1 : -1;

  for (int sq = 0; sq < 64; ++sq) {
    int pc = p.b[sq];
    if (pc == 0 || ((pc > 0) != (sign > 0))) continue;
    const int type = pc > 0 ? pc : -pc;
    const int r = sq >> 3, f = sq & 7;

    if (type == PAWN) {
      const int startR = (us == SIDE_W) ? 1 : 6;
      const int promoR = (us == SIDE_W) ? 6 : 1;
      const int nr = r + sign;
      const int to = nr * 8 + f;
      if (p.b[to] == 0) {
        if (r == promoR) addPromos(out, n, sq, to);
        else if (!capsOnly) {
          out[n++] = mk(sq, to);
          if (r == startR && p.b[to + 8 * sign] == 0) out[n++] = mk(sq, to + 8 * sign);
        }
      }
      for (int df = -1; df <= 1; df += 2) {
        int nf = f + df;
        if (nf < 0 || nf > 7) continue;
        int t = nr * 8 + nf;
        int tp = p.b[t];
        if (tp != 0 && ((tp > 0) != (sign > 0))) {
          if (r == promoR) addPromos(out, n, sq, t);
          else out[n++] = mk(sq, t);
        } else if (tp == 0 && t == p.ep) {
          out[n++] = mk(sq, t);                       // en passant
        }
      }
    } else if (type == KNIGHT) {
      for (int i = 0; i < 8; ++i) {
        int nr = r + KN_DR[i], nf = f + KN_DF[i];
        if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
        int t = nr * 8 + nf, tp = p.b[t];
        if (tp == 0) { if (!capsOnly) out[n++] = mk(sq, t); }
        else if ((tp > 0) != (sign > 0)) out[n++] = mk(sq, t);
      }
    } else if (type == KING) {
      for (int i = 0; i < 8; ++i) {
        int nr = r + DIR_DR[i], nf = f + DIR_DF[i];
        if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
        int t = nr * 8 + nf, tp = p.b[t];
        if (tp == 0) { if (!capsOnly) out[n++] = mk(sq, t); }
        else if ((tp > 0) != (sign > 0)) out[n++] = mk(sq, t);
      }
      if (!capsOnly) {
        const int base = (us == SIDE_W) ? 0 : 56;
        const int enemy = us ^ 1;
        if (sq == base + 4) {
          const int ks = (us == SIDE_W) ? 1 : 4;
          const int qs = (us == SIDE_W) ? 2 : 8;
          if ((p.castle & ks) && p.b[base + 7] == sign * ROOK &&
              p.b[base + 5] == 0 && p.b[base + 6] == 0 &&
              !isAttacked(p, base + 4, enemy) && !isAttacked(p, base + 5, enemy) &&
              !isAttacked(p, base + 6, enemy))
            out[n++] = mk(sq, base + 6);
          if ((p.castle & qs) && p.b[base + 0] == sign * ROOK &&
              p.b[base + 1] == 0 && p.b[base + 2] == 0 && p.b[base + 3] == 0 &&
              !isAttacked(p, base + 4, enemy) && !isAttacked(p, base + 3, enemy) &&
              !isAttacked(p, base + 2, enemy))
            out[n++] = mk(sq, base + 2);
        }
      }
    } else {  // sliders
      int d0 = (type == BISHOP) ? 4 : 0;
      int d1 = (type == ROOK) ? 4 : 8;
      for (int d = d0; d < d1; ++d) {
        int nr = r + DIR_DR[d], nf = f + DIR_DF[d];
        while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
          int t = nr * 8 + nf, tp = p.b[t];
          if (tp == 0) { if (!capsOnly) out[n++] = mk(sq, t); }
          else {
            if ((tp > 0) != (sign > 0)) out[n++] = mk(sq, t);
            break;
          }
          nr += DIR_DR[d]; nf += DIR_DF[d];
        }
      }
    }
  }
  return n;
}

static inline uint8_t castleMask(int sq) {
  switch (sq) {
    case 0:  return 13;   // a1: clears WQ
    case 7:  return 14;   // h1: clears WK
    case 4:  return 12;   // e1: clears WK+WQ
    case 56: return 7;    // a8: clears BQ
    case 63: return 11;   // h8: clears BK
    case 60: return 3;    // e8: clears BK+BQ
    default: return 15;
  }
}

// Plays a (pseudo-legal) move on p.
static inline void makeMove(Position& p, const Move& m) {
  const int from = m.from, to = m.to;
  const int piece = p.b[from];
  const int sign = piece > 0 ? 1 : -1;
  const int type = piece > 0 ? piece : -piece;
  const int side = p.side;
  int captured = p.b[to];
  const int oldEp = p.ep;
  p.ep = -1;

  if (type == PAWN && to == oldEp && captured == 0 && (from & 7) != (to & 7)) {
    int capSq = to - 8 * sign;                 // en passant: victim sits behind the target square
    captured = p.b[capSq];
    p.b[capSq] = 0;
  }

  p.b[from] = 0;
  p.b[to] = (m.promo != 0) ? (int8_t)(sign * m.promo) : (int8_t)piece;

  if (type == KING) {
    p.kingSq[side] = (uint8_t)to;
    if (to - from == 2) { p.b[from + 1] = p.b[from + 3]; p.b[from + 3] = 0; }       // O-O
    else if (from - to == 2) { p.b[from - 1] = p.b[from - 4]; p.b[from - 4] = 0; }  // O-O-O
  }

  if (type == PAWN && (to - from == 16 || from - to == 16)) {
    int enemyPawn = -sign * PAWN;
    bool can = ((to & 7) > 0 && p.b[to - 1] == enemyPawn) ||
               ((to & 7) < 7 && p.b[to + 1] == enemyPawn);
    if (can) p.ep = (int8_t)((from + to) / 2);
  }

  p.castle &= castleMask(from);
  p.castle &= castleMask(to);

  if (type == PAWN || captured != 0) p.halfmove = 0; else p.halfmove++;
  if (side == SIDE_B) p.fullmove++;
  p.side = (uint8_t)(side ^ 1);
}

// Fully legal moves (compacted in place).
static inline int legalMoves(const Position& p, Move* out) {
  int n = genMoves(p, out, false);
  int k = 0;
  const int us = p.side;
  for (int i = 0; i < n; ++i) {
    Position np = p;
    makeMove(np, out[i]);
    if (!isAttacked(np, np.kingSq[us], us ^ 1)) out[k++] = out[i];
  }
  return k;
}

// ------------------------------------------------------------------ hashing / game status
static inline uint64_t hashPos(const Position& p) {
  uint64_t h = 1469598103934665603ULL;
  for (int i = 0; i < 64; ++i) { h ^= (uint64_t)(uint8_t)(p.b[i] + 8); h *= 1099511628211ULL; }
  h ^= (uint64_t)(p.side + 1);                   h *= 1099511628211ULL;
  h ^= (uint64_t)(p.castle + 17);                h *= 1099511628211ULL;
  h ^= (uint64_t)((uint8_t)(p.ep + 1) + 101);    h *= 1099511628211ULL;
  return h;
}

enum { GS_PLAY = 0, GS_CHECKMATE, GS_STALEMATE, GS_DRAW_50, GS_DRAW_MATERIAL, GS_DRAW_REPEAT };

static inline bool insufficientMaterial(const Position& p) {
  int minors = 0, bishopsLight = 0, bishopsDark = 0, knights = 0;
  for (int sq = 0; sq < 64; ++sq) {
    int pc = p.b[sq]; if (pc == 0) continue;
    int t = pc > 0 ? pc : -pc;
    if (t == PAWN || t == ROOK || t == QUEEN) return false;
    if (t == KNIGHT) { minors++; knights++; }
    if (t == BISHOP) {
      minors++;
      if ((((sq >> 3) + (sq & 7)) & 1) == 0) bishopsDark++; else bishopsLight++;
    }
  }
  if (minors <= 1) return true;                         // K v K, K+minor v K
  if (knights == 0 && (bishopsLight == 0 || bishopsDark == 0)) return true;  // only same-coloured bishops
  return false;
}

// nLegal = number of legal moves in p; hist = hashes of every position of the game (incl. p).
static inline int gameStatus(const Position& p, int nLegal, const uint64_t* hist, int nHist) {
  if (nLegal == 0) return inCheck(p) ? GS_CHECKMATE : GS_STALEMATE;
  if (insufficientMaterial(p)) return GS_DRAW_MATERIAL;
  if (nHist > 0) {
    int cnt = 0;
    for (int i = 0; i < nHist; ++i) if (hist[i] == hist[nHist - 1]) cnt++;
    if (cnt >= 3) return GS_DRAW_REPEAT;
  }
  if (p.halfmove >= 100) return GS_DRAW_50;
  return GS_PLAY;
}

static inline void moveToStr(const Move& m, char* s) {   // s must hold >= 6 bytes
  s[0] = (char)('a' + (m.from & 7)); s[1] = (char)('1' + (m.from >> 3));
  s[2] = (char)('a' + (m.to & 7));   s[3] = (char)('1' + (m.to >> 3));
  int k = 4;
  if (m.promo) { static const char pc[7] = { '?', '?', 'n', 'b', 'r', 'q', '?' }; s[k++] = pc[m.promo]; }
  s[k] = 0;
}

// ------------------------------------------------------------------ evaluation
static const int PIECE_VAL[7] = { 0, 100, 320, 330, 500, 900, 0 };

// Piece-square tables, written from White's point of view, first row = rank 8.
static const int8_t PST[7][64] = {
  { 0 },
  { // pawn
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0 },
  { // knight
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50 },
  { // bishop
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -20,-10,-10,-10,-10,-10,-10,-20 },
  { // rook
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0 },
  { // queen
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,
   -10,  5,  5,  5,  5,  5,  0,-10,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20 },
  { // king (middlegame)
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20 }
};

static const int8_t KING_END[64] = {
  -50,-40,-30,-20,-20,-30,-40,-50,
  -30,-20,-10,  0,  0,-10,-20,-30,
  -30,-10, 20, 30, 30, 20,-10,-30,
  -30,-10, 30, 40, 40, 30,-10,-30,
  -30,-10, 30, 40, 40, 30,-10,-30,
  -30,-10, 20, 30, 30, 20,-10,-30,
  -30,-30,  0,  0,  0,  0,-30,-30,
  -50,-30,-30,-30,-30,-30,-30,-50
};

static inline int iabs(int x) { return x < 0 ? -x : x; }
static inline int imin(int a, int b) { return a < b ? a : b; }

// Score in centipawns from the point of view of the side to move.
static inline int evaluate(const Position& p) {
  int mat[2] = { 0, 0 };
  int nonPawn = 0;
  int bishops[2] = { 0, 0 };
  for (int sq = 0; sq < 64; ++sq) {
    int pc = p.b[sq]; if (pc == 0) continue;
    int t = pc > 0 ? pc : -pc;
    int side = pc > 0 ? 0 : 1;
    if (t != KING) mat[side] += PIECE_VAL[t];
    if (t != KING && t != PAWN) nonPawn += PIECE_VAL[t];
    if (t == BISHOP) bishops[side]++;
  }
  const bool endgame = nonPawn <= 1300;
  const int diff = mat[0] - mat[1];
  // "mop-up": a clearly winning side in an endgame drives the lone king to the edge
  const bool mopUp = endgame && (diff >= 450 || diff <= -450);

  int score = 0;
  for (int sq = 0; sq < 64; ++sq) {
    int pc = p.b[sq]; if (pc == 0) continue;
    int t = pc > 0 ? pc : -pc;
    int r = sq >> 3, f = sq & 7;
    int v = (t == KING) ? 0 : PIECE_VAL[t];
    if (pc > 0) {
      int idx = (7 - r) * 8 + f;
      if (t == KING) { if (!mopUp) v += endgame ? KING_END[idx] : PST[KING][idx]; }
      else v += PST[t][idx];
      score += v;
    } else {
      int idx = r * 8 + f;
      if (t == KING) { if (!mopUp) v += endgame ? KING_END[idx] : PST[KING][idx]; }
      else v += PST[t][idx];
      score -= v;
    }
  }
  if (bishops[0] >= 2) score += 30;
  if (bishops[1] >= 2) score -= 30;

  if (mopUp) {
    int strong = diff > 0 ? 0 : 1;
    int weakK = p.kingSq[strong ^ 1], strongK = p.kingSq[strong];
    int wr = weakK >> 3, wf = weakK & 7;
    int edge = (3 - imin(wf, 7 - wf)) + (3 - imin(wr, 7 - wr));     // 0 centre .. 6 corner
    int dist = iabs((weakK >> 3) - (strongK >> 3)) + iabs((weakK & 7) - (strongK & 7));
    int bonus = edge * 10 + (14 - dist) * 4;
    score += (strong == 0) ? bonus : -bonus;
  }
  return (p.side == SIDE_W) ? score : -score;
}

// ------------------------------------------------------------------ search
struct Engine {
  Move buf[MAX_PLY][MAX_MOVES];
  uint32_t nodes;
  uint32_t deadline;
  volatile bool stop;
  int curDepth;

  void checkTime() {
    if (curDepth > 1 && ENG_MILLIS() > deadline) stop = true;
  }

  static void scoreMoves(const Position& p, Move* ml, int n) {
    for (int i = 0; i < n; ++i) {
      Move& m = ml[i];
      int victim = p.b[m.to]; victim = victim < 0 ? -victim : victim;
      int attacker = p.b[m.from]; attacker = attacker < 0 ? -attacker : attacker;
      if (victim == 0 && attacker == PAWN && (m.from & 7) != (m.to & 7)) victim = PAWN;  // e.p.
      int s = 0;
      if (victim) s = 1000 + victim * 10 - attacker;
      if (m.promo) s += 800 + m.promo;
      m.score = (int16_t)s;
    }
  }
  static void pickBest(Move* ml, int n, int i) {
    int best = i;
    for (int j = i + 1; j < n; ++j) if (ml[j].score > ml[best].score) best = j;
    if (best != i) { Move t = ml[i]; ml[i] = ml[best]; ml[best] = t; }
  }

  int quiesce(const Position& pos, int alpha, int beta, int ply) {
    nodes++;
    if ((nodes & 2047) == 0) checkTime();
    if ((nodes & 16383) == 0) ENG_YIELD();
    if (stop) return 0;
    if (ply >= MAX_PLY - 1) return evaluate(pos);

    int stand = evaluate(pos);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    Move* ml = buf[ply];
    int n = genMoves(pos, ml, true);
    scoreMoves(pos, ml, n);
    const int us = pos.side;
    for (int i = 0; i < n; ++i) {
      pickBest(ml, n, i);
      Position np = pos;
      makeMove(np, ml[i]);
      if (isAttacked(np, np.kingSq[us], us ^ 1)) continue;
      int s = -quiesce(np, -beta, -alpha, ply + 1);
      if (stop) return 0;
      if (s >= beta) return beta;
      if (s > alpha) alpha = s;
    }
    return alpha;
  }

  int search(const Position& pos, int depth, int alpha, int beta, int ply) {
    nodes++;
    if ((nodes & 2047) == 0) checkTime();
    if ((nodes & 16383) == 0) ENG_YIELD();
    if (stop) return 0;
    if (pos.halfmove >= 100) return 0;

    const int us = pos.side;
    const bool chk = isAttacked(pos, pos.kingSq[us], us ^ 1);
    if (chk && ply < MAX_PLY - 4) depth++;                     // check extension
    if (depth <= 0) return quiesce(pos, alpha, beta, ply);
    if (ply >= MAX_PLY - 1) return evaluate(pos);

    Move* ml = buf[ply];
    int n = genMoves(pos, ml, false);
    scoreMoves(pos, ml, n);
    int legal = 0;
    for (int i = 0; i < n; ++i) {
      pickBest(ml, n, i);
      Position np = pos;
      makeMove(np, ml[i]);
      if (isAttacked(np, np.kingSq[us], us ^ 1)) continue;
      legal++;
      int s = -search(np, depth - 1, -beta, -alpha, ply + 1);
      if (stop) return 0;
      if (s >= beta) return beta;
      if (s > alpha) alpha = s;
    }
    if (legal == 0) return chk ? -MATE + ply : 0;
    return alpha;
  }

  // level 1 (beginner) .. 4 (hard). Returns false only if there is no legal move.
  bool findBest(const Position& root, int level, const uint64_t* hist, int histLen,
                Move& best, int* scoreOut = 0) {
    static const int maxDepthTab[5] = { 1, 1, 2, 4, 8 };
    static const int timeTab[5]     = { 100, 150, 800, 2500, 5500 };
    if (level < 1) level = 1;
    if (level > 4) level = 4;

    Move rootMoves[MAX_MOVES];
    int rn = legalMoves(root, rootMoves);
    if (rn == 0) return false;
    best = rootMoves[0];
    if (scoreOut) *scoreOut = 0;
    if (rn == 1) return true;

    if (level == 1 && (ENG_RAND() % 100) < 35) {               // beginner: sometimes plays randomly
      best = rootMoves[ENG_RAND() % (uint32_t)rn];
      return true;
    }

    nodes = 0; stop = false; curDepth = 1;
    const uint32_t start = ENG_MILLIS();
    const int limit = timeTab[level];
    deadline = start + (uint32_t)limit;
    scoreMoves(root, rootMoves, rn);
    int bestScore = -INF;

    for (int depth = 1; depth <= maxDepthTab[level]; ++depth) {
      curDepth = depth;
      // previous best move first
      for (int i = 0; i < rn; ++i)
        if (sameMove(rootMoves[i], best)) { Move t = rootMoves[0]; rootMoves[0] = rootMoves[i]; rootMoves[i] = t; break; }

      int alpha = -INF;
      int iterScore = -INF;
      Move iterBest = rootMoves[0];
      bool complete = true;
      for (int i = 0; i < rn; ++i) {
        if (i > 0) {                                              // order the rest by capture value
          int bj = i;
          for (int j = i + 1; j < rn; ++j) if (rootMoves[j].score > rootMoves[bj].score) bj = j;
          if (bj != i) { Move t = rootMoves[i]; rootMoves[i] = rootMoves[bj]; rootMoves[bj] = t; }
        }
        Position np = root;
        makeMove(np, rootMoves[i]);
        int s;
        // moving into a 3rd repetition = draw: avoid it when ahead, accept it when behind
        int reps = 0;
        if (hist && histLen > 0) {
          uint64_t h = hashPos(np);
          for (int k = 0; k < histLen; ++k) if (hist[k] == h) reps++;
        }
        if (reps >= 2) s = 0;
        else s = -search(np, depth - 1, -INF, -alpha, 1);
        if (stop) { complete = false; break; }
        if (s > iterScore) { iterScore = s; iterBest = rootMoves[i]; }
        if (s > alpha) alpha = s;
      }
      if (!complete) break;
      best = iterBest;
      bestScore = iterScore;
      if (bestScore > MATE - 100 || bestScore < -MATE + 100) break;   // forced mate found (or suffered)
      if ((int)(ENG_MILLIS() - start) > limit / 2) break;             // next depth would not finish
    }
    if (scoreOut) *scoreOut = bestScore;
    return true;
  }
};

}  // namespace ch
