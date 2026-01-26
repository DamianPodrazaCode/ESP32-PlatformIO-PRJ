/*
 * ============================================================
 * STEROWNIK PID - ESP32
 * ============================================================
 * Wersja: 1.2 - Captive Portal + mDNS
 * Platforma: ESP32-WROOM-32
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================
// KONFIGURACJA PINÓW (ESP32)
// ============================================================
#define DS1_PIN 17      // Czujnik DS18B20 #1 (PID)
#define DS2_PIN 18      // Czujnik DS18B20 #2 (kontrolny)
#define RESET_HW_PIN 0  // Przycisk reset (aktywny LOW)
#define FAN_PWM_PIN 25  // Wyjście PWM wentylatora
#define PUMP_PIN 16     // Wyjście pompy (przekaźnik)

// ============================================================
// KONFIGURACJA LEDC (PWM dla ESP32)
// ============================================================
#define LEDC_CHANNEL 0
#define LEDC_FREQ 18000
#define LEDC_RESOLUTION 10 // 10-bit = 0-1023
#define PWM_MAX 1023

// ============================================================
// KONFIGURACJA EEPROM
// ============================================================
#define EEPROM_SIZE 512
#define ADDR_MAGIC 0
#define ADDR_SSID 4
#define ADDR_PASS 68
#define ADDR_TEMP_SETPOINT 132
#define ADDR_KP 136
#define ADDR_KI 140
#define ADDR_KD 144
#define EEPROM_MAGIC 0xA5B6C7D8

// ============================================================
// KONFIGURACJA DOMYŚLNA
// ============================================================
#define DEFAULT_SETPOINT 25.0
#define DEFAULT_KP 2.0
#define DEFAULT_KI 0.5
#define DEFAULT_KD 1.0
#define AP_SSID "Sterownik_PID_Setup"
#define AP_PASS "12345678"
#define MDNS_NAME "pid"
#define DNS_PORT 53

// ============================================================
// KONFIGURACJA PID I WYKRESU
// ============================================================
#define PID_INTERVAL_MS 500
#define HISTORY_SIZE 60
#define HISTORY_INTERVAL_MS 2000

// ============================================================
// ZMIENNE GLOBALNE
// ============================================================
OneWire oneWire1(DS1_PIN);
OneWire oneWire2(DS2_PIN);
DallasTemperature sensor1(&oneWire1);
DallasTemperature sensor2(&oneWire2);

WebServer server(80);
DNSServer dnsServer;

String wifiSSID = "";
String wifiPassword = "";
bool isAPMode = true;

float tempSetpoint = DEFAULT_SETPOINT;
float Kp = DEFAULT_KP;
float Ki = DEFAULT_KI;
float Kd = DEFAULT_KD;

bool pidRunning = false;
float tempDS1 = 0.0;
float tempDS2 = 0.0;
int fanPWM = 0;
bool pumpState = false;

float pidIntegral = 0.0;
float pidPrevError = 0.0;
unsigned long pidLastTime = 0;

float tempHistory1[HISTORY_SIZE];
float tempHistory2[HISTORY_SIZE];
int historyIndex = 0;
unsigned long lastHistoryUpdate = 0;

unsigned long lastTempRead = 0;
unsigned long resetButtonPressTime = 0;

// ============================================================
// DEKLARACJE FUNKCJI
// ============================================================
void setupWiFi();
void setupMDNS();
void setupPWM();
void checkResetButton();
void readTemperatures();
void runPIDController();
void setFanPWM(int percent);

void saveWiFiCredentials();
void loadWiFiCredentials();
void saveSettings();
void loadSettings();
void factoryReset();

void handleAPConfig();
void handleConnect();
void handleRoot();
void handleAPI();
void handleSet();
void handleReset();
void handleChart();
void handleCaptivePortal();
void handleNotFound();

// ============================================================
// SETUP
// ============================================================
void setup()
{
    Serial.begin(115200);
    Serial.println(F("\n\n=== STEROWNIK PID ESP32 v1.2 ==="));

    // Piny
    pinMode(RESET_HW_PIN, INPUT_PULLUP);
    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW);

    // PWM dla wentylatora (LEDC)
    setupPWM();

    // Czujniki temperatury
    sensor1.begin();
    sensor2.begin();
    sensor1.setResolution(12);
    sensor2.setResolution(12);
    sensor1.setWaitForConversion(false);
    sensor2.setWaitForConversion(false);

    Serial.print(F("DS1 sensors found: "));
    Serial.println(sensor1.getDeviceCount());
    Serial.print(F("DS2 sensors found: "));
    Serial.println(sensor2.getDeviceCount());

    // EEPROM
    EEPROM.begin(EEPROM_SIZE);

    // Reset podczas startu
    if (digitalRead(RESET_HW_PIN) == LOW)
    {
        Serial.println(F("Reset button pressed..."));
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
        loadSettings();
        setupMDNS();
    }

    // Historia
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        tempHistory1[i] = 0;
        tempHistory2[i] = 0;
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
        server.on("/set", HTTP_POST, handleSet);
        server.on("/reset", HTTP_POST, handleReset);
        server.on("/chart", HTTP_GET, handleChart);
        server.onNotFound([]()
                          { server.send(404, "text/plain", "Not Found"); });
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

    if (millis() - lastTempRead >= 1000)
    {
        readTemperatures();
        lastTempRead = millis();
    }

    if (millis() - lastHistoryUpdate >= HISTORY_INTERVAL_MS)
    {
        tempHistory1[historyIndex] = tempDS1;
        tempHistory2[historyIndex] = tempDS2;
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;
        lastHistoryUpdate = millis();
    }

    if (pidRunning)
    {
        runPIDController();
    }

    // Wyjścia
    setFanPWM(fanPWM);
    digitalWrite(PUMP_PIN, (pidRunning && pumpState) ? HIGH : LOW);

    delay(1);
}

// ============================================================
// FUNKCJE PWM (ESP32 LEDC)
// ============================================================
void setupPWM()
{
    ledcSetup(LEDC_CHANNEL, LEDC_FREQ, LEDC_RESOLUTION);
    ledcAttachPin(FAN_PWM_PIN, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, 0);
    Serial.println(F("PWM (LEDC) configured"));
}

void setFanPWM(int percent)
{
    int pwmValue = map(percent, 0, 100, 0, PWM_MAX);
    ledcWrite(LEDC_CHANNEL, pwmValue);
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

void readTemperatures()
{
    sensor1.requestTemperatures();
    sensor2.requestTemperatures();

    float t1 = sensor1.getTempCByIndex(0);
    float t2 = sensor2.getTempCByIndex(0);

    if (t1 != DEVICE_DISCONNECTED_C && t1 > -50 && t1 < 150)
        tempDS1 = t1;
    if (t2 != DEVICE_DISCONNECTED_C && t2 > -50 && t2 < 150)
        tempDS2 = t2;
}

void runPIDController()
{
    unsigned long now = millis();
    if (now - pidLastTime < PID_INTERVAL_MS)
        return;

    float dt = (now - pidLastTime) / 1000.0;
    pidLastTime = now;

    float error = tempDS1 - tempSetpoint;

    if (error <= 0)
    {
        pidIntegral = 0;
        pidPrevError = 0;
        fanPWM = 0;
        return;
    }

    float P = Kp * error;
    pidIntegral += error * dt;
    if (Ki > 0)
        pidIntegral = constrain(pidIntegral, 0.0f, 100.0f / Ki);
    float I = Ki * pidIntegral;
    float D = Kd * (error - pidPrevError) / dt;
    pidPrevError = error;

    float output = P + I + D;
    fanPWM = constrain((int)output, 0, 100);
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
}

void loadWiFiCredentials()
{
    uint32_t magic;
    EEPROM.get(ADDR_MAGIC, magic);

    if (magic != EEPROM_MAGIC)
    {
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

    isAPMode = false;
}

void saveSettings()
{
    EEPROM.put(ADDR_TEMP_SETPOINT, tempSetpoint);
    EEPROM.put(ADDR_KP, Kp);
    EEPROM.put(ADDR_KI, Ki);
    EEPROM.put(ADDR_KD, Kd);
    EEPROM.commit();
}

void loadSettings()
{
    float temp;
    EEPROM.get(ADDR_TEMP_SETPOINT, temp);
    tempSetpoint = (!isnan(temp) && temp >= -50 && temp <= 150) ? temp : DEFAULT_SETPOINT;

    EEPROM.get(ADDR_KP, temp);
    Kp = (!isnan(temp) && temp >= 0 && temp <= 1000) ? temp : DEFAULT_KP;

    EEPROM.get(ADDR_KI, temp);
    Ki = (!isnan(temp) && temp >= 0 && temp <= 1000) ? temp : DEFAULT_KI;

    EEPROM.get(ADDR_KD, temp);
    Kd = (!isnan(temp) && temp >= 0 && temp <= 1000) ? temp : DEFAULT_KD;
}

void factoryReset()
{
    Serial.println(F("FACTORY RESET!"));
    for (int i = 0; i < EEPROM_SIZE; i++)
        EEPROM.write(i, 0xFF);
    EEPROM.commit();
    ledcWrite(LEDC_CHANNEL, 0);
    digitalWrite(PUMP_PIN, LOW);
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
                    "<title>Sterownik PID - WiFi</title><style>"
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
                    "<h1>&#127777; Sterownik PID</h1>"
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
              "<div class='footer'>Sterownik PID v1.2 ESP32</div>"
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
              "<p>&#127760; Adres: <span>http://pid.local</span></p>"
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
    String json = "{\"ds1\":" + String(tempDS1, 2) +
                  ",\"ds2\":" + String(tempDS2, 2) +
                  ",\"fan\":" + String(fanPWM) +
                  ",\"pump\":" + String(pumpState ? "true" : "false") +
                  ",\"setpoint\":" + String(tempSetpoint, 1) +
                  ",\"kp\":" + String(Kp, 2) +
                  ",\"ki\":" + String(Ki, 2) +
                  ",\"kd\":" + String(Kd, 2) +
                  ",\"running\":" + String(pidRunning ? "true" : "false") +
                  ",\"ip\":\"" + WiFi.localIP().toString() + "\"" +
                  ",\"mdns\":\"" + String(MDNS_NAME) + ".local\"}";
    server.send(200, "application/json", json);
}

void handleSet()
{
    bool settingsChanged = false;

    if (server.hasArg("running"))
    {
        bool newState = (server.arg("running") == "1");
        if (newState != pidRunning)
        {
            pidRunning = newState;
            if (pidRunning)
            {
                pumpState = true;
                pidIntegral = 0;
                pidPrevError = 0;
                pidLastTime = millis();
            }
            else
            {
                pumpState = false;
                fanPWM = 0;
                ledcWrite(LEDC_CHANNEL, 0);
                digitalWrite(PUMP_PIN, LOW);
            }
        }
    }

    if (server.hasArg("setpoint"))
    {
        float v = server.arg("setpoint").toFloat();
        if (v >= -50 && v <= 150)
        {
            tempSetpoint = v;
            settingsChanged = true;
        }
    }
    if (server.hasArg("kp"))
    {
        float v = server.arg("kp").toFloat();
        if (v >= 0 && v <= 1000)
        {
            Kp = v;
            settingsChanged = true;
        }
    }
    if (server.hasArg("ki"))
    {
        float v = server.arg("ki").toFloat();
        if (v >= 0 && v <= 1000)
        {
            Ki = v;
            pidIntegral = 0;
            settingsChanged = true;
        }
    }
    if (server.hasArg("kd"))
    {
        float v = server.arg("kd").toFloat();
        if (v >= 0 && v <= 1000)
        {
            Kd = v;
            settingsChanged = true;
        }
    }

    if (settingsChanged)
        saveSettings();
    server.send(200, "text/plain", "OK");
}

void handleReset()
{
    server.send(200, "text/plain", "Resetting...");
    delay(500);
    factoryReset();
}

void handleChart()
{
    String json = "{\"ds1\":[";
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        int idx = (historyIndex + i) % HISTORY_SIZE;
        if (i > 0)
            json += ",";
        json += String(tempHistory1[idx], 1);
    }
    json += "],\"ds2\":[";
    for (int i = 0; i < HISTORY_SIZE; i++)
    {
        int idx = (historyIndex + i) % HISTORY_SIZE;
        if (i > 0)
            json += ",";
        json += String(tempHistory2[idx], 1);
    }
    json += "],\"sp\":" + String(tempSetpoint, 1) + "}";
    server.send(200, "application/json", json);
}

// ============================================================
// STRONA GŁÓWNA - CSS (w PROGMEM dla oszczędności RAM)
// ============================================================
const char CSS_PART[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Sterownik PID</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial;background:#1a1a2e;color:#fff;padding:15px}
.c{max-width:800px;margin:0 auto}
h1{text-align:center;color:#e94560;font-size:1.8em;margin-bottom:20px}
.p{background:#16213e;border-radius:12px;padding:20px;margin-bottom:15px}
.pt{color:#4ecca3;font-size:1.1em;margin-bottom:15px;border-bottom:1px solid #0f3460;padding-bottom:10px}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
.btn{padding:12px 25px;border:none;border-radius:8px;cursor:pointer;font-size:16px;font-weight:bold}
.bon{background:#4ecca3;color:#1a1a2e}.bof{background:#e94560;color:#fff}
.bon.a{box-shadow:0 0 15px #4ecca3}.bof.a{box-shadow:0 0 15px #e94560}
input[type=number]{background:#0f3460;border:2px solid #4ecca3;border-radius:6px;
padding:10px;color:#fff;font-size:14px;width:80px;text-align:center}
input:focus{outline:none;border-color:#e94560}
label{color:#4ecca3;font-weight:bold;min-width:70px}
.u{color:#888;font-size:12px}
.ig{display:flex;align-items:center;gap:8px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:15px}
.box{background:#0f3460;border-radius:10px;padding:15px;text-align:center}
.box .ic{font-size:1.8em;margin-bottom:8px}
.box .lb{color:#888;font-size:0.85em;margin-bottom:5px}
.box .vl{font-size:1.6em;font-weight:bold;color:#4ecca3}
.vl.r{color:#e94560}.vl.g{color:#4ecca3}.vl.y{color:#ffc107}
.st{padding:6px 15px;border-radius:15px;font-size:12px;font-weight:bold}
.st.on{background:#4ecca3;color:#1a1a2e}.st.of{background:#e94560;color:#fff}
.ch{background:#0f3460;border-radius:10px;height:200px;position:relative}
.lg{display:flex;justify-content:center;gap:20px;margin-bottom:10px;font-size:12px}
.lg span{display:flex;align-items:center;gap:5px}
.lg i{display:inline-block;width:15px;height:3px;border-radius:2px}
.l1{background:#4ecca3}.l2{background:#e94560}.l3{background:#ffc107}
canvas{width:100%!important;height:170px!important}
.br{width:100%;padding:15px;background:linear-gradient(135deg,#ff6b35,#e94560);
color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:bold;cursor:pointer}
.br:hover{opacity:0.9}
.ft{text-align:center;color:#555;padding:15px;font-size:0.85em}
.nfo{background:#0f3460;padding:10px 15px;border-radius:8px;margin-top:10px;font-size:0.85em;color:#888}
.nfo span{color:#4ecca3}
</style></head>)rawliteral";

// ============================================================
// STRONA GŁÓWNA - HTML BODY
// ============================================================
const char HTML_BODY[] PROGMEM = R"rawliteral(<body><div class='c'>
<h1>&#127777; Sterownik PID</h1>
<div class='p'><div class='row'>
<button class='btn bon' id='bo' onclick='sp(1)'>ON</button>
<button class='btn bof a' id='bf' onclick='sp(0)'>OFF</button>
<div class='ig'><label>Temp:</label>
<input type='number' id='st' step='0.5' value='25' onchange='ss()'>
<span class='u'>C</span></div>
<span class='st of' id='ss'>OFF</span>
</div></div>
<div class='p'><div class='pt'>Nastawy PID</div>
<div class='grid'>
<div class='box'><div class='lb'>Kp</div>
<input type='number' id='kp' step='0.1' value='2' onchange='su()'></div>
<div class='box'><div class='lb'>Ki</div>
<input type='number' id='ki' step='0.01' value='0.5' onchange='su()'></div>
<div class='box'><div class='lb'>Kd</div>
<input type='number' id='kd' step='0.01' value='1' onchange='su()'></div>
</div></div>
<div class='p'><div class='pt'>Odczyty</div>
<div class='grid'>
<div class='box'><div class='ic'>&#127777;</div><div class='lb'>DS1 (PID)</div>
<div class='vl' id='d1'>--</div></div>
<div class='box'><div class='ic'>&#127777;</div><div class='lb'>DS2</div>
<div class='vl' id='d2'>--</div></div>
<div class='box'><div class='ic'>&#128168;</div><div class='lb'>Wentylator</div>
<div class='vl' id='fn'>--%</div></div>
<div class='box'><div class='ic'>&#128167;</div><div class='lb'>Pompa</div>
<div class='vl' id='pm'>--</div></div>
</div></div>
<div class='p'><div class='pt'>Wykres</div>
<div class='ch'><div class='lg'>
<span><i class='l1'></i>DS1</span>
<span><i class='l2'></i>DS2</span>
<span><i class='l3'></i>SP</span></div>
<canvas id='cv'></canvas></div></div>
<div class='p'>
<button class='br' onclick='rs()'>RESET FABRYCZNY</button>
<p style='text-align:center;color:#888;margin-top:10px;font-size:0.85em'>
Przytrzymaj RESET_HW 5s lub kliknij powyzej</p>
<div class='nfo'>IP: <span id='ip'>--</span> | mDNS: <span id='mdns'>--</span></div>
</div>
<div class='ft'>Sterownik PID v1.2 ESP32</div></div>)rawliteral";

// ============================================================
// STRONA GŁÓWNA - JAVASCRIPT
// ============================================================
const char JS_PART[] PROGMEM = R"rawliteral(<script>
var c1=[],c2=[],csp=25,cv,cx;
window.onload=function(){
cv=document.getElementById('cv');
cx=cv.getContext('2d');
rs2();window.addEventListener('resize',rs2);
gd();gc();setInterval(gd,1500);setInterval(gc,5000);};

function rs2(){var p=cv.parentElement;
cv.width=p.clientWidth-20;cv.height=160;dc();}

function gd(){fetch('/api').then(r=>r.json()).then(d=>{
document.getElementById('d1').innerText=d.ds1.toFixed(1)+'C';
document.getElementById('d2').innerText=d.ds2.toFixed(1)+'C';
document.getElementById('fn').innerText=d.fan+'%';
var pm=document.getElementById('pm');
pm.innerText=d.pump?'ON':'OFF';
pm.className='vl '+(d.pump?'g':'r');
if(document.activeElement.id!='st')document.getElementById('st').value=d.setpoint;
if(document.activeElement.id!='kp')document.getElementById('kp').value=d.kp;
if(document.activeElement.id!='ki')document.getElementById('ki').value=d.ki;
if(document.activeElement.id!='kd')document.getElementById('kd').value=d.kd;
csp=d.setpoint;
var bo=document.getElementById('bo'),bf=document.getElementById('bf'),
ss=document.getElementById('ss');
if(d.running){bo.className='btn bon a';bf.className='btn bof';
ss.innerText='AKTYWNY';ss.className='st on';}
else{bo.className='btn bon';bf.className='btn bof a';
ss.innerText='OFF';ss.className='st of';}
var d1=document.getElementById('d1');
d1.className='vl '+(d.ds1>d.setpoint+5?'r':(d.ds1>d.setpoint?'y':'g'));
if(d.ip)document.getElementById('ip').innerText=d.ip;
if(d.mdns)document.getElementById('mdns').innerText=d.mdns;
}).catch(e=>console.log(e));}

function gc(){fetch('/chart').then(r=>r.json()).then(d=>{
c1=d.ds1;c2=d.ds2;csp=d.sp;dc();}).catch(e=>console.log(e));}

function dc(){if(!cx||c1.length<2)return;
var w=cv.width,h=cv.height,pl=40,pr=10,pt=10,pb=20;
var cw=w-pl-pr,ch=h-pt-pb;
cx.clearRect(0,0,w,h);
var all=c1.concat(c2).concat([csp]).filter(v=>v!=0&&!isNaN(v));
if(all.length==0)return;
var mn=Math.min.apply(null,all)-2,mx=Math.max.apply(null,all)+2,rg=mx-mn||1;
cx.strokeStyle='#1a3a5c';cx.lineWidth=1;cx.fillStyle='#666';cx.font='10px Arial';
for(var i=0;i<=4;i++){var y=pt+ch*i/4,v=mx-rg*i/4;
cx.beginPath();cx.moveTo(pl,y);cx.lineTo(w-pr,y);cx.stroke();
cx.fillText(v.toFixed(1),2,y+3);}
function gy(v){return pt+ch*(1-(v-mn)/rg);}
function gx(i){return pl+cw*i/(c1.length-1);}
cx.strokeStyle='#ffc107';cx.lineWidth=1;cx.setLineDash([4,4]);
cx.beginPath();var sy=gy(csp);cx.moveTo(pl,sy);cx.lineTo(w-pr,sy);cx.stroke();
cx.setLineDash([]);
cx.strokeStyle='#4ecca3';cx.lineWidth=2;cx.beginPath();var st=0;
c1.forEach(function(v,i){if(v==0)return;var x=gx(i),y=gy(v);
if(!st){cx.moveTo(x,y);st=1;}else cx.lineTo(x,y);});cx.stroke();
cx.strokeStyle='#e94560';cx.lineWidth=2;cx.beginPath();st=0;
c2.forEach(function(v,i){if(v==0)return;var x=gx(i),y=gy(v);
if(!st){cx.moveTo(x,y);st=1;}else cx.lineTo(x,y);});cx.stroke();}

function sp(o){fetch('/set',{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'running='+(o?'1':'0')}).then(()=>gd());}

function ss(){var v=document.getElementById('st').value;
fetch('/set',{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'setpoint='+v});}

function su(){var kp=document.getElementById('kp').value,
ki=document.getElementById('ki').value,
kd=document.getElementById('kd').value;
fetch('/set',{method:'POST',
headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'kp='+kp+'&ki='+ki+'&kd='+kd});}

function rs(){if(confirm('Reset fabryczny?')){
fetch('/reset',{method:'POST'}).then(()=>{
alert('Urzadzenie restartuje...');});}}
</script></body></html>)rawliteral";

// ============================================================
// STRONA GŁÓWNA - wysyłana w częściach
// ============================================================
void handleRoot()
{
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");

    // Na ESP32 sendContent_P działa z PROGMEM
    server.sendContent_P(CSS_PART);
    server.sendContent_P(HTML_BODY);
    server.sendContent_P(JS_PART);
}