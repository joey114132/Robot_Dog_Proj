#include <LiquidCrystal.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <math.h>

/* ================= HARDWARE CONFIGURATION ================= */
LiquidCrystal lcd(5, 4, 3, 2, 11, 12);

const int joyX = A0;
const int joyY = A1;
const int joySW = 10;

// DS1302 RTC: CE=D6, IO=D7, SCLK=D8
const int DS_CE = 6;
const int DS_IO = 7;
const int DS_SCLK = 8;
ThreeWire myWire(DS_IO, DS_SCLK, DS_CE);
RtcDS1302<ThreeWire> rtc(myWire);

/* ================= CUSTOM CHARACTERS (EMOTIONS) ================= */
byte faceHappy[8]   = {B00000, B01010, B00000, B00000, B10001, B01110, B00000, B00000};
byte faceNeutral[8] = {B00000, B01010, B00000, B00000, B11111, B00000, B00000, B00000};
byte faceSad[8]     = {B00000, B01010, B00000, B00000, B01110, B10001, B00000, B00000};
byte faceSleep[8]   = {B00000, B00000, B00111, B00010, B00111, B00000, B00000, B00000};

/* ================= DOG STATE ================= */
String dogName = "DogBot";
int emotion = 1;  // 0=happy, 1=neutral, 2=sad, 3=sleep
int bond = 50;    // 0–100

/* ================= SLEEP SCHEDULE ================= */
bool isSleepTime = false;

bool inNightSleep(int h) {
  return (h >= 22 || h < 7);
}

bool inDayNap(int h) {
  return (h >= 13 && h < 15);
}

/* ================= MESSAGE PAGING ================= */
const int MAX_MSG_LEN = 160;
const int MAX_CHUNKS = 16;
String chunks[MAX_CHUNKS];
int chunkCount = 1;
int currentChunk = 0;

unsigned long lastAutoScroll = 0;
const unsigned long AUTO_SCROLL_MS = 3000;  // 3 seconds per page

/* ================= JOYSTICK CONFIGURATION ================= */
int xCenter = 512;
int yCenter = 512;

const int DEADZONE = 50;
const int ACTIVATION = 200;

const int CENTER_LOW = 430;
const int CENTER_HIGH = 594;

bool joystickNeutralRaw(int xv, int yv) {
  return (xv >= CENTER_LOW && xv <= CENTER_HIGH && 
          yv >= CENTER_LOW && yv <= CENTER_HIGH);
}

int joystickRadius(int xv, int yv) {
  int dx = xv - 512;
  int dy = yv - 512;
  return (int)sqrt((float)(dx * dx + dy * dy));
}

int dirX(int xv) {
  int dx = xv - xCenter;
  if (abs(dx) < DEADZONE) return 0;
  return (dx > 0) ? +1 : -1;
}

int dirY(int yv) {
  int dy = yv - yCenter;
  if (abs(dy) < DEADZONE) return 0;
  return (dy > 0) ? +1 : -1;
}

int magX(int xv) {
  return abs(xv - xCenter);
}

int magY(int yv) {
  return abs(yv - yCenter);
}

void updateCenterIfNeutral(int xv, int yv) {
  if (abs(xv - xCenter) < DEADZONE && abs(yv - yCenter) < DEADZONE) {
    xCenter = (xCenter * 15 + xv) / 16;
    yCenter = (yCenter * 15 + yv) / 16;
  }
}

/* ================= GLOBAL ACTION LOCK ================= */
bool actionLocked = false;
unsigned long actionLockTime = 0;
const unsigned long ACTION_MIN_GAP_MS = 800;  // Slightly reduced gap

void lockActions() {
  actionLocked = true;
  actionLockTime = millis();
}

void unlockIfNeutral() {
  int xv = analogRead(joyX);
  int yv = analogRead(joyY);
  updateCenterIfNeutral(xv, yv);

  static unsigned long neutralStart = 0;
  bool isNeutral = joystickNeutralRaw(xv, yv) && 
                   (abs(xv - xCenter) < DEADZONE) && 
                   (abs(yv - yCenter) < DEADZONE);

  if (!isNeutral) {
    neutralStart = 0;
    return;
  }

  if (neutralStart == 0) {
    neutralStart = millis();
  }

  if (millis() - neutralStart >= 250 && 
      millis() - actionLockTime >= ACTION_MIN_GAP_MS) {
    actionLocked = false;
  }
}

/* ================= GESTURE STATE TRACKING ================= */
enum GestureState {
  GESTURE_NONE = 0,
  GESTURE_THROW,
  GESTURE_SCRATCH,
  GESTURE_HOLD
};

GestureState activeGesture = GESTURE_NONE;

/* ================= THROW GESTURE ================= */
bool throwArmed = false;
unsigned long throwArmTime = 0;
const unsigned long THROW_WINDOW_MS = 1500;
const int THROW_X_DRIFT_MAX = 150;

/* ================= SCRATCH GESTURE ================= */
bool scratchArmed = false;
unsigned long scratchArmTime = 0;
const unsigned long SCRATCH_WINDOW_MS = 2000;
const int SCRATCH_Y_DRIFT_MAX = 150;

/* ================= HOLD GESTURE ================= */
unsigned long holdStartTime = 0;
int holdDir = 0;
bool holdFired = false;
const unsigned long HOLD_DURATION_MS = 600;
const int HOLD_RADIUS_MAX = 200;

/* ================= STATUS BUTTON ================= */
bool swLatch = false;
unsigned long lastSwPress = 0;
const unsigned long SW_DEBOUNCE_MS = 140;

/* ================= RANDOM BEHAVIOR ================= */
unsigned long lastRandomEvent = 0;
unsigned long randomInterval = 15000;
const unsigned long RAND_INTERVAL_MIN = 8000;
const unsigned long RAND_INTERVAL_MAX = 22000;

/* ================= INACTIVITY TRACKING ================= */
unsigned long lastInteraction = 0;
unsigned long lastIdleTick = 0;
const unsigned long IDLE_DECAY_MS = 10000;

/* ================= DIAGNOSTIC MODE ================= */
bool diagnosticMode = true;
unsigned long lastDiagPrint = 0;

/* ================= UTILITY FUNCTIONS ================= */
void ensureRtcValid() {
  RtcDateTime now = rtc.GetDateTime();
  if (!now.IsValid() || now.Year() < 2024 || now.Year() > 2030) {
    rtc.SetDateTime(RtcDateTime(__DATE__, __TIME__));
  }
}

unsigned long randBetween(unsigned long a, unsigned long b) {
  return a + (unsigned long)random((long)(b - a + 1));
}

String fit16(String s) {
  if (s.length() > 16) s = s.substring(0, 16);
  while (s.length() < 16) s += " ";
  return s;
}

void buildChunks(String msg) {
  msg.replace("\r", "");
  msg.replace("\n", " ");
  msg.trim();
  
  if (msg.length() == 0) msg = "...";
  if (msg.length() > MAX_MSG_LEN) {
    msg = msg.substring(0, MAX_MSG_LEN);
  }

  for (int i = 0; i < MAX_CHUNKS; i++) chunks[i] = "";
  chunkCount = 0;
  currentChunk = 0;

  int pos = 0;
  while (pos < (int)msg.length() && chunkCount < MAX_CHUNKS) {
    while (pos < (int)msg.length() && msg[pos] == ' ') pos++;
    if (pos >= (int)msg.length()) break;

    String line = "";
    
    while (pos < (int)msg.length()) {
      while (pos < (int)msg.length() && msg[pos] == ' ') pos++;
      if (pos >= (int)msg.length()) break;

      int wordStart = pos;
      while (pos < (int)msg.length() && msg[pos] != ' ') pos++;
      String word = msg.substring(wordStart, pos);

      if (word.length() > 16) {
        if (line.length() > 0) {
          chunks[chunkCount++] = line;
          line = "";
          if (chunkCount >= MAX_CHUNKS) break;
        }
        chunks[chunkCount++] = word;
        if (chunkCount >= MAX_CHUNKS) break;
        continue;
      }

      String candidate = line;
      if (candidate.length() > 0) candidate += " ";
      candidate += word;

      if (candidate.length() <= 16) {
        line = candidate;
      } else {
        if (line.length() > 0) {
          chunks[chunkCount++] = line;
          if (chunkCount >= MAX_CHUNKS) break;
        }
        line = word;
      }
    }

    if (chunkCount >= MAX_CHUNKS) break;
    if (line.length() > 0) {
      chunks[chunkCount++] = line;
    }
  }

  if (chunkCount == 0) {
    chunks[0] = "...";
    chunkCount = 1;
  }
}

/* ================= DISPLAY FUNCTIONS ================= */
void drawTopBar() {
  lcd.setCursor(0, 0);
  
  String name = dogName;
  if (name.length() > 8) name = name.substring(0, 8);
  while (name.length() < 8) name += " ";
  lcd.print(name);
  
  lcd.print(" ");
  
  if (emotion == 0) lcd.write(byte(0));
  else if (emotion == 1) lcd.write(byte(1));
  else if (emotion == 2) lcd.write(byte(2));
  else lcd.write(byte(3));
  
  lcd.print(" ");
  
  bond = constrain(bond, 0, 100);
  String bondStr = String(bond) + "%";
  while (bondStr.length() < 5) bondStr += " ";
  if (bondStr.length() > 5) bondStr = bondStr.substring(0, 5);
  lcd.print(bondStr);
}

void drawLine2(String text) {
  lcd.setCursor(0, 1);
  
  static unsigned long lastScrollTime = 0;
  static int scrollPos = 0;
  static String lastText = "";
  static bool isPaused = false;
  
  if (text != lastText) {
    scrollPos = 0;
    lastScrollTime = millis();
    lastText = text;
    isPaused = false;
  }
  
  if (text.length() <= 16) {
    lcd.print(fit16(text));
    return;
  }
  
  unsigned long now = millis();
  
  if (isPaused) {
    if (now - lastScrollTime >= 2000) {
      scrollPos = 0;
      isPaused = false;
      lastScrollTime = now;
    }
  } else {
    if (now - lastScrollTime >= 350) {  // Slightly faster smooth scroll
      lastScrollTime = now;
      scrollPos++;
      
      if (scrollPos > (int)text.length() - 16) {
        scrollPos = (int)text.length() - 16;
        isPaused = true;
        lastScrollTime = now;
      }
    }
  }
  
  String window = text.substring(scrollPos, min(scrollPos + 16, (int)text.length()));
  while (window.length() < 16) window += " ";
  lcd.print(window);
}

void drawMessagePage() {
  if (currentChunk < 0) currentChunk = 0;
  if (currentChunk >= chunkCount) currentChunk = chunkCount - 1;
  drawLine2(chunks[currentChunk]);
}

void handleAutoScroll() {
  if (chunkCount <= 1) return;

  unsigned long now = millis();
  if (now - lastAutoScroll >= AUTO_SCROLL_MS) {
    lastAutoScroll = now;
    currentChunk = (currentChunk + 1) % chunkCount;
    drawMessagePage();
  }
}

void showActionCue(const char* line1, const char* line2, unsigned long duration = 500) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(fit16(String(line1)));
  lcd.setCursor(0, 1);
  lcd.print(fit16(String(line2)));
  delay(duration);

  // Reduced lock time - actions can interrupt sooner
  lockActions();

  lcd.clear();
  drawTopBar();
  drawMessagePage();
}

/* ================= SERIAL COMMUNICATION ================= */
void sendEvent(const char* event) {
  Serial.print("EVENT:");
  Serial.println(event);
  Serial.flush();
}

void handleSerial() {
  if (Serial.available() <= 0) return;
  
  String data = Serial.readStringUntil('\n');
  data.trim();
  if (data.length() == 0) return;

  Serial.print("DEBUG:Received:");
  Serial.println(data);

  if (data.startsWith("STAT:")) {
    String payload = data.substring(5);
    int pipe1 = payload.indexOf('|');
    int pipe2 = payload.indexOf('|', pipe1 + 1);
    
    if (pipe1 > 0 && pipe2 > pipe1) {
      bond = payload.substring(0, pipe1).toInt();
      emotion = payload.substring(pipe1 + 1, pipe2).toInt();
      dogName = payload.substring(pipe2 + 1);
    }
    
    drawTopBar();
    drawMessagePage();
  }
  else if (data.startsWith("MSG:")) {
    String message = data.substring(4);
    Serial.print("DEBUG:Message:");
    Serial.println(message);
    
    buildChunks(message);
    currentChunk = 0;
    drawMessagePage();
    lastAutoScroll = millis();
  }
}

void updateSleepState() {
  RtcDateTime now = rtc.GetDateTime();
  if (!now.IsValid()) {
    ensureRtcValid();
    return;
  }

  int hour = now.Hour();
  bool shouldSleep = inNightSleep(hour) || inDayNap(hour);

  if (shouldSleep != isSleepTime) {
    isSleepTime = shouldSleep;
    Serial.print("STATE:");
    Serial.println(isSleepTime ? "SLEEP" : "AWAKE");
    Serial.flush();
  }
}

void markInteraction() {
  lastInteraction = millis();
  lastIdleTick = lastInteraction;
}

void handleIdleDecay() {
  unsigned long now = millis();
  if (now - lastInteraction >= IDLE_DECAY_MS &&
      now - lastIdleTick >= IDLE_DECAY_MS) {
    lastIdleTick = now;
    sendEvent("IDLE10");
  }
}

/* ================= DIAGNOSTIC PRINT ================= */
void printDiagnostics() {
  if (!diagnosticMode) return;
  
  unsigned long now = millis();
  if (now - lastDiagPrint < 500) return;
  lastDiagPrint = now;
  
  int xv = analogRead(joyX);
  int yv = analogRead(joyY);
  int radius = joystickRadius(xv, yv);
  
  Serial.print("DEBUG:Joy X=");
  Serial.print(xv);
  Serial.print(" Y=");
  Serial.print(yv);
  Serial.print(" R=");
  Serial.print(radius);
  Serial.print(" Lock=");
  Serial.print(actionLocked ? "YES" : "NO");
  Serial.println("");
}

/* ================= GESTURE HANDLERS ================= */
void handleThrowGesture() {
  if (actionLocked) return;

  int xv = analogRead(joyX);
  int yv = analogRead(joyY);
  updateCenterIfNeutral(xv, yv);

  if (magX(xv) > THROW_X_DRIFT_MAX) {
    if (activeGesture == GESTURE_THROW) {
      activeGesture = GESTURE_NONE;
      throwArmed = false;
    }
    return;
  }

  if (activeGesture != GESTURE_NONE && activeGesture != GESTURE_THROW) return;

  int yDir = dirY(yv);
  int yMag = magY(yv);
  unsigned long now = millis();

  if (!throwArmed) {
    if (yDir == 1 && yMag > ACTIVATION) {
      throwArmed = true;
      throwArmTime = now;
      activeGesture = GESTURE_THROW;
      Serial.println("DEBUG:THROW armed (down)");
    }
    return;
  }

  if (now - throwArmTime > THROW_WINDOW_MS) {
    throwArmed = false;
    activeGesture = GESTURE_NONE;
    return;
  }

  if (yDir == -1 && yMag > ACTIVATION) {
    throwArmed = false;
    activeGesture = GESTURE_NONE;
    showActionCue("THROW!", "GO FETCH!", 500);
    sendEvent("THROW");
    markInteraction();
  }
}

void handleScratchGesture() {
  if (actionLocked) return;

  int xv = analogRead(joyX);
  int yv = analogRead(joyY);
  updateCenterIfNeutral(xv, yv);

  if (magY(yv) > SCRATCH_Y_DRIFT_MAX) {
    if (activeGesture == GESTURE_SCRATCH) {
      activeGesture = GESTURE_NONE;
      scratchArmed = false;
    }
    return;
  }

  if (activeGesture != GESTURE_NONE && activeGesture != GESTURE_SCRATCH) return;

  int xDir = dirX(xv);
  int xMag = magX(xv);
  unsigned long now = millis();

  if (!scratchArmed) {
    if (xDir == 1 && xMag > ACTIVATION) {
      scratchArmed = true;
      scratchArmTime = now;
      activeGesture = GESTURE_SCRATCH;
      Serial.println("DEBUG:SCRATCH armed (right)");
    }
    return;
  }

  if (now - scratchArmTime > SCRATCH_WINDOW_MS) {
    scratchArmed = false;
    activeGesture = GESTURE_NONE;
    return;
  }

  if (xDir == -1 && xMag > ACTIVATION) {
    scratchArmed = false;
    activeGesture = GESTURE_NONE;
    showActionCue("SCRATCH!", "EAR RUB!", 500);
    sendEvent("SCRATCH");
    markInteraction();
  }
}

void handleHoldGestures() {
  if (actionLocked) return;

  int xv = analogRead(joyX);
  int yv = analogRead(joyY);
  updateCenterIfNeutral(xv, yv);

  int radius = joystickRadius(xv, yv);
  if (radius > HOLD_RADIUS_MAX) {
    if (activeGesture == GESTURE_HOLD) activeGesture = GESTURE_NONE;
    holdDir = 0;
    holdFired = false;
    return;
  }

  if (activeGesture != GESTURE_NONE && activeGesture != GESTURE_HOLD) return;

  int yDir = dirY(yv);
  int yMag = magY(yv);

  if (yMag < ACTIVATION) {
    if (activeGesture == GESTURE_HOLD) activeGesture = GESTURE_NONE;
    holdDir = 0;
    holdFired = false;
    return;
  }

  unsigned long now = millis();

  if (holdDir == 0) {
    holdDir = yDir;
    holdStartTime = now;
    holdFired = false;
    activeGesture = GESTURE_HOLD;
    return;
  }

  if (yDir != holdDir) {
    holdDir = yDir;
    holdStartTime = now;
    holdFired = false;
    return;
  }

  if (!holdFired && (now - holdStartTime >= HOLD_DURATION_MS)) {
    holdFired = true;
    activeGesture = GESTURE_NONE;

    if (holdDir == 1) {
      showActionCue("SIT!", "GOOD BOY!", 500);
      sendEvent("SIT");
    } else {
      showActionCue("TREAT!", "YUM!", 500);
      sendEvent("TREAT");
    }
    markInteraction();
  }
}

void handleStatusButton() {
  if (actionLocked) return;

  bool pressed = (digitalRead(joySW) == LOW);
  unsigned long now = millis();

  if (pressed && !swLatch && (now - lastSwPress > SW_DEBOUNCE_MS)) {
    swLatch = true;
    lastSwPress = now;
    
    showActionCue("STATUS...", "TELL ME!", 400);
    sendEvent("STATUS");
    markInteraction();
  }

  if (!pressed) {
    swLatch = false;
  }
}

void handleRandomBehavior() {
  if (isSleepTime) return;
  if (actionLocked) return;

  unsigned long now = millis();
  if (now - lastRandomEvent >= randomInterval) {
    lastRandomEvent = now;
    
    if (bond < 25) {
      randomInterval = randBetween(RAND_INTERVAL_MAX, RAND_INTERVAL_MAX * 2);
    } else {
      randomInterval = randBetween(RAND_INTERVAL_MIN, RAND_INTERVAL_MAX);
    }
    
    sendEvent("RAND");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(joySW, INPUT_PULLUP);

  lcd.begin(16, 2);
  lcd.createChar(0, faceHappy);
  lcd.createChar(1, faceNeutral);
  lcd.createChar(2, faceSad);
  lcd.createChar(3, faceSleep);

  rtc.Begin();
  if (!rtc.GetIsRunning()) {
    rtc.SetIsRunning(true);
  }
  ensureRtcValid();

  randomSeed(analogRead(A2));

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DOGBOT BOOT     ");
  lcd.setCursor(0, 1);
  lcd.print("Ready...        ");
  delay(650);

  buildChunks("Down-Up=Throw R>L=Scratch");
  lcd.clear();
  drawTopBar();
  drawMessagePage();

  lastInteraction = millis();
  lastIdleTick = lastInteraction;
  lastRandomEvent = lastInteraction;

  Serial.println("HELLO");
  Serial.flush();
}

void loop() {
  handleSerial();
  updateSleepState();

  if (actionLocked) {
    unlockIfNeutral();
  }

  drawTopBar();
  drawMessagePage();
  handleAutoScroll();

  printDiagnostics();

  handleThrowGesture();
  handleScratchGesture();
  handleHoldGestures();
  handleStatusButton();

  handleRandomBehavior();
  handleIdleDecay();
}