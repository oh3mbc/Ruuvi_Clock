#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include "time.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#define H_TYP MD_MAX72XX::FC16_HW
#define M_DEV 4
#define CS_P 5
#define BTN_P 4
#define BOOT_PIN 0

MD_Parola P = MD_Parola(H_TYP, CS_P, M_DEV);
Preferences pr;

char t_mac[18] = "aa:bb:cc:dd:ee:ff";
char ntp[]     = "fi.pool.ntp.org";
char tz[]      = "EET-2EEST,M3.5.0/3,M10.5.0/4";
char d_macs[10][18];
char buf[16];

uint8_t msk = 0x07;
int d_cnt = 0, scr = 0;
bool cfg = false;
volatile bool vld = false;
BLEScan* scan = nullptr;

volatile float c_t = 0, c_h = 0, c_pm = 0;
volatile uint32_t c_p = 0;
volatile uint16_t c_c = 0;
unsigned long l_sw = 0, l_sc = 0, l_sy = 0;
WiFiManager wm;

int get_n(int c) {
  for (int i = 1; i <= 7; i++) {
    int n = (c + i) % 7;
    if (msk & (1 << n)) return n;
  }
  return 0;
}

void upd() {
  struct tm t;
  bool g = getLocalTime(&t);
  switch (scr) {
    case 0: 
      if (g) {
        if (millis() % 1000 < 500) strftime(buf, sizeof(buf), "%H:%M", &t);
        else strftime(buf, sizeof(buf), "%H %M", &t);
      } else strcpy(buf, "SYNC"); 
      break;
    case 1: if (g) strftime(buf, sizeof(buf), "%d.%m", &t); else strcpy(buf, "DATE"); break;
    case 2: if (vld) { char s[8]; dtostrf(c_t, 4, 1, s); snprintf(buf, sizeof(buf), "%sC", s); } else strcpy(buf, "TEMP"); break;
    case 3: if (vld) { char s[8]; dtostrf(c_h, 4, 1, s); snprintf(buf, sizeof(buf), "%sH", s); } else strcpy(buf, "HUM"); break;
    case 4: if (vld && c_p != 0) snprintf(buf, sizeof(buf), "%dP", c_p / 100); else strcpy(buf, "PRES"); break;
    case 5: if (vld && c_pm >= 0) { char s[8]; dtostrf(c_pm, 4, 1, s); snprintf(buf, sizeof(buf), "%sM", s); } else strcpy(buf, "PM25"); break;
    case 6: if (vld && c_c != 0xFFFF) snprintf(buf, sizeof(buf), "%dG", c_c); else strcpy(buf, "CO2"); break;
  }
  Serial.print("Display: "); Serial.println(buf);
  P.print(buf);
}

void sy() {
  Serial.println("Starting network sync..."); P.print("SYNC"); WiFi.mode(WIFI_STA); WiFi.begin(); int c = 0;
  while (WiFi.status() != WL_CONNECTED && c++ < 30) delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(tz, ntp); struct tm t; int k = 0;
    while (!getLocalTime(&t) && k++ < 10) delay(500);
    l_sy = millis(); Serial.println("Time synced.");
  } else { Serial.println("WiFi Sync timeout."); }
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
}

void scn() {
  d_cnt = 0; P.print("SCAN"); BLEDevice::init(""); BLEScan* p = BLEDevice::getScan(); p->setActiveScan(true); BLEScanResults f = p->start(15, false);
  for (int i = 0; i < f.getCount() && d_cnt < 10; i++) {
    BLEAdvertisedDevice d = f.getDevice(i); if (d.haveManufacturerData()) {
      std::string s = d.getManufacturerData(); if (s.length() >= 4 && (uint8_t)s[0] == 0x99 && (uint8_t)s[1] == 0x04) {
        String m = d.getAddress().toString().c_str(); m.toLowerCase(); bool r = false;
        for (int j = 0; j < d_cnt; j++) { if (String(d_macs[j]) == m) { r = true; break; } }
        if (!r) {
          strcpy(d_macs[d_cnt], m.c_str()); uint8_t fmt = (uint8_t)s[2]; String t_str = "Unknown";
          if (fmt == 6) t_str = "Ruuvi Air (v6)"; else if (fmt == 5) t_str = "v5"; else if (fmt == 3) t_str = "v3";
          Serial.print("Found Ruuvi Device -> MAC: "); Serial.print(m); Serial.print(" | Type: "); Serial.println(t_str); d_cnt++;
        }
      }
    }
  }
  BLEDevice::deinit(false);
}

void sv() {
  if (wm.server->hasArg("sel_mac")) {
    String s = wm.server->arg("sel_mac"); if (s.length() > 0 && s != "aa:bb:cc:dd:ee:ff") strcpy(t_mac, s.c_str());
    uint8_t k = 0; if (wm.server->arg("v_t") == "1") k |= (1 << 0); if (wm.server->arg("v_d") == "1") k |= (1 << 1);
    if (wm.server->arg("v_te") == "1") k |= (1 << 2); if (wm.server->arg("v_h") == "1") k |= (1 << 3);
    if (wm.server->arg("v_p") == "1") k |= (1 << 4); if (wm.server->arg("v_pm") == "1") k |= (1 << 5);
    if (wm.server->arg("v_c") == "1") k |= (1 << 6); msk = k;
  }
  pr.begin("ruuvi-clock", false); pr.putString("mac", t_mac); pr.putBytes("mask", &msk, 1); pr.putBool("w_saved", true); pr.end();
}

void setup() {
  Serial.begin(115200); delay(1000); Serial.println("\n--- ESP32 CLOCK SYSTEM BOOT ---"); pinMode(BTN_P, INPUT_PULLUP);
  P.begin(); P.setIntensity(4); P.displayClear(); P.setTextAlignment(PA_CENTER);
  P.setFont(nullptr); 
  P.setCharSpacing(1); 
  P.print("STRT"); delay(500);
  
  bool has_w = false; pr.begin("ruuvi-clock", false);
  if (pr.isKey("mac")) pr.getString("mac", t_mac, sizeof(t_mac));
  if (pr.isKey("mask")) pr.getBytes("mask", &msk, 1);
  if (pr.isKey("w_saved")) has_w = pr.getBool("w_saved"); pr.end();
  
  if (digitalRead(BTN_P) == LOW || !has_w) {
    cfg = true; P.print("AP"); scn(); wm.setSaveParamsCallback(sv); std::vector<const char*> menu = {"wifi", "param", "close"}; wm.setMenu(menu);
    wm.setCustomHeadElement("<script>window.onload=function(){document.querySelector('button.wifi').innerHTML='Configure WiFi';document.querySelector('button.param').innerHTML='Configure Display';};</script>");
    wm.setCustomMenuHTML("<style>button.wifi{font-size:0px;}button.wifi:before{content:'Config WiFi';font-size:16px;font-weight:bold;}button.param{font-size:0px;}button.param:before{content:'Config Display';font-size:16px;font-weight:bold;}</style>");
    wm.setWebServerCallback([](){wm.server->on("/close", HTTP_GET, [](){wm.server->send(200, "text/plain", "Rebooting..."); P.print("REBOOT"); delay(1000); ESP.restart();});});
    String h = "<br/><label><b>Select RuuviTag</b></label><select name='sel_mac'><option value='" + String(t_mac) + "'>Current (" + String(t_mac) + ")</option>";
    for (int i = 0; i < d_cnt; i++) h += "<option value='" + String(d_macs[i]) + "'>" + String(d_macs[i]) + "</option>";
    h += "</select><br/><br/><b>Choose Presentation Layout:</b><br/>";
    h += "<input type='checkbox' name='v_t' value='1' " + String((msk & (1 << 0)) ? "checked" : "") + "> Time<br/>";
    h += "<input type='checkbox' name='v_d' value='1' " + String((msk & (1 << 1)) ? "checked" : "") + "> Date<br/>";
    h += "<input type='checkbox' name='v_te' value='1' " + String((msk & (1 << 2)) ? "checked" : "") + "> Temp<br/>";
    h += "<input type='checkbox' name='v_h' value='1' " + String((msk & (1 << 3)) ? "checked" : "") + "> Hum<br/>";
    h += "<input type='checkbox' name='v_p' value='1' " + String((msk & (1 << 4)) ? "checked" : "") + "> Pres<br/>";
    h += "<input type='checkbox' name='v_pm' value='1' " + String((msk & (1 << 5)) ? "checked" : "") + "> PM2.5<br/>";
    h += "<input type='checkbox' name='v_c' value='1' " + String((msk & (1 << 6)) ? "checked" : "") + "> CO2<br/>";
    WiFiManagerParameter f(h.c_str()); wm.addParameter(&f); wm.startConfigPortal("Ruuvi_Setup", "");
    P.print("REBOOT"); delay(1000); ESP.restart();
  }
  sy(); BLEDevice::init(""); scan = BLEDevice::getScan(); scan->setActiveScan(true); scan->setInterval(100); scan->setWindow(99);
}

void loop() {
  if (cfg) return;
  
  if (digitalRead(BOOT_PIN) == LOW) {
    unsigned long start = millis(); bool lp = true;
    while (millis() - start < 3000) {
      int rem = 3 - ((millis() - start) / 1000); snprintf(buf, sizeof(buf), "CLR %d", rem); P.print(buf);
      if (digitalRead(BOOT_PIN) == HIGH) { lp = false; break; } delay(50);
    }
    if (lp) { P.print("WIPE"); wm.resetSettings(); pr.begin("ruuvi-clock", false); pr.clear(); pr.end(); delay(1500); ESP.restart(); } else { upd(); }
  }
  
  if (millis() - l_sw >= 4000 || scr == 0) { 
    if (millis() - l_sw >= 4000) { scr = get_n(scr); l_sw = millis(); }
    upd();
  }
  
  if (millis() - l_sy >= 7200000) sy();
  if (millis() - l_sc >= 30000 || l_sc == 0) {
    l_sc = millis(); BLEScanResults f = scan->start(2, false); String t = String(t_mac); t.toLowerCase();
    for (int i = 0; i < f.getCount(); i++) {
      BLEAdvertisedDevice d = f.getDevice(i); String m = d.getAddress().toString().c_str(); m.toLowerCase();
      if (m == t && d.haveManufacturerData()) {
        std::string s = d.getManufacturerData();
        if (s.length() >= 4 && (uint8_t)s[0] == 0x99 && (uint8_t)s[1] == 0x04) {
          uint8_t fmt = (uint8_t)s[2];
          if (fmt == 6 && s.length() >= 18) {
            int16_t r_t = ((uint8_t)s[3] << 8) | (uint8_t)s[4]; if (r_t != 0x8000) c_t = r_t * 0.005;
            uint16_t r_h = ((uint8_t)s[5] << 8) | (uint8_t)s[6]; if (r_h != 0xFFFF) c_h = r_h * 0.0025;
            uint32_t r_p = ((uint8_t)s[7] << 24) | ((uint8_t)s[8] << 16) | ((uint8_t)s[9] << 8) | (uint8_t)s[10]; if (r_p != 0xFFFF) c_p = r_p + 50000;
            uint16_t r_pm = ((uint8_t)s[14] << 8) | (uint8_t)s[15]; if (r_pm != 0xFFFF) c_pm = r_pm * 0.1;
            uint16_t r_co = ((uint8_t)s[16] << 8) | (uint8_t)s[17]; if (r_co != 0xFFFF) c_c = r_co; vld = true;
          } else if (fmt == 5 && s.length() >= 18) {
            int16_t r_t = ((uint8_t)s[3] << 8) | (uint8_t)s[4]; if (r_t != 0x8000) c_t = r_t * 0.005;
            uint16_t r_h = ((uint8_t)s[5] << 8) | (uint8_t)s[6]; if (r_h != 0xFFFF) c_h = r_h * 0.0025;
            uint32_t r_p = ((uint8_t)s[7] << 8) | (uint8_t)s[8]; if (r_p != 0xFFFF) c_p = r_p + 50000;
            c_pm = -1; c_c = 0xFFFF; vld = true;
          } else if (fmt == 3 && s.length() >= 14) {
            c_t = (uint8_t)s[4] + (uint8_t)s[5] / 100.0; if ((uint8_t)s[4] & 0x80) c_t = -c_t;
            c_h = (uint8_t)s[3] * 0.5; uint32_t r_p = ((uint8_t)s[6] << 8) | (uint8_t)s[7]; c_p = r_p + 50000; c_pm = -1; c_c = 0xFFFF; vld = true;
          }
          break;
        }
      }
    }
    scan->clearResults();
  }
  yield();
}
