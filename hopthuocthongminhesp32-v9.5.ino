#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <time.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

const int BUZZER_PIN   = 27;
const int BTN_PIN      = 13;
const int STATUS_LED_PIN = 2; 
const int LED_PINS[7] = {4, 5, 18, 19, 32, 33, 25};

const int EEPROM_MAGIC_ADDR = 400;
const byte EEPROM_MAGIC_VAL = 0x7C;
const int EEPROM_ALARM_START = 402;

String defaultZaloBotToken = "3516935780710466264:ugSstUzvFeZmZfcybnQrfsiuegGQekOlaVOxAzbPqtOnWjqsADfSVjQcraMywtBt";
String defaultZaloChatId = "zgr-283fb837225acb04924b";
String zaloBotToken = "";
String zaloChatId = "";
String currentSSID = "";

String serverUrl = "https://httm.ndqm.eu.org/"; 
unsigned long last_zalo_poll = 0;

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool configMode = false;
unsigned long disableApTime = 0;
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

struct Alarm { int hour; int minute; bool active; };
Alarm alarms[21]; 

int current_hour = 0, current_minute = 0, current_second = 0, current_dow = 1;
unsigned long last_time_update = 0;
unsigned long last_ntp_sync = 0; 
bool is_time_synced = false;
unsigned long test_start_time = 0;
int test_led_id = -1;
bool test_buzzer_on = false;
char last_lcd_line1[17] = "";
char last_lcd_line2[17] = "";
int current_alarm_view = 0;
int status_mode_screen = 0;
bool is_alarm_ringing = false;
bool is_waiting_confirmation = false; 
int ringing_alarm_index = -1;
bool was_ringing = false;
int last_checked_minute = -1;
unsigned long alarm_start_time = 0; 
bool is_buzzer_active = false; 

bool btn_was_pressed = false;
unsigned long btn_press_start = 0;
bool long_press_handled = false;
bool very_long_press_handled = false;
unsigned long last_interaction_time = 0;

void playBuzzer() { if (!is_buzzer_active) { tone(BUZZER_PIN, 2500); is_buzzer_active = true; } }
void stopBuzzer() { if (is_buzzer_active) { noTone(BUZZER_PIN); is_buzzer_active = false; } }

void syncNTPtoRTC() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
  }
}

void lcd_print_line(byte row, const char* text) {
  lcd.setCursor(0, row); String str = String(text);
  while(str.length() < 16) { str += " "; } lcd.print(str.substring(0, 16));
}

void reset_lcd_screen() {
  lcd.clear();
  memset(last_lcd_line1, 0, sizeof(last_lcd_line1));
  memset(last_lcd_line2, 0, sizeof(last_lcd_line2));
}

void writeStringToEEPROM(int addrOffset, const String &strToSave) {
  byte len = strToSave.length(); EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++) EEPROM.write(addrOffset + 1 + i, strToSave[i]);
}

String readStringFromEEPROM(int addrOffset) {
  int newStrLen = EEPROM.read(addrOffset);
  if (newStrLen == 255 || newStrLen == 0 || newStrLen > 100) return "";
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++) data[i] = EEPROM.read(addrOffset + 1 + i);
  data[newStrLen] = '\0'; return String(data);
}

void clearWiFiEEPROM() { 
  EEPROM.write(0, 0); 
  EEPROM.write(50, 0); 
  EEPROM.write(200, 0); 
  EEPROM.write(300, 0); 
  EEPROM.commit(); 
}

void saveAlarmToEEPROM(int index) {
  int addr = EEPROM_ALARM_START + (index * sizeof(Alarm));
  EEPROM.put(addr, alarms[index]); EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL); EEPROM.commit();
}

void loadAlarmsFromEEPROM() {
  if (EEPROM.read(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VAL) {
    int addr = EEPROM_ALARM_START;
    for (int i = 0; i < 21; i++) { EEPROM.get(addr, alarms[i]); addr += sizeof(Alarm); }
  } else { for (int i = 0; i < 21; i++) { alarms[i] = {7, 0, false}; saveAlarmToEEPROM(i); } }
}

void sendZaloMessage(String message) {
  if (WiFi.status() == WL_CONNECTED && zaloChatId.length() > 5 && zaloBotToken.length() > 20) {
    WiFiClientSecure client; client.setInsecure(); HTTPClient https; https.setTimeout(3000);
    String url = "https://bot-api.zaloplatforms.com/bot" + zaloBotToken + "/sendMessage";
    if (https.begin(client, url)) {
      https.addHeader("Content-Type", "application/json");
      message.replace("\"", "\\\""); message.replace("\n", "\\n");  
      https.POST("{\"chat_id\":\"" + zaloChatId + "\",\"text\":\"" + message + "\"}");
      https.end();
    }
  }
}

int getNextAlarmIndex() {
  int current_day_idx = (current_dow == 0) ? 6 : (current_dow - 1);
  int current_mins = current_day_idx * 24 * 60 + current_hour * 60 + current_minute;
  
  int best_idx = 0;
  int min_diff = 999999;
  bool found = false;

  for (int i = 0; i < 21; i++) {
    if (alarms[i].active) {
      int a_day = i / 3;
      int a_time = a_day * 24 * 60 + alarms[i].hour * 60 + alarms[i].minute;
      int diff = a_time - current_mins;
      
      if (diff <= 0) diff += 7 * 24 * 60; 
      
      if (diff < min_diff) {
        min_diff = diff;
        best_idx = i;
        found = true;
      }
    }
  }
  return found ? best_idx : 0;
}

void pingServer() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client; HTTPClient http; http.begin(client, serverUrl + "/esp-ping"); http.setTimeout(300); 
    if (http.GET() == 200 && http.getString() == "OK") { lcd_print_line(0, " SERVER LINK: OK"); delay(1500); } 
    else { lcd_print_line(0, " SERVER LINK:ERR"); delay(1500); }
    http.end();
  }
}

void pollZaloCommands() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client; HTTPClient http; http.begin(client, serverUrl + "/esp-pull"); http.setTimeout(300);
    if (http.GET() == 200) {
      String payload = http.getString();
      if (payload != "[]" && payload.length() > 5) {
        DynamicJsonDocument doc(1024);
        if (!deserializeJson(doc, payload)) {
          JsonArray arr = doc.as<JsonArray>();
          for (JsonVariant v : arr) {
            int id = v["id"]; int h = v["h"]; int m = v["m"]; bool en = v["en"]; String slotName = v["slotName"].as<String>();
            alarms[id].hour = h; alarms[id].minute = m; alarms[id].active = en; saveAlarmToEEPROM(id);

            char timeBuf[10]; sprintf(timeBuf, "%02d:%02d", h, m);
            sendZaloMessage("✅ Hộp thuốc cập nhật cữ " + slotName + " thành " + String(timeBuf) + " (" + (en ? "BẬT" : "TẮT") + ")");
            lcd_print_line(0, " UPDATED BY ZALO"); lcd_print_line(1, (" " + slotName + " " + String(timeBuf)).c_str());
            last_interaction_time = millis(); 
            delay(2500); reset_lcd_screen();
          }
        }
      }
    }
    http.end();
  }
}

void sendNoCacheHeaders() { server.sendHeader("Cache-Control", "no-cache"); server.sendHeader("Pragma", "no-cache"); }
void handleGetTime() { sendNoCacheHeaders(); server.send(200, "application/json", "{\"h\":" + String(current_hour) + ",\"m\":" + String(current_minute) + ",\"s\":" + String(current_second) + "}"); }
void handleTestDevice() { sendNoCacheHeaders(); if(server.hasArg("type")){ String type=server.arg("type"); if(type=="led"&&server.hasArg("id")){test_led_id=server.arg("id").toInt(); test_buzzer_on=false;}else if(type=="buzzer"){test_buzzer_on=true; test_led_id=-1;} test_start_time=millis(); server.send(200,"text/plain","OK"); }else server.send(400,"text/plain","Err");}
void handleGetAlarms() { String json = "["; for(int i=0; i<21; i++){ json+="{\"h\":"+String(alarms[i].hour)+",\"m\":"+String(alarms[i].minute)+",\"en\":"+String(alarms[i].active?"true":"false")+"}"; if(i<20)json+=",";} json+="]"; server.send(200,"application/json",json); }
void handleNormalSet() { if(server.hasArg("id")&&server.hasArg("h")&&server.hasArg("m")&&server.hasArg("en")){ int id=server.arg("id").toInt(); if(id>=0&&id<21){ alarms[id].hour=server.arg("h").toInt(); alarms[id].minute=server.arg("m").toInt(); alarms[id].active=server.arg("en").toInt()==1; saveAlarmToEEPROM(id); } server.send(200,"text/plain","OK"); } }

void handleSetupRoot() {
  int n = WiFi.scanNetworks();
  String html = "<!DOCTYPE html><html lang='vi'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>body{font-family:sans-serif;background:#121212;color:white;text-align:center;padding:15px;}";
  html += "input,button,select{width:100%;padding:12px;margin:8px 0;border-radius:5px;border:none;box-sizing:border-box;font-size:15px;background:#fff;color:#000;}";
  html += "button{background:#00d2ff;color:black;font-weight:bold;cursor:pointer;} label{display:block;text-align:left;color:#ff9800;font-weight:bold;font-size:14px;margin-top:10px;}</style></head><body>";
  html += "<h2 style='color:#00d2ff;'>Cài Đặt Hệ Thống</h2>";
  html += "<form action='/save' method='POST'>";
  
  html += "<label>Chọn mạng WiFi:</label><select name='ssid' required>";
  if (n == 0) {
    html += "<option value=''>Không tìm thấy WiFi</option>";
  } else {
    for (int i = 0; i < n; ++i) {
      html += "<option value=\"" + WiFi.SSID(i) + "\">" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";
    }
  }
  html += "</select>";
  
  html += "<label>Mật khẩu WiFi:</label><input type='password' name='pass' placeholder='Để trống nếu không có Pass'>";
  html += "<label>Zalo Bot Token:</label><input type='text' name='zalo_token' value='" + zaloBotToken + "' required>";
  html += "<label>Zalo Chat ID / Group ID:</label><input type='text' name='zalo_id' value='" + zaloChatId + "' required>";
  html += "<br><br><button type='submit'>LƯU VÀ KHỞI ĐỘNG LẠI</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handleSetupSave() {
  String ssid = server.arg("ssid"); String pass = server.arg("pass"); String zid = server.arg("zalo_id"); String ztok = server.arg("zalo_token");
  ssid.trim(); pass.trim(); zid.trim(); ztok.trim();
  if (zid.length() < 5 || ztok.length() < 20) { server.send(200, "text/html", "<meta charset='UTF-8'><h2>Lỗi! ID hoặc Token Zalo không hợp lệ.</h2><button onclick='history.back()'>Quay lại</button>"); return; }
  
  zaloChatId = zid; writeStringToEEPROM(200, zaloChatId); zaloBotToken = ztok; writeStringToEEPROM(300, zaloBotToken); EEPROM.commit();
  WiFi.disconnect(); delay(100); WiFi.begin(ssid.c_str(), pass.c_str());
  int retries = 0; while (WiFi.status() != WL_CONNECTED && retries < 20) { delay(500); retries++; }
  
  if (WiFi.status() == WL_CONNECTED) {
    writeStringToEEPROM(0, ssid); writeStringToEEPROM(50, pass); EEPROM.commit(); currentSSID = ssid;
    disableApTime = millis(); server.send(200, "text/html", "<meta charset='UTF-8'><h2>Lưu thành công! Mạch đang khởi động lại...</h2>");
  } else { server.send(200, "text/html", "<meta charset='UTF-8'><h2>Lỗi kết nối WiFi sai mật khẩu! Vui lòng thử lại.</h2>"); }
}

void handleNormalRoot() {
  sendNoCacheHeaders();
  String html = R"=====(
<!DOCTYPE html><html lang='vi'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Smart Pill Box</title>
<style>
body { font-family: 'Segoe UI', sans-serif; background: #121212; color: #fff; text-align: center; margin:0; padding: 15px; }
h2 { color: #00d2ff; margin-bottom: 5px; }
#clock { font-size: 30px; font-weight: bold; color: #ff007f; margin-bottom: 15px; text-shadow: 0 0 10px rgba(255,0,127,0.5); }
#app { display: grid; gap: 15px; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }
.card { background: #1e1e1e; padding: 15px; border-radius: 12px; border-top: 4px solid #00d2ff;}
.slot { display: flex; align-items: center; justify-content: space-between; background: #2a2a2a; margin-top: 10px; padding: 10px; border-radius: 8px;}
label { font-weight: bold; width: 60px; text-align: left; color:#ff007f;}
input[type='time'], select { background: #121212; color: #fff; border: 1px solid #444; padding: 8px; border-radius: 5px; flex: 1; margin: 0 10px;}
button { background: #4caf50; color: #fff; border: none; padding: 10px; border-radius: 5px; cursor: pointer; font-weight: bold;}
.btn-test { background: #ff9800; padding: 12px; font-size:15px; margin:4px; flex: 1 1 calc(20% - 8px); box-sizing:border-box; border-radius: 5px;}
.btn-buzz { background: #f44336; }
.toast { position: fixed; top: 20px; left: 50%; transform: translateX(-50%); background: #4caf50; padding: 10px 20px; border-radius: 20px; display: none; z-index: 1000; font-weight:bold;}
</style></head><body>

<h2>QUẢN LÝ THUỐC 7 NGÀY</h2>
<div id='clock'>--:--:--</div>
<div id="toast" class="toast">Đã lưu!</div>

<div class='card' style='border-top: 4px solid #ff9800; margin-bottom: 20px;'>
  <h3 style='margin:0 0 10px 0; color:#ff9800;'>Test Phần Cứng (5 Giây)</h3>
  <div style='display:flex; flex-wrap:wrap; justify-content:center;'>
    <button class='btn-test' onclick='testDev("led",0)'>L.T2</button>
    <button class='btn-test' onclick='testDev("led",1)'>L.T3</button>
    <button class='btn-test' onclick='testDev("led",2)'>L.T4</button>
    <button class='btn-test' onclick='testDev("led",3)'>L.T5</button>
    <button class='btn-test' onclick='testDev("led",4)'>L.T6</button>
    <button class='btn-test' onclick='testDev("led",5)'>L.T7</button>
    <button class='btn-test' onclick='testDev("led",6)'>L.CN</button>
    <button class='btn-test btn-buzz' onclick='testDev("buzzer",0)'>Còi Bíp</button>
  </div>
</div>
<div id="app"></div>
<script>
const days = ['Thứ 2', 'Thứ 3', 'Thứ 4', 'Thứ 5', 'Thứ 6', 'Thứ 7', 'Chủ Nhật'];
const sessions = ['Sáng', 'Trưa', 'Chiều'];
)=====";

  html += "let sys_h=" + String(current_hour) + ", sys_m=" + String(current_minute) + ", sys_s=" + String(current_second) + ";\n";
  
  html += R"=====(
function tick() {
  sys_s++; if(sys_s>59){sys_s=0; sys_m++;} if(sys_m>59){sys_m=0; sys_h++;} if(sys_h>23)sys_h=0;
  let str = String(sys_h).padStart(2,'0')+':'+String(sys_m).padStart(2,'0')+':'+String(sys_s).padStart(2,'0');
  let clk = document.getElementById('clock'); if(clk) clk.innerText = str;
}
setInterval(tick, 1000); tick(); 
function syncTime() { fetch('/api/time').then(r=>r.json()).then(d=>{sys_h=d.h; sys_m=d.m; sys_s=d.s;}).catch(e=>{}); }
setInterval(syncTime, 10000); 

let appHtml = '';
fetch('/get_alarms').then(r=>r.json()).then(data => {
  days.forEach((day, dIdx) => {
    appHtml += `<div class='card'><h3>${day}</h3>`;
    sessions.forEach((sess, sIdx) => {
      let id = dIdx * 3 + sIdx; let a = data[id];
      let h = String(a.h).padStart(2,'0'); let m = String(a.m).padStart(2,'0');
      appHtml += `<div class='slot'><label>${sess}</label>
        <input type='time' id='time_${id}' value='${h}:${m}'>
        <select id='en_${id}'><option value='1' ${a.en?'selected':''}>Bật</option><option value='0' ${!a.en?'selected':''}>Tắt</option></select>
        <button onclick='save(${id})'>Lưu</button></div>`;
    });
    appHtml += `</div>`;
  });
  document.getElementById('app').innerHTML = appHtml;
});

function showToast(msg) {
  let t = document.getElementById('toast');
  t.innerText = msg; t.style.display = 'block'; setTimeout(() => t.style.display = 'none', 2000);
}
function save(id) {
  let t = document.getElementById('time_'+id).value; let en = document.getElementById('en_'+id).value;
  if(!t) return;
  fetch(`/set?id=${id}&h=${t.split(':')[0]}&m=${t.split(':')[1]}&en=${en}`).then(()=>showToast('Đã lưu mạch!'));
}
function testDev(type, id) { fetch(`/test?type=${type}&id=${id}`).then(()=>showToast('Đang Test (5s)...')); }
</script></body></html>
)=====";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(1024);
  pinMode(BUZZER_PIN, OUTPUT); pinMode(BTN_PIN, INPUT_PULLUP); pinMode(STATUS_LED_PIN, OUTPUT); digitalWrite(STATUS_LED_PIN, HIGH);
  for (int i = 0; i < 7; i++) { pinMode(LED_PINS[i], OUTPUT); digitalWrite(LED_PINS[i], LOW); }
  
  loadAlarmsFromEEPROM();
  Wire.begin(21, 22);

  lcd.init(); lcd.backlight();
  if (!rtc.begin()) { lcd_print_line(0, " RTC ERROR!     "); while (1) delay(10); }
  if (rtc.lostPower()) { rtc.adjust(DateTime(2023, 1, 1, 0, 0, 0)); }

  lcd_print_line(0, " ESP32 PILL BOX "); lcd_print_line(1, " CONNECTING...  ");
  
  String savedSSID = readStringFromEEPROM(0); String savedPass = readStringFromEEPROM(50); 
  zaloChatId = readStringFromEEPROM(200); zaloBotToken = readStringFromEEPROM(300);
  savedSSID.trim(); savedPass.trim(); zaloChatId.trim(); zaloBotToken.trim();
  
  if (zaloChatId.length() < 5) zaloChatId = defaultZaloChatId;
  if (zaloBotToken.length() < 20) zaloBotToken = defaultZaloBotToken;
  
  if (savedSSID.length() > 0) {
    WiFi.mode(WIFI_STA); WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    int retries = 0; while (WiFi.status() != WL_CONNECTED && retries < 30) { delay(500); retries++; }
  }
  
  server.on("/", []() { if(configMode) handleSetupRoot(); else handleNormalRoot(); }); 
  server.on("/save", HTTP_POST, []() { if(configMode) handleSetupSave(); else server.send(404); });
  server.on("/set", []() { if(!configMode) handleNormalSet(); else server.send(404); });
  server.on("/get_alarms", []() { if(!configMode) handleGetAlarms(); else server.send(404); });
  server.on("/api/time", handleGetTime); server.on("/test", handleTestDevice);
  
  if (WiFi.status() == WL_CONNECTED) {
    configMode = false; currentSSID = savedSSID; 
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov"); syncNTPtoRTC(); 
    sendZaloMessage("✅ Hệ thống Hộp thuốc khởi động!\n🌐 IP Web App: http://" + WiFi.localIP().toString());
    server.begin(); pingServer(); 
  } else {
    configMode = true; WiFi.mode(WIFI_AP_STA);
    IPAddress apIP(192, 168, 4, 1); WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Cài Đặt Hộp Thuốc"); dnsServer.start(DNS_PORT, "*", apIP); server.begin();
  }
  delay(1000); 
  
  current_alarm_view = getNextAlarmIndex();
  reset_lcd_screen();
}

void loop() {
  unsigned long current_millis = millis();
  
  if (configMode) dnsServer.processNextRequest();
  server.handleClient();
  
  if (disableApTime > 0 && (current_millis - disableApTime >= 3000)) {
    dnsServer.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); configMode = false; disableApTime = 0;
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov"); syncNTPtoRTC(); pingServer();
    sendZaloMessage("✅ Hệ thống đã cập nhật WiFi thành công!\n🌐 IP Web App: http://" + WiFi.localIP().toString());
  }
  
  if (!configMode && WiFi.status() == WL_CONNECTED && (current_millis - last_zalo_poll >= 3000)) {
    last_zalo_poll = current_millis;
    pollZaloCommands(); 
  }
  
  if (!configMode && WiFi.status() == WL_CONNECTED && (current_millis - last_ntp_sync > 600000)) {
    last_ntp_sync = current_millis;
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov"); syncNTPtoRTC();
  }
  
  if (!is_alarm_ringing && !is_waiting_confirmation) {
    if (current_millis - last_interaction_time > 10000) {
      if (status_mode_screen != 0 || current_alarm_view != getNextAlarmIndex()) {
        status_mode_screen = 0;
        current_alarm_view = getNextAlarmIndex();
        reset_lcd_screen();
      }
    }
  }
  
  if (current_millis - last_time_update >= 1000) {
    last_time_update = current_millis;
    DateTime now = rtc.now();
    current_hour = now.hour(); current_minute = now.minute(); current_second = now.second(); current_dow = now.dayOfTheWeek();
    is_time_synced = true; 
    
    if (status_mode_screen == 0) {
      if (current_minute != last_checked_minute) {
        last_checked_minute = current_minute;
        for (int i = 0; i < 21; i++) {
          if (alarms[i].active && alarms[i].hour == current_hour && alarms[i].minute == current_minute) {
            is_alarm_ringing = true; is_waiting_confirmation = false; ringing_alarm_index = i; 
            status_mode_screen = 0; reset_lcd_screen(); alarm_start_time = current_millis; 
          }
        }
      }
    }
  }

  if (is_alarm_ringing && (current_millis - alarm_start_time >= 60000)) {
    is_alarm_ringing = false; is_waiting_confirmation = true; stopBuzzer();
    reset_lcd_screen(); lcd_print_line(0, " QUA 60S! CHO   "); lcd_print_line(1, " XAC NHAN...    ");
    sendZaloMessage("⚠ CẢNH BÁO: Đã qua 60 giây không có phản hồi! Đã tắt chuông chờ xác nhận.");
  }
  
  bool system_error = (!is_time_synced || (WiFi.status() != WL_CONNECTED && !configMode));
  bool is_testing = (test_start_time > 0 && (current_millis - test_start_time < 5000));
  
  if (status_mode_screen == 4) {
    digitalWrite(STATUS_LED_PIN, HIGH); for(int i=0; i<7; i++) digitalWrite(LED_PINS[i], HIGH);
    stopBuzzer(); was_ringing = false;
  } else if (is_testing) {
    digitalWrite(STATUS_LED_PIN, HIGH); for(int i=0; i<7; i++) digitalWrite(LED_PINS[i], (i == test_led_id) ? HIGH : LOW);
    if (test_buzzer_on) { playBuzzer(); } else { stopBuzzer(); } was_ringing = false;
  } else if (is_alarm_ringing) {
    digitalWrite(STATUS_LED_PIN, system_error ? LOW : HIGH);
    if (!was_ringing) { sendZaloMessage("⏰ ĐẾN GIỜ UỐNG THUỐC RỒI! HÃY MỞ NGĂN THUỐC CÓ ĐÈN SÁNG!!!"); was_ringing = true; }
    if (current_millis % 500 < 250) {
      playBuzzer(); if (ringing_alarm_index >= 0 && ringing_alarm_index < 21) digitalWrite(LED_PINS[ringing_alarm_index / 3], HIGH);
    } else { stopBuzzer(); for(int i=0; i<7; i++) digitalWrite(LED_PINS[i], LOW); }
  } else if (is_waiting_confirmation) {
    was_ringing = false; stopBuzzer(); digitalWrite(STATUS_LED_PIN, system_error ? LOW : HIGH);
    for(int i = 0; i < 7; i++) {
      if (ringing_alarm_index >= 0 && ringing_alarm_index < 21 && i == (ringing_alarm_index / 3)) { digitalWrite(LED_PINS[i], HIGH); } 
      else { digitalWrite(LED_PINS[i], LOW); }
    }
  } else {
    was_ringing = false; stopBuzzer(); digitalWrite(STATUS_LED_PIN, system_error ? LOW : HIGH);
    for(int i=0; i<7; i++) digitalWrite(LED_PINS[i], LOW);
  }
  
  bool btn_current_state = digitalRead(BTN_PIN);
  
  if (btn_current_state == LOW) { 
    if (!btn_was_pressed) {
      btn_was_pressed = true;
      btn_press_start = current_millis;
      long_press_handled = false;
      very_long_press_handled = false;
    } else {
      if (current_millis - btn_press_start >= 5000 && !very_long_press_handled) {
        very_long_press_handled = true;
        reset_lcd_screen();
        lcd_print_line(0, " FACTORY RESET  ");
        lcd_print_line(1, " REBOOTING...   ");
        clearWiFiEEPROM(); 
        delay(2000);
        ESP.restart(); 
      }
      else if (current_millis - btn_press_start >= 1000 && !long_press_handled && !very_long_press_handled) {
        long_press_handled = true;
        status_mode_screen = (status_mode_screen + 1) % 5;
        last_interaction_time = current_millis; 
        reset_lcd_screen();
      }
    }
  } else { 
    if (btn_was_pressed) {
      btn_was_pressed = false;
      if (!long_press_handled && !very_long_press_handled) {
        last_interaction_time = current_millis; 
        
        if (is_alarm_ringing || is_waiting_confirmation) { 
          bool is_late_cancel = is_waiting_confirmation; 
          is_alarm_ringing = false; is_waiting_confirmation = false; stopBuzzer(); 
          for(int i=0; i<7; i++) digitalWrite(LED_PINS[i], LOW); 
          reset_lcd_screen(); lcd_print_line(0, " DA XAC NHAN!   "); lcd_print_line(1, " SENDING ZALO...");
          
          const char* dayNamesFull[] = {"Thứ 2", "Thứ 3", "Thứ 4", "Thứ 5", "Thứ 6", "Thứ 7", "Chủ Nhật"};
          const char* sessionNamesFull[] = {"Sáng", "Trưa", "Chiều"}; String slotInfo = "";
          if (ringing_alarm_index >= 0 && ringing_alarm_index < 21) { slotInfo = String(dayNamesFull[ringing_alarm_index / 3]) + " - " + String(sessionNamesFull[ringing_alarm_index % 3]); }
          if (is_late_cancel) { sendZaloMessage("☑️ Đã tắt đèn cữ: [" + slotInfo + "] sau khi quá thời gian."); } 
          else { sendZaloMessage("✅ Đã uống thuốc cữ: [" + slotInfo + "] và tắt báo động an toàn."); }
          
          ringing_alarm_index = -1; status_mode_screen = 0; 
          current_alarm_view = getNextAlarmIndex(); 
        } else {
          current_alarm_view = (current_alarm_view + 1) % 21;
          reset_lcd_screen();
        }
      }
    }
  }
  
  char line1_buf[17]; char line2_buf[17];
  if (status_mode_screen == 0) {
    const char* dow_str[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"}; 
    const char* status_str = is_time_synced ? ((is_alarm_ringing || is_waiting_confirmation) ? " DRG" : "    ") : " SYNC";
    snprintf(line1_buf, sizeof(line1_buf), "%s %02d:%02d:%02d%s", dow_str[current_dow], current_hour, current_minute, current_second, status_str);
    const char* dayNames[] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"}; const char* sessionNames[] = {"SA", "TR", "CH"};
    const char* state = alarms[current_alarm_view].active ? "[ON]" : "[OF]";
    snprintf(line2_buf, sizeof(line2_buf), "%s-%s %02d:%02d %s", dayNames[current_alarm_view / 3], sessionNames[current_alarm_view % 3], alarms[current_alarm_view].hour, alarms[current_alarm_view].minute, state);
  } else if (status_mode_screen == 1) {
    snprintf(line1_buf, 17, " SYSTEM STATUS  "); snprintf(line2_buf, 17, " WIFI: %s", (WiFi.status() == WL_CONNECTED) ? "CONNECTED  " : "DISCONN    ");
  } else if (status_mode_screen == 2) {
    snprintf(line1_buf, 17, " NODE.JS SERVER "); snprintf(line2_buf, 17, " POLLING: AUTO  ");
  } else if (status_mode_screen == 3) {
    snprintf(line1_buf, 17, " SMART PILL BOX "); snprintf(line2_buf, 17, " FIRMWARE V9.5  ");
  } else if (status_mode_screen == 4) {
    snprintf(line1_buf, 17, " SYSTEM DEBUG   "); snprintf(line2_buf, 17, " ALL LEDS ON    ");
  }
  
  if (strncmp(line1_buf, last_lcd_line1, 16) != 0) { lcd_print_line(0, line1_buf); strncpy(last_lcd_line1, line1_buf, 16); }
  if (strncmp(line2_buf, last_lcd_line2, 16) != 0) { lcd_print_line(1, line2_buf); strncpy(last_lcd_line2, line2_buf, 16); }
}