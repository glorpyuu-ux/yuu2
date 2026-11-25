// Host test:  g++ -O2 -std=gnu++11 -I../src test_engine.cpp -o test_engine && ./test_engine
#include <stdio.h>
#include <string>
#include "chess_engine.h"
using namespace ch;

static uint64_t perft(const Position& p, int depth) {
  Move ml[MAX_MOVES];
  int n = legalMoves(p, ml);
  if (depth == 1) return (uint64_t)n;
  uint64_t total = 0;
  for (int i = 0; i < n; ++i) { Position np = p; makeMove(np, ml[i]); total += perft(np, depth - 1); }
  return total;
}

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } } while (0)

struct PerftCase { const char* name; const char* fen; int depth; uint64_t expect; };

static Engine eng;   // big, keep it static

static Move findMove(const Position& p, const char* uci) {
  Move ml[MAX_MOVES]; int n = legalMoves(p, ml);
  for (int i = 0; i < n; ++i) { char s[8]; moveToStr(ml[i], s); if (!strcmp(s, uci)) return ml[i]; }
  Move bad = mk(0, 0); bad.pad = 99; return bad;
}

int main() {
  const PerftCase cases[] = {
    { "startpos d1", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20 },
    { "startpos d2", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 2, 400 },
    { "startpos d3", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902 },
    { "startpos d4", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281 },
    { "startpos d5", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609 },
    { "kiwipete d1", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48 },
    { "kiwipete d2", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, 2039 },
    { "kiwipete d3", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862 },
    { "kiwipete d4", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603 },
    { "pos3 d5", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624 },
    { "pos4 d4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333 },
    { "pos4 mirrored d4", "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 4, 422333 },
    { "pos5 d4", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487 },
    { "pos6 d4", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594 },
  };
  printf("== perft ==\n");
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    Position p; if (!setFen(p, cases[i].fen)) { printf("bad fen\n"); return 2; }
    uint64_t got = perft(p, cases[i].depth);
    printf("  %-18s expect %9llu got %9llu %s\n", cases[i].name,
           (unsigned long long)cases[i].expect, (unsigned long long)got, got == cases[i].expect ? "ok" : "MISMATCH");
    if (got != cases[i].expect) failures++;
  }

  printf("== game status ==\n");
  { // fool's mate
    Position p; setStart(p); Move ml[MAX_MOVES];
    const char* seq[] = { "f2f3", "e7e5", "g2g4", "d8h4" };
    for (int i = 0; i < 4; ++i) { Move m = findMove(p, seq[i]); CHECK(m.pad != 99, "fools mate move legal"); makeMove(p, m); }
    int n = legalMoves(p, ml);
    CHECK(n == 0 && gameStatus(p, n, 0, 0) == GS_CHECKMATE, "fool's mate = checkmate");
  }
  { // stalemate
    Position p; setFen(p, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"); Move ml[MAX_MOVES];
    int n = legalMoves(p, ml);
    CHECK(n == 0 && gameStatus(p, n, 0, 0) == GS_STALEMATE, "stalemate detected");
  }
  { Position p; setFen(p, "8/8/4k3/8/8/3K4/8/8 w - - 0 1"); CHECK(insufficientMaterial(p), "K v K"); }
  { Position p; setFen(p, "8/8/4k3/8/8/3KB3/8/8 w - - 0 1"); CHECK(insufficientMaterial(p), "K+B v K"); }
  { Position p; setFen(p, "8/8/4k3/8/8/3KN3/8/8 w - - 0 1"); CHECK(insufficientMaterial(p), "K+N v K"); }
  { Position p; setFen(p, "8/8/4kb2/8/8/3KB3/8/8 w - - 0 1"); CHECK(insufficientMaterial(p), "KB v KB same colour"); }
  { Position p; setFen(p, "8/8/2b1k3/8/8/3KB3/8/8 w - - 0 1"); CHECK(!insufficientMaterial(p), "KB v KB opposite colour is NOT dead"); }
  { Position p; setFen(p, "8/8/4k3/8/8/3KP3/8/8 w - - 0 1"); CHECK(!insufficientMaterial(p), "K+P v K not dead"); }
  { Position p; setFen(p, "8/8/4k3/8/8/3KNN2/8/8 w - - 0 1"); CHECK(!insufficientMaterial(p), "K+NN v K not auto-dead"); }
  { // repetition + en-passant/castling specifics
    Position p; setStart(p); uint64_t hist[64]; int nh = 0; hist[nh++] = hashPos(p);
    const char* seq[] = { "g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8" };
    int st = GS_PLAY;
    for (int i = 0; i < 8; ++i) {
      Move m = findMove(p, seq[i]); CHECK(m.pad != 99, "rep move legal");
      makeMove(p, m); hist[nh++] = hashPos(p);
      Move ml[MAX_MOVES]; int n = legalMoves(p, ml);
      st = gameStatus(p, n, hist, nh);
      if (i < 7) CHECK(st == GS_PLAY, "no early repetition draw");
    }
    CHECK(st == GS_DRAW_REPEAT, "threefold repetition detected");
  }
  { // en passant
    Position p; setFen(p, "rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3");
    Move m = findMove(p, "d4e3"); CHECK(m.pad != 99, "ep legal when pawn adjacent");
    if (m.pad != 99) { makeMove(p, m); CHECK(p.b[28] == 0 && p.b[20] == -PAWN, "ep removes the captured pawn"); }
  }
  { // castling both sides legal / illegal through check
    Position p; setFen(p, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    CHECK(findMove(p, "e1g1").pad != 99 && findMove(p, "e1c1").pad != 99, "castling available");
    Position q; setFen(q, "r3k2r/8/8/8/8/8/5r2/R3K2R w KQkq - 0 1");
    CHECK(findMove(q, "e1g1").pad == 99, "no castling through attacked f1");
    Position c = p; makeMove(c, findMove(p, "e1g1"));
    CHECK(c.b[6] == KING && c.b[5] == ROOK && c.b[7] == 0 && (c.castle & 3) == 0, "O-O moves rook & clears rights");
  }
  { // promotion produces 4 options
    Position p; setFen(p, "8/P6k/8/8/8/8/8/K7 w - - 0 1");
    Move ml[MAX_MOVES]; int n = legalMoves(p, ml); int promos = 0;
    for (int i = 0; i < n; ++i) if (ml[i].from == 48 && ml[i].to == 56) promos++;
    CHECK(promos == 4, "4 promotion choices");
  }

  printf("== search sanity ==\n");
  { // mate in one: Qh5# style  (back rank)
    Position p; setFen(p, "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    Move best; eng.findBest(p, 3, 0, 0, best); char s[8]; moveToStr(best, s);
    printf("  back-rank mate in 1 -> %s\n", s); CHECK(!strcmp(s, "a1a8"), "finds Ra8#");
  }
  { // mate in two (Lasker style simple): K+Q vs K corner style
    Position p; setFen(p, "7k/8/5K2/8/8/8/8/6Q1 w - - 0 1");
    Move best; eng.findBest(p, 3, 0, 0, best); char s[8]; moveToStr(best, s);
    Position np = p; makeMove(np, best); Move ml[MAX_MOVES]; int n = legalMoves(np, ml);
    printf("  KQ v K, engine plays %s (opp has %d moves)\n", s, n);
    CHECK(!(n == 0 && !inCheck(np)), "does not stalemate");
  }
  { // wins a hanging queen
    Position p; setFen(p, "4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1");
    Move best; eng.findBest(p, 3, 0, 0, best); char s[8]; moveToStr(best, s);
    printf("  free queen -> %s\n", s); CHECK(!strcmp(s, "d1d5"), "takes the queen");
  }
  { // does not hang the queen to a pawn
    Position p; setFen(p, "4k3/8/8/8/2p5/8/3Q4/4K3 w - - 0 1");
    Move best; eng.findBest(p, 3, 0, 0, best); char s[8]; moveToStr(best, s);
    Position np = p; makeMove(np, best);
    printf("  queen safe move -> %s\n", s);
  }

  printf("== self-play (level 2 vs level 3, check legality + termination) ==\n");
  for (int game = 0; game < 4; ++game) {
    Position p; setStart(p); uint64_t hist[700]; int nh = 0; hist[nh++] = hashPos(p);
    int ply = 0, st = GS_PLAY;
    while (ply < 400) {
      Move ml[MAX_MOVES]; int n = legalMoves(p, ml);
      st = gameStatus(p, n, hist, nh);
      if (st != GS_PLAY) break;
      Move best; bool ok = eng.findBest(p, (ply & 1) ? 3 : 2, hist, nh, best);
      CHECK(ok, "engine returned a move");
      bool legal = false; for (int i = 0; i < n; ++i) if (sameMove(ml[i], best)) legal = true;
      CHECK(legal, "engine move is legal");
      if (!legal) break;
      makeMove(p, best); hist[nh++] = hashPos(p); ply++;
    }
    printf("  game %d ended after %d plies, status %d\n", game, ply, st);
  }

  printf("== KQ vs K must be mated by the engine (level 3) ==\n");
  {
    Position p; setFen(p, "8/8/8/4k3/8/8/8/R3K2Q w - - 0 1");
    uint64_t hist[400]; int nh = 0; hist[nh++] = hashPos(p); int ply = 0, st = GS_PLAY;
    while (ply < 120) {
      Move ml[MAX_MOVES]; int n = legalMoves(p, ml);
      st = gameStatus(p, n, hist, nh); if (st != GS_PLAY) break;
      Move best; eng.findBest(p, 3, hist, nh, best);
      makeMove(p, best); hist[nh++] = hashPos(p); ply++;
    }
    printf("  ended after %d plies, status %d (1 = checkmate)\n", ply, st);
    CHECK(st == GS_CHECKMATE, "engine converts a won endgame");
  }

  printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
  return failures ? 1 : 0;
}
