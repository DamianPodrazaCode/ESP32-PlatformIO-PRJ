/*
 * ============================================================
 * STEROWNIK HARMONOGRAMU - ESP32
 * ============================================================
 * Wersja: 1.0
 * Platforma: ESP32 (LC Technology DC5-60V 2 Channel Relay)
 * ============================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <time.h>

// ============================================================
// KONFIGURACJA PINÓW
// ============================================================
#define RL1_PIN 16     // Przekaźnik 1
#define RL2_PIN 17     // Przekaźnik 2
#define RESET_HW_PIN 0 // Przycisk reset (aktywny LOW, wewnętrzny pull-up)

// ============================================================
// KONFIGURACJA EEPROM
// ============================================================
#define EEPROM_SIZE 512
#define ADDR_MAGIC 0
#define ADDR_SSID 4
#define ADDR_PASS 68
#define ADDR_SCHEDULE_ON 132
#define ADDR_SCHEDULE_DATA 133 // 7 dni x 6 bajtów = 42 bajty
#define EEPROM_MAGIC 0xA5B6C7D9

// ============================================================
// KONFIGURACJA DOMYŚLNA
// ============================================================
#define AP_SSID "Harmonogram_Setup"
#define AP_PASS "12345678"
#define MDNS_NAME "harm"
#define DNS_PORT 53

// ============================================================
// KONFIGURACJA NTP
// ============================================================
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.google.com"
#define NTP_UPDATE_MS 600000 // 10 minut

// ============================================================
// STRUKTURA HARMONOGRAMU
// ============================================================
struct ScheduleDay
{
    uint8_t hourOn;    // 0-23
    uint8_t minuteOn;  // 0-59
    uint8_t hourOff;   // 0-23
    uint8_t minuteOff; // 0-59
    uint8_t relay;     // 1, 2, lub 3 (oba)
    uint8_t active;    // 0 lub 1
};

// ============================================================
// ZMIENNE GLOBALNE
// ============================================================
WebServer server(80);
DNSServer dnsServer;

String wifiSSID = "";
String wifiPassword = "";
bool isAPMode = true;

bool scheduleRunning = false;
ScheduleDay schedule[7]; // 0=Poniedziałek, 6=Niedziela

bool relay1State = false;
bool relay2State = false;

unsigned long lastNTPUpdate = 0;
unsigned long resetButtonPressTime = 0;
unsigned long lastScheduleCheck = 0;

const char *dayNamesShort[] = {"Pon", "Wt", "Sr", "Czw", "Pt", "Sob", "Ndz"};
const char *dayNamesFull[] = {"Poniedzialek", "Wtorek", "Sroda", "Czwartek", "Piatek", "Sobota", "Niedziela"};

// ============================================================
// DEKLARACJE FUNKCJI
// ============================================================
void setupWiFi();
void setupMDNS();
void setupNTP();
void checkResetButton();
void checkSchedule();
void setRelay(int relay, bool state);

void saveWiFiCredentials();
void loadWiFiCredentials();
void saveSchedule();
void loadSchedule();
void factoryReset();
void initDefaultSchedule();

void handleAPConfig();
void handleConnect();
void handleRoot();
void handleAPI();
void handleSetSchedule();
void forceScheduleCheck();
void handleSetRunning();
void handleReset();
void handleCaptivePortal();
void handleNotFound();
void handleFavicon();
void handleNotFoundNormal();

String getFormattedTime();
String getFormattedDate();
int getCurrentDayOfWeek();

// ============================================================
// SETUP
// ============================================================
void setup()
{
    Serial.begin(115200);
    Serial.println(F("\n\n=== STEROWNIK HARMONOGRAMU ESP32 v1.0 ==="));

    // Piny
    pinMode(RESET_HW_PIN, INPUT_PULLUP);
    pinMode(RL1_PIN, OUTPUT);
    pinMode(RL2_PIN, OUTPUT);
    digitalWrite(RL1_PIN, LOW);
    digitalWrite(RL2_PIN, LOW);

    // EEPROM
    EEPROM.begin(EEPROM_SIZE);

    // Inicjalizacja domyślnego harmonogramu
    initDefaultSchedule();

    // Reset podczas startu
    if (digitalRead(RESET_HW_PIN) == LOW)
    {
        Serial.println(F("Reset button pressed at boot..."));
        delay(3000);
        if (digitalRead(RESET_HW_PIN) == LOW)
        {
            factoryReset();
        }
    }

    loadWiFiCredentials();
    setupWiFi();

    if (!isAPMode)
    {
        loadSchedule();
        setupMDNS();
        setupNTP();
    }

    // Serwer WWW
    if (isAPMode)
    {
        server.on("/", HTTP_GET, handleAPConfig);
        server.on("/connect", HTTP_POST, handleConnect);
        server.on("/generate_204", HTTP_GET, handleCaptivePortal);
        server.on("/gen_204", HTTP_GET, handleCaptivePortal);
        server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
        server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
        server.on("/success.txt", HTTP_GET, handleCaptivePortal);
        server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
        server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);
        server.on("/redirect", HTTP_GET, handleCaptivePortal);
        server.on("/canonical.html", HTTP_GET, handleCaptivePortal);
        server.onNotFound(handleNotFound);
    }
    else
    {
        server.on("/", HTTP_GET, handleRoot);
        server.on("/api", HTTP_GET, handleAPI);
        server.on("/setSchedule", HTTP_POST, handleSetSchedule);
        server.on("/setRunning", HTTP_POST, handleSetRunning);
        server.on("/reset", HTTP_POST, handleReset);

        // Obsługa favicon i innych zasobów
        server.on("/favicon.ico", HTTP_GET, handleFavicon);

        // Handler dla nieznanych żądań
        server.onNotFound(handleNotFoundNormal);
    }
    server.begin();

    Serial.print(F("Free heap: "));
    Serial.println(ESP.getFreeHeap());
    Serial.println(F("System ready!"));
}

// ============================================================
// LOOP
// ============================================================
void loop()
{
    if (isAPMode)
    {
        dnsServer.processNextRequest();
    }

    server.handleClient();
    checkResetButton();

    if (!isAPMode)
    {
        // Aktualizacja NTP co 10 minut
        if (millis() - lastNTPUpdate >= NTP_UPDATE_MS)
        {
            configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
            tzset();
            lastNTPUpdate = millis();
            Serial.println(F("NTP updated"));
        }

        // Sprawdzanie harmonogramu co sekundę
        if (millis() - lastScheduleCheck >= 1000)
        {
            checkSchedule();
            lastScheduleCheck = millis();
        }
    }

    delay(1);
}

// ============================================================
// FUNKCJE SYSTEMOWE
// ============================================================
void setupWiFi()
{
    if (isAPMode)
    {
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(
            IPAddress(192, 168, 4, 1),
            IPAddress(192, 168, 4, 1),
            IPAddress(255, 255, 255, 0));
        WiFi.softAP(AP_SSID, AP_PASS);

        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

        Serial.println(F("=== CAPTIVE PORTAL ACTIVE ==="));
        Serial.print(F("AP SSID: "));
        Serial.println(AP_SSID);
        Serial.print(F("AP Password: "));
        Serial.println(AP_PASS);
        Serial.print(F("AP IP: "));
        Serial.println(WiFi.softAPIP());
    }
    else
    {
        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

        Serial.print(F("Connecting to: "));
        Serial.println(wifiSSID);

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 40)
        {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println(F("\n=== CONNECTED ==="));
            Serial.print(F("IP: "));
            Serial.println(WiFi.localIP());
        }
        else
        {
            Serial.println(F("\nConnection failed, switching to AP"));
            isAPMode = true;
            WiFi.mode(WIFI_AP);
            WiFi.softAPConfig(
                IPAddress(192, 168, 4, 1),
                IPAddress(192, 168, 4, 1),
                IPAddress(255, 255, 255, 0));
            WiFi.softAP(AP_SSID, AP_PASS);
            dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
            dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        }
    }
}

void setupMDNS()
{
    if (MDNS.begin(MDNS_NAME))
    {
        MDNS.addService("http", "tcp", 80);
        Serial.println(F("=== mDNS ACTIVE ==="));
        Serial.print(F("Access via: http://"));
        Serial.print(MDNS_NAME);
        Serial.println(F(".local"));
    }
    else
    {
        Serial.println(F("mDNS failed to start"));
    }
}

void setupNTP()
{
    configTime(0, 0, NTP_SERVER1, NTP_SERVER2);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); // Europa/Warszawa
    tzset();

    Serial.println(F("NTP configured for Europe/Warsaw"));
    lastNTPUpdate = millis();

    // Czekaj na synchronizację
    int retry = 0;
    while (time(nullptr) < 100000 && retry < 20)
    {
        delay(500);
        Serial.print(".");
        retry++;
    }
    Serial.println();
    Serial.print(F("Current time: "));
    Serial.println(getFormattedTime());
}

void checkResetButton()
{
    if (digitalRead(RESET_HW_PIN) == LOW)
    {
        if (resetButtonPressTime == 0)
        {
            resetButtonPressTime = millis();
        }
        else if (millis() - resetButtonPressTime > 5000)
        {
            factoryReset();
        }
    }
    else
    {
        resetButtonPressTime = 0;
    }
}

void initDefaultSchedule()
{
    for (int i = 0; i < 7; i++)
    {
        schedule[i].hourOn = 8;
        schedule[i].minuteOn = 0;
        schedule[i].hourOff = 20;
        schedule[i].minuteOff = 0;
        schedule[i].relay = 1;
        schedule[i].active = 0; // Domyślnie wyłączone
    }
}

void setRelay(int relay, bool state)
{
    if (relay == 1 || relay == 3)
    {
        relay1State = state;
        digitalWrite(RL1_PIN, state ? HIGH : LOW);
    }
    if (relay == 2 || relay == 3)
    {
        relay2State = state;
        digitalWrite(RL2_PIN, state ? HIGH : LOW);
    }
}

void checkSchedule()
{
    if (!scheduleRunning)
    {
        // Wyłącz przekaźniki gdy harmonogram nieaktywny
        if (relay1State || relay2State)
        {
            setRelay(3, false);
        }
        return;
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return;
    }

    // Konwersja dnia tygodnia (tm_wday: 0=niedziela, 1=poniedziałek...)
    // Nasz format: 0=poniedziałek, 6=niedziela
    int dayIndex = (timeinfo.tm_wday == 0) ? 6 : timeinfo.tm_wday - 1;

    ScheduleDay &day = schedule[dayIndex];

    if (!day.active)
    {
        // Dzień nieaktywny - wyłącz przekaźniki przypisane do tego dnia
        return;
    }

    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int onMinutes = day.hourOn * 60 + day.minuteOn;
    int offMinutes = day.hourOff * 60 + day.minuteOff;

    bool shouldBeOn = false;

    if (onMinutes <= offMinutes)
    {
        // Normalny przypadek (np. 8:00 - 20:00)
        shouldBeOn = (currentMinutes >= onMinutes && currentMinutes < offMinutes);
    }
    else
    {
        // Przechodzi przez północ (np. 22:00 - 6:00)
        shouldBeOn = (currentMinutes >= onMinutes || currentMinutes < offMinutes);
    }

    // Sterowanie przekaźnikami
    if (day.relay == 1)
    {
        if (shouldBeOn != relay1State)
        {
            setRelay(1, shouldBeOn);
            Serial.printf("Relay 1: %s\n", shouldBeOn ? "ON" : "OFF");
        }
    }
    else if (day.relay == 2)
    {
        if (shouldBeOn != relay2State)
        {
            setRelay(2, shouldBeOn);
            Serial.printf("Relay 2: %s\n", shouldBeOn ? "ON" : "OFF");
        }
    }
    else if (day.relay == 3)
    {
        if (shouldBeOn != relay1State || shouldBeOn != relay2State)
        {
            setRelay(3, shouldBeOn);
            Serial.printf("Relay 1+2: %s\n", shouldBeOn ? "ON" : "OFF");
        }
    }
}

String getFormattedTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return "--:--:--";
    }
    char buf[12];
    sprintf(buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buf);
}

String getFormattedDate()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return "--.--.----";
    }
    char buf[12];
    sprintf(buf, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    return String(buf);
}

int getCurrentDayOfWeek()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return -1;
    }
    return (timeinfo.tm_wday == 0) ? 6 : timeinfo.tm_wday - 1;
}

// ============================================================
// EEPROM
// ============================================================
void saveWiFiCredentials()
{
    uint32_t magic = EEPROM_MAGIC;
    EEPROM.put(ADDR_MAGIC, magic);

    for (int i = 0; i < 64; i++)
    {
        EEPROM.write(ADDR_SSID + i, (i < wifiSSID.length()) ? wifiSSID[i] : 0);
        EEPROM.write(ADDR_PASS + i, (i < wifiPassword.length()) ? wifiPassword[i] : 0);
    }
    EEPROM.commit();
    Serial.println(F("WiFi credentials saved"));
}

void loadWiFiCredentials()
{
    uint32_t magic;
    EEPROM.get(ADDR_MAGIC, magic);

    if (magic != EEPROM_MAGIC)
    {
        Serial.println(F("No valid EEPROM data - AP mode"));
        isAPMode = true;
        return;
    }

    char buf[65];
    for (int i = 0; i < 64; i++)
        buf[i] = EEPROM.read(ADDR_SSID + i);
    buf[64] = 0;
    wifiSSID = String(buf);

    for (int i = 0; i < 64; i++)
        buf[i] = EEPROM.read(ADDR_PASS + i);
    buf[64] = 0;
    wifiPassword = String(buf);

    isAPMode = (wifiSSID.length() == 0);
    Serial.print(F("Loaded SSID: "));
    Serial.println(wifiSSID);
}

void saveSchedule()
{
    EEPROM.write(ADDR_SCHEDULE_ON, scheduleRunning ? 1 : 0);

    for (int i = 0; i < 7; i++)
    {
        int addr = ADDR_SCHEDULE_DATA + (i * 6);
        EEPROM.write(addr + 0, schedule[i].hourOn);
        EEPROM.write(addr + 1, schedule[i].minuteOn);
        EEPROM.write(addr + 2, schedule[i].hourOff);
        EEPROM.write(addr + 3, schedule[i].minuteOff);
        EEPROM.write(addr + 4, schedule[i].relay);
        EEPROM.write(addr + 5, schedule[i].active);
    }
    EEPROM.commit();
    Serial.println(F("Schedule saved to EEPROM"));
}

void loadSchedule()
{
    uint8_t running = EEPROM.read(ADDR_SCHEDULE_ON);
    scheduleRunning = (running == 1);

    for (int i = 0; i < 7; i++)
    {
        int addr = ADDR_SCHEDULE_DATA + (i * 6);
        schedule[i].hourOn = EEPROM.read(addr + 0);
        schedule[i].minuteOn = EEPROM.read(addr + 1);
        schedule[i].hourOff = EEPROM.read(addr + 2);
        schedule[i].minuteOff = EEPROM.read(addr + 3);
        schedule[i].relay = EEPROM.read(addr + 4);
        schedule[i].active = EEPROM.read(addr + 5);

        // Walidacja
        if (schedule[i].hourOn > 23)
            schedule[i].hourOn = 8;
        if (schedule[i].minuteOn > 59)
            schedule[i].minuteOn = 0;
        if (schedule[i].hourOff > 23)
            schedule[i].hourOff = 20;
        if (schedule[i].minuteOff > 59)
            schedule[i].minuteOff = 0;
        if (schedule[i].relay < 1 || schedule[i].relay > 3)
            schedule[i].relay = 1;
        if (schedule[i].active > 1)
            schedule[i].active = 0;
    }
    Serial.println(F("Schedule loaded from EEPROM"));
    Serial.print(F("Schedule running: "));
    Serial.println(scheduleRunning ? "YES" : "NO");
}

void factoryReset()
{
    Serial.println(F("=== FACTORY RESET ==="));
    for (int i = 0; i < EEPROM_SIZE; i++)
        EEPROM.write(i, 0xFF);
    EEPROM.commit();
    digitalWrite(RL1_PIN, LOW);
    digitalWrite(RL2_PIN, LOW);
    delay(1000);
    ESP.restart();
}

// ============================================================
// CAPTIVE PORTAL HANDLERS
// ============================================================
void handleCaptivePortal()
{
    Serial.print(F("Captive portal request: "));
    Serial.println(server.uri());
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
}

void handleNotFound()
{
    String host = server.hostHeader();
    if (host != WiFi.softAPIP().toString())
    {
        handleCaptivePortal();
        return;
    }
    handleAPConfig();
}

// ============================================================
// HANDLERY WWW - AP MODE
// ============================================================
void handleAPConfig()
{
    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>Harmonogram - WiFi</title><style>"
                    "body{font-family:Arial;background:#1a1a2e;color:#fff;display:flex;"
                    "justify-content:center;align-items:center;min-height:100vh;margin:0;padding:20px;}"
                    ".box{background:#16213e;padding:40px;border-radius:20px;text-align:center;max-width:350px;width:100%;}"
                    "h1{color:#e94560;margin-bottom:10px;font-size:1.8em;}h3{color:#4ecca3;margin-bottom:30px;}"
                    ".info{background:#0f3460;padding:15px;border-radius:10px;margin-bottom:20px;font-size:0.9em;}"
                    ".info p{margin:5px 0;color:#888;}"
                    ".info span{color:#4ecca3;}"
                    "input{width:100%;padding:15px;margin:10px 0;border:2px solid #0f3460;"
                    "border-radius:10px;background:#0f3460;color:#fff;font-size:16px;box-sizing:border-box;}"
                    "input:focus{outline:none;border-color:#4ecca3;}"
                    "input::placeholder{color:#666;}"
                    "button{width:100%;padding:15px;margin-top:20px;background:#4ecca3;color:#1a1a2e;"
                    "border:none;border-radius:10px;font-size:18px;font-weight:bold;cursor:pointer;}"
                    "button:hover{background:#3db892;}"
                    ".footer{margin-top:20px;color:#555;font-size:0.8em;}"
                    "</style></head><body><div class='box'>"
                    "<h1>&#128197; Harmonogram</h1>"
                    "<h3>Konfiguracja WiFi</h3>"
                    "<div class='info'>"
                    "<p>&#128246; Polacz sie z siecia WiFi</p>"
                    "<p>AP: <span>");
    html += AP_SSID;
    html += F("</span></p>"
              "<p>Platform: <span>ESP32</span></p></div>"
              "<form action='/connect' method='POST'>"
              "<input type='text' name='ssid' placeholder='&#128246; Nazwa sieci WiFi' required autocomplete='off'>"
              "<input type='password' name='pass' placeholder='&#128274; Haslo sieci' required autocomplete='off'>"
              "<button type='submit'>&#10004; Polacz</button></form>"
              "<div class='footer'>Sterownik Harmonogramu | damian.podraza@gmail.com</div>"
              "</div></body></html>");

    server.send(200, "text/html", html);
}

void handleConnect()
{
    wifiSSID = server.arg("ssid");
    wifiPassword = server.arg("pass");
    saveWiFiCredentials();

    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>Laczenie...</title><style>"
                    "body{font-family:Arial;background:#1a1a2e;color:#fff;display:flex;"
                    "justify-content:center;align-items:center;min-height:100vh;margin:0;}"
                    ".box{background:#16213e;padding:40px;border-radius:20px;text-align:center;max-width:400px;}"
                    "h2{color:#4ecca3;margin-bottom:20px;}"
                    ".spinner{width:50px;height:50px;border:4px solid #0f3460;border-top:4px solid #4ecca3;"
                    "border-radius:50%;animation:spin 1s linear infinite;margin:20px auto;}"
                    "@keyframes spin{100%{transform:rotate(360deg);}}"
                    ".info{background:#0f3460;padding:15px;border-radius:10px;margin-top:20px;text-align:left;}"
                    ".info p{margin:8px 0;font-size:0.9em;}"
                    ".info span{color:#4ecca3;}"
                    "</style></head><body><div class='box'>"
                    "<h2>&#10004; Dane zapisane!</h2>"
                    "<div class='spinner'></div>"
                    "<p>Urzadzenie uruchomi sie ponownie<br>i polaczy z siecia WiFi...</p>"
                    "<div class='info'>"
                    "<p>&#128246; Siec: <span>");
    html += wifiSSID;
    html += F("</span></p>"
              "<p>&#127760; Adres: <span>http://harmonogram.local</span></p>"
              "<p style='color:#888;font-size:0.85em;'>lub sprawdz IP w routerze</p>"
              "</div></div></body></html>");

    server.send(200, "text/html", html);
    delay(2000);
    ESP.restart();
}

// ============================================================
// HANDLERY WWW - NORMAL MODE
// ============================================================
void handleAPI()
{
    String json = "{";
    json += "\"running\":" + String(scheduleRunning ? "true" : "false");
    json += ",\"relay1\":" + String(relay1State ? "true" : "false");
    json += ",\"relay2\":" + String(relay2State ? "true" : "false");
    json += ",\"time\":\"" + getFormattedTime() + "\"";
    json += ",\"date\":\"" + getFormattedDate() + "\"";
    json += ",\"dayIndex\":" + String(getCurrentDayOfWeek());
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"mdns\":\"" + String(MDNS_NAME) + ".local\"";
    json += ",\"schedule\":[";

    for (int i = 0; i < 7; i++)
    {
        if (i > 0)
            json += ",";
        json += "{\"hourOn\":" + String(schedule[i].hourOn);
        json += ",\"minuteOn\":" + String(schedule[i].minuteOn);
        json += ",\"hourOff\":" + String(schedule[i].hourOff);
        json += ",\"minuteOff\":" + String(schedule[i].minuteOff);
        json += ",\"relay\":" + String(schedule[i].relay);
        json += ",\"active\":" + String(schedule[i].active ? "true" : "false") + "}";
    }
    json += "]}";

    server.send(200, "application/json", json);
}

void handleSetSchedule()
{
    bool changed = false;

    for (int i = 0; i < 7; i++)
    {
        String prefix = "d" + String(i);

        if (server.hasArg(prefix + "hon"))
        {
            schedule[i].hourOn = server.arg(prefix + "hon").toInt();
            changed = true;
        }
        if (server.hasArg(prefix + "mon"))
        {
            schedule[i].minuteOn = server.arg(prefix + "mon").toInt();
            changed = true;
        }
        if (server.hasArg(prefix + "hof"))
        {
            schedule[i].hourOff = server.arg(prefix + "hof").toInt();
            changed = true;
        }
        if (server.hasArg(prefix + "mof"))
        {
            schedule[i].minuteOff = server.arg(prefix + "mof").toInt();
            changed = true;
        }
        if (server.hasArg(prefix + "rl"))
        {
            schedule[i].relay = server.arg(prefix + "rl").toInt();
            changed = true;
        }
        if (server.hasArg(prefix + "act"))
        {
            schedule[i].active = (server.arg(prefix + "act") == "1") ? 1 : 0;
            changed = true;
        }
    }

    if (changed)
    {
        saveSchedule();

        // Wymuś natychmiastowe sprawdzenie harmonogramu
        if (scheduleRunning)
        {
            forceScheduleCheck();
        }
    }

    server.send(200, "text/plain", "OK");
}

void forceScheduleCheck()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return;
    }

    int dayIndex = (timeinfo.tm_wday == 0) ? 6 : timeinfo.tm_wday - 1;
    ScheduleDay &day = schedule[dayIndex];

    // Najpierw wyłącz wszystkie przekaźniki
    setRelay(3, false);

    if (!day.active)
    {
        Serial.println(F("Day not active - relays OFF"));
        return;
    }

    int currentMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int onMinutes = day.hourOn * 60 + day.minuteOn;
    int offMinutes = day.hourOff * 60 + day.minuteOff;

    bool shouldBeOn = (currentMinutes >= onMinutes && currentMinutes < offMinutes);

    Serial.printf("Force check: current=%d:%02d, ON=%d:%02d, OFF=%d:%02d, shouldBeOn=%d\n",
                  timeinfo.tm_hour, timeinfo.tm_min,
                  day.hourOn, day.minuteOn,
                  day.hourOff, day.minuteOff,
                  shouldBeOn);

    // Ustaw odpowiedni przekaźnik
    if (shouldBeOn)
    {
        setRelay(day.relay, true);
        Serial.printf("Relay %d: ON\n", day.relay);
    }
}

void handleSetRunning()
{
    if (server.hasArg("running"))
    {
        bool newState = (server.arg("running") == "1");
        if (newState != scheduleRunning)
        {
            scheduleRunning = newState;
            if (!scheduleRunning)
            {
                setRelay(3, false); // Wyłącz oba przekaźniki
            }
            saveSchedule();
            Serial.print(F("Schedule running: "));
            Serial.println(scheduleRunning ? "ON" : "OFF");
        }
    }
    server.send(200, "text/plain", "OK");
}

void handleReset()
{
    server.send(200, "text/plain", "Resetting...");
    delay(500);
    factoryReset();
}

// ============================================================
// HANDLERY DLA ZASOBÓW
// ============================================================
void handleFavicon() {
    // Prosty favicon - calendar emoji jako base64 PNG (1x1 pixel zielony)
    // Możesz też zwrócić 204 No Content
    server.send(204, "image/x-icon", "");
}

void handleNotFoundNormal() {
    String uri = server.uri();
    Serial.print(F("404 Not Found: "));
    Serial.println(uri);
    
    // Ignoruj typowe żądania przeglądarki
    if (uri == "/favicon.ico" || 
        uri == "/apple-touch-icon.png" ||
        uri == "/apple-touch-icon-precomposed.png" ||
        uri == "/manifest.json" ||
        uri == "/robots.txt" ||
        uri.endsWith(".map")) {
        server.send(204, "text/plain", "");  // 204 = No Content
        return;
    }
    
    server.send(404, "text/plain", "Not Found: " + uri);
}

// ============================================================
// STRONA GŁÓWNA - CSS
// ============================================================
const char CSS_PART[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Sterownik Harmonogramu</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial;background:#1a1a2e;color:#fff;padding:15px}
.c{max-width:900px;margin:0 auto}
h1{text-align:center;color:#e94560;font-size:1.8em;margin-bottom:20px}
.p{background:#16213e;border-radius:12px;padding:20px;margin-bottom:15px}
.pt{color:#4ecca3;font-size:1.1em;margin-bottom:15px;border-bottom:1px solid #0f3460;padding-bottom:10px}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;justify-content:center}
.btn{padding:12px 25px;border:none;border-radius:8px;cursor:pointer;font-size:16px;font-weight:bold;transition:all 0.3s}
.bon{background:#4ecca3;color:#1a1a2e}.bof{background:#e94560;color:#fff}
.bon.a{box-shadow:0 0 15px #4ecca3}.bof.a{box-shadow:0 0 15px #e94560}
.btn:hover{transform:scale(1.05)}
.st{padding:8px 20px;border-radius:15px;font-size:14px;font-weight:bold;margin-left:15px}
.st.on{background:#4ecca3;color:#1a1a2e}.st.of{background:#e94560;color:#fff}
table{width:100%;border-collapse:collapse;margin-top:10px}
th,td{padding:10px 8px;text-align:center;border-bottom:1px solid #0f3460}
th{background:#0f3460;color:#4ecca3;font-size:0.9em}
td{font-size:0.9em}
.day{text-align:left;font-weight:bold;color:#fff;min-width:100px}
.day.today{color:#4ecca3}
input[type=time]{background:#0f3460;border:1px solid #4ecca3;border-radius:6px;
padding:8px;color:#fff;font-size:14px;width:90px;text-align:center}
input[type=time]:focus{outline:none;border-color:#e94560}
select{background:#0f3460;border:1px solid #4ecca3;border-radius:6px;
padding:8px;color:#fff;font-size:14px;cursor:pointer}
select:focus{outline:none;border-color:#e94560}
.dbtn{padding:6px 15px;border:none;border-radius:6px;cursor:pointer;font-size:12px;font-weight:bold}
.dbon{background:#4ecca3;color:#1a1a2e}.dbof{background:#555;color:#888}
.time-box{background:#0f3460;border-radius:10px;padding:20px;text-align:center;margin-top:10px}
.time-display{font-size:2.5em;font-weight:bold;color:#4ecca3;font-family:monospace}
.date-display{font-size:1.2em;color:#888;margin-top:5px}
.relay-status{display:flex;justify-content:center;gap:30px;margin-top:15px}
.relay-box{background:#0f3460;padding:15px 25px;border-radius:10px;text-align:center}
.relay-box .lb{color:#888;font-size:0.85em;margin-bottom:5px}
.relay-box .vl{font-size:1.3em;font-weight:bold}
.relay-box .vl.on{color:#4ecca3}.relay-box .vl.off{color:#e94560}
.br{width:100%;padding:15px;background:linear-gradient(135deg,#ff6b35,#e94560);
color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer}
.br:hover{opacity:0.9}
.nfo{background:#0f3460;padding:10px 15px;border-radius:8px;margin-top:15px;font-size:0.85em;color:#888;text-align:center}
.nfo span{color:#4ecca3}
.ft{text-align:center;color:#555;padding:15px;font-size:0.85em}
@media(max-width:600px){
.day{min-width:60px;font-size:0.8em}
input[type=time]{width:75px;padding:6px;font-size:12px}
select{padding:6px;font-size:12px}
th,td{padding:6px 4px;font-size:0.8em}
}
</style></head>)rawliteral";

// ============================================================
// STRONA GŁÓWNA - BODY
// ============================================================
const char HTML_BODY[] PROGMEM = R"rawliteral(<body><div class='c'>
<h1>Sterownik Harmonogramu</h1>

<div class='p'>
<div class='row'>
<button class='btn bon' id='bo' onclick='setRun(1)'>ON</button>
<button class='btn bof a' id='bf' onclick='setRun(0)'>OFF</button>
<span class='st of' id='ss'>NIEAKTYWNY</span>
</div>
</div>

<div class='p'>
<div style='overflow-x:auto'>
<table>
<tr><th>Dzien</th><th>Włączenie</th><th>Wyłączenie</th><th>Przekażnik</th><th>Status</th></tr>
<tr id='r0'><td class='day'>Poniedzialek</td><td><input type='time' id='t0on'></td><td><input type='time' id='t0of'></td>
<td><select id='s0'><option value='1'>1</option><option value='2'>2</option><option value='3'>1-2</option></select></td>
<td><button class='dbtn dbof' id='b0' onclick='toggleDay(0)'>OFF</button></td></tr>
<tr id='r1'><td class='day'>Wtorek</td><td><input type='time' id='t1on'></td><td><input type='time' id='t1of'></td>
<td><select id='s1'><option value='1'>1</option><option value='2'>2</option><option value='3'>1-2</option></select></td>
<td><button class='dbtn dbof' id='b1' onclick='toggleDay(1)'>OFF</button></td></tr>
<tr id='r2'><td class='day'>Sroda</td><td><input type='time' id='t2on'></td><td><input type='time' id='t2of'></td>
<td><select id='s2'><option value='1'>1</option><option value='2'>2</option><option value='3'>1-2</option></select></td>
<td><button class='dbtn dbof' id='b2' onclick='toggleDay(2)'>OFF</button></td></tr>
<tr id='r3'><td class='day'>Czwartek</td><td><input type='time' id='t3on'></td><td><input type='time' id='t3of'></td>
<td><select id='s3'><option value='1'>1</option><option value='2'>2</option><option value='3'>1-2</option></select></td>
<td><button class='dbtn dbof' id='b3' onclick='toggleDay(3)'>OFF</button></td></tr>
<tr id='r4'><td class='day'>Piatek</td><td><input type='time' id='t4on'></td><td><input type='time' id='t4of'></td>
<td><select id='s4'><option value='1'>1</option><option value='2'>2</option><option value='3'>1-2</option></select></td>
<td><button class='dbtn dbof' id='b4' onclick='toggleDay(4)'>OFF</button></td></tr>
<tr id='r5'><td class='day'>Sobota</td><td><input type='time' id='t5on'></td><td><input type='time' id='t5of'></td>
<td><select id='s5'><option value='1'>1</option><option value='2'>2</option><option value='3'>1-2</option></select></td>
<td><button class='dbtn dbof' id='b5' onclick='toggleDay(5)'>OFF</button></td></tr>
<tr id='r6'><td class='day'>Niedziela</td><td><input type='time' id='t6on'></td><td><input type='time' id='t6of'></td>
<td><select id='s6'><option value='1'>1</option><option value='2'>2</option><option value='3'>1-2</option></select></td>
<td><button class='dbtn dbof' id='b6' onclick='toggleDay(6)'>OFF</button></td></tr>
</table>
</div>
<div class='row' style='margin-top:15px'>
<button class='btn bon' style='flex:1' onclick='saveAll()'>&#128190; Zapisz harmonogram</button>
<button class='btn bof' style='flex:1' onclick='restoreSchedule()'>&#128260; Przywróc harmonogram</button>
</div>
</div>

<div class='p'>
<div class='time-box'>
<div class='time-display' id='tm'>--:--:--</div>
<div class='date-display' id='dt'>--.--.-</div>
</div>
<div class='relay-status'>
<div class='relay-box'><div class='lb'>Przekażnik 1</div><div class='vl off' id='rl1'>OFF</div></div>
<div class='relay-box'><div class='lb'>Przekażnik 2</div><div class='vl off' id='rl2'>OFF</div></div>
</div>
</div>

<div class='p'>
<button class='br' onclick='doReset()'>&#9888; RESET FABRYCZNY</button>
<p style='text-align:center;color:#888;margin-top:10px;font-size:0.85em'>
Przytrzymaj przycisk RESET (IO0) 5 sekund lub kliknij powyżej</p>
<div class='nfo'>IP: <span id='ip'>--</span> | mDNS: <span id='mdns'>--</span></div>
</div>

<div class='ft'>Sterownik Harmonogramu | damian.podraza@gmail.com</div>
</div>)rawliteral";

// ============================================================
// STRONA GŁÓWNA - JAVASCRIPT (POPRAWIONY)
// ============================================================
const char JS_PART[] PROGMEM = R"rawliteral(<script>
var dayActive=[0,0,0,0,0,0,0];
var currentDay=-1;
var scheduleLoaded=false;
var dayNames=['Poniedzialek','Wtorek','Sroda','Czwartek','Piatek','Sobota','Niedziela'];

window.onload=function(){
getStatus();
setInterval(getStatus,2000);
};

function pad(n){return n<10?'0'+n:n;}

function timeToMinutes(timeStr){
var parts=timeStr.split(':');
return parseInt(parts[0])*60+parseInt(parts[1]);
}

function getStatus(){
fetch('/api').then(r=>r.json()).then(d=>{
document.getElementById('tm').innerText=d.time;
document.getElementById('dt').innerText=d.date;
currentDay=d.dayIndex;

var rl1=document.getElementById('rl1');
var rl2=document.getElementById('rl2');
rl1.innerText=d.relay1?'ON':'OFF';
rl1.className='vl '+(d.relay1?'on':'off');
rl2.innerText=d.relay2?'ON':'OFF';
rl2.className='vl '+(d.relay2?'on':'off');

var bo=document.getElementById('bo');
var bf=document.getElementById('bf');
var ss=document.getElementById('ss');
if(d.running){
bo.className='btn bon a';bf.className='btn bof';
ss.innerText='AKTYWNY';ss.className='st on';
}else{
bo.className='btn bon';bf.className='btn bof a';
ss.innerText='NIEAKTYWNY';ss.className='st of';
}

if(d.ip)document.getElementById('ip').innerText=d.ip;
if(d.mdns)document.getElementById('mdns').innerText=d.mdns;

if(!scheduleLoaded){
loadScheduleFromData(d);
scheduleLoaded=true;
}

updateDayHighlight();

}).catch(e=>console.log(e));
}

function loadScheduleFromData(d){
for(var i=0;i<7;i++){
var s=d.schedule[i];
document.getElementById('t'+i+'on').value=pad(s.hourOn)+':'+pad(s.minuteOn);
document.getElementById('t'+i+'of').value=pad(s.hourOff)+':'+pad(s.minuteOff);
document.getElementById('s'+i).value=s.relay;
dayActive[i]=s.active?1:0;
updateDayButton(i);
}
}

function updateDayHighlight(){
for(var i=0;i<7;i++){
var row=document.getElementById('r'+i);
var dayCell=row.querySelector('.day');
if(i===currentDay){
dayCell.classList.add('today');
}else{
dayCell.classList.remove('today');
}
}
}

function updateDayButton(i){
var btn=document.getElementById('b'+i);
if(dayActive[i]){
btn.innerText='ON';btn.className='dbtn dbon';
}else{
btn.innerText='OFF';btn.className='dbtn dbof';
}
}

function setRun(on){
fetch('/setRunning',{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'running='+(on?'1':'0')}).then(()=>{
setTimeout(getStatus,300);
});
}

function toggleDay(i){
dayActive[i]=dayActive[i]?0:1;
updateDayButton(i);
}

function restoreSchedule(){
if(confirm('Przywrocic ostatnio zapisany harmonogram?')){
scheduleLoaded=false;
getStatus();
}
}

function validateSchedule(){
var errors=[];
for(var i=0;i<7;i++){
var tonVal=document.getElementById('t'+i+'on').value;
var tofVal=document.getElementById('t'+i+'of').value;
if(!tonVal||!tofVal){
errors.push(dayNames[i]+': Brak ustawionego czasu');
continue;
}
var onMin=timeToMinutes(tonVal);
var offMin=timeToMinutes(tofVal);
if(offMin<=onMin){
errors.push(dayNames[i]+': Czas wylaczenia musi byc pozniejszy niz włączenia');
}
}
return errors;
}

function saveAll(){
var errors=validateSchedule();
if(errors.length>0){
alert('Bledy w harmonogramie:\n\n'+errors.join('\n'));
return;
}

var params=[];
for(var i=0;i<7;i++){
var ton=document.getElementById('t'+i+'on').value.split(':');
var tof=document.getElementById('t'+i+'of').value.split(':');
var rl=document.getElementById('s'+i).value;
params.push('d'+i+'hon='+parseInt(ton[0]||0));
params.push('d'+i+'mon='+parseInt(ton[1]||0));
params.push('d'+i+'hof='+parseInt(tof[0]||0));
params.push('d'+i+'mof='+parseInt(tof[1]||0));
params.push('d'+i+'rl='+rl);
params.push('d'+i+'act='+(dayActive[i]?'1':'0'));
}
var body=params.join('&');

fetch('/setSchedule',{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:body}).then(()=>{
alert('Harmonogram zapisany!');
setTimeout(getStatus,500);
}).catch(e=>{
alert('Blad zapisu!');
});
}

function doReset(){
if(confirm('Czy na pewno chcesz przywrocic ustawienia fabryczne?\nWszystkie dane zostana usuniete!')){
fetch('/reset',{method:'POST'}).then(()=>{
alert('Urzadzenie restartuje sie...');
});
}
}
</script></body></html>)rawliteral";

// ============================================================
// STRONA GŁÓWNA - wysyłana w częściach
// ============================================================
void handleRoot()
{
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    server.sendContent_P(CSS_PART);
    server.sendContent_P(HTML_BODY);
    server.sendContent_P(JS_PART);
}