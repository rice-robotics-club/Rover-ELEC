/*
 * ESP32_UART_Bridge.ino
 *
 * Bidirectional UART-to-USB bridge for the Arduino Dynamixel Hat.
 * Bridges USB Serial (PC) <-> Serial2 (Arduino Hat command port).
 *
 * Wiring:
 *   ESP32 GPIO16 (RX2)  ->  Arduino TX  (command serial)
 *   ESP32 GPIO17 (TX2)  ->  Arduino RX  (command serial)
 *   ESP32 GND           ->  Arduino GND
 *
 * The PC sees the ESP32 as a standard USB CDC serial port.
 * Any bytes sent from the PC are forwarded to the Arduino.
 * Any bytes received from the Arduino are forwarded to the PC.
 *
 * Baud rates:
 *   - USB Serial:  115200 (or whatever the PC opens)
 *   - Serial2:     115200 (must match Arduino's CMD_SERIAL baud)
 */

#include <HardwareSerial.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define ARDUINO_BAUD   115200   // Must match Arduino's CMD_SERIAL baud rate

// Serial2 pins (ESP32 default)
#define RX2_PIN  16
#define TX2_PIN  17

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  // USB Serial — the built-in USB-CDC port
  Serial.begin(115200);
  while (!Serial);  // Wait for USB serial to connect (native USB)

  // Serial2 — connected to the Arduino Hat's command serial port
  Serial2.begin(ARDUINO_BAUD, SERIAL_8N1, RX2_PIN, TX2_PIN);

  // Brief startup message on USB Serial only (don't spam the Arduino)
  Serial.println();
  Serial.println(F("=== ESP32 UART-to-USB Bridge ==="));
  Serial.println(F("PC <--> Serial2 (GPIO16/17)"));
  Serial.print(F("Arduino baud: "));
  Serial.println(ARDUINO_BAUD);
  Serial.println(F("Ready. All data relayed bidirectionally."));
  Serial.println(F("================================="));
  Serial.println();
}

// ---------------------------------------------------------------------------
// Main Loop — Bidirectional Relay
// ---------------------------------------------------------------------------

void loop() {
  // --- PC -> Arduino: forward bytes from USB Serial to Serial2 ---
  while (Serial.available()) {
    char c = Serial.read();
    Serial2.write(c);
  }

  // --- Arduino -> PC: forward bytes from Serial2 to USB Serial ---
  while (Serial2.available()) {
    char c = Serial2.read();
    Serial.write(c);
  }

  // Small yield to avoid watchdog issues and allow other ESP32 tasks to run
  delay(1);
}
