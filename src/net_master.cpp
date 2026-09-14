#include "net_master.h"
#include "master_config.h"
#include "log.h"
#include <WiFi.h>
#include <time.h>

static bool     staWanted   = false;
static uint32_t lastAttempt = 0;
static bool     wasUp       = false;
static bool     sntpArmed   = false;

void netBegin() {
  staWanted = mcfg.staEnabled && mcfg.staSsid[0];
  if (!staWanted) return;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // menor latencia para Modbus TCP; gateway con alimentacion fija
  WiFi.setAutoReconnect(true);

  if (mcfg.staStatic && mcfg.staIp) {
    // Sin dns1/dns2, WiFi.config() los deja en 0.0.0.0 -- con IP fija el
    // equipo queda sin ningun DNS configurado (a diferencia de DHCP, que
    // lo recibe solo del router) y hostByName() falla siempre, sin
    // importar la red: asi fallaba el chequeo OTA (github.com) y quedaba
    // rota tambien la hora SNTP (pool.ntp.org/time.google.com) que usa el
    // cierre automatico de dia/mes. dns1 = el propio gateway LAN (la
    // mayoria hace de proxy DNS); dns2 = publico de respaldo.
    WiFi.config(IPAddress(mcfg.staIp), IPAddress(mcfg.staGw),
                IPAddress(mcfg.staMask ? mcfg.staMask : 0xFFFFFF00UL),
                IPAddress(mcfg.staGw), IPAddress(8, 8, 8, 8));
  }
  WiFi.begin(mcfg.staSsid, mcfg.staPass);
  lastAttempt = millis();
  LOGF("[net] STA -> '%s'%s\n", mcfg.staSsid, mcfg.staStatic ? " (IP fija)" : " (DHCP)");
}

void netLoop() {
  if (!staWanted) return;

  bool up = (WiFi.status() == WL_CONNECTED);
  if (up && !wasUp) {
    LOGF("[net] STA OK  ip=%s  rssi=%d\n",
         WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    if (!sntpArmed) {   // SNTP: hora para los timestamps del puente MQTT
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      sntpArmed = true;
    }
  }
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

uint32_t netEpoch() {
  time_t t = time(nullptr);
  return (t > 1600000000) ? (uint32_t)t : 0;   // ~2020-09 -> ya sincronizado
}
bool netTimeOk() { return netEpoch() != 0; }
