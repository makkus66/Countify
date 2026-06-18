#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <HTTPClient.h>

const int SENSOR1_PIN = 3;
const int SENSOR2_PIN = 4;
const int LED_PIN = 8;

// Schwellwerte – werden durch Kalibrierung gesetzt
int SENSOR1_SCHWELLWERT = 0;
int SENSOR2_SCHWELLWERT = 0;
bool kalibriert = false;

const int HYSTERESE = 100;

const String SERVER_URL = "http://172.20.10.3:5000/u  pdate";

int personCount = 0;
unsigned long letzterSend = 0;
unsigned long ledBis = 0;

enum Zustand { IDLE, S1_ZUERST, S2_ZUERST };
Zustand zustand = IDLE;
unsigned long zustandZeit = 0;
const unsigned long TIMEOUT_MS = 5000;

const unsigned long SPERRZEIT = 1500;
unsigned long gesperrtBis = 0;

const unsigned long ENTPRELLZEIT = 80;
unsigned long s1AktivSeit = 0;
unsigned long s2AktivSeit = 0;
bool s1Vor    = false;
bool s2Vor    = false;
bool s1Stabil = false;
bool s2Stabil = false;

// Kalibrierung nicht-blockierend
bool kalAktiv = false;
unsigned long kalStart = 0;
long kalS1Summe = 0, kalS2Summe = 0;
int kalMessungen = 0;
const unsigned long KAL_DAUER = 8000;

int leseStabil(int pin) {
  long summe = 0;
  for (int i = 0; i < 5; i++) {
    summe += analogRead(pin);
    delay(2);
  }
  return summe / 5;
}

WebServer server(80);

void ledGruen() { neopixelWrite(LED_PIN, 0, 255, 0); ledBis = millis() + 1500; }
void ledRot()   { neopixelWrite(LED_PIN, 255, 0, 0); ledBis = millis() + 1500; }
void ledBlau()  { neopixelWrite(LED_PIN, 0, 0, 255); }
void ledAus()   { neopixelWrite(LED_PIN, 0, 0, 0); }

void resetZustand() {
  zustand      = IDLE;
  gesperrtBis  = 0;
  s1Vor        = false; s2Vor        = false;
  s1Stabil     = false; s2Stabil     = false;
  s1AktivSeit  = 0;     s2AktivSeit  = 0;
}

void sendeAnServer() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  String json = "{\"personen\":"  + String(personCount) +
                ",\"sensor1\":"   + String(leseStabil(SENSOR1_PIN)) +
                ",\"sensor2\":"   + String(leseStabil(SENSOR2_PIN)) + "}";
  int code = http.POST(json);
  Serial.println("Server POST: " + String(code));
  http.end();
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    String html = "<html><body style='font-family:Arial;text-align:center;margin-top:50px'>";
    html += "<h1>Countify</h1>";
    html += "<h2>Personen: <b>" + String(personCount) + "</b></h2>";
    html += "<p>S1: " + String(leseStabil(SENSOR1_PIN)) + " | SW: " + String(SENSOR1_SCHWELLWERT) + "</p>";
    html += "<p>S2: " + String(leseStabil(SENSOR2_PIN)) + " | SW: " + String(SENSOR2_SCHWELLWERT) + "</p>";
    if (kalAktiv) {
      int rest = (KAL_DAUER - (millis() - kalStart)) / 1000;
      html += "<p style='color:blue'><b>Kalibrierung läuft... noch " + String(rest) + "s</b></p>";
    } else if (kalibriert) {
      html += "<p style='color:green'>✓ Kalibriert</p>";
    } else {
      html += "<p style='color:red'>Nicht kalibriert</p>";
    }
    html += "<br><a href='/reset'><button>Reset</button></a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/api", HTTP_GET, []() {
    String json = "{";
    json += "\"personen\":"            + String(personCount)             + ",";
    json += "\"sensor1\":"             + String(leseStabil(SENSOR1_PIN)) + ",";
    json += "\"sensor2\":"             + String(leseStabil(SENSOR2_PIN)) + ",";
    json += "\"sensor1_schwellwert\":" + String(SENSOR1_SCHWELLWERT)     + ",";
    json += "\"sensor2_schwellwert\":" + String(SENSOR2_SCHWELLWERT)     + ",";
    json += "\"kalibriert\":"          + String(kalibriert ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  });

  // Dashboard Button ruft das auf
  server.on("/api/calibrate", HTTP_POST, []() {
    kalAktiv     = true;
    kalStart     = millis();
    kalS1Summe   = 0;
    kalS2Summe   = 0;
    kalMessungen = 0;
    kalibriert   = false;
    personCount  = 0;
    resetZustand();
    ledBlau();
    Serial.println("=== KALIBRIERUNG GESTARTET – Türrahmen messen ===");
    server.send(200, "application/json",
      "{\"status\":\"gestartet\",\"dauer\":" + String(KAL_DAUER/1000) + "}");
  });

  // Dashboard pollt das um Status zu bekommen
  server.on("/api/kal_status", HTTP_GET, []() {
    if (kalAktiv) {
      int vergangen = (millis() - kalStart) / 1000;
      int rest = (KAL_DAUER - (millis() - kalStart)) / 1000;
      String json = "{\"fertig\":false,\"vergangen\":" + String(vergangen) +
                    ",\"rest\":" + String(rest) + "}";
      server.send(200, "application/json", json);
    } else {
      String json = "{";
      json += "\"fertig\":true,";
      json += "\"kalibriert\":" + String(kalibriert ? "true" : "false") + ",";
      json += "\"sensor1_schwellwert\":" + String(SENSOR1_SCHWELLWERT) + ",";
      json += "\"sensor2_schwellwert\":" + String(SENSOR2_SCHWELLWERT);
      json += "}";
      server.send(200, "application/json", json);
    }
  });

  server.on("/reset", HTTP_GET, []() {
    personCount = 0;
    resetZustand();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });
}

void loopKalibrierung() {
  if (!kalAktiv) return;

  // Werte sammeln
  kalS1Summe += analogRead(SENSOR1_PIN);
  kalS2Summe += analogRead(SENSOR2_PIN);
  kalMessungen++;

  if (millis() - kalStart >= KAL_DAUER) {
    // Durchschnitt = Ruhewert (Türrahmen/Wand)
    int s1Ruhe = kalS1Summe / kalMessungen;
    int s2Ruhe = kalS2Summe / kalMessungen;

    // Schwellwert = Ruhewert - 15% des Ruhewerts
    // Person kommt näher → Wert SINKT → wenn Wert < Schwellwert → Person da
    SENSOR1_SCHWELLWERT = s1Ruhe - (int)(s1Ruhe * 0.15f);
    SENSOR2_SCHWELLWERT = s2Ruhe - (int)(s2Ruhe * 0.15f);

    Serial.printf("S1 Ruhe: %d → SW: %d\n", s1Ruhe, SENSOR1_SCHWELLWERT);
    Serial.printf("S2 Ruhe: %d → SW: %d\n", s2Ruhe, SENSOR2_SCHWELLWERT);

    kalAktiv   = false;
    kalibriert = true;
    ledAus();
    resetZustand();
    sendeAnServer();
    Serial.println("=== KALIBRIERUNG FERTIG ===");
  }
}

void setup() {
  Serial.begin(115200);
  ledAus();

  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  if (!wm.autoConnect("Countify-Setup", "12345678")) {
    Serial.println("WLAN fehlgeschlagen – Neustart");
    ESP.restart();
  }
  Serial.print("WLAN verbunden! IP: ");
  Serial.println(WiFi.localIP());

  setupWebServer();
  server.begin();
  Serial.println("Webserver laeuft.");
}

void loop() {
  server.handleClient();

  if (ledBis > 0 && millis() > ledBis) { ledAus(); ledBis = 0; }

  // Kalibrierung läuft
  if (kalAktiv) {
    loopKalibrierung();
    return;
  }

  // Noch nicht kalibriert – nichts zählen
  if (!kalibriert) return;

  unsigned long jetzt = millis();
  if (jetzt < gesperrtBis) return;

  int roh1 = leseStabil(SENSOR1_PIN);
  int roh2 = leseStabil(SENSOR2_PIN);

  // UMGEKEHRTE LOGIK: Person da = Wert UNTER Schwellwert
  // (Person näher als Türrahmen → kleinerer Wert)
  bool s1Roh = s1Vor ? (roh1 < SENSOR1_SCHWELLWERT + HYSTERESE)
                     : (roh1 < SENSOR1_SCHWELLWERT - HYSTERESE);
  bool s2Roh = s2Vor ? (roh2 < SENSOR2_SCHWELLWERT + HYSTERESE)
                     : (roh2 < SENSOR2_SCHWELLWERT - HYSTERESE);
  s1Vor = s1Roh;
  s2Vor = s2Roh;

  if (s1Roh) {
    if (s1AktivSeit == 0) s1AktivSeit = jetzt;
    s1Stabil = (jetzt - s1AktivSeit) >= ENTPRELLZEIT;
  } else {
    s1AktivSeit = 0;
    s1Stabil    = false;
  }

  if (s2Roh) {
    if (s2AktivSeit == 0) s2AktivSeit = jetzt;
    s2Stabil = (jetzt - s2AktivSeit) >= ENTPRELLZEIT;
  } else {
    s2AktivSeit = 0;
    s2Stabil    = false;
  }

  Serial.printf("S1: %d (%s) | S2: %d (%s) | Zustand: %d\n",
    roh1, s1Stabil ? "AKT" : "frei",
    roh2, s2Stabil ? "AKT" : "frei",
    zustand);

  switch (zustand) {

    case IDLE:
      if (s1Stabil && !s2Stabil) {
        zustand     = S1_ZUERST;
        zustandZeit = jetzt;
        Serial.println("S1 ausgeloest – warte auf S2");
      } else if (s2Stabil && !s1Stabil) {
        zustand     = S2_ZUERST;
        zustandZeit = jetzt;
        Serial.println("S2 ausgeloest – warte auf S1");
      }
      break;

    case S1_ZUERST:
      if (s2Stabil) {
        if (personCount > 0) personCount--;
        ledRot();
        Serial.print("RAUS -1 | Personen: "); Serial.println(personCount);
        sendeAnServer();
        zustand     = IDLE;
        gesperrtBis = jetzt + SPERRZEIT;
        s1AktivSeit = 0; s1Stabil = false; s1Vor = false;
        s2AktivSeit = 0; s2Stabil = false; s2Vor = false;
      } else if (jetzt - zustandZeit > TIMEOUT_MS) {
        Serial.println("Timeout S1 – reset");
        zustand = IDLE;
      }
      break;

    case S2_ZUERST:
      if (s1Stabil) {
        personCount++;
        ledGruen();
        Serial.print("REIN +1 | Personen: "); Serial.println(personCount);
        sendeAnServer();
        zustand     = IDLE;
        gesperrtBis = jetzt + SPERRZEIT;
        s1AktivSeit = 0; s1Stabil = false; s1Vor = false;
        s2AktivSeit = 0; s2Stabil = false; s2Vor = false;
      } else if (jetzt - zustandZeit > TIMEOUT_MS) {
        Serial.println("Timeout S2 – reset");
        zustand = IDLE;
      }
      break;
  }

  if (millis() - letzterSend > 5000) {
    letzterSend = millis();
    sendeAnServer();
  }
}