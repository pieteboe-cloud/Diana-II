#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// The phone/computer will connect to this Wi-Fi network.
// The password must be at least 8 characters.
const char* apSsid = "RoomLights";
const char* apPassword = "LightRoom42";

WebServer server(80);

// A small web page stored in the ESP32's program memory.
const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Room Lights</title>
  <style>
    body {
      font-family: sans-serif;
      max-width: 500px;
      margin: 40px auto;
      padding: 0 16px;
    }

    button {
      font-size: 18px;
      padding: 12px 18px;
    }
  </style>
</head>
<body>
  <h1>Room Lights</h1>
  <p>ESP32 access-point test page.</p>

  <button id="testButton">Test connection</button>
  <p id="status">Not tested yet.</p>

  <script>
    const button = document.getElementById("testButton");
    const statusText = document.getElementById("status");

    button.addEventListener("click", async () => {
      statusText.textContent = "Sending test...";

      try {
        const response = await fetch("/api/test", {
          method: "POST"
        });

        if (!response.ok) {
          throw new Error("Request failed");
        }

        statusText.textContent = "The ESP32 received the test!";
      } catch (error) {
        statusText.textContent = "Could not reach the ESP32.";
      }
    });
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Starting ESP32 access point...");

  WiFi.mode(WIFI_AP);

  if (!WiFi.softAP(apSsid, apPassword)) {
    Serial.println("ERROR: Could not start the access point.");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("Wi-Fi name: ");
  Serial.println(apSsid);

  Serial.print("Wi-Fi password: ");
  Serial.println(apPassword);

  Serial.print("Open this address in your browser: http://");
  Serial.println(WiFi.softAPIP());

  // Send the web page when the browser visits the ESP32.
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", PAGE);
  });

  // Handle the test button.
  server.on("/api/test", HTTP_POST, []() {
    Serial.println("Browser button reached the ESP32.");
    server.send(200, "text/plain", "OK");
  });

  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();

  // Your light-control code can run here later.
}
