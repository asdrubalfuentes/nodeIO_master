#include "net_master.h"
#include "master_config.h"
#include "log.h"
#include <WiFi.h>

static bool     staWanted   = false;
static uint32_t lastAttempt = 0;
static bool     wasUp       = false;

void netBegin() {
  staWanted = mcfg.staEnabled && mcfg.staSsid[0];
  if (!staWanted) return;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // menor latencia para Modbus TCP; gateway con alimentacion fija
  WiFi.setAutoReconnect(true);

  if (mcfg.staStatic && mcfg.staIp) {
    WiFi.config(IPAddress(mcfg.staIp), IPAddress(mcfg.staGw),
                IPAddress(mcfg.staMask ? mcfg.staMask : 0xFFFFFF00UL));
  }
  WiFi.begin(mcfg.staSsid, mcfg.staPass);
  lastAttempt = millis();
  LOGF("[net] STA -> '%s'%s\n", mcfg.staSsid, mcfg.staStatic ? " (IP fija)" : " (DHCP)");
}

void netLoop() {
  if (!staWanted) return;

  bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !wasUp) LOGF("[net] STA OK  ip=%s  rssi=%d\n",
                         WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  if (!up && wasUp) LOGLN("[net] STA caida");
  wasUp = up;
  if (up) return;

  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();
  LOGLN("[net] STA reintento");
  WiFi.disconnect(false, false);
  WiFi.begin(mcfg.staSsid, mcfg.staPass);
}

bool   netStaUp() { return staWanted && WiFi.status() == WL_CONNECTED; }
String netStaIp() { return netStaUp() ? WiFi.localIP().toString() : String("0.0.0.0"); }
