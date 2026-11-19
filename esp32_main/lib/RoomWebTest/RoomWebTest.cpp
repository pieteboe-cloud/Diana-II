#include "RoomWebTest.h"

const char RoomWebTest::PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Room Lights</title>
  <style>
    body {
      font-family: sans-serif;
      max-width: 640px;
      margin: 20px auto;
      padding: 0 16px;
    }
    canvas {
      border: 1px solid #999;
      background: #fafafa;
      display: block;
      margin: 16px auto;
      cursor: pointer;
    }
    .controls {
      text-align: center;
      margin: 16px 0;
    }
    label {
      display: block;
      margin: 10px 0 4px;
      font-weight: bold;
    }
    input[type="range"] {
      width: 300px;
    }
    button {
      padding: 8px 16px;
      font-size: 16px;
      margin: 6px;
    }
    #status {
      margin-top: 10px;
      color: #555;
      font-family: monospace;
    }
    h1 { text-align: center; }
    .test-box {
      margin-top: 24px;
      padding: 16px;
      background: #f0f0f0;
      border-radius: 8px;
    }
    .test-box h3 {
      margin-top: 0;
      text-align: center;
    }
    .test-buttons {
      display: flex;
      gap: 8px;
      justify-content: center;
      flex-wrap: wrap;
    }
  </style>
</head>
<body>

  <h1>Room Lights — 12 Lamps</h1>

  <!-- Canvas draws the room top-down. Click a lamp dot to select it. -->
  <canvas id="roomCanvas" width="600" height="340"></canvas>

  <div class="controls">
    <label>Selected lamp: <span id="selLamp">0</span></label>
    <label>Colour:</label>
    <input type="color" id="colorPicker" value="#ff0000">

    <label>Master brightness (Value):</label>
    <input type="range" id="brightness" min="0" max="255" value="255">

    <button id="sendBtn">Send to lamps</button>
  </div>

  <p id="status">Click a lamp, pick a colour, set brightness, then press Send.</p>

  <!-- Test buttons for RF and IR codes -->
  <div class="test-box">
    <h3>Test Buttons (RF / IR)</h3>
    <div class="test-buttons">
      <button id="rfOnBtn">RF On</button>
      <button id="rfOffBtn">RF Off</button>
      <button id="irOffBtn">IR Off</button>
      <button id="irOnBtn">IR On</button>
      <button id="irRedBtn">IR Red</button>
    </div>
  </div>

  <script>
    // -----------------------------------------------------------
    // Lamp positions (top-down view of your room)
    // You can move these x/y values to match your real layout.
    // -----------------------------------------------------------
    const lamps = [
      {id:0, x:100, y:280}, {id:1, x:180, y:280}, {id:2, x:260, y:280},
      {id:3, x:340, y:280}, {id:4, x:420, y:280}, {id:5, x:500, y:280},
      {id:6, x:570, y:170},                   // right side
      {id:7, x:100, y:60},  {id:8, x:180, y:60}, {id:9, x:260, y:60}, {id:10, x:340, y:60}, // back row
      {id:11, x:30, y:170}                   // left side (last)
    ];

    let selectedLamp = 0;
    const canvas = document.getElementById('roomCanvas');
    const ctx = canvas.getContext('2d');

    // Draw the room outline + lamp dots.
    function draw() {
      ctx.clearRect(0, 0, canvas.width, canvas.height);

      // Simple room border
      ctx.strokeStyle = '#999';
      ctx.strokeRect(20, 20, 560, 300);
      ctx.fillStyle = '#333';
      ctx.font = '14px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Front', 300, 320);
      ctx.fillText('Back', 300, 40);
      ctx.fillText('Left', 10, 170);
      ctx.fillText('Right', 590, 170);

      for (let l of lamps) {
        const isSel = (selectedLamp === l.id);

        ctx.beginPath();
        ctx.arc(l.x, l.y, 16, 0, Math.PI * 2);
        ctx.fillStyle = '#cccccc';   // unlit = grey
        ctx.fill();
        ctx.strokeStyle = isSel ? '#000000' : '#666666';
        ctx.lineWidth = isSel ? 4 : 2;
        ctx.stroke();

        // Lamp index number under the dot
        ctx.fillStyle = '#000';
        ctx.font = '12px sans-serif';
        ctx.fillText(l.id, l.x, l.y + 4);
      }
    }

    // Click on canvas → select the closest lamp dot
    canvas.addEventListener('click', (e) => {
      const rect = canvas.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;

      for (let l of lamps) {
        const dx = mx - l.x;
        const dy = my - l.y;
        if (dx * dx + dy * dy < 16 * 16) {
          selectedLamp = l.id;
          document.getElementById('selLamp').textContent = l.id;
          draw();
          return;
        }
      }
    });

    // Send the selected lamp's colour to the ESP32
    document.getElementById('sendBtn').addEventListener('click', () => {
      const color = document.getElementById('colorPicker').value; // e.g. "#ff0000"
      const brightness = document.getElementById('brightness').value; // 0-255

      // Parse hex #RRGGBB into three numbers 0-255
      const r = parseInt(color.substring(1, 3), 16);
      const g = parseInt(color.substring(3, 5), 16);
      const b = parseInt(color.substring(5, 7), 16);

      // Build form data (simple text fields, easy for ESP32 to read)
      const form = new FormData();
      form.append('lamp', selectedLamp);
      form.append('r', r);
      form.append('g', g);
      form.append('b', b);
      form.append('v', brightness);

      fetch('/api/lamp', { method: 'POST', body: form })
        .then(resp => resp.text())
        .then(text => {
          document.getElementById('status').textContent =
            'Lamp ' + selectedLamp + ' sent → ' + text;
        })
        .catch(err => {
          document.getElementById('status').textContent = 'Error: ' + err;
        });
    });

    // Send an RF or IR test code to the ESP32
    function sendTestCode(type, code) {
      const params = new URLSearchParams();
      params.append('code', code);

      fetch('/api/' + type, { method: 'POST', body: params })
        .then(resp => resp.text())
        .then(text => {
          document.getElementById('status').textContent =
            type.toUpperCase() + ' ' + code + ' → ' + text;
        })
        .catch(err => {
          document.getElementById('status').textContent = 'Error: ' + err;
        });
    }

    // RF test buttons (decimal values converted to hex on the ESP32 side)
    document.getElementById('rfOnBtn').addEventListener('click', () => {
      sendTestCode('rf', '4477265');
    });
    document.getElementById('rfOffBtn').addEventListener('click', () => {
      sendTestCode('rf', '4477268');
    });

    // IR test buttons (hex strings, 0x prefix stripped)
    document.getElementById('irOffBtn').addEventListener('click', () => {
      sendTestCode('ir', 'FD02EF00');
    });
    document.getElementById('irOnBtn').addEventListener('click', () => {
      sendTestCode('ir', 'FC03EF00');
    });
    document.getElementById('irRedBtn').addEventListener('click', () => {
      sendTestCode('ir', 'FB04EF00');
    });

    draw();
  </script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
RoomWebTest::RoomWebTest(const char* ssid, const char* password)
  : _ssid(ssid), _password(password), _server(80),
    _lampHandler(nullptr), _rfHandler(nullptr), _irHandler(nullptr) {}

// ---------------------------------------------------------------------------
// begin() - start the access point and the web server
// ---------------------------------------------------------------------------
void RoomWebTest::begin() {
  Serial.println();
  Serial.println("Starting ESP32 access point...");

  WiFi.mode(WIFI_AP);

  if (!WiFi.softAP(_ssid, _password)) {
    Serial.println("ERROR: Could not start the access point.");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("Wi-Fi name: ");
  Serial.println(_ssid);
  Serial.print("Wi-Fi password: ");
  Serial.println(_password);
  Serial.print("Open this address in your browser: http://");
  Serial.println(WiFi.softAPIP());

  _setupRoutes();
  _server.begin();
  Serial.println("Web server started.");
}

// ---------------------------------------------------------------------------
// handleClient() - process pending HTTP requests
// ---------------------------------------------------------------------------
void RoomWebTest::handleClient() {
  _server.handleClient();
}

// ---------------------------------------------------------------------------
// setLampHandler() - wire the browser's colour changes to your code
// ---------------------------------------------------------------------------
void RoomWebTest::setLampHandler(LampHandler handler) {
  _lampHandler = handler;
}

// ---------------------------------------------------------------------------
// setRfHandler() - wire the RF test buttons to your code
// ---------------------------------------------------------------------------
void RoomWebTest::setRfHandler(RfHandler handler) {
  _rfHandler = handler;
}

// ---------------------------------------------------------------------------
// setIrHandler() - wire the IR test buttons to your code
// ---------------------------------------------------------------------------
void RoomWebTest::setIrHandler(IrHandler handler) {
  _irHandler = handler;
}

// ---------------------------------------------------------------------------
// apIP() - the IP the AP is broadcasting on
// ---------------------------------------------------------------------------
IPAddress RoomWebTest::apIP() const {
  return WiFi.softAPIP();
}

// ---------------------------------------------------------------------------
// _setupRoutes() - register the HTTP endpoints
// ---------------------------------------------------------------------------
void RoomWebTest::_setupRoutes() {
  // Serve the room-map page at /
  _server.on("/", HTTP_GET, [this]() {
    _server.send_P(200, "text/html", PAGE);
  });

  // Receive a single lamp colour change from the browser.
  // Form fields: lamp, r, g, b, v (brightness)
  _server.on("/api/lamp", HTTP_POST, [this]() {
    _handleLampPost();
  });

  // Receive an RF test code from the browser.
  // Form field: code (hex string, e.g. "444E21")
  _server.on("/api/rf", HTTP_POST, [this]() {
    _handleRfPost();
  });

  // Receive an IR test code from the browser.
  // Form field: code (hex string, e.g. "FD02EF00")
  _server.on("/api/ir", HTTP_POST, [this]() {
    _handleIrPost();
  });
}

// ---------------------------------------------------------------------------
// _handleLampPost() - read the POST fields and call the lamp handler
// ---------------------------------------------------------------------------
void RoomWebTest::_handleLampPost() {
  // Check all required fields are present
  if (!_server.hasArg("lamp") || !_server.hasArg("r") ||
      !_server.hasArg("g") || !_server.hasArg("b") || !_server.hasArg("v")) {

    Serial.println("[ESP32] /api/lamp missing fields");
    _server.send(400, "text/plain", "Missing fields");
    return;
  }

  // Parse the numbers
  uint8_t lamp  = (uint8_t)_server.arg("lamp").toInt();
  uint8_t r     = (uint8_t)_server.arg("r").toInt();
  uint8_t g     = (uint8_t)_server.arg("g").toInt();
  uint8_t b     = (uint8_t)_server.arg("b").toInt();
  uint8_t bri   = (uint8_t)_server.arg("v").toInt();

  Serial.print("[ESP32] /api/lamp lamp=");
  Serial.print(lamp);
  Serial.print(" r="); Serial.print(r);
  Serial.print(" g="); Serial.print(g);
  Serial.print(" b="); Serial.print(b);
  Serial.print(" v="); Serial.println(bri);

  // Forward to the handler wired in main.cpp
  if (_lampHandler) {
    // NOTE: browser sends RGB. LampController stores HSV.
    // The RGB→HSV conversion is done in main.cpp via LampController::rgbToHsv.
    _lampHandler(lamp, r, g, b, bri);
    _server.send(200, "text/plain", "OK");
  } else {
    _server.send(500, "text/plain", "No handler");
  }
}

// ---------------------------------------------------------------------------
// _handleRfPost() - read the RF code and call the RF handler
// ---------------------------------------------------------------------------
void RoomWebTest::_handleRfPost() {
  if (!_server.hasArg("code")) {
    Serial.println("[ESP32] /api/rf missing 'code' field");
    _server.send(400, "text/plain", "Missing code field");
    return;
  }

  String codeStr = _server.arg("code");
  Serial.print("[ESP32] /api/rf code=");
  Serial.println(codeStr);

  if (_rfHandler) {
    _rfHandler(codeStr.c_str());
    _server.send(200, "text/plain", "OK");
  } else {
    _server.send(500, "text/plain", "No handler");
  }
}

// ---------------------------------------------------------------------------
// _handleIrPost() - read the IR code and call the IR handler
// ---------------------------------------------------------------------------
void RoomWebTest::_handleIrPost() {
  if (!_server.hasArg("code")) {
    Serial.println("[ESP32] /api/ir missing 'code' field");
    _server.send(400, "text/plain", "Missing code field");
    return;
  }

  String codeStr = _server.arg("code");
  Serial.print("[ESP32] /api/ir code=");
  Serial.println(codeStr);

  if (_irHandler) {
    _irHandler(codeStr.c_str());
    _server.send(200, "text/plain", "OK");
  } else {
    _server.send(500, "text/plain", "No handler");
  }
}
