#include <Arduino.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_BW.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <qrcode.h>

Preferences prefs;

// Pin mapping for ESP32-C3 Mini
#define CS_PIN 8    // Chip Select
#define DC_PIN 7    // Data/Command
#define RST_PIN 9   // Reset
#define BUSY_PIN 2  // Busy
#define MOSI_PIN 4  // 6 SPI MOSI
#define SCK_PIN 10  // S2 10   scl   //S2 14    SPI Clock

// Display class for 2.13" 250x122 SSD1680
// GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(GxEPD2_213_BN(CS_PIN, DC_PIN, RST_PIN, -1));
GxEPD2_3C<GxEPD2_213_Z98c, GxEPD2_213_Z98c::HEIGHT> display(GxEPD2_213_Z98c(CS_PIN, DC_PIN, RST_PIN, BUSY_PIN));

const char *ssid = "YOUR NETWORK";
const char *password = "YOUR PASSWORD";  // Replace with your actual password
// AP mode credentials
const char *ap_ssid = "Todo-Wrist";
const char *ap_password = "tinkertailor";

WebServer server(80);
String tasks[5];

int qrSize = 4;  // QR code size (1 to 4)
volatile bool resetRequested = false;
bool completed[5] = {false, false, false, false, false};
bool isAPMode = false;  // Track current WiFi mode

void setupWiFi();
void handleRoot();
void handleSubmit();
void setupWebServer();
void updateDisplay();
bool tasksExist();
void showWelcomeMessage(String ip);
void drawQRCode(String ip);
void handleReset();
void IRAM_ATTR handleButtonPress();
void handleSleep();
void handleStyle();
void handleToggle();
String getCurrentIP();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Booting...");
  Serial.println("=== Setup started ===");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(5, INPUT_PULLUP);
  attachInterrupt(5, handleButtonPress, FALLING);

  SPI.begin(SCK_PIN, -1, MOSI_PIN);
  SPI.setFrequency(1000000);  // 1 MHz SPI.setFrequency(800000); // 800 kHz

  setupWiFi();
  setupWebServer();

  // TROUBLE SHOOTING TO CLEAR TASKS
  // prefs.begin("tasks", false);
  // prefs.clear();
  // prefs.end();
  // END TROUBLE SHOOTING TO CLEAR TASKS

  // Load saved tasks and completion status
  prefs.begin("tasks", true);  // read-only
  for (int i = 0; i < 5; i++) {
    tasks[i] = prefs.getString(("task" + String(i)).c_str(), "");
    completed[i] = prefs.getBool(("done" + String(i)).c_str(), false);
  }
  prefs.end();

  if (!tasksExist()) {
    String ip = getCurrentIP();
    drawQRCode("http://" + ip);
  } else {
    updateDisplay();  // show tasks
  }
  // Setup deep sleep wake on GPIO 5
  esp_deep_sleep_enable_gpio_wakeup(1ULL << GPIO_NUM_5, ESP_GPIO_WAKEUP_GPIO_LOW);

  Serial.println("Setup complete.");
}

void loop() {
  server.handleClient();
  // LED heartbeat - try both polarities
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  if (millis() - lastBlink > 1000) {
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH);  // Try LOW/HIGH
    lastBlink = millis();
  }

  if (resetRequested) {
    resetRequested = false;
    handleReset();
  }
  delay(10);
}

void setupWiFi() {
  Serial.println("Attempting to connect to home WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Try to connect for 15 seconds
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("Connected to home WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    isAPMode = false;
  } else {
    Serial.println();
    Serial.println("Failed to connect to home WiFi. Starting AP mode...");

    // Start AP mode
    WiFi.mode(WIFI_AP);
    bool apStarted = WiFi.softAP(ap_ssid, ap_password);

    if (apStarted) {
      Serial.println("AP mode started successfully!");
      Serial.print("AP SSID: ");
      Serial.println(ap_ssid);
      Serial.print("AP Password: ");
      Serial.println(ap_password);
      Serial.print("AP IP address: ");
      Serial.println(WiFi.softAPIP());
      isAPMode = true;
    } else {
      Serial.println("Failed to start AP mode!");
    }
  }
}

String getCurrentIP() {
  if (isAPMode) {
    return WiFi.softAPIP().toString();
  } else {
    return WiFi.localIP().toString();
  }
}

void handleRoot() {
  prefs.begin("style", true);
  String bg = prefs.getString("bg", "white");
  String text = prefs.getString("text", "black");
  prefs.end();

  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { background-color: #66899aff; color: #000000; font-family: Arial, sans-serif; padding: 20px; }";
  html += "h1 { font-size: 28px; margin-bottom: 10px; }";
  html += "p { font-size: 16px; margin-bottom: 20px; }";
  html += ".status { background-color: " + String(isAPMode ? "#fa9d45ff" : "#4CAF50") +
          "; color: white; padding: 8px; border-radius: 4px; margin-bottom: 15px; }";
  html += ".reset { background-color: #f790e4ff; }";
  html += ".update { background-color: #7d7d7dff; }";
  html += "input[type='text'] { width: 15ch; padding: 10px; font-size: 16px; margin-bottom: 10px; }";
  html += "input[type='submit'] { padding: 10px 20px; font-size: 16px; margin: 10px 5px 10px 0; }";
  html += "select { padding: 8px; font-size: 16px; margin-bottom: 10px; }";
  html += "form.inline { display: inline-block; margin-right: 10px; }";
  html += "</style></head><body>";

  // Connection status
  html += "<div class='status'>";
  if (isAPMode) {
    html += "AP Mode - Connect to: " + String(ap_ssid);
  } else {
    html += "Connected to: " + String(ssid);
  }
  html += "</div>";

  html += "<h1>Top Five for Today</h1>";
  html += "<p>Focus on what matters most. One step at a time.</p>";

  // Task input form
  html += "<form action='/submit' method='POST'>";
  for (int i = 0; i < 5; i++) {
    html += "Task " + String(i + 1) + ": <input type='text' name='task" + String(i) + "' value='" + tasks[i] +
            "' maxlength='15'><br>";
  }
  html += "<input type='submit' class='update' value='Update Tasks'>";
  html += "</form>";

  // Button row: Reset + Sleep
  html += "<div style='display: flex; gap: 10px; margin-top: 20px;'>";
  html += "<form class='inline' action='/reset' method='POST'>";
  html += "<input type='submit' class='reset' value='Reset Tasks'>";
  html += "</form>";

  html += "<form class='inline' action='/sleep' method='POST'>";
  html += "<input type='submit' class='update' value='Sleep Now'>";
  html += "</form>";
  html += "</div>";

  // Style selection form
  html += "<form action='/style' method='POST' style='margin-top: 30px;'>";
  html += "Background Color: <select name='bg'>";
  html += "<option value='white'" + String(bg == "white" ? " selected" : "") + ">White</option>";
  html += "<option value='black'" + String(bg == "black" ? " selected" : "") + ">Black</option>";
  html += "<option value='red'" + String(bg == "red" ? " selected" : "") + ">Red</option>";
  html += "</select><br>";

  html += "Text Color: <select name='text'>";
  html += "<option value='white'" + String(text == "white" ? " selected" : "") + ">White</option>";
  html += "<option value='black'" + String(text == "black" ? " selected" : "") + ">Black</option>";
  html += "<option value='red'" + String(text == "red" ? " selected" : "") + ">Red</option>";
  html += "</select><br>";

  html += "<input type='submit' class='update' value='Update Style'>";
  html += "</form>";

  // Task completion toggle
  html += "<form action='/toggle' method='POST' style='margin-top: 30px;'>";
  html += "Mark task as done: <select name='task'>";
  for (int i = 0; i < 5; i++) {
    String label = tasks[i];
    if (label.length() == 0) label = "(empty)";
    String status = completed[i] ? " ✓" : "";
    html += "<option value='" + String(i) + "'>" + label + status + "</option>";
  }
  html += "</select>";
  html += "<input type='submit' class='update' value='Toggle Completion'>";
  html += "</form>";

  html += "<p style='margin-top: 30px; font-size: 12px; color: #ffffffff;'>Current IP: " + getCurrentIP() + "</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleSubmit() {
  Serial.println("handleSubmit triggered");

  prefs.begin("tasks", false);
  for (int i = 0; i < 5; i++) {
    String newTask = server.arg("task" + String(i));
    String oldTask = prefs.getString(("task" + String(i)).c_str(), "");

    tasks[i] = newTask;
    prefs.putString(("task" + String(i)).c_str(), newTask);

    // Only reset completion if the task text changed
    if (newTask != oldTask) {
      prefs.putBool(("done" + String(i)).c_str(), false);
      completed[i] = false;
    }
  }
  prefs.end();

  updateDisplay();

  server.send(200, "text/html", R"rawliteral(
  <html>
    <head>
      <meta http-equiv="refresh" content="2;url=/" />
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        body {
          background-color: #66899aff;
          color: #000000;
          font-family: Arial, sans-serif;
          text-align: center;
          padding-top: 50px;
        }
        h1 {
          font-size: 28px;
          margin-bottom: 10px;
        }
        p {
          font-size: 16px;
          margin-bottom: 20px;
        }
        .checkmark {
          width: 56px;
          height: 56px;
          border-radius: 50%;
          display: inline-block;
          border: 4px solid #4caf50;
          position: relative;
          animation: pop 0.3s ease-out forwards;
          margin-bottom: 20px;
        }
        .checkmark::after {
          content: '';
          position: absolute;
          left: 14px;
          top: 18px;
          width: 14px;
          height: 28px;
          border-right: 4px solid #4caf50;
          border-bottom: 4px solid #4caf50;
          transform: rotate(45deg);
          opacity: 0;
          animation: draw 0.5s ease-out 0.3s forwards;
        }
        @keyframes pop {
          0% { transform: scale(0); opacity: 0; }
          100% { transform: scale(1); opacity: 1; }
        }
        @keyframes draw {
          0% { opacity: 0; transform: rotate(45deg) scale(0); }
          100% { opacity: 1; transform: rotate(45deg) scale(1); }
        }
      </style>
    </head>
    <body>
      <div class="checkmark"></div>
      <h1>Tasks Updated!</h1>
      <p>Redirecting back to the task page...</p>
    </body>
  </html>
  )rawliteral");
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/reset", handleReset);
  server.on("/sleep", handleSleep);
  server.on("/style", HTTP_POST, handleStyle);
  server.on("/toggle", HTTP_POST, handleToggle);
  server.begin();
}

void updateDisplay() {
  String ipAddress = getCurrentIP();

  prefs.begin("style", true);
  String bg = prefs.getString("bg", "white");
  String text = prefs.getString("text", "black");
  prefs.end();

  // Map string values to GxEPD2 color constants
  uint16_t bgColor = GxEPD_WHITE;
  uint16_t textColor = GxEPD_BLACK;

  if (bg == "black")
    bgColor = GxEPD_BLACK;
  else if (bg == "red")
    bgColor = GxEPD_RED;

  if (text == "white")
    textColor = GxEPD_WHITE;
  else if (text == "red")
    textColor = GxEPD_RED;
  else if (text == "black")
    textColor = GxEPD_BLACK;

  display.init();
  display.setRotation(1);
  display.setFont(&FreeMonoBold9pt7b);
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(bgColor);
    display.setTextColor(textColor);

    for (int i = 0; i < 5; i++) {
      int x = 2;
      int y = 15 + i * 20;
      String label = String(i + 1) + ".";
      String status = completed[i] ? "[X]" : "[ ]";
      String taskText = tasks[i];
      String fullLine = label + status + taskText;

      display.setCursor(x, y);
      display.print(fullLine);

      if (completed[i]) {
        // Draw strikethrough line
        int lineStartX = x + 40;  // Rough offset after "1.[X]"
        int lineEndX = display.width() - 5;
        int lineY = y - 6;  // Slightly above baseline
        display.drawLine(lineStartX, lineY, lineEndX, lineY, textColor);
      }
    }

    // Display connection status and IP
    display.setFont(NULL);
    display.setCursor(2, 102);
    display.print(isAPMode ? "AP:" : "IP:");
    display.setCursor(22, 102);
    display.print(ipAddress);

    display.setCursor(2, 112);
    display.print(isAPMode ? "SSID: TaskManager-Setup" : "Connected to WiFi");

  } while (display.nextPage());

  display.hibernate();
}

bool tasksExist() {
  for (int i = 0; i < 5; i++) {
    if (tasks[i].length() > 0) return true;
  }
  return false;
}

void showWelcomeMessage(String ip) {
  display.init();
  display.setRotation(1);
  display.setFont(&FreeMonoBold12pt7b);
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, 30);
    display.print("Welcome!");

    display.setCursor(10, 60);
    display.print("Go to:");

    display.setCursor(10, 90);
    display.print(ip);

    display.setCursor(10, 120);
    display.print("to enter tasks.");
  } while (display.nextPage());

  display.hibernate();
}

void drawQRCode(String ip) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, ip.c_str());

  display.init();
  display.setRotation(1);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    int scale = qrSize;  // Scale factor for better visibility
    int qrSizePx = qrcode.size * scale;
    int xOffset = (display.width() - qrSizePx) / 2;
    int yOffset = 1;

    for (int y = 0; y < qrcode.size; y++) {
      for (int x = 0; x < qrcode.size; x++) {
        if (qrcode_getModule(&qrcode, x, y)) {
          display.fillRect(xOffset + x * scale, yOffset + y * scale, scale, scale, GxEPD_BLACK);
        }
      }
    }

    // Add connection mode indicator
    display.setFont(NULL);
    display.setCursor(
        2, 30);  // ===========================================================================================
    if (isAPMode) {
      display.print(" HELLO!\n\n\n AP Mode \n\n Connect \n to WiFi");
    } else {
      display.print(" Scan QR \n to connect");
    }

  } while (display.nextPage());

  display.hibernate();
}

void handleReset() {
  prefs.begin("tasks", false);
  for (int i = 0; i < 5; i++) {
    String taskKey = "task" + String(i);
    String doneKey = "done" + String(i);
    prefs.remove(taskKey.c_str());
    prefs.remove(doneKey.c_str());
    tasks[i] = "";
    completed[i] = false;
  }
  prefs.end();

  String ip = getCurrentIP();
  drawQRCode("http://" + ip);

  server.send(200, "text/html", R"rawliteral(
  <html>
    <head>
      <meta http-equiv="refresh" content="2;url=/" />
      <style>
        body {
          font-family: Arial, sans-serif;
          text-align: center;
          padding-top: 50px;
          background-color: #66899aff;
          color: #000000;
        }
        h1 {
          font-size: 28px;
          margin-bottom: 10px;
        }
        p {
          font-size: 16px;
          margin-bottom: 20px;
        }
        .progress-container {
          width: 80%;
          background-color: #d7d7d7ff;
          border-radius: 20px;
          margin: 20px auto;
          height: 20px;
          overflow: hidden;
        }
        .progress-bar {
          height: 100%;
          width: 0%;
          background-color: #4caf50;
          animation: fill 2s linear forwards;
        }
        @keyframes fill {
          from { width: 0%; }
          to { width: 100%; }
        }
      </style>
    </head>
    <body>
      <h1>Tasks cleared!</h1>
      <p>Redirecting back to the task page...</p>
      <div class="progress-container">
        <div class="progress-bar"></div>
      </div>
    </body>
  </html>
)rawliteral");
}

void IRAM_ATTR handleButtonPress() { 
  // 
  resetRequested = true; 
}

void handleSleep() {
  server.send(200, "text/html",
              "<html><head><meta http-equiv='refresh' content='2;url=/' /><style>body { font-family: Arial, "
              "sans-serif; text-align: center; padding-top: 50px; background-color: #66899aff; color: #000000; } h1 { "
              "font-size: 28px; margin-bottom: 10px; } p { font-size: 16px; }</style></head><body><h1>Going to "
              "sleep...</h1><p>Press button to wake up</p></body></html>");
  delay(1000);
  esp_deep_sleep_start();
}

void handleStyle() {
  String bg = server.arg("bg");
  String text = server.arg("text");

  prefs.begin("style", false);
  prefs.putString("bg", bg);
  prefs.putString("text", text);
  prefs.end();

  updateDisplay();

  server.send(200, "text/html",
              "<html><head><meta http-equiv='refresh' content='2;url=/' /><style>body { font-family: Arial, "
              "sans-serif; text-align: center; padding-top: 50px; background-color: #66899aff; color: #000000; } h1 { "
              "font-size: 28px; margin-bottom: 10px; } a { color: #ffffffff; text-decoration: none; font-weight: bold; "
              "} a:hover { text-decoration: underline; }</style></head><body><h1>Style Updated!</h1><p>Redirecting "
              "back to the task page...</p></body></html>");
}

void handleToggle() {
  int index = server.arg("task").toInt();
  if (index >= 0 && index < 5) {
    completed[index] = !completed[index];
    prefs.begin("tasks", false);
    prefs.putBool(("done" + String(index)).c_str(), completed[index]);
    prefs.end();
    updateDisplay();
  }
  server.send(200, "text/html",
              "<html><head><meta http-equiv='refresh' content='2;url=/' /><style>body { font-family: Arial, "
              "sans-serif; text-align: center; padding-top: 50px; background-color: #66899aff; color: #000000; } h1 { "
              "font-size: 28px; margin-bottom: 10px; } a { color: #ffffffff; text-decoration: none; font-weight: bold; "
              "} a:hover { text-decoration: underline; }</style></head><body><h1>Task toggled!</h1><p>Redirecting back "
              "to the task page...</p></body></html>");
}
