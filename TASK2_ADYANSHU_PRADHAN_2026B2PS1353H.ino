#include <LiquidCrystal.h>

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int PIN_TRIG   = 9;
const int PIN_ECHO   = 10;
const int PIN_LDR    = A0;
const int PIN_BUTTON = 8;
const int PIN_LED    = 7;
const int PIN_BUZZER = 6;

// THRESHOLDS
const int   LIGHT_THRESHOLD_PCT   = 92; // LDR doesn't have a linear relationship with voltage, this is approx 50% on slider
const float DIST_THRESHOLD_CM     = 100;
const unsigned long DANGER_TIME_MS    = 5000;
const unsigned long BLINK_INTERVAL_MS = 300;
const unsigned long DEBOUNCE_MS       = 50;

// CUSTOM LCD CHARACTERS

byte anchorIcon[8] = {
  B00100, B01110, B00100, B10101,
  B10101, B01010, B00100, B00000
};
byte skullIcon[8] = {
  B01110, B10101, B11011, B01110,
  B01110, B10101, B01010, B00000
};
byte shipLeft[8] = {
  B00010, B00110, B01110, B11110, 
  B00010, B10010, B11111, B01111
};

byte shipRight[8] = {
  B00000, B00000, B00000, B00000, 
  B00000, B00001, B11111, B11110
};
const int CHAR_ANCHOR   = 0;
const int CHAR_SKULL    = 1;
const int CHAR_SHIP_L   = 2;
const int CHAR_SHIP_R   = 3;

// STATE DEFINITION

enum ShipState { OPEN_SEA, ANCHOR_DROPPED, STORM, CHARYBDIS, WRECKED };
ShipState currentState = OPEN_SEA;

// Forward declarations (Arduino's auto-prototyping runs before the enum
// below is known, so we declare these ourselves to avoid "ShipState was
// not declared in this scope" compile errors).
void toggleAnchor();
void enterWrecked();
void runStateEffects();
float readDistanceCm();
int readLightPercent();
bool readButtonPressed();
void updateLCD();
void playWreckJingle();
void bootAnimation();

// Our danger flags
bool stormActive     = false;
bool charybdisActive = false;
bool wasInDanger      = false;   // was any danger active last loop pass?

unsigned long dangerStartTime = 0; // set ONCE when entering danger from OPEN_SEA;
                                    // never reset while any danger persists
unsigned long lastBlinkTime   = 0;
bool ledOn = false;  // lastBlinkTime and ledOn are used to make the LED blink at a steady rate

// Debouncing logic
unsigned long lastButtonCheck = 0;
int  lastButtonReading  = HIGH;
int  stableButtonState  = HIGH;
bool anchorDropped      = false;

// SETUP
void setup() {
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  lcd.begin(16, 2);
  lcd.createChar(CHAR_ANCHOR, anchorIcon);
  lcd.createChar(CHAR_SKULL, skullIcon);
  lcd.createChar(CHAR_SHIP_L, shipLeft);
  lcd.createChar(CHAR_SHIP_R, shipRight);

  bootAnimation();
  updateLCD();
}

// MAIN STUFF
void loop() {
  if (currentState == WRECKED) {
    digitalWrite(PIN_LED, HIGH);
    updateLCD();
    return;
  }

  bool buttonPressed = readButtonPressed();
  float distanceCm   = readDistanceCm();
  int   lightPct     = readLightPercent();

  bool stormCondition     = lightPct < LIGHT_THRESHOLD_PCT;
  bool charybdisCondition = distanceCm < DIST_THRESHOLD_CM;

  if (buttonPressed) {
    toggleAnchor();
  }

  if (anchorDropped) {
    currentState = ANCHOR_DROPPED;
    // Anchor cancels all danger tracking cleanly.
    stormActive = false;
    charybdisActive = false;
    wasInDanger = false;
    updateLCD();
    return;
  }

  // Update independent danger flags every loop pass
  stormActive     = stormCondition;
  charybdisActive = charybdisCondition;
  bool inDangerNow = stormActive || charybdisActive;

  if (inDangerNow) {
    if (!wasInDanger) {
      // Freshly entering danger from OPEN_SEA -- this is the ONLY place
      // the timer starts.
      dangerStartTime = millis();
    }
    // If we were already in danger last loop, dangerStartTime is left
    // untouched, whether the same calamity continues, a second one
    // joins, or one calamity is replaced by the other, the timer keeps
    // counting from when danger FIRST began. This is what satisfies
    // "the timer shouldn't reset, just continue."

    if (millis() - dangerStartTime >= DANGER_TIME_MS) {
      enterWrecked();
      wasInDanger = inDangerNow;
      return;
    }

    // currentState still reflects a valid enum value for any other logic
    // that reads it; display distinguishes the combined case separately.
    currentState = stormActive ? STORM : CHARYBDIS;

  } else {
    currentState = OPEN_SEA;
    digitalWrite(PIN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    noTone(PIN_BUZZER);
  }

  wasInDanger = inDangerNow;

  runStateEffects();
  updateLCD();
}

// STATE TRANSITIONS
void enterWrecked() {
  currentState = WRECKED;
  playWreckJingle();
}

void toggleAnchor() {
  anchorDropped = !anchorDropped;
  if (anchorDropped) {
    currentState = ANCHOR_DROPPED;
    digitalWrite(PIN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    noTone(PIN_BUZZER);
    tone(PIN_BUZZER, 1200, 80);
  } else {
    currentState = OPEN_SEA;
    tone(PIN_BUZZER, 900, 80);
  }
}

// STATE EFFECTS -- now driven by the independent flags, so both alarms
// can run at once instead of only whichever "state" is active.
void runStateEffects() {
  if (currentState == OPEN_SEA) {
    digitalWrite(PIN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  // Storm effect: blinking LED
  if (stormActive) {
    if (millis() - lastBlinkTime >= BLINK_INTERVAL_MS) {
      lastBlinkTime = millis();
      ledOn = !ledOn;
      digitalWrite(PIN_LED, ledOn ? HIGH : LOW);
    }
  } else {
    digitalWrite(PIN_LED, LOW);
  }

  // Charybdis effect: rising/falling siren
  if (charybdisActive) {
    int pitch = 600 + (int)(200 * sin(millis() / 150.0));
    tone(PIN_BUZZER, pitch);
  } else {
    noTone(PIN_BUZZER);
  }
}

// BOOT ANIMATION
void bootAnimation() {
  lcd.setCursor(0, 0);
  lcd.print("Owl Guides Home");
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) {
    lcd.setCursor(i, 1);
    lcd.print("#");
    tone(PIN_BUZZER, 400 + i * 40, 15);
    delay(80);
  }
  delay(400);
  lcd.clear();
}

// WRECK JINGLE
void playWreckJingle() {
  int notes[] = { 880, 740, 660, 587, 494, 392 };
  for (int i = 0; i < 6; i++) {
    tone(PIN_BUZZER, notes[i], 180);
    delay(190);
  }
  noTone(PIN_BUZZER);
}

// SENSOR READS
float readDistanceCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duration = pulseIn(PIN_ECHO, HIGH, 30000);
  if (duration == 0) return 9999;
  return duration * 0.0343 / 2.0;
}

int readLightPercent() {
  int raw = analogRead(PIN_LDR);
  return map(raw, 0, 1023, 0, 100);
}

bool readButtonPressed() {
  int reading = digitalRead(PIN_BUTTON);
  if (reading != lastButtonReading) {
    lastButtonCheck = millis();
  }
  bool pressedEdge = false;
  if (millis() - lastButtonCheck > DEBOUNCE_MS) {
    if (reading != stableButtonState) {
      stableButtonState = reading;
      if (stableButtonState == LOW) pressedEdge = true;
    }
  }
  lastButtonReading = reading;
  return pressedEdge;
}

// LCD DISPLAY
void updateLCD() {
  if (currentState == OPEN_SEA) {
    lcd.setCursor(0, 0);
    lcd.print(" ");
    lcd.write(byte(CHAR_SHIP_L));
    lcd.print(" ");
    lcd.print("OPEN SEA");
    lcd.print(" ");
    lcd.write(byte(CHAR_SHIP_R));
    lcd.print("  ");
  for (int offset = 0; offset < 4; offset++) {

    lcd.setCursor(0, 1);

    for (int x = 0; x < 16; x++) {
      int p = (x + offset) % 4;

      if (p == 0)
        lcd.print("~");
      else
        lcd.print(" ");
    }

    delay(150);
  }
    return;
  }

  lcd.setCursor(0, 0);
  switch (currentState) {
    case ANCHOR_DROPPED:
      lcd.write(byte(CHAR_ANCHOR));
      lcd.print(" ANCHORED       ");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      break;
    case STORM:
    case CHARYBDIS:
      if (stormActive && charybdisActive) {
        lcd.print("STORM+CHARYBDIS!"); // exactly 16 chars, fits perfectly
      } else if (stormActive) {
        lcd.print("!! STORM !!     ");
      } else {
        lcd.print("!! CHARYBDIS !! ");
      }
      break;
    case WRECKED:
      lcd.write(byte(CHAR_SKULL));
      lcd.print(" WRECKED        ");
      break;
    default:
      break;
  }

  lcd.setCursor(0, 1);
  if (currentState == STORM || currentState == CHARYBDIS) {
    unsigned long elapsed = millis() - dangerStartTime;
    long remainingMs = (long)DANGER_TIME_MS - (long)elapsed;
    if (remainingMs < 0) remainingMs = 0;
    lcd.print("Wreck in: ");
    lcd.print(remainingMs / 1000.0, 1);
    lcd.print("s  ");
  } else if (currentState == WRECKED) {
    lcd.print("RESTART SIM!    ");
  }
}
