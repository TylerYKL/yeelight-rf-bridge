import io

p = r"C:\Users\TY\Documents\Doubao\YEELIGHT\yeelight_rf_bridge_v2.ino"
with io.open(p, "r", encoding="utf-8") as f:
    c = f.read()

# 1. Replace capture section
old = """// ==================== Learn / capture ====================
volatile unsigned long learnLastTime = 0;
volatile uint16_t learnBuf[300];
volatile int learnLen = 0;
volatile bool learnDone = false;

void IRAM_ATTR learnIsr() {
  unsigned long now = micros();
  unsigned long dur = now - learnLastTime;
  learnLastTime = now;
  if (dur < 50) return;
  if (dur > 8000) learnDone = true;
  else if (learnLen < 300) learnBuf[learnLen++] = (uint16_t)dur;
}

String captureFrame() { return captureFrameTimeout(cfgCaptureMs); }

String captureFrameTimeout(int timeoutMs) {
  noInterrupts();
  learnLen = 0; learnDone = false;
  learnLastTime = micros();
  interrupts();
  attachInterrupt(digitalPinToInterrupt(RX_PIN), learnIsr, CHANGE);
  unsigned long t0 = millis();
  while (!learnDone && millis() - t0 < (unsigned long)timeoutMs) delay(2);
  detachInterrupt(digitalPinToInterrupt(RX_PIN));
  noInterrupts();
  int len = learnLen;
  uint16_t copy[300];
  for (int i = 0; i < len; i++) copy[i] = learnBuf[i];
  interrupts();
  if (len < 4) return "";
  String s = "[";
  for (int i = 0; i < len; i++) { if (i) s += ","; s += copy[i]; }
  s += "]";
  return s;
}"""

new = """// ==================== Continuous RF receiver ====================
#define MAX_FRAMES 10
#define MAX_PULSES 300

volatile unsigned long rfLastTime = 0;
volatile uint16_t rfPulseBuf[MAX_PULSES];
volatile int rfPulseLen = 0;

struct RfFrame { uint16_t pulses[MAX_PULSES]; int len; };
RfFrame rfRing[MAX_FRAMES];
volatile int rfRingHead = 0;
volatile int rfRingCount = 0;

void IRAM_ATTR rfIsr() {
  unsigned long now = micros();
  unsigned long dur = now - rfLastTime;
  rfLastTime = now;
  if (dur < 40) return;
  if (dur > 8000) {
    if (rfPulseLen >= 10) {
      int idx = rfRingHead;
      for (int i = 0; i < rfPulseLen; i++) rfRing[idx].pulses[i] = rfPulseBuf[i];
      rfRing[idx].len = rfPulseLen;
      rfRingHead = (rfRingHead + 1) % MAX_FRAMES;
      if (rfRingCount < MAX_FRAMES) rfRingCount++;
    }
    rfPulseLen = 0;
    return;
  }
  if (rfPulseLen < MAX_PULSES) rfPulseBuf[rfPulseLen++] = (uint16_t)dur;
}

void rfStartListening() {
  rfPulseLen = 0; rfRingHead = 0; rfRingCount = 0;
  rfLastTime = micros();
  attachInterrupt(digitalPinToInterrupt(RX_PIN), rfIsr, CHANGE);
}

String rfPopFrame(int &outLen) {
  noInterrupts();
  if (rfRingCount == 0) { outLen = 0; interrupts(); return ""; }
  int tail = (rfRingHead - rfRingCount + MAX_FRAMES) % MAX_FRAMES;
  RfFrame &f = rfRing[tail];
  outLen = f.len;
  uint16_t tmp[MAX_PULSES];
  for (int i = 0; i < f.len; i++) tmp[i] = f.pulses[i];
  rfRingCount--;
  interrupts();
  if (outLen < 10) return "";
  String s = "[";
  for (int i = 0; i < outLen; i++) { if (i) s += ","; s += tmp[i]; }
  s += "]";
  return s;
}

String captureFrameTimeout(int timeoutMs) {
  int dummy;
  do { rfPopFrame(dummy); } while (dummy > 0);
  unsigned long t0 = millis();
  while (millis() - t0 < (unsigned long)timeoutMs) {
    delay(5);
    int len;
    String s = rfPopFrame(len);
    if (len >= 10) return s;
  }
  return "";
}
String captureFrame() { return captureFrameTimeout(cfgCaptureMs); }"""

c = c.replace(old, new)

# 2. Update handleSniff
old_sniff = """void handleSniff() {
  // Block up to 600ms waiting for a frame, return as JSON
  String pulses = captureFrameTimeout(600);
  DynamicJsonDocument doc(2048);
  doc["count"] = 0;
  doc["ms"] = 0;
  doc["data"] = "[]";
  if (pulses.length() > 2) {
    DynamicJsonDocument pdoc(2048);
    deserializeJson(pdoc, pulses);
    JsonArray arr = pdoc.as<JsonArray>();
    long total = 0;
    for (int i = 0; i < arr.size(); i++) total += arr[i].as<long>();
    doc["count"] = arr.size();
    doc["ms"] = total / 1000;  // us -> ms
    doc["data"] = pulses;
    ledFlash(80);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}"""

new_sniff = """void handleSniff() {
  int len;
  String pulses = rfPopFrame(len);
  DynamicJsonDocument doc(2048);
  doc["count"] = 0;
  doc["ms"] = 0;
  doc["data"] = "[]";
  if (len >= 10) {
    DynamicJsonDocument pdoc(2048);
    deserializeJson(pdoc, pulses);
    JsonArray arr = pdoc.as<JsonArray>();
    long total = 0;
    for (int i = 0; i < arr.size(); i++) total += arr[i].as<long>();
    doc["count"] = arr.size();
    doc["ms"] = total / 1000;
    doc["data"] = pulses;
    ledFlash(80);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}"""

c = c.replace(old_sniff, new_sniff)

# 3. Start background listening in setup
c = c.replace(
  "  pinMode(RX_PIN, INPUT);",
  "  pinMode(RX_PIN, INPUT);\n  rfStartListening();"
)

with io.open(p, "w", encoding="utf-8", newline="\n") as f:
    f.write(c)
print("done")
