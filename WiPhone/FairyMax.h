/***************************************************************************/
/*                               Fairy-Max,                                */
/* Version of the sub-2KB (source) micro-Max Chess program, fused to a     */
/* generic WinBoard interface, loading its move-generator tables from file */
/***************************************************************************/

/*****************************************************************/
/*                      LICENCE NOTIFICATION                     */
/* Fairy-Max 5.0 is free software, released in the public domain */
/* so that you have my permission do with it whatever you want,  */
/* whether it is commercial or not, at your own risk. Those that */
/* are not comfortable with this, can also use or redistribute   */
/* it under the GNU Public License or the MIT License.           */
/* Note, however, that Fairy-Max can easily be configured through*/
/* its fmax.ini file to play Chess variants that are legally pro-*/
/* tected by patents, and that to do so would also require per-  */
/* mission of the holders of such patents. No guarantees are     */
/* given that Fairy-Max does anything in particular, or that it  */
/* would not wreck the hardware it runs on, and running it is    */
/* entirely for your own risk.  H.G.Muller, author of Fairy-Max  */
/*****************************************************************/

/*  MIT License
 *
 *  Copyright (c) 2016, H.G.Muller, author of Fairy-Max.
 *            (c) 2019, Anriy Makukha, deobfuscated, ported and ammended in
 *                      Arduino IDE for the ESP32 WiPhone project.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 *  NOTE: This permission is only applicable to the Fairy-Max chess engine file,
 *        and NOT to any other WiPhone firmware files.
 */

/* Fairy-Max 5.0b-1 original source code:
 *   https://codesearch.isocpp.org/actcd19/main/f/fairymax/fairymax_5.0b-1/fairymax.c
 */

/* Port notes:
 *  - "_CLOCKS_PER_SEC_" instead of "sysconf(_SC_CLK_TCK)"
 * Removed code:
 *  - Windows and Shatranj-specific macros removed for simplicity
 * Commented out:
 *  - LoadHash, hashfile -   persistent hash functionality
 *  - "edit" command, setupPosition
 *  - "undo" "remove" commands
 *  - inifile - replaced with PROGMEM string
 *  - Don't expect pieceToChar from the engine
 *  - PrintOptions
 *  - fflush(stdout);
 *  - exit(0)
 */

// TODO:
// - debug PrintVariants (outputs no variants)

// Bugs:
// - fails to find mate in one ("sd 4", "st 5")
// - search goes more than twenty levels deep with "sd 4", "st 5"

#ifndef _FAIRYMAX_H_
#define _FAIRYMAX_H_

#define VERSION "5.0b"

#include "Arduino.h"
#include "machine/time.h"
#include <sys/time.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "config.h"

#ifdef BUILD_GAMES
namespace FairyMax {

#define FAIRY_HASH_TABLE_SIZE 12
#define MAX_BOARD_WIDTH 8             // each cell is 30*30px, app space is 240*250px, so we can fit only 8*8 games (though we could fit 10*8 horizontally in full-screen)
#define MAX_BOARD_HEIGH 8

#define FAIRYMAX_MULTIPATH
/* make unique integer from engine move representation */
#define PACK_MOVE 256*K + L + (PromPiece << 16) + (GT<<24);
/* convert intger argument back to engine move representation */
#define UNPACK_MOVE(A) K = (A)>>8 & 255; L = (A) & 255; PromPiece = (A)>>16 & 255; GT = (A)>>24 & 255;
#define FAIRYMAX_CLEAR(X) for(int i=0; i<S*S; i++) map[i] = X

// Current search stack
#define SSS SS[stack]

// Access to packed random-number table Zobrist[piece][square] for hash key
#define K(square, piece) *(int*)(Zobrist + square + S*(piece&31))               // TODO: looks like KeyLo and KeyHi use overlapping hashes, which probably reduces the quality
// Differential update of hash key
#define J(offset) K(ToSqr + offset, Board[ToSqr]) - K(FromSqr + offset, CurPiece) - K(CaptSqr + offset, Victim)
#define SETKEY(Key, offset) for(int i = Key = 0; i<=BE; i++) Key += K(i + offset, Board[i])

class XboardChessEngine {
public:
//  virtual int exchange(const char* input);
  String output;

//  virtual const char* version();
//  virtual const char* name();
//  virtual const char* author();

  enum : int { Quit = 0, EmptyInput = 1, Continue = 2 };
};

class FairyMax : public XboardChessEngine {
protected:
  // Constants
  static const int ANALYZE = -2;
  static const int EMPTY = -1;
  static const int WHITE = 0;
  static const int BLACK = 16;

  static const int INF = 8000;    // Constant 8000 = 'infinity' score; short name `I`
  static const int M = 0x88;      // Constant 0x88 = unused bits in valid square numbers
  static const int S = 0x100;     // Constant 0x100 = 256-bit; a.k.a. "dummy square" (originally 128-bit, "S"ign bit of char)

  static const int MAX_PLY = 98;      // "to prevent problems with stack overflow in a tree that does not branch" (see. "Iterative deepening" @ Micro-Max website)
  static const int INF_PLY = 99;      // indicates end of the game, like checkmate (see. "Iterative deepening" @ Micro-Max website)
  static const int MAX_STACK = 100;

  static const int HISTORY = 1024;
  static const int STATE = 256;
  static const int REGION = 3*STATE + 1;
  static const int CENTER = 2*STATE + 1;
  static const int HILL = 1;
  static const int CORNER = 2;

  static const int FAC = 128;
  static const int EG = 10;
  static constexpr const char* NAME = "Fairy-Max";

  // Chess variants definitions (NOTE: Chess960 uses same definition as "normal" chess)
  const char* FMAX_INI PROGMEM = "version 4.8(w)\n"
                                 // FIDE Chess (a.k.a. Mad Queen variant)
                                 "Game: normal\n"
                                 "8x8\n"
                                 "8 5 6 9 3 6 5 8\n"
                                 "8 5 6 9 4 6 5 8\n"
                                 "p:74 -16,24 -16,6 -15,5 -17,5\n"
                                 "p:74  16,24 16,6 15,5 17,5\n"
                                 "k:-1  1,34 -1,34 1,7 16,7 15,7 17,7 -1,7 -16,7 -15,7 -17,7\n"
                                 "k:-1  1,34 -1,34 1,7 16,7 15,7 17,7 -1,7 -16,7 -15,7 -17,7\n"
                                 "n:259 14,7 31,7 33,7 18,7 -14,7 -31,7 -33,7 -18,7\n"
                                 "b:296 15,3 17,3 -15,3 -17,3\n"
                                 "Q:851 1,3 16,3 15,3 17,3 -1,3 -16,3 -15,3 -17,3\n"
                                 "R:444 1,3 16,3 -1,3 -16,3\n"
                                 "S:851 1,3 16,3 15,3 17,3 -1,3 -16,3 -15,3 -17,3\n"

                                 // King of the Hill (King MUST be #3 and have value -2 to trigger hill eval)
                                 // Apart from mate, reaching one of the 4 central squares with K also wins
                                 "Game: king-of-the-hill # PNBRQKpnbrqk # fairy\n"
                                 "8x8\n"
                                 "6 4 5 7 3 5 4 6\n"
                                 "6 4 5 7 3 5 4 6\n"
                                 "p:66 -16,24 -16,6 -15,5 -17,5\n"
                                 "p:66  16,24 16,6 15,5 17,5\n"
                                 "k:-2  1,34 -1,34 1,7 16,7 15,7 17,7 -1,7 -16,7 -15,7 -17,7\n"
                                 "n:259 14,7 31,7 33,7 18,7 -14,7 -31,7 -33,7 -18,7\n"
                                 "b:296 15,3 17,3 -15,3 -17,3\n"
                                 "R:444 1,3 16,3 -1,3 -16,3\n"
                                 "Q:851 1,3 16,3 15,3 17,3 -1,3 -16,3 -15,3 -17,3\n";

  static const int RBITS = 0b1100;

protected:

  void(*postCallback)(const char*);

public:
  FairyMax(void(*postCallback)(const char*), int m = FAIRY_HASH_TABLE_SIZE) {

    this->postCallback = postCallback;    // this is used for producing output while the engine is still thinking
    this->output = "";                    // this is used for returning detailed output after it's done

    // C89-style zeroing of global variables
    Side = 0;
    Move = 0;
    PromPiece = 0;
    Result = 0;
    TimeLeft = 0;
    MovesLeft = 0;
    Post = 0;
    Fifty = 0;
    GameNr = 0;
    Randomize = 0;
    Resign = 0;
    Score = 0;
    zone = pRank = popup = 0;
    Computer = MaxTime = MaxMoves = TimeInc = 0;
    prom = pm = gating = succession = hill = 0;
    memset(piecename, 0, sizeof(piecename));
    memset(piecetype, 0, sizeof(piecetype));
    memset(blacktype, 0, sizeof(blacktype));
    memset(selectedFairy, 0, sizeof(selectedFairy));
    memset(info, 0, sizeof(info));

    Ticks = tlim = Setup = SetupQ = 0;
    GamePtr = HistPtr = 0;

    Q = O = K = N = j = R = HashKeyLo = HashKeyHi = LL = GT = BW = BH = BE = sh = RR = ab = CONS = L = ep = stale = wk = bk = bareK = bareL = score = R2 = 0;
    memset(pt, 0, sizeof(pt));
    memset(StepVecs, 0, sizeof(StepVecs));
    memset(BackRank, 0, sizeof(BackRank));
    memset(MoveModes, 0, sizeof(MoveModes));
    memset(PieceVecs, 0, sizeof(PieceVecs));
    memset(PieceCount, 0, sizeof(PieceCount));
    memset(centr, 0, sizeof(centr));

    margin = 0;

    // Debug
    nodes = 0;
    totalNodes = 0;
    stack = 0;

    // Other initializations
    MaxDepth = 30;
    Threshold = 800;
    drawMoves = 50;

    // Allocate hash table
    U = (1<<m)-1;
    HashTab = (struct HashTable *) this->calloc(U+1, sizeof(struct HashTable));

    // Allocate the search stack
    SS = (struct SearchStack*) this->calloc(MAX_STACK, sizeof(struct SearchStack));

    // Allocate other arrays
    map = (int*) this->calloc(1<<16, sizeof(int));
    Board = (signed char*) this->calloc(4*STATE+1, sizeof(signed char));
    Zobrist = (signed char*) this->calloc((4*STATE+1)*8, sizeof(signed char));
    PrincVar = (int*) this->calloc(10000, sizeof(int));
    sp = PrincVar;

    // Allocate GameHistory & HistoryBoards
    GameHistory = (int*) this->calloc(HISTORY, sizeof(int));
    HistoryBoards = (char**) this->calloc(HISTORY, sizeof(char*));
    for (int i=0; i<HISTORY; i++) {
      HistoryBoards[i] = (char*) this->calloc(STATE, sizeof(char));
    }

    // setup from the main()

    InitEngine();
    LoadGame("normal");
    InitGame();

    Computer = EMPTY;
    MaxTime  = 10000;   /* 10 sec */
  }

  /* Description:
   *     This function is meant to handle large arrays allocation.
   *     On ESP32 the internal RAM is only 520 Kb, so we allocate large arrays like HashTab and HistoryBoards in external RAM.
   */
  void* calloc(size_t count, size_t size) {
    void* p = extCalloc(count, size);
    if (!p) {
      log_e("FAILED TO ALLOCATE %d BYTES", count*size);
    }
    return p;
  }

  ~FairyMax() {
    free(HashTab);
    free(map);
    free(Board);
    free(Zobrist);
    free(PrincVar);
    free(GameHistory);
    for (int i=0; i<HISTORY; i++) {
      free(HistoryBoards[i]);
    }
    free(HistoryBoards);
  }

  const char* version() {
    return VERSION;
  };
  const char* name() {
    return NAME;
  };
  const char* author() {
    return "H.G. Muller";
  };

  int exchange(const char* line) {

    // This function if for the infinite loop from the main()

    PromPiece = 0; /* Always promote to Queen ourselves */
    for(N=K=0; K<S; K++) {
      N += Board[K] ? ((Board[K]&16) ? S : 1) : 0;  /* count pieces for detecting bare King */
    }
    if(PieceVal[wk]<0&PieceVal[bk]<0) {
      if(N<2*S) {
        bareK=bk;
      }
      if(!(N&S-2)) {
        bareK=wk;
      }
    }
    R = R2 - 2*abs(Q)/(3*FAC);
    if(R < 0) {
      R=0;  /* treat strongly unbalanced as if later game phase */
    }
    if(bareK) {
      centr[bareK]=1+Fifty/10,R=4;
    }
    SETKEY(HashKeyLo,0);
    SETKEY(HashKeyHi,4); /* absolutize key, so it can be used for persistent hash */
    K=(bareL&15);
    L=(bareL>>4);
    if( Board[CENTER+bareL] && (((!K)|(K==(BW-1))) && ((!L)|(L==(BH-1)))) )
      for (int i=0; i<BH; i++)
        for(int j=0; j<BW; j++) {
          Board[CENTER+16*i+j] = abs(abs(i-L)-abs(j-K));
        }
    if(hill) {
      centr[3] = R>20 ? 1 : 22-R;
    }
    Ticks = GetTickCount();
    if (Side == Computer) {
      /* think up & do move, measure time used  */
      /* it is the responsibility of the engine */
      /* to control its search time based on    */
      /* MovesLeft, TimeLeft, MaxMoves, TimeInc */
      /* Next 'MovesLeft' moves have to be done */
      /* within TimeLeft+(MovesLeft-1)*TimeInc  */
      /* If MovesLeft<0 all remaining moves of  */
      /* the game have to be done in this time. */
      /* If MaxMoves=1 any leftover time is lost*/
      double cpuT = CPUtime();
      printf("# times @ %u\n", Ticks);
      {
        int moves = MovesLeft<=0 ? 40 : MovesLeft;
        tlim = (0.6-0.06*(BW-8))*(TimeLeft+(moves-1)*TimeInc)/(moves+7);
      }
      if(tlim>TimeLeft/15) {
        tlim = TimeLeft/15;
      }
      printf("# %d+%d pieces, centr = (%d,%d) R=%d\n", N&63, N>>8, centr[wk], centr[bk], R);
      if(bareK|RR>4^R>4) // with bare King or after switching on or off null move
        for (int i=0; i<=U; i++)
          if(HashTab[i].D<INF_PLY && abs(HashTab[i].V)<INF-S) {
            HashTab[i].K=0;  // clear hash
          }
      N=ab=0;
      K=INF;
      RR=R;
      if (Search(Side,-INF,INF,Q,O,LL|9*S,3)==INF) {
        Side ^= BLACK^WHITE;
        int TicksCount = GetTickCount();
        printf("# times @ %u: real=%d cpu=%1.0f\n", TicksCount, TicksCount - Ticks,
               (CPUtime() - cpuT)/CLOCKS_PER_SEC);
        printf("# promo = %d (%c) GT = %d\n", prom, piecename[prom]+'`', GT);
        printf("# nodes = %d, total = %d\n", nodes, totalNodes);
        printf("move ");
        printf("%c%d%c%d",'a'+(K&15),BH-(K>>4)-(BH==10),
               'a'+(L&15),BH-(L>>4)-(BH==10));
        if(prom) {
          printf("%c",piecename[prom]+'a'-1);
        }
        printf("\n");

        /* time-control accounting */
        TimeLeft -= TicksCount;
        TimeLeft += TimeInc;
        if(--MovesLeft == 0) {
          MovesLeft = MaxMoves;
          if(MaxMoves == 1) {
            TimeLeft  = MaxTime;
          } else {
            TimeLeft += MaxTime;
          }
        }
        nodes = 0;

        GameHistory[GamePtr++] = PACK_MOVE;
        CopyBoard(HistPtr=HistPtr+1&1023);
        if (Resign && Score <= -Threshold) {
          printf("resign\n");
          Computer=EMPTY;
        } else if(PrintResult(Side, Computer)) {
          Computer = EMPTY;
        }
      } else {
        if(!PrintResult(Side, Computer)) {
          printf("resign { refuses own move }\n");
        }
        Computer = EMPTY;
      }
      return XboardChessEngine::Continue;
    }
    if(Computer == ANALYZE) {
      if(popup-- == 1) {
        popup++, printf("askuser remember Save score in hash file (OK/Cancel)?\n");
      }
      N=ab=0;
      K=INF;
      tlim=1e9;
      Search(Side,-INF,INF,Q,O,LL|S,3);
    }

    // Analyze input string
    if (line[0] == '\0') {
      return XboardChessEngine::EmptyInput;
    }
    if (line[0] == '\n') {
      return XboardChessEngine::Continue;
    }

    int nr;
    int len = strspn(line, " \t\n");
    len += strcspn(line, " \t\n");
    char command[len+1];
    sscanf(line, "%s", command);
    if (!strcmp(command, "xboard")) {
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "protover")) {
      printf("feature myname=\"%s %s\"\n", NAME, VERSION);
      printf("feature memory=1 exclude=1\n");
      printf("feature setboard=0 xedit=1 ping=1 done=0\n");
      printf("feature variants=\"");
      PrintVariants(0);
      printf("\"\n");
      //PrintOptions();
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "ping")) {
      int num=0;
      sscanf(line, "ping %d", &num);
      printf("pong %d\n", num);
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "memory")) {
      int mem, mask;
      sscanf(line+6, "%d", &mem);
      mem = (mem*1024*1024)/12; // max nr of hash entries
      mask = 0x3FFFFFF;
      while(mask > mem) {
        mask >>= 1;
      }
      if(mask != U) {
        free(HashTab);
        U = mask;
        HashTab = (struct HashTable *) this->calloc(U+1, sizeof(struct HashTable));
      }
      return XboardChessEngine::Continue;
    } else if (!strcmp(command+2, "clude")) { // include / exclude
      const char *c=line+8, K=c[0]-16*c[1]+CONS, L=c[2]-16*c[3]+CONS, r = *command - 'i';
      if(!strcmp(line+8, "all\n")) {
        FAIRYMAX_CLEAR(r);
      } else {
        map[K+S*L] = r;
      }
      return XboardChessEngine::Continue;
    }
    FAIRYMAX_CLEAR(0);
    if (!strcmp(command, "new")) {
      /* start new game */
      LoadGame("normal");
      InitGame();
      GamePtr   = Setup = 0;
      GameNr++;
      HistPtr   = 0;
      Computer  = BLACK;
      TimeLeft  = MaxTime;
      MovesLeft = MaxMoves;
      Randomize = 0;
      for(int i=0; i<HISTORY; i++)
        for(int j=0; j<STATE; j++) {
          HistoryBoards[i][j] = 0;
        }
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "quit"))
      /* exit engine */
    {
      return XboardChessEngine::Quit;
    } else if (!strcmp(command, "analyze")) {
      /* computer plays neither */
      Computer = ANALYZE;
      Randomize *= 2;
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "exit") ||
               !strcmp(command, "force")) {
      /* computer plays neither */
      Computer = EMPTY;
      Randomize = (Randomize > 0);
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "white")) {
      /* set white to move in current position */
      if(Side == BLACK) {
        Q = -Q;
      }
      Side     = WHITE;
      Computer = BLACK;
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "black")) {
      /* set blck to move in current position */
      if(Side == WHITE) {
        Q = -Q;
      }
      Side     = BLACK;
      Computer = WHITE;
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "st")) {
      /* move-on-the-bell mode     */
      /* indicated by MaxMoves = 1 */
      sscanf(line, "st %d", &MaxTime);
      MovesLeft = MaxMoves = 1;
      TimeLeft  = MaxTime *= 1000;
      log_d("MaxTime = %d", MaxTime);
      TimeInc   = 0;
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "sd")) {
      /* set depth limit (remains in force */
      /* until next 'sd n' command)        */
      sscanf(line, "sd %d", &MaxDepth);
      MaxDepth += 2; /* QS depth */
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "level")) {
      /* normal or blitz time control */
      int sec = 0;
      if(sscanf(line, "level %d %d %d",
                &MaxMoves, &MaxTime, &TimeInc)!=3 &&
          sscanf(line, "level %d %d:%d %d",
                 &MaxMoves, &MaxTime, &sec, &TimeInc)!=4) {
        return XboardChessEngine::Continue;
      }
      MovesLeft = MaxMoves;
      TimeLeft  = MaxTime = 60000*MaxTime + 1000*sec;
      TimeInc  *= 1000;
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "time")) {
      /* set time left on clock */
      sscanf(line, "time %d", &TimeLeft);
      TimeLeft  *= 10; /* centi-sec to ms */
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "otim")) {
      /* opponent's time (not kept, so ignore) */
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "easy")) {
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "hard")) {
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "accepted")) {
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "rejected")) {
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "random")) {
      Randomize = !Randomize;
      return XboardChessEngine::Continue;
    }
    /*    // Persistent hash
        if (!strcmp(command, "remember")) {
          FILE *f = fopen(hashfile, "a"); // add current position to persistent hash
          sscanf(line+8, "%d", &score);   // user can overrule score
          if(f) fprintf(f, "%08x:%08x=%d\n", HashKeyLo+(S+Side)*O, HashKeyHi, score), fclose(f);
          popup = 2; // suppresses repeat of popup on restart of analysis search
          return XboardChessEngine::Continue;
        }*/
    else if (!strcmp(command, "option")) {
      int i;
      static char filename[80];
      if(sscanf(line+7, "Resign=%d", &Resign) == 1) {
        return XboardChessEngine::Continue;
      }
      if(sscanf(line+7, "Resign Threshold=%d", &Threshold) == 1) {
        return XboardChessEngine::Continue;
      }
      /*
            if(sscanf(line+7, "Ini File=%s", filename) == 1) {
              inifile = filename; return XboardChessEngine::Continue;
            }
      */
      char c;
      if(sscanf(line+7, "Clear Hash%c", &c) == 1) for (int i=0; i<=U; i++) {
          HashTab->K = 0;
        }
      if(sscanf(line+7, "Info%c", &c) == 1) {
        printf("telluser %s\n", info+3);
      }
      if(sscanf(line+7, "MultiVariation Margin=%d", &margin) == 1) {
        return XboardChessEngine::Continue;
      }
      if(sscanf(line+7, "Variant fairy selects=%s", selectedFairy+6) == 1) {
        return XboardChessEngine::Continue;
      }
      if(sscanf(line+7, "Makruk rules=%s", Cambodian) == 1) {
        return XboardChessEngine::Continue;
      }
      if(sscanf(line+7, "Claim draw after=%d", &drawMoves) == 1) {
        return XboardChessEngine::Continue;
      }
      if(sscanf(line+7, "Automatic persistent-hash dialog=%d", &popup) == 1) {
        return XboardChessEngine::Continue;
      }
      /*      if(sscanf(line+7, "Save in hash file%c", &c) == 1 && Computer == ANALYZE) {
                 FILE *f = fopen(hashfile, "a"); // add current position to persistent hash
                 if(f) fprintf(f, "%08x:%08x=%d\n", HashKeyLo+(S+Side)*O, HashKeyHi, score), fclose(f);
            } */
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "go")) {
      /* set computer to play current side to move */
      Computer = Side;
      MovesLeft = -(GamePtr+(Side==WHITE)>>1);
      while(MaxMoves>0 && MovesLeft<=0) {
        MovesLeft += MaxMoves;
      }
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "hint")) {
      Ticks = GetTickCount();
      tlim = 1000;
      ab = 0;
      Search(Side,-INF,INF,Q,O,LL|4*S,6);
      if (K==0 && L==0) {
        return XboardChessEngine::Continue;
      }
      printf("Hint: ");
      printf("%c%d%c%d",'a'+(K&15),BH-(K>>4)-(BH==10),
             'a'+(L&15),BH-(L>>4)-(BH==10));
      printf("\n");
      return XboardChessEngine::Continue;
    }
    /*    if (!strcmp(command, "undo")   && (nr=1) ||
            !strcmp(command, "remove") && (nr=2)   )
        {
          // 'take back' moves by replaying game from history until desired ply
          if (GamePtr - nr < 0)
            return XboardChessEngine::Continue;
          GamePtr -= nr;
          HistPtr -= nr;   // erase history boards
          while(nr-- > 0)
              for(int j=0; j<STATE; j++)
                  HistoryBoards[HistPtr+nr+1&1023][j] = 0;
          InitGame();
          if(Setup) {
              for (int i=0; i<S; i++) Board[i] = setupPosition[i];
              for (int i=0; i<32; i++) PieceCount[i] = setupPosition[i+S+2];
              Side = setupPosition[S]; Q = SetupQ;
              R2 = setupPosition[S+1];
          }
          for (int i=0; i<=U; i++) if(HashTab[i].D == INF_PLY) HashTab[i].D = HashTab[i].K = 0; // clear game history from hash table
                            for(nr=0; nr<GamePtr; nr++) {
                                UNPACK_MOVE(GameHistory[nr]); SETKEY(HashKeyLo,0); SETKEY(HashKeyHi,4);
                                ab=0;Search(Side,-INF,INF,Q,O,LL|9*S,3);
                                Side ^= BLACK^WHITE;
                            }
          return XboardChessEngine::Continue;
        } */
    else if (!strcmp(command, "post")) {
      Post = 1;
      log_d("Post = 1");
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "nopost")) {
      Post = 0;
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "variant")) {
      int len = strspn(line + 7, " \t\n");
      len += strcspn(line + 7 + len, " \t\n");
      char variantName[len + 1];
      sscanf(line, "variant %s", variantName);
      LoadGame(variantName);
      InitGame();
      Setup = 0;
      return XboardChessEngine::Continue;
    } else if (!strcmp(command, "board")) {
      // Return visual board state (without piece history)
      // Not included into XBoard protocol, but this allows "stupid" GUI to rely on the chess engine to keep the state of the board and process the rules
      // The pieces order is: a8..h8...a1..h1
      char res[BH*BW+1];
      int cnt = 0;
      for (char rank='8'; rank>='1'; rank--)
        for (char file='a'; file<='h'; file++) {
          res[cnt++] = cell(file, rank);
        }
      res[BH*BW] = '\0';
      log_d("board %s\n", res);
      return XboardChessEngine::Continue;
    }

    /*    if (!strcmp(command, "edit")) {
          int color = WHITE, p, r;

                    while(fgets(line, 256, stdin)) {
                            int j = line[0];
                            if(j=='.') break;
                            if(j=='#') {
                                    for (int i=0; i<S; i++) Board[i]=0;
                                    for (int i=0; i<32; i++) PieceCount[i]=0;
                                    Q=0; R=0; O=S;
                                    PieceCount[WHITE]=PieceCount[BLACK]=0;
                                    return XboardChessEngine::Continue;
                            }
                            if(j=='c') {
                                    color = WHITE+BLACK - color;
                                    Q = -Q;
                                    return XboardChessEngine::Continue;
                            }
                            if( j >= 'A' && j <= 'Z' && piecetype[j&31]) {
                              p = (color == WHITE ? piecetype : blacktype)[line[0]&31];
                              if(line[1] == '@') { // stuff holdings
                                  PieceCount[color+p+5] = j = line[2] - '0';
                                  PieceCount[BLACK+WHITE-color]+=j;PieceCount[p+color]+=j;
                                  Q += j*PieceVal[p]; R += j*(PieceVal[p]/FAC);
                                  return XboardChessEngine::Continue;
                              } else if (line[1] >= 'a' && line[1] <= 'a'+BW-1
                                      && line[2] >= '0' && line[2] <= '0'+BH) {
                                  line[2] = '0' + atoi(line + 2) + (BH==10); // allow 2-digit rank
                                  j = line[1]-16*line[2]+CONS; r = j & 0xF0;
                                  switch(p)
                                  {
                                  case 1:
                                  case 2:
                                      if(color==WHITE)
                                           Board[j]=r==0x10?161:r==0x20?97:r==16*(BH-2)?1:33,
                                           Q+=PieceVal[1]+(r==0x10?128:r==0x20?64:0);
                                      else Board[j]=r==16*(BH-2)?178:r==16*(BH-3)?114:r==0x10?18:50,
                                           Q+=PieceVal[2]+(r==16*(BH-2)?128:r==16*(BH-3)?64:0);
                                      break;
                                  default:
                                      Board[j]=p+color+32; // assume non-virgin
                                      if(color==BLACK && j<0x10 && p==BackRank[j+16] || // but make virgin on original square
                                         color==WHITE && j>=16*(BH-1) && p==BackRank[j-16*(BH-1)]) Board[j] -= 32;
                                      if(PieceVal[p]<0) { // Royal piece on original square: virgin
                                          Q-=PieceVal[p]; // assume value was flipped to indicate royalty
                                          if(PieceCount[p+color])R-=PieceVal[p]/FAC; // capturable King, add to material
                                      } else { Q+=PieceVal[p]; R+=PieceVal[p]/FAC; }
                                  case 0: // undefined piece, ignore
                                    break;
                                  }
                                  PieceCount[BLACK+WHITE-color]++;PieceCount[p+color]++;
                                  if(PieceVal[p+color] == -1)PieceCount[p+color]=1; // fake we have one if value = -1, to thwart extinction condition
                                  return XboardChessEngine::Continue;
                                }
                            }
                    }
            if(Side != color) Q = -Q;
            GamePtr = HistPtr = 0; Setup = 1; SetupQ = Q; // start anew
            for (int i=0; i<S; i++) setupPosition[i] = Board[i]; // remember position
            setupPosition[S] = Side;
            setupPosition[S+1] = RR = R2 = R;
            for (int i=0; i<32; i++) setupPosition[i+S+2] = PieceCount[i];
            Computer = EMPTY; // after edit: force mode!
            return XboardChessEngine::Continue;
          } */

    /* command not recognized, assume input move */
    char c, ff, ft;
    int rf, rt;
    GT = 0;
    int scanned = sscanf(line, "%c%d%c%d%c", &ff, &rf, &ft, &rt, &c);     // NOTE: it will return 5 even if the move is e2e4, because the expected input string will be "e2e4\n"
    if(BH==10) {
      rf++,rt++;
    }
    if(c != '\n') {
      GT = (Side == WHITE ? piecetype : blacktype)[c&31];
    }
    K=ff-16*(rf+'0')+CONS;    // a8 = 0,  ... h8 = 7,  ... a1 = 112, ... h1 = 119
    L=ft-16*(rt+'0')+CONS;
    if(GT) {
      PromPiece = (pt[L]&15) + 1 + (Side == BLACK) - GT, GT |= 32 + Side;
    }
    if(PieceVal[GT&15] == -1 || PieceVal[GT&15]%10 == 3) {
      L = S;  // spoil move for promotion to King (or when marked non-promoting)
    }
    if(pRank == 3 && PromPiece) {
      L = S;  // no promotion choice, spoil move if not default piece
    }
    if ((scanned<5) && (line[1] != '@'))                            // special case for bughouse: moves like "P@h3" are allowed ("dropping" a piece)
      /* doesn't have move syntax */
    {
      printf("Error (unknown command): %s\n", command);
    } else {
      int i=-1;
      if(Board[L] && (Board[L]&16) == Side && PieceVal[Board[L]&15] < 0) {
        // capture own King: castling
        i=K;
        K = L;
        L = i>L ? i-1 : i+2;
      }
      if(PieceVal[GT&15] < -1) {
        PieceCount[GT&31]++, HashKeyLo+=89729;  // promotion to royal piece
      }
      if((Board[K]&15) < 3) {
        GT = 0;  // Pawn => true promotion rather than gating
      }
      ab=0;
      if(Search(Side,-INF,INF,Q,O,LL|9*S,3)!=INF) {
        /* did have move syntax, but illegal move */
        printf("Illegal move:%s\n", line);
      } else {  /* legal move, perform it */
        if(i >= 0) {
          Board[i]=Board[K],Board[K]=0;  // reverse Seirawan gating
        }
        GameHistory[GamePtr++] = PACK_MOVE;
        Side ^= BLACK^WHITE;
        CopyBoard(HistPtr=HistPtr+1&1023);
        if(PrintResult(Side, Computer) && Computer != ANALYZE) {
          Computer = EMPTY;
        }
      }
    }

    return XboardChessEngine::Continue;
  }

protected:

  int GetTickCount() {          // with thanks to Tord
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec*1000 + t.tv_usec/1000;
  }

  double CPUtime() {
    // get CPU time used by process, converted to 'MILLICLOCKS'
    struct tms cpuTimes;
    static int cps = 0;
    if(!cps) {
      cps = _CLOCKS_PER_SEC_;
    }
    times(&cpuTimes);
    return ((double)(cpuTimes.tms_utime + cpuTimes.tms_stime) * CLOCKS_PER_SEC * 1000)/cps;
  }

  int Input() {
    int cnt;
    if(ioctl(0, FIONREAD, &cnt)) {
      return 1;
    }
    return cnt;
  }


  /* Global variables visible to engine. Normally they */
  /* would be replaced by the names under which these  */
  /* are known to your engine, so that they can be     */
  /* manipulated directly by the interface.            */

  int Side;
  int Move;
  int PromPiece;
  int Result;
  int TimeLeft;
  int MovesLeft;
  int MaxDepth = 30;            /* maximum depth of your search */
  int Post;
  int Fifty;
  int GameNr;
  int Randomize;
  int Resign;
  char Cambodian[80] = "makruk";
  int Threshold = 800;
  int drawMoves = 50;
  int Score;
  int zone, pRank, popup;
  int prom, pm, gating, succession, hill;
  bool chess960 = false;
  char piecename[32];     // int -> printable char & 31 (so piece name 'a' will be stored as 1)
  char piecetype[32];     // printable char & 31 -> int (reverse of piecename)
  char blacktype[32];     // similar to piecetype but for black assymetrical army
  char selectedFairy[80];
  // char *inifile = INI_FILE;
  char info[999];                           // TODO:  reduce and/or initialize in PSRAM
  /*  char hashfile[256];  */

  int Ticks, tlim, Setup, SetupQ;
  int Computer, MaxTime, MaxMoves, TimeInc;

  int* GameHistory;
  char** HistoryBoards;
  // char setupPosition[290];
  int GamePtr, HistPtr;
  int* map;

  int U;
  struct HashTable {
    int K,V;
    unsigned char X,Y,D,F;
  } *HashTab;    // short name `A`: Hash table, 16M+8 entries (originally)

  // This sctucture is intoduced in Fairy-Max++ to reduce memory usage by the recursion stack on ESP32 (without it the Search crashes at around 23-26 recursive calls)
  struct SearchStack {

    // Long names (v.3.2) and explanations (v.3.2 and 4.0) are taken from Micro-Max documentation:
    //  - http://home.hccnet.nl/h.g.muller/var.html
    //  - http://home.hccnet.nl/h.g.muller/maximax.txt

    // TODO: reduce number and size of these variables to cut stack usage
    int8_t IterDepth;  // short name `d`: Loop counter of iterative deepening, indicates depth
    int LastKeyLo;     // short name `f`: last value of HashKeyLo (short name `J`)
    int LastKeyHi;     // short name `g`: last value of HashKeyHi (short name `Z`)
    int j;             // short name `j`: Loop counter for loop over directions
    int BestScore;     // short name `m`: Value of best move so far
    int StepVec;       // short name `r`: Step vector of current ray in move generation
    int Score;         // short name `v`: Temporary, used to hold some evaluation terms
    int FromSqr;       // short name `x`: Origin square of move under consideration
    int ToSqr;         // short name `y`: Target square of move under consideration
    int StartSqr;      // short name `B`: Start square of board scan for move generation
    int SkipSqr;       // short name `F`: e.p. flag: square skipped over in double move of P or K
    int SkipSqrF;      // short name `FF` (not in version 4.0)
    int RookSqr;       // short name `G`: Corner square for castling, contains S = 0x80 on non-castling
    int CaptSqr;       // short name `H`: Capture square, where captured piece stands
    int BestFrom;      // short name `X`: Origin square of best move so far
    int BestTo;        // short name `Y`: Target square of best move so far, marked with S-bit as non-castling (see constant S)
    int MoveFlags;     // short name `flag`

    int h,i,P,V,C,s,y,*ps,kk,rk;

    signed char Gate;          // short name `gt`: In Seirawan chess: "Elephant or Hawk can be 'gated' into play, by placing them on a square evacuated by a back-rank piece that moved for the first time, as second part of the same turn" (Source: GNU)
    signed char PieceType;     // short name `p`: Type of piece doing the move under consideration
    signed char Victim;        // short name `t`: Piece to be captured by move under consideration
    signed char CurPiece;      // short name `u`, original long name `Piece`: Piece doing the move under consideration

    signed char rg,vf;

    void init(int hashKeyLo, int hashKeyHi, int* sp, int S) {
      // Store values in stack
      LastKeyLo = hashKeyLo;
      LastKeyHi = hashKeyHi;
      ps = sp;
      kk = S;
      // Zero the rest of the variables
      MoveFlags=Score=SkipSqr=SkipSqrF=RookSqr=StepVec=IterDepth=StartSqr=BestFrom=BestTo=FromSqr=ToSqr=j=0;
      h=i=P=V=C=s=rk=0;
      Gate=Victim=CurPiece=PieceType=0;
      rg=vf=0;
    }
  } *SS;

  int HashKeyLo, HashKeyHi;       // these are made global in micro-Max version 4.5: http://home.hccnet.nl/h.g.muller/keep.html
  int PieceVal[16] = {            // short name `w`: relative piece values, loaded from "fmax.ini"
    0,2,2,-1,7,8,12,23,7,5
  };
  int StepVecs[256];              // short name `o`: list of move vectors
  int MoveModes[256];             // short name `of`: binary flags for different movement modes
  int PieceVecs[16];              // short name `od`: index into this list - 1st dir. in StepVecs[] per piece
  int Q,O,K,N,j,R,LL=0,GT=0,
                  BW,BH,BE,sh,RR,ab,CONS,L,ep,stale,wk,bk,bareK,bareL,score,R2,
                  pt[2*STATE+1],                                 /* promotion bonus/upgrade  */         // TODO:  reduce and/or initialize in PSRAM
                  BackRank[32];                   // short name `oo`: initial piece setup on the back rank (behind pawns)

  signed char PieceCount[32];     // short name `pl`
  signed char* Board;             // short name `b`: Chess board as 0x88 array, invalid squares contain piece-square table (originally: "16x8+dummy, + PST")
  signed char* Zobrist;           // short name `T`: Random numbers for hash key, really int, but packed as char (a.k.a. "hash translation table")
  signed char centr[32];

  int *PrincVar;    // short name `pv`; principal variation (stored as triangular array)
  int *sp;
  int margin;       // multi-PV margin

  int seed=76596595;        // random number generator seed

  // Debugging
  int nodes, totalNodes;
  int stack;
  int maxStack = 0;

protected:

  int Rand() {
    return (seed = 1103515245*seed + 12345)*150610563>>14;
  }

  // TODO: why does it go 20 levels deep on "sd 4"?
  int Search(int8_t Side, int Alpha, int Beta, int Eval, int epSqr, int LastTo, int8_t Depth) {          // TODO: these can be move to SearchStack
    /* recursive minimax search, Side=moving side, Depth=depth*/
    /* (Alpha,Beta)=window, Eval=current eval. score, epSqr=e.p. sqr.*/
    /* LastTo=prev.dest; return score*/

    static char info[100];       // string to be sent to `postCallback` to be displayed on screen

    // Debugging
    //log_d("Side=%d, A/B=%d/%d, score/epSqr=%d/%d, LastTo=%d, Depth=%d", Side, Alpha, Beta, Eval, epSqr, LastTo, Depth);
    nodes++;
    totalNodes++;
    stack++;
    if (stack > maxStack) {
      maxStack = stack;
      if (maxStack >= 21) {
        sprintf(info, "MAX. STACK: %d", maxStack);
        this->postCallback(info);
      }
    }
    SSS.init(HashKeyLo, HashKeyHi, sp, S);

    int BestScore = 0;      // TODO: why does this cause problems when moved to SearchStack?

    // References into search stack (purely for readability down below)
    int& LastKeyLo = (SSS.LastKeyLo);
    int& LastKeyHi = (SSS.LastKeyHi);
    int& Score = (SSS.Score);
    int& StepVec = (SSS.StepVec);
    int& CaptSqr = (SSS.CaptSqr);
    int& BestFrom = (SSS.BestFrom);
    int& BestTo = (SSS.BestTo);
    int& FromSqr = (SSS.FromSqr);
    int& ToSqr = (SSS.ToSqr);
    int& StartSqr = (SSS.StartSqr);
    int& SkipSqr = (SSS.SkipSqr);
    int& SkipSqrF = (SSS.SkipSqrF);
    int& RookSqr = (SSS.RookSqr);
    int8_t& IterDepth = (SSS.IterDepth);
    int& MoveFlags = (SSS.MoveFlags);
    signed char& PieceType = (SSS.PieceType);
    signed char& Victim = (SSS.Victim);
    signed char& CurPiece = (SSS.CurPiece);
    signed char& Gate = (SSS.Gate);

    struct HashTable* a=HashTab+(HashKeyLo+(Side+S)*epSqr&U);                   /* lookup pos. in hash table*/
    Alpha -= (Alpha<Eval);
    Beta -= (Beta<=Eval);        /* adj. window: delay bonus */
    IterDepth=a->D;
    BestScore=a->V;
    BestFrom=a->F;
    BestTo=a->Y+S-1;              /* resume at stored depth   */
    if(a->K-HashKeyHi|LastTo&S&&(BestFrom=8)||                        /* miss: other pos. or empty*/
        !((BestScore<=Alpha) | BestFrom&4 && (BestScore>=Beta) | BestFrom&2)) {              /*   or window incompatible */
      IterDepth=BestTo=0;  /* start iter. from scratch */
    }
    if(BestFrom&1) {
      stack--;
      return 0;
    }                                  /* busy-flag set: rep-draw  */
    *sp++=0;                                      /* initialize empty PV      */
    BestFrom=a->X;                                       /* start at best-move hint  */

    // Iterative deepening loop
    while(IterDepth++<Depth || IterDepth<3 ||              /*** min depth = 2   iterative deepening loop */
          LastTo&S&&K==INF&&(GetTickCount()-Ticks<tlim&IterDepth<=MaxDepth|| /* root: deepen upto time   */
                             (K=BestFrom, L=BestTo&~S, Score=BestScore, IterDepth=3))) {                /* time's up: go do best    */
      FromSqr = StartSqr = BestFrom;                                       /* start scan at prev. best */
      SSS.h = BestTo&S;                                       /* request try noncastl. 1st*/
      if(a->D<INF_PLY) {
        a->F=1, a->K=HashKeyHi;  /* mark hash entry 'busy'   */
      }
      SSS.P=IterDepth>2&&Beta+INF?Search(16-Side,-Beta,1-Beta,-Eval,S,2*S,IterDepth-3):INF;    /* search null move         */
      BestScore=(-SSS.P<Beta|R<5?IterDepth-2?-INF:Eval:-SSS.P);   /*** prune if > beta  unconsidered:static eval */
      ab|=!(N++&4095)&&tlim>1e8&&Input();          /* node count (for timing)  */
      do {
        CurPiece=Board[FromSqr];                                   /* scan board looking for   */
        if(CurPiece&&(CurPiece&16)==Side) {                            /*  own piece (inefficient!)*/
          StepVec = PieceType = CurPiece&15;                                  /* PieceType = piece type (set StepVec>0) */
          if(hill && (PieceVal[PieceType]<0) && (Board[REGION+FromSqr]&HILL)) {
            BestScore=INF, IterDepth=MAX_PLY;  /* King on the hill: we won */
          }
          SSS.j=PieceVecs[PieceType];                                   /* first step vector f.piece*/
          while((StepVec=StepVecs[++SSS.j])) {                          /* loop over directions o[] */
replay:                                       /* resume normal after best */
            MoveFlags = SSS.h ? 3 : MoveModes[SSS.j];            /* move modes (for fairies) */
            ToSqr = FromSqr;                 /* (FromSqr,ToSqr)=move */
            SkipSqr = SkipSqrF = RookSqr = S;                  /* (SkipSqr,RookSqr)=castl.R*/
            SSS.rg = (MoveFlags>>10) & 3;
            SSS.vf=32;
            if(SSS.rg>PieceType) {
              SSS.rg=pt[FromSqr]&~CurPiece&2*CurPiece?(SSS.vf=0):1;
            }
            do {                                      /* ToSqr traverses ray, or:     */
              CaptSqr = ToSqr = SSS.h ? BestTo^SSS.h : ToSqr+StepVec;                           /* sneak in prev. best move */
              if(MoveFlags&(1<<8)) {
                CaptSqr=ToSqr=(ToSqr&15)>13?ToSqr+BW:(ToSqr&15)>=BW?ToSqr-BW:ToSqr;  /* cylinder board */
              }
              if(ToSqr<0|ToSqr>BE|(ToSqr&15)>=BW) {
                break;  /* board edge hit           */
              }
#ifdef FAIRYMAX_MULTIPATH
              if(MoveFlags&1<<9) {                          /* if multipath move        */
                Victim=MoveFlags>>RBITS;                          /* get dir. stepped twice   */
                if(Board[FromSqr+Victim]) {
                  if(Board[ToSqr-2*Victim] | Board[ToSqr-Victim]) {
                    break;
                  }
                } else if(Board[FromSqr+2*Victim] && Board[ToSqr-Victim]) {
                  break;  /* test if empty path exists*/
                }
              }
#endif
              BestScore=(epSqr<S&&(ToSqr<(LastTo&S-1)?epSqr-ToSqr<2:ToSqr-epSqr<2)&MoveFlags?INF:BestScore); /* bad castling             */
              if((PieceType<3) & MoveFlags) {
                CaptSqr = (ToSqr^epSqr)&(epSqr>>9^511) ? CaptSqr : LastTo&S-1;  /* shift capt.sqr. CaptSqr if e.p.*/
              }
              Victim=Board[CaptSqr];
              if(MoveFlags & (1+!Victim)) {                          /* mode (capt/nonc) allowed?*/
                if(Victim && (Victim&16)==Side) {
                  break;  /* capture own              */
                }
                SSS.i = PieceVal[Victim&15] + ((Victim & 0xC0)>>sh);                /* value of capt. piece Victim   */
                if(SSS.i<0&&(PieceCount[Victim&31]<2||                   /* K capture, (of last K),  */
                             Victim>>3&SSS.kk!=CaptSqr&SSS.kk!=S||(SSS.kk=CaptSqr,SSS.i=-SSS.i,0))) {
                  BestScore=INF, IterDepth=MAX_PLY;  /* or duple check: cutoff*/
                }
                if((BestScore>=Beta) & (IterDepth>1)) {
                  goto cutoff;  /* abort on fail high       */
                }
                Score=IterDepth-1?Eval:SSS.i-PieceType;                            /*** MVV/LVA scoring if IterDepth=1**/
                if(IterDepth-!Victim>1) {                            /*** all captures if IterDepth=2  ***/
                  Score=Gate=0;
gating:                              /* retry move with gating   */
                  Score+=centr[PieceType]*(Board[FromSqr+CENTER]-Board[ToSqr+CENTER]);       /* center positional pts.   */
                  if(RookSqr-S) {
                    Board[SkipSqrF]=(SSS.rk=Board[RookSqr])|32, Score+=20;  /* castling: put R & score  */
                  }

                  Board[RookSqr]=Board[CaptSqr]=0;                                           /* do move */
                  Board[FromSqr]=Gate;
                  Board[ToSqr]=CurPiece|32;                                                  /* set non-virgin  */

                  PieceCount[Victim&31]-=!!Victim;                         /* update victim piece count */
                  Score-=PieceVal[PieceType]>0|R<EG?0:20-30*((FromSqr-ToSqr+1&7)>2);  /*** freeze K in mid-game ***/
                  if(PieceType<3) {                              /* pawns:                   */
                    Score-=9*((Board[FromSqr-2]!=CurPiece)+                    /* structure, undefended    */
                              (Board[FromSqr+2]!=CurPiece)                     /*        squares plus bias */
                              +(PieceVal[Board[FromSqr^16]&15]<0))              /*** cling to magnetic K ***/
                           +(R-76>>2);                      /* end-game Pawn-push bonus */
                    Board[ToSqr]+=SSS.V=CurPiece&32?pt[ToSqr]:0;                 /*upgrade P or convert to Q */
                    SSS.V>>=sh;                               /* for Shatranj promo to F  */
                    SSS.i+=SSS.V+abs(PieceVal[Board[ToSqr]&15])-PieceVal[PieceType];            /* promotion / passer bonus */
                  }
                  if(LastTo&S) {
                    if(map[FromSqr+S*ToSqr]) {
                      Score=-INF;  /* skip if excluded move    */
                      goto skip;
                    }
                    if(GamePtr<6&Randomize) {
                      Score+=(Rand()>>10&31)-16;  /* randomize      */
                    }
                  }
                  HashKeyLo+=J(0);
                  HashKeyHi+=J(4)+RookSqr-S;
                  Score += Eval+SSS.i;
                  SSS.V = (BestScore>Alpha) ? BestScore : Alpha;                      /*** new eval & alpha    ****/
                  if(LastTo&S) {
                    SSS.V = (BestScore-margin>Alpha) ? (BestScore-margin) : Alpha;  /* multiPV                  */
                  }
                  SSS.C = IterDepth-1-(IterDepth>5&PieceType>2&!Victim&!SSS.h);                 /* nw depth, reduce non-cpt.*/
                  SSS.C = R<EG|SSS.P-INF|IterDepth<3||Victim&&PieceVal[PieceType]>0?SSS.C:IterDepth;         /* extend 1 ply if in-check */
                  if(bareK) {
                    SSS.C = (PieceType==bareK) && (CORNER&(Board[REGION+FromSqr])) ? (IterDepth+1) : (IterDepth-1);  /* corner-leave extension */
                  }
                  do
                    SSS.s=SSS.C>2|Score>SSS.V?-Search(16-Side,-Beta,-SSS.V,-Score,/*** futility, recursive eval. of reply */
                                                      SkipSqr,ToSqr&255,SSS.C):Score;
                  while ( (SSS.s>Alpha) & (++SSS.C<IterDepth) );
                  Score=SSS.s;                     /* no fail:re-srch unreduced*/
                  if(Score>SSS.V && Score<Beta) {
                    int *p=sp;
                    sp=SSS.ps+1;
                    while((*sp++=*p++));
                    *SSS.ps=512*FromSqr+ToSqr;
                  }
                  if(LastTo&8*S&&K-INF) {                       /* move pending: check legal*/
                    if( (Score+INF) && ((FromSqr==K) & (ToSqr==L) & (Gate==GT)) ) {           /*   if move found          */
                      Q=-Eval-SSS.i;
                      O=SkipSqr;
                      LL=L;
                      prom=Gate&15;
                      if(Board[ToSqr]-CurPiece&15)prom=Board[ToSqr]-=PromPiece,   /* (under-)promotion:       */
                                                    Q-=abs(PieceVal[prom&=15]),Q+=PieceVal[prom+PromPiece], /*  correct piece & score & */
                                                        HashKeyHi+=PromPiece;           /*  invalidate hash         */
                      a->D=INF_PLY;
                      a->V=0;                      /* lock game in hash as draw*/
                      R2-=SSS.i/FAC;                           /*** total captd material ***/
                      Fifty = Victim|PieceType<3?0:Fifty+1;
                      if(centr[PieceType]>2) {
                        bareL=ToSqr;  /* remember location bare K */
                      }
                      sp=SSS.ps;
                      stack--;
                      return Beta;
                    }            /*   & not in check, signal */
                    Score = BestScore;                                  /* (prevent fail-lows on    */
                  }                                      /*   K-capt. replies)       */
skip:
                  PieceCount[Victim&31]+=!!Victim;       /* victim back in hand */
                  Board[RookSqr]=SSS.rk;
                  Board[SkipSqrF]=Board[ToSqr]=0;
                  Board[FromSqr]=CurPiece;
                  Board[CaptSqr]=Victim;    /* undo move,RookSqr can be dummy */
                }                                       /*          if non-castling */
                if( (LastTo&S) && ((!ab) & (K==INF) & (IterDepth>2) & (Score>SSS.V) & (Score<Beta)) ) {
                  if(Post && IterDepth-2 > 2) {       // depth >= 3 (don't post "d=1" and "d=2" events as they quickly disappear from screen)
                    // NOTE: this output currently deviates from the output format prescribed by the Xboard protocol specification
                    static char* s;
                    s = info;
                    s += sprintf(s, "d=%d ",IterDepth-2);
                    score=Score;
                    s += sprintf(s, "s=%d ", Score > INF-S ? 100000+INF-Score : Score < S-INF ? -100000-INF-Score : Score);
                    s += sprintf(s, "t=%0.1fs node=%d", ((float)GetTickCount()-Ticks)/1000, N);
                    // Return the current principal variation
                    int *p=SSS.ps;
                    char X,Y;
                    while(*p && s-info+6 < sizeof(info) ) {
                      X=*p>>9;
                      Y=*p++;
                      s += sprintf(s, " %c%d%c%d",'a'+(X&15),BH-(X>>4&15)-(BH==10),'a'+(Y&15),BH-(Y>>4&15)-(BH==10));
                      break;   // show only best move considered, don't show the principal variation
                    }
                    //printf("\n");//fflush(stdout);
                    this->postCallback(info);
                  }
                  GT=Gate;                                 /* In root, remember gated  */
                }
                if(Score>BestScore) {                               /* new best, update max,best*/
                  BestScore=Score, BestFrom=FromSqr, BestTo=ToSqr|(S&SkipSqr);  /* mark non-double with S   */
                }
                if(gating&&!(CurPiece&32)&&PieceType>2&&IterDepth-!Victim>1) {   /* virgin non-Pawn: gate    */
                  PieceCount[(Gate|=Side+40)-27]++;                              /* prev. gated back in hand */
                  if(BestScore>=Beta) {
                    goto cutoff;  /* loop skips cutoff :-(    */
                  }
                  while(++Gate<Side+43) if(PieceCount[Gate-27]) {                /* look if more to gate     */
                      PieceCount[Gate-27]--;
                      Score=10;
                      goto gating;                /* remove from hand & retry */
                    }
                }
                HashKeyLo=LastKeyLo;
                HashKeyHi=LastKeyHi;                        /* restore hash keys        */
                if(ab) {
                  a->F&=6;
                  sp=SSS.ps;
                  stack--;
                  return 0;
                }       /* unwind search to abort   */
                if(SSS.h) {
                  SSS.h=0;  /* redo after doing old best*/
                  goto replay;
                }
              }
              SSS.s=Victim&&2&~SSS.rg|~Victim&16^Side;
              Score = StepVec^(MoveFlags>>RBITS);      /* platform & toggled vector*/
              if( ((MoveFlags&15)^4)|(CurPiece & SSS.vf) ||                      /* no double or moved before*/
                  (PieceType>2)&(!(MoveFlags & 128)) &&                     /* no P & no virgin jump,   */
                  ((Board[RookSqr=FromSqr&~15|(StepVec>0)*(BW-1)]^32)<33      /* no virgin R in corner RookSqr, */
                   || Board[RookSqr-StepVec]|Board[RookSqr-2*StepVec]|Board[SkipSqrF=ToSqr+Score-StepVec]|Board[ToSqr+StepVec]) /* no 2 empty sq. next to R */
                ) {
                Victim += MoveFlags&4;  /* fake capt. for nonsliding*/
              } else if(MoveFlags&64) {
                Victim=MoveFlags&128?0:Victim,MoveFlags&=63;  /* e.p-immune initial step  */
              } else {
                SkipSqr=ToSqr+(PieceType<3)*(ep&~CurPiece<<8);  /* set e.p. rights          */
              }
              if(SSS.s&&MoveFlags&8&&!(ToSqr=SSS.rg&1?ToSqr-StepVec:ToSqr,Victim=0)        /* hoppers go to next phase */
                  ||!(MoveFlags&128)&&!SSS.rg--) {               /* zig-zag piece? (w. delay)*/
                StepVec=Score, MoveFlags^=(MoveFlags>>4&15);  /* alternate vector & mode  */
              }
            } while(!Victim);                                   /* if not capt. continue ray*/
          }
        }
        if((++FromSqr&15)>=BW) {
          FromSqr=FromSqr>BE?0:FromSqr+16&~15;  /* next sqr. of board, wrap */
        }
      } while(FromSqr-StartSqr);
cutoff:
      BestScore = ((BestScore+stale) | SSS.P==INF) ? BestScore : (BestFrom=BestTo=0);              /* if stalemate, draw-score */
      if(a->D<INF_PLY)                                  /* protect game history     */
        a->K=HashKeyHi, a->V = BestScore, a->D=IterDepth, a->X=BestFrom,                /* always store in hash tab */
           a->F=4*(BestScore>Alpha) | 2*(BestScore<Beta), a->Y = BestTo&S ? BestTo+1 : 0;        /* move, type (bound/exact),*/
    }                                             /*    encoded in BestFrom 2,4 bits */
    if(LastTo&4*S) {
      K=BestFrom, L=BestTo&~S;
    }
    sp=SSS.ps;
    stack--;
    return BestScore += (BestScore<Eval);                                /* delayed-loss bonus       */
  }


  /* Generic main() for Winboard-compatible engine     */
  /* (Inspired by TSCP)                                */
  /* Author: H.G. Muller                               */

  /* The engine is invoked through the following       */
  /* subroutines, that can draw on the global vaiables */
  /* that are maintained by the interface:             */
  /* Side         side to move                         */
  /* Move         move input to or output from engine  */
  /* PromPiece    requested piece on promotion move    */
  /* TimeLeft     ms left to next time control         */
  /* MovesLeft    nr of moves to play within TimeLeft  */
  /* MaxDepth     search-depth limit in ply            */
  /* Post         boolean to invite engine babble      */

  /* InitEngine() progran start-up initialization      */
  /* InitGame()   initialization to start new game     */
  /*              (sets Side, but not time control)    */
  /* Think()      think up move from current position  */
  /*              (leaves move in Move, can be invalid */
  /*               if position is check- or stalemate) */
  /* DoMove()     perform the move in Move             */
  /*              (togglese Side)                      */
  /* ReadMove()   convert input move to engine format  */
  /* PrintMove()  print Move on standard output        */
  /* Legal()      check Move for legality              */
  /* ClearBoard() make board empty                     */
  /* PutPiece()   put a piece on the board             */

  /* define this to the codes used in your engine,     */
  /* if the engine hasn't defined it already.          */

  int PrintResult(int s, int mode) {
    int j, k, cnt=0;
    log_d("totalNodes = %d", totalNodes);

    /* search last 50 states with this stm for third repeat */
    for(j=2; j<=100 && j <= HistPtr; j+=2) {
      for(k=0; k<STATE; k++)
        if(HistoryBoards[HistPtr][k] != HistoryBoards[HistPtr-j&1023][k]) {
          goto differs;
        }
      /* is the same, count it */
      if(++cnt > 1) { /* third repeat */
        if(mode != EMPTY) {
          printf("1/2-1/2 {Draw by repetition}\n");
        }
        return 1;
      }
differs:
      ;
    }
    K=INF;
    ab=0;
    cnt = Search(s,-INF,INF,Q,O,LL|4*S,3);
    if(cnt>-INF+1 && K==0 && L==0) {
      printf("1/2-1/2 {Stalemate}\n");
      return 2;
    }
    if(cnt==-INF+1) {
      if (s == WHITE) {
        printf("0-1 {Black mates}\n");
      } else {
        if(succession) { // suppress loss claim if black might be able to replace its King by promotion
          for(j=0; j<BW; j++)if((Board[j+96]&31)==18) {
              return 0;
            }
        }
        printf("1-0 {White mates}\n");
      }
      return 3;
    }
    if(Fifty >= 2*drawMoves) {
      if(mode != EMPTY) {
        printf("1/2-1/2 {Draw by fifty move rule}\n");
      }
      return 4;
    }
    return 0;
  }


  void InitEngine() {
    log_d("initing engine");
    N=32*S+7;
    while(N-->S+3) {
      Zobrist[N]=Rand()>>9;
    }
    seed = GetTickCount();
  }

  void InitGame() {
    log_d("initing game");

    Side = WHITE;
    Q=0;
    O=S;
    Fifty = 0;
    R = 0;
    memset(Board, 0, sizeof(Board));
    memset(PieceCount, 0, sizeof(PieceCount));
    if (chess960) {
      // Initialize back rank randomly
      bool occupied[8];
      memset(occupied, 0, sizeof(occupied));
      int pos, cnt;
      // - allocate the bishops
      for (pos = abs(Rand()) % 4; occupied[pos*2]; pos = (pos + 1) % 4);
      Board[pos*2] = BackRank[2+BLACK] + BLACK;
      Board[pos*2+(BH-1)*16] = BackRank[2];
      occupied[pos*2] = true;
      for (pos = abs(Rand()) % 4; occupied[pos*2+1]; pos = (pos + 1) % 4);
      Board[pos*2+1] = BackRank[5+BLACK] + BLACK;
      Board[pos*2+1+(BH-1)*16] = BackRank[5];
      occupied[pos*2+1] = true;
      // - allocate the queen & knights
      pos = abs(Rand()) % 8;
      for (cnt = abs(Rand()) % 6 + 1; occupied[pos] || --cnt; pos = (pos + 1) % 8);
      Board[pos] = BackRank[3+BLACK] + BLACK;
      Board[pos+(BH-1)*16] = BackRank[3];
      occupied[pos] = true;
      for (cnt = abs(Rand()) % 5 + 1; occupied[pos] || --cnt; pos = (pos + 1) % 8);
      Board[pos] = BackRank[1+BLACK] + BLACK;
      Board[pos+(BH-1)*16] = BackRank[1];
      occupied[pos] = true;
      for (cnt = abs(Rand()) % 4 + 1; occupied[pos] || --cnt; pos = (pos + 1) % 8);
      Board[pos] = BackRank[6+BLACK] + BLACK;
      Board[pos+(BH-1)*16] = BackRank[6];
      occupied[pos] = true;
      // - allocate the rook, king and rook deterministically
      for (pos = 0; occupied[pos]; pos++);
      Board[pos] = BackRank[0+BLACK] + BLACK;
      Board[pos+(BH-1)*16] = BackRank[0];
      for (pos++; occupied[pos]; pos++);
      Board[pos] = BackRank[4+BLACK] + BLACK;
      Board[pos+(BH-1)*16] = BackRank[4];
      for (pos++; occupied[pos]; pos++);
      Board[pos] = BackRank[7+BLACK] + BLACK;
      Board[pos+(BH-1)*16] = BackRank[7];
    }
    for(int K=BW; K--;) { // files
      /* initial board setup*/
      if (!chess960) {
        Board[K] = BackRank[K+BLACK]+BLACK;
        Board[K+(BH-1)*16] = BackRank[K];
      }
      Board[K+16*pRank] = 2+BLACK;
      Board[K+(BH-1-pRank)*16] = 1;

      PieceCount[BackRank[K+BLACK]+BLACK]++;
      PieceCount[BackRank[K]]++;
      PieceCount[2+BLACK]++;
      PieceCount[1]++;

      // There is only one King in PieceCount
      for (int side=WHITE; side<=BLACK; side+=BLACK)
        if(PieceVal[BackRank[K+side]+side] == -1) {
          PieceCount[BackRank[K+side]+side]=1;
        }

      for (L=BH; L--;) { // ranks
        Board[16*L+K+CENTER]=(K-BW/2+hill/2.)*(K-BW/2+hill/2.)+(L-(BH-1)/2.)*(L-(BH-1)/2.);   /* center-pts table   */
        pt[16*L+K]=0;
      }
      pt[K+16] = pt[K+32] = pt[K+(BH-3)*16] = pt[K+(BH-2)*16] = 64;
      pt[K+16*zone] = 6-128;
      pt[K+(BH-1-zone)*16] = 5-128; /* promotion bonus & piece upgrade */
      if(pRank == 3) {  // special case
        L = BackRank[K-(PieceVal[BackRank[K]]<0)];
        pt[K] = L-129;
        pt[K+(BH-1)*16] = L-130;
      }
    }

    // Mark different regions on the board (TODO: this looks like static information that doesn't have to be stored in Board)
    Board[REGION+16*3+BW/2]=Board[REGION+16*4+BW/2]=Board[REGION+16*3+BW/2-1]=Board[REGION+16*4+BW/2-1]=HILL;
    Board[REGION]=Board[REGION+16*(BH-1)]=Board[REGION+16*(BH-1)+BW-1]=Board[REGION+BW-1]=CORNER;

    int k=0;
    for(int i=0; i<BW; i++) {
      R += abs(PieceVal[BackRank[i]])/FAC + abs(PieceVal[BackRank[i+BLACK]])/FAC;
      Q += abs(PieceVal[BackRank[i]]) - abs(PieceVal[BackRank[i+BLACK]]) + PieceVal[1] - PieceVal[2];
      if(PieceVal[BackRank[i]] < 0) {
        k = PieceVal[BackRank[i]];
      }
    }
    RR = R2 = R -= 2*(-k/FAC);

    // Total piece count
    PieceCount[WHITE] = PieceCount[BLACK] = 2*BW;

    // Special cases for certain variants
    pm = !PieceCount[BLACK+7] && PieceCount[BLACK+9] && PieceCount[WHITE+7] ? 2 : 0; // Unlike white, black has no 'Q', so promote to 9, which he does have.
    for(int K=BW; K--;) {
      pt[K+(BH-1)*16]+=pm;  // alter black promotion choice
    }
    if(gating) {
      PieceCount[14] = PieceCount[15] = PieceCount[30] = PieceCount[31] = 1, R2 = R += 2*(PieceVal[9]/FAC + PieceVal[10]/FAC);
    }
  }

  void CopyBoard(int s) {
    /* copy game representation of engine to HistoryBoard */
    /* don't forget castling rights and e.p. state!       */
    for(int i=0; i<BH; i++)
      for(int j=0; j<BW; j++) {               /* board squares  */
        HistoryBoards[s][BW*i+j] = Board[16*i+j] | 64*(16*i+j==O);  // IWONDER: bit 6 is set for all non-zero? what is the point?
      }
  }

  void PrintVariants(bool fairyCombo) {
    int count=0, total=0;
    char c=EOF+1, buf[80];
    FILE *f;

    // f = fopen(inifile, "r");
    char* fmaxini = strdup(FMAX_INI);
    if (fmaxini==NULL) {
      return;
    }
    f = fmemopen(fmaxini, strlen(fmaxini), "r");

    /* search for game names in definition file */
    do {
      while(fscanf(f, "Game: %s", buf) != 1 && c != EOF && c != '\0')
        while((c = fgetc(f)) != EOF && c != '\n' && c != '\0');
      if(c == EOF || c == '\0') {
        break;
      }
      total++;
      if(*buf < 'a') {
        continue;
      }
      if (fairyCombo && (strstr(buf, "fairy/") != buf)) {
        continue;
      }
      if(fairyCombo && count == 0) {
        strcpy(selectedFairy, buf);
      }
      if(count++) {
        printf(fairyCombo ? " /// " : ",");
      }
      printf("%s", fairyCombo ? buf+6 : buf);
    } while(c != EOF && c != '\0');

    fclose(f);
    if(!fairyCombo && total != count) {
      printf("%sfairy", count ? "," : "");
    }

    free(fmaxini);
  }

  void PrintOptions() {
    printf("feature option=\"Resign -check %d\"\n", Resign);
    printf("feature option=\"Resign Threshold -spin %d 200 1200\"\n", Threshold);
    printf("feature option=\"Claim draw after -spin %d 0 200\"\n", drawMoves);
    // printf("feature option=\"Ini File -file %s\"\n", inifile);
    printf("feature option=\"Multi-PV Margin -spin %d 0 1000\"\n", margin);
    printf("feature option=\"Variant fairy selects -combo ");
    PrintVariants(1);
    printf("\"\n");
    printf("feature option=\"Makruk rules -combo makruk /// Cambodian /// Ai-wok\"\n");
    printf("feature option=\"Dummy Slider Example -slider 20 0 100\"\n");
    printf("feature option=\"Dummy String Example -string happy birthday!\"\n");
    printf("feature option=\"Dummy Path Example -path .\"\n");
    printf("feature option=\"Automatic persistent-hash dialog -check %d\"\n", popup);
    printf("feature option=\"Info -button\"\n");
    printf("feature option=\"Save in hash file -button\"\n");
    printf("feature option=\"Clear Hash -button\"\n");
    printf("feature done=1\n");
  }

  /*  void LoadHash(char *dir, char *name)
    {       // read persistent-hash file into hash table as protected entries that always cause cutoff
            FILE *f;
            snprintf(hashfile, 256, "%s/%s.hash", dir, name);
            if(f = fopen(hashfile, "r")) {
                while(fscanf(f, "%x:%x=%d", &HashKeyLo, &HashKeyHi, &j) == 3) HashKeyLo&=U, HashTab[HashKeyLo].V=j, HashTab[HashKeyLo].K=HashKeyHi, HashTab[HashKeyLo].D=INF_PLY, HashTab[HashKeyLo].F=6;
                fclose(f);
            }
    } */

  void LoadGame(const char *name) {
    log_d("loading game %s", name);
    int i, j, ptc=0, count=0, step2;
    char c, buf[80], pieceToChar[200], parent[80];
    static int currentVariant;
    FILE *f;

    // f = fopen(inifile, "r");
    char* fmaxini = strdup(FMAX_INI);
    if (!fmaxini) {
      return;
    }
    f = fmemopen(fmaxini, strlen(fmaxini),"r");
    /*
        //// The "file" is always found
        if (f==NULL) {
          printf("telluser piece-description file '%s'  not found\n", inifile);
          exit(0);
        }
    */
    if(fscanf(f, "version 4.8(%c)", &c)!=1 || c != 'w') {
      printf("telluser incompatible fmax.ini file\n");
    }

    gating = succession = 0;
    chess960 = false;
    if(name != NULL) {
      // search for game name in definition file

      // Special games
      if(!strcmp(name, "makruk")) {
        name = Cambodian;
      } else if(!strcmp(name, "fairy")) {
        name = selectedFairy;
      } else if(!strcmp(name, "seirawan")) {
        gating = true;
      } else if (!strcmp(name, "fischerandom")) {
        chess960 = true;  // TODO: it crashes when asked to load "fisherandom"
        name = "normal";
      }

      while((ptc=fscanf(f, "Game: %s # %s # %s", buf, pieceToChar, parent))==0 || strcmp(name, buf) ) {        // TODO: how does PTC work?
        char *p = info;
        while((c = fgetc(f)) != EOF && c != '\n' && c != '\0') {
          *p++ = c;
        }
        if(*info == '/') {
          *p = 0;
        } else {
          *info = 0;  // remember last line before Game if it was comment
        }
        count++;
        if(c == EOF || c == '\0') {
          printf("telluser variant %s not supported\n", name);
          free(fmaxini);
          fclose(f);
          return; // keep old settings
        }
      }
      currentVariant = count;
    }
    log_d("# variant %s found", name);

    /* We have found variant, or if none specified, are at beginning of file */
    BW = BH = 0;
    if (fscanf(f, "%dx%d", &BW, &BH)!=2 || BW>MAX_BOARD_WIDTH || BH>MAX_BOARD_HEIGH) {
      printf("telluser unsupported board size %dx%d\n",BW,BH); //exit(0);
    }
    BE = (BH-1)*16 + BW-1;      // highest valid square number
    CONS = 799 + 16*(BH-8);     // move-conversion constant

    // new method to indicate deviant zone depth
    i = 1;
    fscanf(f, "=%d", &i);
    zone = i - 1;

    for(i=0; i<BW; i++) {
      fscanf(f, "%d", BackRank+i);
    }
    for(i=0; i<BW; i++) {
      fscanf(f, "%d", BackRank+i+BLACK);
    }
    memset(HashTab, 0, sizeof(HashTab));  /* clear hash */
    memset(piecetype, 0, sizeof(piecetype));
    memset(blacktype, 0, sizeof(blacktype));

    i=0;
    j=-1;
    c=0;
    ep=1<<20;
    stale=INF;
    bk=1;
    bareK=0;
    bareL=-1;
    step2 = 666;
    while(fscanf(f, "%d,%x,%d", StepVecs+j, MoveModes+j, &step2)>=2 ||
          fscanf(f,"%c:%d", &c, PieceVal+i+1)==2) {
      if(c) {
        PieceVecs[++i]=j;
        centr[i] = c>='a';
        blacktype[c&31]=i;
        piecename[i]=c&31;
        if(piecetype[c&31]==0) {
          piecetype[c&31]=i;  // only first
        }
        succession |= PieceVal[i] < -4;         // expendable royalty; assume we can promote to it
        if(PieceVal[i]<0) {
          wk=bk, bk=i;  // remember royals
        }
      }
      if(step2 != 666) {
        MoveModes[j] += (step2 ^ StepVecs[j]) << RBITS, step2 = 666;  // compute toggle-vector from 3rd move parameter
      }
      j++;
      StepVecs[j]=0;
      /* this->printf("# c='%c' i=%d PieceVecs[i]=%d j=%d (%3d,%8x)\n", c?c:' ', i, PieceVecs[i], j, StepVecs[j-1], MoveModes[j-1]); */
      c=0;
      if(i>15 || j>255) {
        break;
      }
    }

    if(BH == 10 && StepVecs[0] == -16 && MoveModes[0] & 0xC00) {
      ep += 16<<9;  // pawn with triple-push
    }
    sh = PieceVal[7] < 250 ? 3 : 0;
    hill = (PieceVal[3] == -2);               // as of version 5.0b, this is only for "king-of-the-hill" variant
    stale -= (PieceVal[9] == -2);             // as of version 5.0b, this is only for "almost-wildebeest" variant
    pRank = (zone ? zone : 1);                // pawn rank
    if(zone < 0) {
      pRank = -1-zone, zone = 0;  // negative =N suffix is kludge for configuring Pawn rank (used in "asean" and "grande-acedrex" variants)
    }

    if(ptc > 1) {
      // setup board in GUI, by sending it pieceToCharTable and FEN (Forsyth–Edwards Notation) for the board setup
      if(ptc == 2) {
        printf("setup (%s) ", pieceToChar);
      } else {
        printf("setup (%s) %dx%d+0_%s ", pieceToChar, BW, BH, parent);
      }
      for(i=0; i<BW; i++) {
        printf("%c", piecename[BackRank[i+BLACK]]+'`');
      }
      printf("/");
      for(i=1; i<pRank; i++) {
        printf("8/");
      }
      for(i=0; i<BW; i++) {
        printf("%c", piecename[2]+'`');
      }
      printf("/");
      for(i=1+pRank; i<BH-1-pRank; i++) {
        printf("%d/", BW);
      }
      for(i=0; i<BW; i++) {
        printf("%c", piecename[1]+'@');
      }
      printf("/");
      for(i=1; i<pRank; i++) {
        printf("8/");
      }
      for(i=0; i<BW; i++) {
        printf("%c", BackRank[i] ? piecename[BackRank[i]]+'@' : '1');
      }
      printf(" w KQkq - 0 1\n");
    }
    /*
        //// Don't expect pieceToChar from the engine
        while(fscanf(f, " # %[^\n]", pieceToChar)) printf("piece %s\n", pieceToChar);
    */
    free(fmaxini);
    fclose(f);
    /*
                 LoadHash(FAIRYDIR, name); LoadHash(".", name); // initialize persistent hash
    */
  }

  void printf(const char *fmt, ...) {
    static char buff[256];
    va_list args;
    va_start(args, fmt);
    if (vsnprintf(buff, 256, fmt, args)==255) {
      log_e("printed too much");
    }
    va_end(args);
    output += buff;
  }

  char cell(char file, char rank) {
    int content = Board[file-16*rank+CONS];
    uint8_t pieceType = content & 15;
    return pieceType ? piecename[pieceType] + (content & BLACK ? '`' : '@') : '.';
  }

};
}

#endif // _FAIRYMAX_H_
#endif
