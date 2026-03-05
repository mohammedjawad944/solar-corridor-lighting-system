/*
 *  SMART LIGHTING SYSTEM v4.0 - RELAY FIX
 *  INVERTED LOGIC FOR ACTIVE-LOW RELAY MODULE
 *  LDR + PIR + ESP32 + Web UI + Telegram + Clock + Timer
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <time.h>

// WiFi CREDENTIALS
const char* ssid = "BOYS CK";
const char* password = "1278";

// WEB LOGIN
const char* web_username = "jaad";
const char* web_password = "jaw234";

// TELEGRAM
String TELEGRAM_BOT_TOKEN = "8372756507:AAE6_E3JSo2T5Ap4Wd7kT4";
String TELEGRAM_CHAT_ID = "8052887";

// NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;  // GMT+5:30
const int daylightOffset_sec = 0;

// PINS
#define LDR_D0_PIN 34
#define PIR_A_PIN 13
#define PIR_B_PIN 12
#define PIR_C_PIN 14
#define RELAY_A_PIN 25  // Changed from LED to RELAY
#define RELAY_B_PIN 26
#define RELAY_C_PIN 27

// VARIABLES
WebServer server(80);
Preferences preferences;
WiFiClientSecure secured_client;
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, secured_client);

bool autoMode = true;
bool manualLED_A = false, manualLED_B = false, manualLED_C = false;
bool isLoggedIn = false;

int ldrState = 0;
int pirA_State = 0, pirB_State = 0, pirC_State = 0;

String lastMotionA = "Never", lastMotionB = "Never", lastMotionC = "Never";
unsigned long uptimeSeconds = 0;

bool ledA_Active = false, ledB_Active = false, ledC_Active = false;
unsigned long autoOffDelay = 30000;
unsigned long ledA_LastMotionTime = 0, ledB_LastMotionTime = 0, ledC_LastMotionTime = 0;
bool timerEnabled = true;

int ledA_TimeLeft = 0, ledB_TimeLeft = 0, ledC_TimeLeft = 0;

int brightnessA = 50, brightnessB = 50, brightnessC = 50;
int masterBrightness = 100;

bool alertEnabled = true;
int alertStartHour = 23, alertStartMinute = 0;
int alertEndHour = 6, alertEndMinute = 0;

// HELPERS
String formatUptime(unsigned long seconds) {
  unsigned long d = seconds / 86400;
  unsigned long h = (seconds % 86400) / 3600;
  unsigned long m = (seconds % 3600) / 60;
  unsigned long s = seconds % 60;
  String res = "";
  if (d > 0) res += String(d) + "d ";
  if (h > 0 || d > 0) res += String(h) + "h ";
  if (m > 0 || h > 0 || d > 0) res += String(m) + "m ";
  res += String(s) + "s";
  return res;
}

String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00 AM";
  char buf[20];
  strftime(buf, sizeof(buf), "%I:%M:%S %p", &timeinfo);
  return String(buf);
}

String getCurrentDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Loading...";
  char buf[30];
  strftime(buf, sizeof(buf), "%A, %B %d, %Y", &timeinfo);
  return String(buf);
}

bool isWithinAlertTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  int cur = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int start = alertStartHour * 60 + alertStartMinute;
  int end = alertEndHour * 60 + alertEndMinute;
  return (start > end) ? (cur >= start || cur < end) : (cur >= start && cur < end);
}

void sendTelegramAlert(String sensor, String msg) {
  if (TELEGRAM_BOT_TOKEN.length() < 10 || TELEGRAM_CHAT_ID.length() < 5) return;
  String text = "MIDNIGHT ALERT\n\nSensor: " + sensor + "\nStatus: " + msg +
                "\nTime: " + getCurrentTime() + "\nDate: " + getCurrentDate();
  bot.sendMessage(TELEGRAM_CHAT_ID, text, "");
}

// RELAY CONTROL - INVERTED LOGIC
// Relays are active-LOW: LOW = ON, HIGH = OFF
void setRelay(int pin, bool turnOn) {
  digitalWrite(pin, turnOn ? LOW : HIGH);  // Inverted!
}

void saveSettings() {
  preferences.begin("lighting", false);
  preferences.putULong("autoOffDelay", autoOffDelay);
  preferences.putBool("timerEnabled", timerEnabled);
  preferences.putInt("brightnessA", brightnessA);
  preferences.putInt("brightnessB", brightnessB);
  preferences.putInt("brightnessC", brightnessC);
  preferences.putInt("masterBright", masterBrightness);
  preferences.putBool("alertEnabled", alertEnabled);
  preferences.putInt("alertStartH", alertStartHour);
  preferences.putInt("alertStartM", alertStartMinute);
  preferences.putInt("alertEndH", alertEndHour);
  preferences.putInt("alertEndM", alertEndMinute);
  preferences.putString("botToken", TELEGRAM_BOT_TOKEN);
  preferences.putString("chatId", TELEGRAM_CHAT_ID);
  preferences.end();
}

void loadSettings() {
  preferences.begin("lighting", true);
  autoOffDelay = preferences.getULong("autoOffDelay", 30000);
  timerEnabled = preferences.getBool("timerEnabled", true);
  brightnessA = preferences.getInt("brightnessA", 50);
  brightnessB = preferences.getInt("brightnessB", 50);
  brightnessC = preferences.getInt("brightnessC", 50);
  masterBrightness = preferences.getInt("masterBright", 100);
  alertEnabled = preferences.getBool("alertEnabled", true);
  alertStartHour = preferences.getInt("alertStartH", 23);
  alertStartMinute = preferences.getInt("alertStartM", 0);
  alertEndHour = preferences.getInt("alertEndH", 6);
  alertEndMinute = preferences.getInt("alertEndM", 0);
  TELEGRAM_BOT_TOKEN = preferences.getString("botToken", "");
  TELEGRAM_CHAT_ID = preferences.getString("chatId", "");
  preferences.end();
  bot.updateToken(TELEGRAM_BOT_TOKEN);
}

// LOGIN PAGE
const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Smart Lighting - Login</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#1e3c72,#2a5298,#7e22ce);display:flex;justify-content:center;align-items:center;min-height:100vh;padding:20px}
.login-container{background:rgba(255,255,255,0.95);padding:50px 40px;border-radius:20px;box-shadow:0 20px 60px rgba(0,0,0,0.4);max-width:400px;width:100%}
h2{text-align:center;color:#1e3c72;margin-bottom:10px;font-size:28px}
.subtitle{text-align:center;color:#666;margin-bottom:35px;font-size:14px}
.input-group{margin-bottom:25px}
label{display:block;color:#555;font-size:14px;margin-bottom:8px;font-weight:600}
input{width:100%;padding:15px;border:2px solid #e0e0e0;border-radius:10px;font-size:16px;background:#f8f9fa}
input:focus{outline:none;border-color:#2a5298}
button{width:100%;padding:15px;background:linear-gradient(135deg,#1e3c72,#2a5298);color:white;border:none;border-radius:10px;font-size:18px;cursor:pointer}
button:hover{transform:translateY(-3px);box-shadow:0 6px 25px rgba(30,60,114,0.4)}
.footer{text-align:center;margin-top:25px;color:#999;font-size:12px}
</style></head><body>
<div class="login-container">
<h2>Smart Lighting System</h2>
<p class="subtitle">Secure Access Control</p>
<form action="/login" method="POST">
<div class="input-group"><label>Username</label><input type="text" name="username" required></div>
<div class="input-group"><label>Password</label><input type="password" name="password" required></div>
<button type="submit">LOGIN</button>
</form>
<div class="footer">Smart Home Automation v4.0</div>
</div></body></html>
)rawliteral";

// DASHBOARD PART 1
const char DASHBOARD_HTML_1[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><title>Smart Lighting Control</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',sans-serif;background:linear-gradient(135deg,#1e3c72,#2a5298,#7e22ce);padding:20px;min-height:100vh}
.container{max-width:900px;margin:0 auto;background:rgba(255,255,255,0.98);border-radius:25px;padding:35px;box-shadow:0 25px 70px rgba(0,0,0,0.4)}
.header{text-align:center;margin-bottom:35px;padding-bottom:25px;border-bottom:3px solid #f0f0f0}
h1{color:#1e3c72;margin-bottom:8px;font-size:36px;font-weight:700}
.subtitle{color:#666;font-size:15px}
.live-indicator{display:inline-flex;align-items:center;gap:8px;background:linear-gradient(135deg,#10b981,#059669);color:white;padding:8px 16px;border-radius:20px;font-size:13px;margin-top:10px}
.live-dot{width:8px;height:8px;background:white;border-radius:50%;animation:livePulse 1.5s infinite}
@keyframes livePulse{0%,100%{opacity:1}50%{opacity:0.5}}
.clock-widget{background:linear-gradient(135deg,#667eea,#764ba2);padding:20px;border-radius:15px;margin-bottom:25px;color:white;text-align:center}
.current-time{font-size:42px;font-weight:700;margin-bottom:5px;font-family:'Courier New'}
.current-date{font-size:16px;opacity:0.9}
.section{background:#f8f9fa;padding:25px;border-radius:15px;margin-bottom:25px}
.section-title{font-size:20px;font-weight:700;color:#333;margin-bottom:20px}
.mode-buttons{display:grid;grid-template-columns:1fr 1fr;gap:15px}
.mode-btn{padding:18px;border:none;border-radius:12px;font-size:17px;cursor:pointer;display:flex;align-items:center;justify-content:center}
.mode-btn.active{background:linear-gradient(135deg,#10b981,#059669);color:white}
.mode-btn.inactive{background:#e5e7eb;color:#6b7280}
.alert-control{background:white;padding:20px;border-radius:12px;margin-top:15px}
.alert-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:15px}
.alert-toggle{display:flex;align-items:center;gap:10px}
.switch{position:relative;display:inline-block;width:50px;height:26px}
.switch input{opacity:0;width:0;height:0}
.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#ccc;transition:.4s;border-radius:34px}
.slider:before{position:absolute;content:"";height:18px;width:18px;left:4px;bottom:4px;background:white;transition:.4s;border-radius:50%}
input:checked+.slider{background:#10b981}
input:checked+.slider:before{transform:translateX(24px)}
.time-input-group{display:grid;grid-template-columns:1fr 1fr;gap:15px;margin-bottom:15px}
.time-input-box{background:#f8f9fa;padding:15px;border-radius:10px}
.time-input-label{font-size:13px;font-weight:600;color:#666;margin-bottom:8px}
.time-input{width:100%;padding:10px;border:2px solid #e5e7eb;border-radius:8px;font-size:16px;text-align:center}
.telegram-config{margin-top:15px;padding-top:15px;border-top:1px solid #e5e7eb}
.telegram-input{width:100%;padding:12px;border:2px solid #e5e7eb;border-radius:8px;font-size:14px;margin-bottom:10px;font-family:monospace}
.save-btn,.test-btn{width:100%;padding:12px;color:white;border:none;border-radius:8px;font-size:15px;cursor:pointer;margin-top:10px}
.save-btn{background:linear-gradient(135deg,#10b981,#059669)}
.test-btn{background:linear-gradient(135deg,#3b82f6,#2563eb)}
.brightness-control{background:white;padding:20px;border-radius:12px;margin-top:15px}
.brightness-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:15px}
.slider-label{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
.slider-label-text{font-size:14px;font-weight:600;color:#666}
.brightness-percent{font-size:13px;font-weight:700;color:#1e3c72;background:#e0e7ff;padding:4px 10px;border-radius:12px}
input[type="range"]{width:100%;height:8px;border-radius:5px;background:#e5e7eb;outline:none}
input[type="range"]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:linear-gradient(135deg,#1e3c72,#2a5298);cursor:pointer}
.master-brightness{background:linear-gradient(135deg,#fef3c7,#fde68a);padding:15px;border-radius:10px;border-left:4px solid #f59e0b;margin-bottom:15px}
.preset-buttons{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin-top:10px}
.preset-btn{padding:8px;border:2px solid #e5e7eb;background:white;border-radius:8px;font-size:13px;cursor:pointer;text-align:center}
.timer-controls{background:white;padding:20px;border-radius:12px;margin-top:15px}
.delay-options{display:grid;grid-template-columns:repeat(5,1fr);gap:10px}
.delay-btn{padding:12px 8px;border:2px solid #e5e7eb;background:white;border-radius:8px;font-size:14px;cursor:pointer;text-align:center}
.delay-btn.active{background:linear-gradient(135deg,#1e3c72,#2a5298);color:white}
.led-grid{display:grid;gap:15px}
.led-card{background:white;padding:20px;border-radius:12px}
.led-card.led-active{background:linear-gradient(135deg,#fef3c7,#fde68a)}
.led-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
.led-info{display:flex;align-items:center;gap:12px}
.led-label{font-size:20px;font-weight:600;color:#333}
.led-status-badge{display:inline-block;padding:4px 12px;border-radius:12px;font-size:11px;margin-left:10px}
.badge-on{background:linear-gradient(135deg,#10b981,#059669);color:white}
.badge-off{background:#e5e7eb;color:#6b7280}
.timer-display{display:flex;align-items:center;gap:8px;font-size:14px;color:#666;margin-top:8px;padding:8px 12px;background:rgba(0,0,0,0.03);border-radius:8px}
.progress-bar{height:4px;background:#e5e7eb;border-radius:2px;margin-top:8px;overflow:hidden}
.progress-fill{height:100%;background:linear-gradient(90deg,#10b981,#059669);border-radius:2px}
.led-buttons{display:flex;gap:10px}
.led-btn{padding:10px 22px;border:none;border-radius:8px;font-size:15px;cursor:pointer;min-width:65px}
.btn-on{background:linear-gradient(135deg,#10b981,#059669);color:white}
.btn-off{background:linear-gradient(135deg,#ef4444,#dc2626);color:white}
.status-grid{display:grid;gap:12px}
.status-item{background:white;padding:18px;border-radius:10px;display:flex;justify-content:space-between;align-items:center}
.status-item.motion-active{background:linear-gradient(135deg,#dbeafe,#bfdbfe)}
.status-label{font-weight:600;color:#666;font-size:15px}
.status-value{color:#1e3c72;font-weight:700;font-size:15px}
.motion-timestamp{background:linear-gradient(135deg,#fef3c7,#fde68a);padding:12px 18px;border-radius:8px;margin-top:8px;border-left:4px solid #f59e0b;font-size:13px;color:#92400e;font-weight:600}
.logout-btn{width:100%;padding:16px;background:linear-gradient(135deg,#ef4444,#dc2626);color:white;border:none;border-radius:12px;font-size:17px;cursor:pointer;margin-bottom:15px}
.footer{text-align:center;color:#999;margin-top:25px;font-size:13px;padding-top:20px;border-top:2px solid #f0f0f0}
.last-update{text-align:center;background:linear-gradient(135deg,#e0e7ff,#c7d2fe);padding:8px;border-radius:8px;margin-bottom:15px;font-size:12px;color:#3730a3}
.relay-warning{background:linear-gradient(135deg,#fee2e2,#fecaca);padding:15px;border-radius:10px;border-left:4px solid #ef4444;margin-bottom:15px;color:#991b1b;font-size:13px;font-weight:600}
</style>
</head><body><div class="container">
<div class="header"><h1>Smart Lighting Control</h1><p class="subtitle">Motion Timer Brightness Clock Alerts - RELAY MODULE</p>
<div class="live-indicator"><div class="live-dot"></div>LIVE UPDATE</div></div>
<div class="relay-warning">NOTE: Using INVERTED relay logic (Active-LOW). Brightness control disabled for relay modules.</div>
<div class="clock-widget"><div class="current-time" id="currentTime">00:00:00 AM</div>
<div class="current-date" id="currentDate">Loading...</div></div>
<div class="last-update" id="lastUpdate">Last updated: Just now</div>
<div class="section"><div class="section-title">Control Mode</div>
<div class="mode-buttons">
<button class="mode-btn" id="autoBtn" onclick="setMode('auto')">AUTO MODE</button>
<button class="mode-btn" id="manualBtn" onclick="setMode('manual')">MANUAL MODE</button>
</div>
<div class="alert-control"><div class="alert-header">
<h3 style="color:#333;font-size:16px;margin:0">Midnight Alert System</h3>
<div class="alert-toggle"><span style="font-size:14px;color:#666">Enabled</span>
<label class="switch"><input type="checkbox" id="alertToggle" onchange="toggleAlert(this.checked)">
<span class="slider"></span></label></div></div>
<div style="margin-bottom:15px;color:#666;font-size:13px">Send Telegram alerts when motion detected during specified hours</div>
<div class="time-input-group">
<div class="time-input-box"><div class="time-input-label">Alert Start Time</div>
<input type="time" class="time-input" id="alertStart" value="23:00"></div>
<div class="time-input-box"><div class="time-input-label">Alert End Time</div>
<input type="time" class="time-input" id="alertEnd" value="06:00"></div></div>
<div class="telegram-config">
<div style="font-size:14px;font-weight:600;color:#666;margin-bottom:10px">Telegram Bot Configuration</div>
<input type="text" class="telegram-input" id="botToken" placeholder="Bot Token">
<input type="text" class="telegram-input" id="chatId" placeholder="Chat ID">
<button class="save-btn" onclick="saveAlertSettings()">SAVE SETTINGS</button>
<button class="test-btn" onclick="testTelegram()">SEND TEST MESSAGE</button>
</div></div>
<div class="timer-controls"><div class="timer-header">
<h3 style="color:#333;font-size:16px;margin:0">Auto-Off Timer</h3>
<div class="timer-toggle"><span style="font-size:14px;color:#666">Enabled</span>
<label class="switch"><input type="checkbox" id="timerToggle" onchange="toggleTimer(this.checked)">
<span class="slider"></span></label></div></div>
<div style="margin-bottom:10px;color:#666;font-size:13px">Relays turn off after motion stops</div>
<div class="delay-options">
<button class="delay-btn" onclick="setDelay(10)">10s</button>
<button class="delay-btn" onclick="setDelay(30)">30s</button>
<button class="delay-btn" onclick="setDelay(60)">1min</button>
<button class="delay-btn" onclick="setDelay(120)">2min</button>
<button class="delay-btn" onclick="setDelay(300)">5min</button>
</div></div></div>
<div class="section"><div class="section-title">Relay Controls</div>
<div class="led-grid">
<div class="led-card" id="ledCardA"><div class="led-header">
<div class="led-info"><span class="led-label">RELAY A</span>
<span class="led-status-badge" id="badgeA">OFF</span></div>
<div class="led-buttons">
<button class="led-btn btn-on" onclick="setLED('A','ON')">ON</button>
<button class="led-btn btn-off" onclick="setLED('A','OFF')">OFF</button>
</div></div>
<div class="timer-display" id="timerA" style="display:none">
<span class="timer-icon">Auto-off in: </span><span class="countdown" id="countdownA">--</span></div>
<div class="progress-bar" id="progressBarA" style="display:none">
<div class="progress-fill" id="progressFillA"></div></div></div>
<div class="led-card" id="ledCardB"><div class="led-header">
<div class="led-info"><span class="led-label">RELAY B</span>
<span class="led-status-badge" id="badgeB">OFF</span></div>
<div class="led-buttons">
<button class="led-btn btn-on" onclick="setLED('B','ON')">ON</button>
<button class="led-btn btn-off" onclick="setLED('B','OFF')">OFF</button>
</div></div>
<div class="timer-display" id="timerB" style="display:none">
<span class="timer-icon">Auto-off in: </span><span class="countdown" id="countdownB">--</span></div>
<div class="progress-bar" id="progressBarB" style="display:none">
<div class="progress-fill" id="progressFillB"></div></div></div>
<div class="led-card" id="ledCardC"><div class="led-header">
<div class="led-info"><span class="led-label">RELAY C</span>
<span class="led-status-badge" id="badgeC">OFF</span></div>
<div class="led-buttons">
<button class="led-btn btn-on" onclick="setLED('C','ON')">ON</button>
<button class="led-btn btn-off" onclick="setLED('C','OFF')">OFF</button>
</div></div>
<div class="timer-display" id="timerC" style="display:none">
<span class="timer-icon">Auto-off in: </span><span class="countdown" id="countdownC">--</span></div>
<div class="progress-bar" id="progressBarC" style="display:none">
<div class="progress-fill" id="progressFillC"></div></div></div>
</div></div>
<div class="section"><div class="section-title">System Status</div>
<div class="status-grid">
<div class="status-item"><span class="status-label">Current Mode</span><span class="status-value" id="currentMode">AUTO</span></div>
<div class="status-item"><span class="status-label">Alert System</span><span class="status-value" id="alertStatusDisplay">ENABLED</span></div>
<div class="status-item"><span class="status-label">Alert Window</span><span class="status-value" id="alertWindowDisplay">23:00 - 06:00</span></div>
<div class="status-item"><span class="status-label">Timer Status</span><span class="status-value" id="timerStatus">ENABLED</span></div>
<div class="status-item"><span class="status-label">Auto-Off Delay</span><span class="status-value" id="delayDisplay">30s</span></div>
<div class="status-item"><span class="status-label">Light Level</span><span class="status-value" id="lightLevel">BRIGHT</span></div>
<div class="status-item" id="pirStatusA"><span class="status-label">PIR Sensor A</span><span class="status-value" id="pirA">NO MOTION</span></div>
<div class="motion-timestamp" id="timestampA">Last Motion: Never</div>
<div class="status-item" id="pirStatusB"><span class="status-label">PIR Sensor B</span><span class="status-value" id="pirB">NO MOTION</span></div>
<div class="motion-timestamp" id="timestampB">Last Motion: Never</div>
<div class="status-item" id="pirStatusC"><span class="status-label">PIR Sensor C</span><span class="status-value" id="pirC">NO MOTION</span></div>
<div class="motion-timestamp" id="timestampC">Last Motion: Never</div>
<div class="status-item"><span class="status-label">System Uptime</span><span class="status-value" id="uptime">0s</span></div>
</div></div>
<button class="logout-btn" onclick="location.href='/logout'">LOGOUT</button>
<div class="footer"><div>Smart Home Automation System v4.0 - RELAY</div>
<div style="margin-top:5px;font-size:11px">Clock Alerts Timer Motion Control</div></div>
</div>
<script>
let currentDelay=30;
function updateStatus(){
fetch('/status').then(r=>r.json()).then(d=>{
document.getElementById('currentTime').textContent=d.currentTime;
document.getElementById('currentDate').textContent=d.currentDate;
document.getElementById('autoBtn').className='mode-btn '+(d.autoMode?'active':'inactive');
document.getElementById('manualBtn').className='mode-btn '+(d.autoMode?'inactive':'active');
document.getElementById('currentMode').textContent=d.autoMode?'AUTO':'MANUAL';
document.getElementById('alertToggle').checked=d.alertEnabled;
document.getElementById('alertStatusDisplay').textContent=d.alertEnabled?'ENABLED':'DISABLED';
document.getElementById('alertWindowDisplay').textContent=d.alertWindow;
document.getElementById('timerStatus').textContent=d.timerEnabled?'ENABLED':'DISABLED';
document.getElementById('timerToggle').checked=d.timerEnabled;
currentDelay=d.autoOffDelay;
updateDelayButtons(); updateDelayDisplay(d.autoOffDelay);
document.getElementById('lightLevel').textContent=d.ldrDark?'DARK':'BRIGHT';
document.getElementById('pirA').textContent=d.pirA?'MOTION':'NO MOTION';
document.getElementById('pirB').textContent=d.pirB?'MOTION':'NO MOTION';
document.getElementById('pirC').textContent=d.pirC?'MOTION':'NO MOTION';
document.getElementById('pirStatusA').className='status-item'+(d.pirA?' motion-active':'');
document.getElementById('pirStatusB').className='status-item'+(d.pirB?' motion-active':'');
document.getElementById('pirStatusC').className='status-item'+(d.pirC?' motion-active':'');
document.getElementById('timestampA').textContent='Last Motion: '+d.lastMotionA;
document.getElementById('timestampB').textContent='Last Motion: '+d.lastMotionB;
document.getElementById('timestampC').textContent='Last Motion: '+d.lastMotionC;
document.getElementById('uptime').textContent=d.uptime;
document.getElementById('ledCardA').className='led-card'+(d.ledA?' led-active':'');
document.getElementById('ledCardB').className='led-card'+(d.ledB?' led-active':'');
document.getElementById('ledCardC').className='led-card'+(d.ledC?' led-active':'');
document.getElementById('badgeA').className='led-status-badge '+(d.ledA?'badge-on':'badge-off');
document.getElementById('badgeB').className='led-status-badge '+(d.ledB?'badge-on':'badge-off');
document.getElementById('badgeC').className='led-status-badge '+(d.ledC?'badge-on':'badge-off');
document.getElementById('badgeA').textContent=d.ledA?'ON':'OFF';
document.getElementById('badgeB').textContent=d.ledB?'ON':'OFF';
document.getElementById('badgeC').textContent=d.ledC?'ON':'OFF';
['A','B','C'].forEach(id=>updateTimer(id,d['led'+id],d['timeLeft'+id],d.autoOffDelay,d.timerEnabled));
let now=new Date(); document.getElementById('lastUpdate').textContent='Last updated: '+now.toLocaleTimeString();
}).catch(()=>{});
}
function updateTimer(id,on,left,total,enabled){
let d=document.getElementById('timer'+id), c=document.getElementById('countdown'+id),
p=document.getElementById('progressBar'+id), f=document.getElementById('progressFill'+id);
if(on&&left>0&&enabled){d.style.display='flex';p.style.display='block';c.textContent=left+'s';f.style.width=(left/total)*100+'%';}
else{d.style.display='none';p.style.display='none';}
}
function updateDelayDisplay(s){let t=s<60?s+'s':(s==60?'1min':(s==120?'2min':(s==300?'5min':Math.floor(s/60)+'min')));
document.getElementById('delayDisplay').textContent=t;}
function updateDelayButtons(){document.querySelectorAll('.delay-btn').forEach(b=>b.className='delay-btn');
let v=currentDelay; [10,30,60,120,300].forEach((val,i)=>{if(v==val)document.querySelectorAll('.delay-btn')[i].classList.add('active');});}
function setMode(m){fetch('/setmode?mode='+m).then(()=>updateStatus());}
function setLED(id,s){fetch('/led?id='+id+'&state='+s).then(()=>updateStatus());}
function setDelay(s){fetch('/setdelay?delay='+s).then(()=>{currentDelay=s;updateDelayButtons();updateStatus();});}
function toggleTimer(e){fetch('/toggletimer?enabled='+(e?'1':'0')).then(()=>updateStatus());}
function toggleAlert(e){fetch('/togglealert?enabled='+(e?'1':'0')).then(()=>updateStatus());}
function saveAlertSettings(){
let s=document.getElementById('alertStart').value, e=document.getElementById('alertEnd').value,
t=document.getElementById('botToken').value, c=document.getElementById('chatId').value;
fetch('/savealert?start='+encodeURIComponent(s)+'&end='+encodeURIComponent(e)+'&token='+encodeURIComponent(t)+'&chat='+encodeURIComponent(c))
.then(r=>r.text()).then(()=>{alert('Saved!');updateStatus();});
}
function testTelegram(){fetch('/testtelegram').then(r=>r.text()).then(d=>alert(d=='OK'?'Test sent!':'Failed'));}
fetch('/getalert').then(r=>r.json()).then(d=>{
document.getElementById('alertStart').value=d.start;
document.getElementById('alertEnd').value=d.end;
document.getElementById('botToken').value=d.token;
document.getElementById('chatId').value=d.chatId;
});
setInterval(updateStatus,500); updateStatus();
</script></body></html>
)rawliteral";

// WEB HANDLERS
void handleRoot() {
  if (isLoggedIn) server.send(200, "text/html", DASHBOARD_HTML_1);
  else server.send(200, "text/html", LOGIN_HTML);
}

void handleLogin() {
  if (server.hasArg("username") && server.hasArg("password") &&
      server.arg("username") == web_username && server.arg("password") == web_password) {
    isLoggedIn = true;
    server.sendHeader("Location", "/"); server.send(303);
  } else {
    server.send(200, "text/html", String(LOGIN_HTML) + "<p style='color:red;text-align:center'>Invalid credentials!</p>");
  }
}

void handleLogout() { isLoggedIn = false; server.sendHeader("Location", "/"); server.send(303); }
void handleSetMode() { if (isLoggedIn && server.hasArg("mode")) autoMode = (server.arg("mode") == "auto"); server.send(200, "text/plain", "OK"); }
void handleLED() { if (isLoggedIn && server.hasArg("id") && server.hasArg("state")) {
  String id = server.arg("id"); bool on = (server.arg("state") == "ON");
  if (id == "A") manualLED_A = on; else if (id == "B") manualLED_B = on; else if (id == "C") manualLED_C = on;
} server.send(200, "text/plain", "OK"); }
void handleSetDelay() { if (isLoggedIn && server.hasArg("delay")) { autoOffDelay = server.arg("delay").toInt() * 1000; saveSettings(); } server.send(200, "text/plain", "OK"); }
void handleToggleTimer() { if (isLoggedIn && server.hasArg("enabled")) { timerEnabled = (server.arg("enabled") == "1"); saveSettings(); } server.send(200, "text/plain", "OK"); }
void handleToggleAlert() { if (isLoggedIn && server.hasArg("enabled")) { alertEnabled = (server.arg("enabled") == "1"); saveSettings(); } server.send(200, "text/plain", "OK"); }
void handleSaveAlert() {
  if (isLoggedIn && server.hasArg("start") && server.hasArg("end") && server.hasArg("token") && server.hasArg("chat")) {
    String s = server.arg("start"), e = server.arg("end");
    TELEGRAM_BOT_TOKEN = server.arg("token"); TELEGRAM_CHAT_ID = server.arg("chat");
    int c = s.indexOf(':'); if (c > 0) { alertStartHour = s.substring(0,c).toInt(); alertStartMinute = s.substring(c+1).toInt(); }
    c = e.indexOf(':'); if (c > 0) { alertEndHour = e.substring(0,c).toInt(); alertEndMinute = e.substring(c+1).toInt(); }
    bot.updateToken(TELEGRAM_BOT_TOKEN); saveSettings();
  }
  server.send(200, "text/plain", "OK");
}
void handleGetAlert() {
  if (!isLoggedIn) { server.send(401, "application/json", "{\"error\":\"Not logged in\"}"); return; }
  String sh = (alertStartHour < 10 ? "0" : "") + String(alertStartHour);
  String sm = (alertStartMinute < 10 ? "0" : "") + String(alertStartMinute);
  String eh = (alertEndHour < 10 ? "0" : "") + String(alertEndHour);
  String em = (alertEndMinute < 10 ? "0" : "") + String(alertEndMinute);
  String json = "{\"start\":\"" + sh + ":" + sm + "\",\"end\":\"" + eh + ":" + em +
                "\",\"token\":\"" + TELEGRAM_BOT_TOKEN + "\",\"chatId\":\"" + TELEGRAM_CHAT_ID + "\"}";
  server.send(200, "application/json", json);
}
void handleTestTelegram() {
  if (!isLoggedIn) { server.send(401, "text/plain", "Unauthorized"); return; }
  if (TELEGRAM_BOT_TOKEN.length() < 10 || TELEGRAM_CHAT_ID.length() < 5) {
    server.send(200, "text/plain", "Configure token and chat ID first!");
    return;
  }
  String msg = "TEST MESSAGE\nTime: " + getCurrentTime();
  server.send(200, "text/plain", bot.sendMessage(TELEGRAM_CHAT_ID, msg, "") ? "OK" : "Failed");
}
void handleStatus() {
  if (!isLoggedIn) { server.send(401, "application/json", "{\"error\":\"Not logged in\"}"); return; }
  String aw = (alertStartHour<10?"0":"")+String(alertStartHour)+":"+(alertStartMinute<10?"0":"")+String(alertStartMinute)
            + " - " + (alertEndHour<10?"0":"")+String(alertEndHour)+":"+(alertEndMinute<10?"0":"")+String(alertEndMinute);
  String json = "{";
  json += "\"currentTime\":\"" + getCurrentTime() + "\",";
  json += "\"currentDate\":\"" + getCurrentDate() + "\",";
  json += "\"autoMode\":" + String(autoMode?"true":"false") + ",";
  json += "\"alertEnabled\":" + String(alertEnabled?"true":"false") + ",";
  json += "\"alertWindow\":\"" + aw + "\",";
  json += "\"timerEnabled\":" + String(timerEnabled?"true":"false") + ",";
  json += "\"autoOffDelay\":" + String(autoOffDelay/1000) + ",";
  json += "\"brightnessA\":" + String(brightnessA) + ",";
  json += "\"brightnessB\":" + String(brightnessB) + ",";
  json += "\"brightnessC\":" + String(brightnessC) + ",";
  json += "\"masterBright\":" + String(masterBrightness) + ",";
  json += "\"ldrDark\":" + String(ldrState==HIGH?"true":"false") + ",";
  json += "\"pirA\":" + String(pirA_State==HIGH?"true":"false") + ",";
  json += "\"pirB\":" + String(pirB_State==HIGH?"true":"false") + ",";
  json += "\"pirC\":" + String(pirC_State==HIGH?"true":"false") + ",";
  json += "\"ledA\":" + String(ledA_Active?"true":"false") + ",";
  json += "\"ledB\":" + String(ledB_Active?"true":"false") + ",";
  json += "\"ledC\":" + String(ledC_Active?"true":"false") + ",";
  json += "\"timeLeftA\":" + String(ledA_TimeLeft) + ",";
  json += "\"timeLeftB\":" + String(ledB_TimeLeft) + ",";
  json += "\"timeLeftC\":" + String(ledC_TimeLeft) + ",";
  json += "\"lastMotionA\":\"" + lastMotionA + "\",";
  json += "\"lastMotionB\":\"" + lastMotionB + "\",";
  json += "\"lastMotionC\":\"" + lastMotionC + "\",";
  json += "\"uptime\":\"" + formatUptime(uptimeSeconds) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// SETUP
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println();
  Serial.println("=== SMART LIGHTING SYSTEM v4.0 - RELAY ===");
  Serial.println("Initializing with INVERTED logic for Active-LOW relays...");

  loadSettings();
  Serial.println("Settings loaded");

  pinMode(LDR_D0_PIN, INPUT);
  pinMode(PIR_A_PIN, INPUT); 
  pinMode(PIR_B_PIN, INPUT); 
  pinMode(PIR_C_PIN, INPUT);
  
  // Configure relay pins as OUTPUT and set them HIGH (OFF for active-LOW relays)
  pinMode(RELAY_A_PIN, OUTPUT);
  pinMode(RELAY_B_PIN, OUTPUT);
  pinMode(RELAY_C_PIN, OUTPUT);
  digitalWrite(RELAY_A_PIN, HIGH); // HIGH = OFF for active-LOW relay
  digitalWrite(RELAY_B_PIN, HIGH);
  digitalWrite(RELAY_C_PIN, HIGH);
  
  Serial.println("Relay pins configured (Active-LOW mode)");

  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Failed! Check credentials.");
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("NTP Time Sync Started");

  secured_client.setInsecure();
  Serial.println("Telegram Bot Ready");

  server.on("/", handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", handleLogout);
  server.on("/setmode", handleSetMode);
  server.on("/led", handleLED);
  server.on("/setdelay", handleSetDelay);
  server.on("/toggletimer", handleToggleTimer);
  server.on("/togglealert", handleToggleAlert);
  server.on("/savealert", handleSaveAlert);
  server.on("/getalert", handleGetAlert);
  server.on("/testtelegram", handleTestTelegram);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("Web Server Started");

  Serial.println("=== SYSTEM READY ===");
  Serial.println("NOTE: Using INVERTED relay logic (LOW=ON, HIGH=OFF)");
  Serial.println();
}

// LOOP
void loop() {
  server.handleClient();
  static unsigned long lastSec = 0;
  if (millis() - lastSec >= 1000) { lastSec = millis(); uptimeSeconds++; }

  ldrState = digitalRead(LDR_D0_PIN);
  int pa = digitalRead(PIR_A_PIN), pb = digitalRead(PIR_B_PIN), pc = digitalRead(PIR_C_PIN);
  
  if (pa == HIGH && pirA_State == LOW) { 
    lastMotionA = formatUptime(uptimeSeconds) + " ago"; 
    ledA_LastMotionTime = millis(); 
    if (alertEnabled && isWithinAlertTime()) sendTelegramAlert("PIR A", "Motion"); 
  }
  if (pb == HIGH && pirB_State == LOW) { 
    lastMotionB = formatUptime(uptimeSeconds) + " ago"; 
    ledB_LastMotionTime = millis(); 
    if (alertEnabled && isWithinAlertTime()) sendTelegramAlert("PIR B", "Motion"); 
  }
  if (pc == HIGH && pirC_State == LOW) { 
    lastMotionC = formatUptime(uptimeSeconds) + " ago"; 
    ledC_LastMotionTime = millis(); 
    if (alertEnabled && isWithinAlertTime()) sendTelegramAlert("PIR C", "Motion"); 
  }
  
  if (pa == HIGH) ledA_LastMotionTime = millis();
  if (pb == HIGH) ledB_LastMotionTime = millis();
  if (pc == HIGH) ledC_LastMotionTime = millis();
  
  pirA_State = pa; pirB_State = pb; pirC_State = pc;

  if (autoMode) {
    bool dark = (ldrState == HIGH);
    if (dark) {
      auto controlRelay = [&](int& state, int pir, unsigned long& last, bool& active, int& timeLeft, int pin) {
        if (pir == HIGH) { 
          active = true; 
          setRelay(pin, true); // true = turn ON (will send LOW to relay)
        }
        else if (timerEnabled) {
          unsigned long e = millis() - last;
          if (e < autoOffDelay) { 
            timeLeft = (autoOffDelay - e)/1000; 
            active = true; 
            setRelay(pin, true); // Keep ON
          }
          else { 
            active = false; 
            timeLeft = 0; 
            setRelay(pin, false); // Turn OFF
          }
        } else { 
          active = false; 
          setRelay(pin, false); 
        }
      };
      
      controlRelay(pirA_State, pa, ledA_LastMotionTime, ledA_Active, ledA_TimeLeft, RELAY_A_PIN);
      controlRelay(pirB_State, pb, ledB_LastMotionTime, ledB_Active, ledB_TimeLeft, RELAY_B_PIN);
      controlRelay(pirC_State, pc, ledC_LastMotionTime, ledC_Active, ledC_TimeLeft, RELAY_C_PIN);
    } else {
      ledA_Active = ledB_Active = ledC_Active = false;
      ledA_TimeLeft = ledB_TimeLeft = ledC_TimeLeft = 0;
      setRelay(RELAY_A_PIN, false); 
      setRelay(RELAY_B_PIN, false); 
      setRelay(RELAY_C_PIN, false);
    }
  } else {
    ledA_Active = manualLED_A; 
    ledB_Active = manualLED_B; 
    ledC_Active = manualLED_C;
    
    setRelay(RELAY_A_PIN, manualLED_A);
    setRelay(RELAY_B_PIN, manualLED_B);
    setRelay(RELAY_C_PIN, manualLED_C);
  }

  // SERIAL DEBUG OUTPUT (EVERY 2 SECONDS)
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();
    Serial.println("--- SYSTEM STATUS (RELAY MODE) ---");
    Serial.print("Mode: "); Serial.println(autoMode ? "AUTO" : "MANUAL");
    Serial.print("LDR (D0): "); Serial.println(ldrState == HIGH ? "DARK" : "BRIGHT");
    Serial.print("PIR A: "); Serial.println(pirA_State == HIGH ? "MOTION" : "NO");
    Serial.print("PIR B: "); Serial.println(pirB_State == HIGH ? "MOTION" : "NO");
    Serial.print("PIR C: "); Serial.println(pirC_State == HIGH ? "MOTION" : "NO");
    Serial.print("RELAY A: "); Serial.print(ledA_Active ? "ON" : "OFF");
    Serial.print(" (Pin "); Serial.print(digitalRead(RELAY_A_PIN) == LOW ? "LOW" : "HIGH"); Serial.print(")");
    if (ledA_Active && timerEnabled && ledA_TimeLeft > 0) { Serial.print(" ("); Serial.print(ledA_TimeLeft); Serial.print("s left)"); }
    Serial.println();
    Serial.print("RELAY B: "); Serial.print(ledB_Active ? "ON" : "OFF");
    Serial.print(" (Pin "); Serial.print(digitalRead(RELAY_B_PIN) == LOW ? "LOW" : "HIGH"); Serial.print(")");
    if (ledB_Active && timerEnabled && ledB_TimeLeft > 0) { Serial.print(" ("); Serial.print(ledB_TimeLeft); Serial.print("s left)"); }
    Serial.println();
    Serial.print("RELAY C: "); Serial.print(ledC_Active ? "ON" : "OFF");
    Serial.print(" (Pin "); Serial.print(digitalRead(RELAY_C_PIN) == LOW ? "LOW" : "HIGH"); Serial.print(")");
    if (ledC_Active && timerEnabled && ledC_TimeLeft > 0) { Serial.print(" ("); Serial.print(ledC_TimeLeft); Serial.print("s left)"); }
    Serial.println();
    Serial.print("Uptime: "); Serial.println(formatUptime(uptimeSeconds));
    Serial.println("----------------------------------");
    Serial.println();
  }

  delay(100);
}
