/*
 * Yeelight RF Bridge  (Wemos D1 mini + RX470-4 + WL102-341)
 * ------------------------------------------------------------------
 * 用途：替换易来 YLYTD-0033-YKQ 的 433MHz 遥控器
 * 接线：
 *   WL102-341  +  -> 3V3
 *   WL102-341  -  -> GND
 *   WL102-341 DAT -> D2 (GPIO4)
 *   WL102-341 OUT -> 天线
 *   WL102-341 EN  -> 悬空
 *
 * 烧录后：
 *   1. 首次使用 ESP 会自己开热点 Yeelight-RF-Bridge
 *   2. 手机连这个热点，浏览器打开 http://192.168.4.1
 *   3. 点按钮即可
 *   （后续可改成连你家 WiFi，见下方 SSID/PASS 注释）
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ====== 改成连你家 WiFi：把下面两行填好，并把 AP 模式那段注释掉 ======
// const char* WIFI_SSID = "your-wifi";
// const char* WIFI_PASS = "your-password";
// ====================================================================

const int TX_PIN = 4;   // D2 = GPIO4

// ---------- 9 个按键的脉冲帧（us）----------
// 关灯
const uint16_t P_OFF[] = {
  3522,2484,3519,2496,319,882,914,295,904,300,883,426,892,303,896,307,
  303,891,310,890,313,887,315,886,317,882,914,296,312,881,321,879,
  323,878,918,292,301,893,323,876,328,875,328,872,328,871,924,286,
  322,872,921,289,913,290,316,879,916,292,316,879,323,891,297,890,
  920,290,317,876,328,872,329,872,331,869,333,970,333,867,928
};
// 开灯
const uint16_t P_ON[] = {
  3505,2505,3502,2514,301,899,898,312,888,315,886,414,886,320,881,324,
  285,909,294,905,298,903,301,899,303,900,894,313,297,897,306,893,
  311,890,905,306,301,891,312,888,907,305,896,307,300,893,902,308,
  299,895,901,312,887,312,296,898,898,312,296,898,306,894,307,893,
  905,306,300,892,311,889,313,887,908,302,897,404,304,896,900
};
// 后灯
const uint16_t P_REAR[] = {
  3516,2493,3514,2501,313,887,910,300,901,303,898,402,896,308,892,314,
  293,901,302,898,305,895,307,926,253,913,907,303,305,889,314,887,
  316,883,912,297,311,883,318,882,915,295,312,881,321,880,916,295,
  296,903,907,299,902,302,305,889,906,302,306,887,316,884,319,882,
  913,297,311,882,321,881,322,880,914,326,256,1007,322,876,919
};
// 双灯
const uint16_t P_BOTH[] = {
  3506,2504,3505,2512,305,905,890,309,892,311,889,410,891,315,884,321,
  288,905,298,902,301,899,304,895,308,895,900,307,300,895,309,900,
  293,898,906,305,303,890,313,888,907,302,898,305,303,890,905,306,
  303,892,903,307,892,310,299,896,890,324,294,897,306,893,310,888,
  907,305,303,890,312,888,316,885,909,301,901,399,309,889,906
};
// 前灯
const uint16_t P_FRONT[] = {
  3511,2499,3504,2512,308,892,904,306,894,310,890,409,893,314,887,317,
  290,903,299,900,303,896,308,896,306,898,896,306,303,892,312,889,
  313,887,909,302,305,888,316,884,319,881,914,297,310,883,912,298,
  311,884,906,310,894,303,306,889,906,304,305,890,312,887,317,883,
  912,298,308,887,318,883,317,883,321,878,919,395,308,886,914
};
// 亮度
const uint16_t P_BRIGHT[] = {
  3511,2477,3500,2553,275,911,876,345,861,330,864,450,887,283,916,292,
  313,882,321,924,243,948,260,919,306,914,861,348,261,911,314,907,
  276,924,889,311,294,913,308,861,344,859,935,276,888,357,252,933,
  886,305,284,929,867,342,265,911,885,341,268,924,287,904,290,890,
  938,273,333,860,344,860,901,348,855,324,300,1010,277,923,873
};
// 色温
const uint16_t P_COLOR[] = {
  3514,2494,3514,2501,310,888,909,301,896,309,890,407,895,309,890,314,
  292,901,303,896,308,894,306,891,314,886,910,302,301,891,311,887,
  318,883,909,301,306,887,318,883,317,882,913,296,905,296,312,883,
  912,297,309,883,913,300,303,888,908,300,308,886,313,888,315,884,
  912,298,308,885,317,883,915,293,904,298,311,985,317,882,909
};
// 顺时针
const uint16_t P_CW[] = {
  3537,2488,3503,2507,322,874,923,289,916,287,910,391,916,282,914,296,
  313,882,310,894,323,887,298,903,300,900,907,299,307,889,312,886,
  320,877,922,292,315,883,312,879,336,869,922,306,882,304,320,889,
  890,325,283,912,892,312,296,902,884,328,285,902,310,895,305,904,
  895,302,307,890,308,908,891,305,877,344,264,1029,275,914,894
};
// 逆时针
const uint16_t P_CCW[] = {
  3516,2495,3512,2503,316,885,910,300,901,304,896,403,899,308,891,319,
  284,911,292,924,267,922,292,903,304,897,884,341,285,890,315,887,
  316,884,913,298,309,885,319,881,322,880,915,298,309,898,292,903,
  309,888,909,309,876,315,312,882,917,290,318,878,325,877,328,872,
  329,872,925,285,916,288,894,325,287,898,907,401,306,904,900
};

struct Cmd { const char* name; const uint16_t* pulses; int len; int repeats; };
const Cmd CMDS[] = {
  {"off",    P_OFF,     sizeof(P_OFF)/sizeof(P_OFF[0]),     5},
  {"on",     P_ON,      sizeof(P_ON)/sizeof(P_ON[0]),       5},
  {"rear",   P_REAR,    sizeof(P_REAR)/sizeof(P_REAR[0]),   5},
  {"both",   P_BOTH,    sizeof(P_BOTH)/sizeof(P_BOTH[0]),   5},
  {"front",  P_FRONT,   sizeof(P_FRONT)/sizeof(P_FRONT[0]), 5},
  {"bright", P_BRIGHT,  sizeof(P_BRIGHT)/sizeof(P_BRIGHT[0]), 20},
  {"color",  P_COLOR,   sizeof(P_COLOR)/sizeof(P_COLOR[0]),  20},
  {"cw",     P_CW,      sizeof(P_CW)/sizeof(P_CW[0]),       20},
  {"ccw",    P_CCW,     sizeof(P_CCW)/sizeof(P_CCW[0]),     20},
};
const int CMD_N = sizeof(CMDS)/sizeof(CMDS[0]);

ESP8266WebServer server(80);

void sendFrame(const uint16_t* pulses, int n) {
  noInterrupts();
  for (int i = 0; i < n; i++) {
    digitalWrite(TX_PIN, (i & 1) ? LOW : HIGH);
    delayMicroseconds(pulses[i]);
  }
  digitalWrite(TX_PIN, LOW);
  interrupts();
}

void sendCmd(int idx) {
  for (int k = 0; k < CMDS[idx].repeats; k++) {
    sendFrame(CMDS[idx].pulses, CMDS[idx].len);
    delay(15);
  }
}

const char HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name=viewport content="width=device-width,initial-scale=1">
<style>
 body{font-family:system-ui;background:#111;color:#eee;padding:16px;margin:0}
 h2{margin:0 0 12px;font-weight:600}
 .g{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}
 button{padding:18px 6px;font-size:15px;border:0;border-radius:12px;
   background:#2a2a2e;color:#fff;cursor:pointer}
 button:active{background:#4a4a52}
 .big{grid-column:span 3;background:#0a84ff}
</style></head><body>
<h2>Yeelight Remote</h2>
<div class=g>
 <button class=big onclick=go('on')>Power On</button>
 <button class=big onclick=go('off')>Power Off</button>
 <button onclick=go('front')>Front</button>
 <button onclick=go('rear')>Rear</button>
 <button onclick=go('both')>Both</button>
 <button onclick=go('bright')>Brightness</button>
 <button onclick=go('color')>Color Temp</button>
 <button onclick=go('cw')>CW Knob</button>
 <button onclick=go('ccw')>CCW Knob</button>
</div>
<script>async function go(c){await fetch('/'+c)}</script>
</body></html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", HTML);
}

void handleCmd() {
  String path = server.uri();
  path = path.substring(1);
  for (int i = 0; i < CMD_N; i++) {
    if (path == CMDS[i].name) {
      sendCmd(i);
      server.send(200, "text/plain", "ok:" + path);
      return;
    }
  }
  server.send(404, "text/plain", "unknown");
}

void setup() {
  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  Serial.begin(115200);

  // 先用 AP 模式，零配置
  WiFi.softAP("Yeelight-RF-Bridge");
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(ip);

  // 改成连家里 WiFi：注释掉上面 softAP 两行，解开下面
  // WiFi.begin(WIFI_SSID, WIFI_PASS);
  // while (WiFi.status() != WL_CONNECTED) { delay(300); }
  // Serial.print("IP: "); Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  for (int i = 0; i < CMD_N; i++) {
    server.on("/" + String(CMDS[i].name), handleCmd);
  }
  server.begin();
  Serial.println("ready");
}

void loop() {
  server.handleClient();
}
