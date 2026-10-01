#include <FastLED.h>
#include <TM1637Display.h>

// ====================== НАЛАШТУВАННЯ ПІНІВ ======================
#define LED_PIN       2
#define LED_COUNT     90
#define BUZZER_PIN    8

#define BTN_BLUE      4
#define BTN_RED       5
#define BTN_GREEN     6
#define BTN_YELLOW    3

#define TM1637_DIO    9
#define TM1637_CLK    10

// ====================== ЗАГАЛЬНІ НАЛАШТУВАННЯ ======================
#define BRIGHTNESS    200
#define MAX_ENEMIES   8
#define MAX_SHOTS     8
#define DAMAGE_LIMIT  3

// Якщо стрічка "біжить" не в той бік, зміни false на true
const bool REVERSE_STRIP = false;

// Зона гравця на початку стрічки
const int PLAYER_ZONE_END = 3;   // 0..2 - індикатор життів, 3 - гармата
const int SHOT_START_POS  = 4;   // звідси вилітають постріли

CRGB leds[LED_COUNT];
TM1637Display display(TM1637_CLK, TM1637_DIO);

// ====================== КОЛЬОРИ ======================
enum ColorId : uint8_t {
  BLUE_ID = 0,
  RED_ID,
  GREEN_ID,
  YELLOW_ID,
  NO_COLOR = 255
};

struct Enemy {
  bool active;
  int pos;
  ColorId color;
};

struct Shot {
  bool active;
  int pos;
  ColorId color;
};

Enemy enemies[MAX_ENEMIES];
Shot shots[MAX_SHOTS];

// ====================== КНОПКИ ======================
const uint8_t buttonPins[4]   = {BTN_BLUE, BTN_RED, BTN_GREEN, BTN_YELLOW};
const ColorId buttonColors[4] = {BLUE_ID, RED_ID, GREEN_ID, YELLOW_ID};

bool buttonReading[4];
bool buttonStableState[4];
unsigned long buttonDebounceTime[4];
const unsigned long DEBOUNCE_MS = 25;

// ====================== СТАН ГРИ ======================
int score = 0;
uint8_t wounds = 0;
bool gameOver = false;

unsigned long lastSpawnTime = 0;
unsigned long lastEnemyStep = 0;
unsigned long lastShotStep  = 0;

unsigned long hurtFlashUntil = 0;
unsigned long explosionUntil = 0;
int explosionPos = -1;
CRGB explosionColor = CRGB::White;

unsigned long shipGlowUntil = 0;
CRGB shipGlowColor = CRGB::White;

// ====================== ЗВУКИ / МУЗИКА ======================
// Ноти в Гц. 0 = пауза
const uint16_t ambientNotes[]      = {196, 0, 220, 0, 247, 0, 220, 0, 175, 0, 147, 0};
const uint16_t ambientDurations[]  = {120, 30, 120, 30, 120, 30, 120, 60, 180, 40, 120, 60};

const uint16_t fireNotes[]         = {1175, 1568, 0};
const uint16_t fireDurations[]     = {50,   70,   20};

const uint16_t explosionNotes[]    = {1400, 1100, 800, 450, 0};
const uint16_t explosionDurations[]= {40,   50,   60,  80,  20};

const uint16_t wrongNotes[]        = {330, 220, 165, 110};
const uint16_t wrongDurations[]    = {90,  110, 140, 180};

const uint16_t damageNotes[]       = {262, 175, 131};
const uint16_t damageDurations[]   = {120, 120, 180};

const uint16_t gameOverNotes[]     = {523, 494, 440, 392, 349, 330, 294, 262, 196, 0, 196};
const uint16_t gameOverDurations[] = {150, 150, 150, 180, 180, 200, 220, 240, 350, 150, 600};

const uint16_t noTargetNotes[]     = {600, 0};
const uint16_t noTargetDurations[] = {30,  20};

const uint16_t* currentNotes = nullptr;
const uint16_t* currentDurations = nullptr;
uint8_t currentMelodyLength = 0;
uint8_t currentMelodyIndex = 0;
bool currentMelodyLoop = false;
bool resetAfterMelody = false;
unsigned long nextNoteTime = 0;

// ====================== ДОПОМІЖНІ ФУНКЦІЇ ======================
CRGB colorToCRGB(ColorId c) {
  switch (c) {
    case BLUE_ID:   return CRGB(0, 90, 255);
    case RED_ID:    return CRGB(255, 0, 0);
    case GREEN_ID:  return CRGB(0, 255, 70);
    case YELLOW_ID: return CRGB(255, 180, 0);
    default:        return CRGB::Black;
  }
}

uint8_t toPhysicalIndex(int logicalIndex) {
  if (!REVERSE_STRIP) return logicalIndex;
  return LED_COUNT - 1 - logicalIndex;
}

void addPixel(int logicalIndex, const CRGB &color) {
  if (logicalIndex < 0 || logicalIndex >= LED_COUNT) return;
  leds[toPhysicalIndex(logicalIndex)] += color;
}

template <size_t N>
void playMelody(const uint16_t (&notes)[N], const uint16_t (&durations)[N], bool loopMelody = false, bool doResetAfter = false) {
  currentNotes = notes;
  currentDurations = durations;
  currentMelodyLength = N;
  currentMelodyIndex = 0;
  currentMelodyLoop = loopMelody;
  resetAfterMelody = doResetAfter;
  nextNoteTime = 0;
}

void startAmbient() {
  if (!gameOver) {
    playMelody(ambientNotes, ambientDurations, true, false);
  }
}

void updateSound() {
  if (currentNotes == nullptr) {
    if (!gameOver) startAmbient();
    return;
  }

  unsigned long now = millis();
  if (now < nextNoteTime) return;

  if (currentMelodyIndex >= currentMelodyLength) {
    if (resetAfterMelody) {
      noTone(BUZZER_PIN);
      resetAfterMelody = false;

      // Повне перезавантаження гри
      score = 0;
      wounds = 0;
      gameOver = false;
      hurtFlashUntil = 0;
      explosionUntil = 0;
      explosionPos = -1;
      shipGlowUntil = 0;
      shipGlowColor = CRGB::White;
      lastSpawnTime = now;
      lastEnemyStep = now;
      lastShotStep  = now;

      for (uint8_t i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
      for (uint8_t i = 0; i < MAX_SHOTS; i++)   shots[i].active = false;

      startAmbient();
      return;
    }

    if (currentMelodyLoop) {
      currentMelodyIndex = 0;
    } else {
      currentNotes = nullptr;
      currentDurations = nullptr;
      currentMelodyLength = 0;
      currentMelodyIndex = 0;
      noTone(BUZZER_PIN);
      if (!gameOver) startAmbient();
      return;
    }
  }

  uint16_t note = currentNotes[currentMelodyIndex];
  uint16_t dur  = currentDurations[currentMelodyIndex];

  if (note == 0) {
    noTone(BUZZER_PIN);
  } else {
    tone(BUZZER_PIN, note, dur * 0.85);
  }

  nextNoteTime = now + dur;
  currentMelodyIndex++;
}

unsigned long getSpawnInterval() {
  int kills = score / 50;
  long val = 1100 - (kills * 35L);
  if (val < 280) val = 280;
  return val;
}

unsigned long getEnemyStepInterval() {
  int kills = score / 50;
  long val = 170 - (kills * 3L);
  if (val < 85) val = 85;
  return val;
}

unsigned long getShotStepInterval() {
  // Чим менше число, тим швидше летить постріл.
  // Було 55 мс. 15 мс робить постріл дуже швидким.
  return 15;
}

void updateDisplay() {
  int shown = score;
  if (shown > 9999) shown = 9999;
  display.showNumberDec(shown, true, 4, 0);
}

// ====================== ІГРОВА ЛОГІКА ======================
void clearEntities() {
  for (uint8_t i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
  for (uint8_t i = 0; i < MAX_SHOTS; i++)   shots[i].active = false;
}

bool spawnEnemy() {
  for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      enemies[i].active = true;
      enemies[i].pos = LED_COUNT - 1;
      enemies[i].color = (ColorId)random(0, 4);
      return true;
    }
  }
  return false;
}

bool spawnShot(ColorId c) {
  for (uint8_t i = 0; i < MAX_SHOTS; i++) {
    if (!shots[i].active) {
      shots[i].active = true;
      shots[i].pos = SHOT_START_POS;
      shots[i].color = c;
      return true;
    }
  }
  return false;
}

ColorId nearestEnemyColor() {
  int nearestPos = LED_COUNT + 1;
  ColorId foundColor = NO_COLOR;

  for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active && enemies[i].pos < nearestPos) {
      nearestPos = enemies[i].pos;
      foundColor = enemies[i].color;
    }
  }

  return foundColor;
}

void startGameOver() {
  gameOver = true;
  clearEntities();
  hurtFlashUntil = 0;
  explosionUntil = 0;
  explosionPos = -1;
  shipGlowUntil = 0;
  playMelody(gameOverNotes, gameOverDurations, false, true);
}

void loseLife(bool wrongButton) {
  if (gameOver) return;

  wounds++;
  hurtFlashUntil = millis() + 220;
  shipGlowUntil = millis() + 180;
  shipGlowColor = CRGB::Red;

  if (wounds >= DAMAGE_LIMIT) {
    startGameOver();
    return;
  }

  if (wrongButton) {
    playMelody(wrongNotes, wrongDurations, false, false);
  } else {
    playMelody(damageNotes, damageDurations, false, false);
  }
}

void checkCollisions() {
  for (uint8_t s = 0; s < MAX_SHOTS; s++) {
    if (!shots[s].active) continue;

    for (uint8_t e = 0; e < MAX_ENEMIES; e++) {
      if (!enemies[e].active) continue;

      // Постріл знищує тільки противника того ж кольору
      if (shots[s].color == enemies[e].color) {
        int dist = shots[s].pos - enemies[e].pos;
        if (dist >= -1 && dist <= 1) {
          ColorId hitColor = shots[s].color;
          int hitPos = (shots[s].pos + enemies[e].pos) / 2;

          shots[s].active = false;
          enemies[e].active = false;

          score += 50;
          explosionPos = hitPos;
          explosionColor = colorToCRGB(hitColor);
          explosionUntil = millis() + 140;

          playMelody(explosionNotes, explosionDurations, false, false);
          break;
        }
      }
    }
  }
}

void moveEnemies() {
  for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;

    enemies[i].pos--;

    if (enemies[i].pos <= PLAYER_ZONE_END) {
      enemies[i].active = false;
      loseLife(false);
    }
  }

  checkCollisions();
}

void moveShots() {
  for (uint8_t i = 0; i < MAX_SHOTS; i++) {
    if (!shots[i].active) continue;

    shots[i].pos++;

    if (shots[i].pos >= LED_COUNT) {
      shots[i].active = false;
    }
  }

  checkCollisions();
}

void handleButtonPress(ColorId pressedColor) {
  if (gameOver) return;

  shipGlowUntil = millis() + 120;
  shipGlowColor = colorToCRGB(pressedColor);

  ColorId targetColor = nearestEnemyColor();

  // Якщо ворогів нема - просто короткий "холостий" звук, без штрафу
  if (targetColor == NO_COLOR) {
    playMelody(noTargetNotes, noTargetDurations, false, false);
    return;
  }

  // Якщо натиснули не той колір - мінус життя
  if (pressedColor != targetColor) {
    loseLife(true);
    return;
  }

  // Правильний колір - постріл
  if (spawnShot(pressedColor)) {
    playMelody(fireNotes, fireDurations, false, false);
  }
}

void updateButtons() {
  unsigned long now = millis();

  for (uint8_t i = 0; i < 4; i++) {
    bool rawPressed = (digitalRead(buttonPins[i]) == LOW);

    if (rawPressed != buttonReading[i]) {
      buttonReading[i] = rawPressed;
      buttonDebounceTime[i] = now;
    }

    if ((now - buttonDebounceTime[i]) > DEBOUNCE_MS) {
      if (buttonStableState[i] != rawPressed) {
        buttonStableState[i] = rawPressed;

        if (buttonStableState[i]) {
          handleButtonPress(buttonColors[i]);
        }
      }
    }
  }
}

void updateGame() {
  if (gameOver) return;

  unsigned long now = millis();

  if (now - lastSpawnTime >= getSpawnInterval()) {
    spawnEnemy();
    lastSpawnTime = now;
  }

  if (now - lastEnemyStep >= getEnemyStepInterval()) {
    moveEnemies();
    lastEnemyStep = now;
  }

  if (now - lastShotStep >= getShotStepInterval()) {
    moveShots();
    lastShotStep = now;
  }
}

// ====================== ВІДМАЛЬОВУВАННЯ ======================
void drawLivesAndShip() {
  // Життя: 3 точки на початку стрічки
  for (uint8_t i = 0; i < 3; i++) {
    if (i < (DAMAGE_LIMIT - wounds)) {
      addPixel(i, CRGB(40, 40, 40));
    } else {
      addPixel(i, CRGB(80, 0, 0));
    }
  }

  // Гармата / корабель
  CRGB cannon = CRGB(10, 10, 25);
  if (millis() < shipGlowUntil) cannon = shipGlowColor;
  if (millis() < hurtFlashUntil) cannon = CRGB::Red;

  addPixel(3, cannon);
}

void drawEnemies() {
  for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;

    CRGB c = colorToCRGB(enemies[i].color);
    CRGB tail = c;
    tail.nscale8(80);

    addPixel(enemies[i].pos, c);
    addPixel(enemies[i].pos + 1, tail);
  }
}

void drawShots() {
  for (uint8_t i = 0; i < MAX_SHOTS; i++) {
    if (!shots[i].active) continue;

    CRGB c = colorToCRGB(shots[i].color);
    CRGB tail = c;
    tail.nscale8(100);

    addPixel(shots[i].pos, c);
    addPixel(shots[i].pos - 1, tail);
  }
}

void drawExplosion() {
  if (millis() >= explosionUntil || explosionPos < 0) return;

  addPixel(explosionPos, CRGB::White);

  CRGB side = explosionColor;
  side.nscale8(140);
  addPixel(explosionPos - 1, side);
  addPixel(explosionPos + 1, side);
}

void renderGame() {
  fill_solid(leds, LED_COUNT, CRGB::Black);

  if (gameOver) {
    bool blink = ((millis() / 150) % 2 == 0);
    if (blink) {
      for (int i = 0; i < LED_COUNT; i++) {
        addPixel(i, CRGB(100, 0, 0));
      }
    }
    updateDisplay();
    FastLED.show();
    return;
  }

  drawLivesAndShip();
  drawEnemies();
  drawShots();
  drawExplosion();

  if (millis() < hurtFlashUntil) {
    addPixel(0, CRGB::Red);
    addPixel(1, CRGB::Red);
    addPixel(2, CRGB::Red);
    addPixel(3, CRGB::Red);
  }

  updateDisplay();
  FastLED.show();
}

// ====================== SETUP / LOOP ======================
void setup() {
  pinMode(BUZZER_PIN, OUTPUT);

  for (uint8_t i = 0; i < 4; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    buttonReading[i] = (digitalRead(buttonPins[i]) == LOW);
    buttonStableState[i] = buttonReading[i];
    buttonDebounceTime[i] = 0;
  }

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  display.setBrightness(0x0f);
  updateDisplay();

  randomSeed(analogRead(A0));

  clearEntities();
  score = 0;
  wounds = 0;
  gameOver = false;

  lastSpawnTime = millis();
  lastEnemyStep = millis();
  lastShotStep  = millis();

  startAmbient();
}

void loop() {
  updateButtons();
  updateGame();
  updateSound();
  renderGame();
}