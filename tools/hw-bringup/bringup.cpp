#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// Standalone hardware self-test probe (NOT the firmware). Build/flash with its own env:
//   pio run -e bringup -t upload   &&   pio device monitor -e bringup   ('h' for help)
// Lives outside src/ so it never collides with the real firmware (src/main.cpp).
//
// Interactive module bring-up (digital modules: slots 2/3/4 + LCD slot 1). Confirms the
// exact pin-within-module assignments for a Genesis Mini:
//   slot 3 (LED push button): which P3_IO is the button input vs the LED output
//   slot 4 (rotary encoder): which P4_IO is A / B / push
//   slot 2 (passive buzzer): which P2_IO carries the signal
//   slot 1 (IPS LCD): which P1_IO is CS/DC/RST + ST7735 driver tab / rotation / inversion
//
// Drive over the serial console (115200). Two things happen at once:
//   * INPUT SCAN: all 6 button/encoder candidate pins are INPUT_PULLUP and printed live
//     whenever any of them changes — rotate the encoder / press the button to see which
//     GPIOs move (those are the INPUTS; the ones that never move are LED outputs).
//   * Commands drive candidate OUTPUTS so you can find the LED and the buzzer signal pin.
//
// GPIOs from CLAUDE.md "Module layout (confirmed 2026-06-27)".

struct Cand { const char* label; uint8_t gpio; };

// Slot 3 (LED button) + slot 4 (encoder) — monitored live as inputs.
static Cand gInputs[] = {
  {"S3_IO0", 9}, {"S3_IO1", 16}, {"S3_IO2", 15},   // LED push button slot
  {"S4_IO0", 1}, {"S4_IO1", 17}, {"S4_IO2", 18},   // rotary encoder slot
};
static const int N_IN = sizeof(gInputs) / sizeof(gInputs[0]);

// Slot 2 (buzzer) candidates — driven with a tone on command.
static Cand gBuzzer[] = { {"S2_IO0", 7}, {"S2_IO1", 6}, {"S2_IO2", 5} };
static const int N_BZ = sizeof(gBuzzer) / sizeof(gBuzzer[0]);

// --- Slot 1: 0.96" IPS colour LCD (AX22-0034). SPI MOSI=12 SCK=14; CS/DC/RST among 4/3/2.
// Unknowns at bring-up: which P1 IO is CS/DC/RST, and the driver tab/resolution.
static const uint8_t LCD_MOSI = 12, LCD_SCK = 14;
// 6 permutations of (CS, DC, RST) over {P1_IO0=4, P1_IO1=3, P1_IO2=2}.
static const uint8_t PERM[6][3] = {
  {4,3,2},{4,2,3},{3,4,2},{3,2,4},{2,4,3},{2,3,4},
};
struct TabOpt { const char* name; uint8_t id; bool invert; };
static TabOpt TABS[] = {
  {"MINI160x80(IPS,inv)", INITR_MINI160x80,        true },
  {"MINI160x80_PLUGIN",   INITR_MINI160x80_PLUGIN, true },
  {"BLACKTAB 128x160",    INITR_BLACKTAB,          false},
  {"144GREEN 128x128",    INITR_144GREENTAB,       false},
};
static const int N_TAB = sizeof(TABS) / sizeof(TABS[0]);
static int gPerm = 5, gTab = 0, gRot = 0;  // first 'p' press wraps to perm 0
static bool gInvert = false;               // IPS colour inversion (toggle with 'i')

static void lcdDraw() {
  uint8_t cs = PERM[gPerm][0], dc = PERM[gPerm][1], rst = PERM[gPerm][2];
  TabOpt t = TABS[gTab];
  Serial.printf(">> LCD perm %d  CS=%u DC=%u RST=%u  tab=%s  rot=%d\n",
                gPerm, cs, dc, rst, t.name, gRot);
  SPI.begin(LCD_SCK, -1, LCD_MOSI, -1);
  static Adafruit_ST7735* tft = nullptr;
  if (tft) delete tft;
  tft = new Adafruit_ST7735(cs, dc, rst);
  tft->initR(t.id);
  tft->invertDisplay(gInvert);
  tft->setRotation(gRot);
  Serial.printf("   invert=%d\n", gInvert);
  int w = tft->width(), h = tft->height();
  // colour flash so a live panel is obvious even if geometry is off
  tft->fillScreen(ST77XX_RED);   delay(400);
  tft->fillScreen(ST77XX_GREEN); delay(400);
  tft->fillScreen(ST77XX_BLUE);  delay(400);
  // geometry/colour reference: white 1px border + R/G/B corner blocks + label
  tft->fillScreen(ST77XX_BLACK);
  tft->drawRect(0, 0, w, h, ST77XX_WHITE);
  tft->fillRect(2, 2, 12, 12, ST77XX_RED);
  tft->fillRect(w - 14, 2, 12, 12, ST77XX_GREEN);
  tft->fillRect(2, h - 14, 12, 12, ST77XX_BLUE);
  tft->setTextColor(ST77XX_WHITE);
  tft->setTextSize(1);
  tft->setCursor(4, 18);  tft->printf("P%d %dx%d", gPerm, w, h);
  tft->setCursor(4, 30);  tft->print(t.name);
  Serial.printf("   drew %dx%d (expect: white border touches all 4 edges, R=TL G=TR B=BL)\n", w, h);
}

static const uint8_t RGB_PIN = 21;  // on-board RGB LED — status feedback

static int  gLastState[N_IN];
static int  gEdgeCount[N_IN];       // how often each input toggled (rotate/press counter)

static void allInputsMode() {
  for (int i = 0; i < N_IN; i++) {
    pinMode(gInputs[i].gpio, INPUT_PULLUP);
    gLastState[i] = digitalRead(gInputs[i].gpio);
  }
}

static void printHeader() {
  Serial.println();
  Serial.println("=== phase 0c module bring-up ===");
  Serial.println("INPUT SCAN is live: rotate the encoder, press the button — moving pins are INPUTS.");
  Serial.println("Commands:");
  Serial.println("  1..6  drive that input-candidate pin as OUTPUT, blink 6x (find the LED)");
  Serial.println("        1=S3_IO0/9  2=S3_IO1/16  3=S3_IO2/15  4=S4_IO0/1  5=S4_IO1/17  6=S4_IO2/18");
  Serial.println("  q w e beep buzzer candidate  q=S2_IO0/7  w=S2_IO1/6  e=S2_IO2/5");
  Serial.println("  LCD (slot 1, SPI MOSI=12 SCK=14, CS/DC/RST among 4/3/2):");
  Serial.println("    p   next CS/DC/RST permutation + draw test pattern (6 total)");
  Serial.println("    t   next driver tab/resolution, redraw same permutation");
  Serial.println("    r   rotate 90deg, redraw");
  Serial.println("    i   toggle colour inversion, redraw");
  Serial.println("  s     print current input states + edge counts");
  Serial.println("  z     reset edge counts");
  Serial.println("  h     this help");
  Serial.println();
}

static void printStates() {
  Serial.print("STATE  ");
  for (int i = 0; i < N_IN; i++) {
    Serial.printf("%s=%d(%d) ", gInputs[i].label, digitalRead(gInputs[i].gpio), gEdgeCount[i]);
  }
  Serial.println();
}

static void blinkPin(int idx) {
  Cand c = gInputs[idx];
  Serial.printf(">> driving %s (GPIO%u) SOLID for 4 s — watch for a steady LED\n", c.label, c.gpio);
  pinMode(c.gpio, OUTPUT);
  digitalWrite(c.gpio, HIGH); delay(4000);  // hold solid so it's unambiguous
  digitalWrite(c.gpio, LOW);
  pinMode(c.gpio, INPUT_PULLUP);            // restore to monitored input
  gLastState[idx] = digitalRead(c.gpio);
  Serial.printf("   %s off, restored to INPUT_PULLUP\n", c.label);
}

static void beep(int idx) {
  Cand c = gBuzzer[idx];
  Serial.printf(">> buzzer %s (GPIO%u): 1.5 s tone (sweep 2.7->3.2 kHz) — listen\n", c.label, c.gpio);
  // passive buzzers are loudest near resonance (~2.7-3.5 kHz); sweep so it's obvious.
  for (int f = 2700; f <= 3200; f += 100) { tone(c.gpio, f); delay(250); }
  noTone(c.gpio);
  pinMode(c.gpio, INPUT);  // release the line
}

void setup() {
  Serial.begin(115200);
  delay(300);
  for (int i = 0; i < N_IN; i++) gEdgeCount[i] = 0;
  allInputsMode();
  printHeader();
  printStates();
  neopixelWrite(RGB_PIN, 0, 0, 30);        // dim blue = bring-up mode
}

void loop() {
  // live input-change detection
  bool changed = false;
  for (int i = 0; i < N_IN; i++) {
    int v = digitalRead(gInputs[i].gpio);
    if (v != gLastState[i]) {
      gLastState[i] = v;
      gEdgeCount[i]++;
      changed = true;
    }
  }
  if (changed) printStates();

  // serial commands
  if (Serial.available()) {
    char ch = Serial.read();
    switch (ch) {
      case '1': case '2': case '3': case '4': case '5': case '6':
        blinkPin(ch - '1'); break;
      case 'q': beep(0); break;
      case 'w': beep(1); break;
      case 'e': beep(2); break;
      case 'p': gPerm = (gPerm + 1) % 6; lcdDraw(); break;  // gPerm always == what's on screen
      case 't': gTab  = (gTab + 1) % N_TAB; lcdDraw(); break;
      case 'r': gRot  = (gRot + 1) % 4; lcdDraw(); break;
      case 'i': gInvert = !gInvert; lcdDraw(); break;
      case 's': printStates(); break;
      case 'z':
        for (int i = 0; i < N_IN; i++) gEdgeCount[i] = 0;
        Serial.println("edge counts reset"); break;
      case 'h': printHeader(); break;
      case '\n': case '\r': break;
      default: break;
    }
  }
  delay(2);
}
