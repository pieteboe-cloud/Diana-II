#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

/**
 * RoomWebTest - Wi-Fi access point + web server for the room-light demo.
 * Serves a browser page with a canvas room map. The browser posts lamp
 * colour changes to /api/lamp, RF test codes to /api/rf, and IR test
 * codes to /api/ir. We forward them to handler callbacks wired in main.cpp.
 */
class RoomWebTest {
public:
  // Callback fired when the browser changes a lamp's colour.
  // lamp: 0-11, r/g/b: 0-255 (RGB from the color picker),
  // brightness: 0-255 (from the master slider)
  using LampHandler = void (*)(uint8_t lamp, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);

  // Callback fired when the browser presses an RF test button.
  // hexCode: null-terminated hex string (e.g. "444E21") sent to the Uno as "O,RF,<hex>\n"
  using RfHandler = void (*)(const char* hexCode);

  // Callback fired when the browser presses an IR test button.
  // hexCode: null-terminated hex string (e.g. "FD02EF00") sent to the Uno as "O,IR,<hex>\n"
  using IrHandler = void (*)(const char* hexCode);

  RoomWebTest(const char* ssid, const char* password);

  /** Start the access point and the web server. Call from setup(). */
  void begin();

  /** Handle pending HTTP clients. Call from loop(). */
  void handleClient();

  /** Wire up the lamp colour-change handler. */
  void setLampHandler(LampHandler handler);

  /** Wire up the RF test-button handler. */
  void setRfHandler(RfHandler handler);

  /** Wire up the IR test-button handler. */
  void setIrHandler(IrHandler handler);

  IPAddress apIP() const;

private:
  const char* _ssid;
  const char* _password;
  WebServer _server;
  LampHandler _lampHandler;
  RfHandler _rfHandler;
  IrHandler _irHandler;

  static const char PAGE[];

  void _setupRoutes();
  void _handleLampPost();   // browser POSTs lamp colour
  void _handleRfPost();     // browser POSTs RF test code
  void _handleIrPost();     // browser POSTs IR test code
};
