/*
 * ArduinoHat_DynamixelController.ino
 *
 * Firmware for the Arduino Dynamixel Hat.
 * Controls Dynamixel X-series servos (IDs 1-4) using the DynamixelShield library.
 * Accepts serial commands on a second UART port (Serial2).
 *
 * Hardware:
 *   - Dynamixel bus on the shield's default serial port @ 1,000,000 baud
 *   - Command input on SoftwareSerial (pins 6/7) @ 57600 baud
 *   - Direction pin handled automatically by DynamixelShield
 *
 * Serial Command Protocol (space-delimited, newline-terminated):
 *   P <id>                Ping motor <id>
 *   T <id> <0|1>          Torque off/on for motor <id>
 *   G <id> <position>     Set goal position (raw) for motor <id>
 *   GD <id> <degrees>     Set goal position in degrees for motor <id>
 *   V <id> <velocity>     Set goal velocity for motor <id>
 *   S <id>                Read present position (raw) of motor <id>
 *   SD <id>               Read present position in degrees
 *   MODE <id> <mode>      Set operating mode (0=current, 1=velocity, 3=position, 4=extpos, 16=pwm)
 *   R <id> <addr> <len>   Read <len> bytes from register <addr>
 *   W  <id> <addr> <val>  Write 1-byte <val> to register <addr>
 *   W2 <id> <addr> <val>  Write 2-byte <val> to register <addr>
 *   W4 <id> <addr> <val>  Write 4-byte <val> to register <addr>
 *   LED <id> <0|1>        LED off/on
 *   REBOOT <id>           Reboot motor
 *   FACTORY <id>          Factory reset motor (DANGER)
 *
 * Responses (newline-terminated):
 *   OK <data...>          Command succeeded
 *   ERR <message>         Command failed
 */

#include <DynamixelShield.h>
#include <SoftwareSerial.h>

// ---------------------------------------------------------------------------
// Serial Port Setup
// ---------------------------------------------------------------------------

// Command serial — SoftwareSerial on pins 6 (TX) and 7 (RX)
// Connect: Pi GPIO14 (TXD) → Arduino pin 7 (RX)
//          Pi GPIO15 (RXD) → Arduino pin 6 (TX)
#define CMD_RX_PIN   7
#define CMD_TX_PIN   6
SoftwareSerial cmdSerial(CMD_RX_PIN, CMD_TX_PIN);
#define CMD_SERIAL   cmdSerial

// Debug serial — USB Serial for status messages
#define DBG_SERIAL   Serial

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define DXL_BAUD          1000000
#define DXL_PROTOCOL       2.0f
#define CMD_BAUD           57600

// Motor IDs
const uint8_t MOTOR_IDS[] = {1, 2, 3, 4};
#define MOTOR_COUNT  4

// ---------------------------------------------------------------------------
// DynamixelShield Object
// ---------------------------------------------------------------------------

DynamixelShield dxl;

using namespace ControlTableItem;

// ---------------------------------------------------------------------------
// Command Buffer
// ---------------------------------------------------------------------------

#define CMD_BUF_SIZE  80

char cmdBuf[CMD_BUF_SIZE];
uint8_t cmdIdx = 0;

// ---------------------------------------------------------------------------
// Helper: Check if Motor ID Is Valid
// ---------------------------------------------------------------------------

bool validID(uint8_t id) {
  for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
    if (MOTOR_IDS[i] == id) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Command Processing
// ---------------------------------------------------------------------------

void processCommand(const char *cmd) {
  // Skip leading whitespace
  while (*cmd == ' ' || *cmd == '\t') cmd++;
  if (*cmd == '\0') return;

  int id, val, addr, len;
  long lval;
  float deg;

  // ---- Multi-character commands first ----

  if (strncmp(cmd, "LED", 3) == 0) {
    if (sscanf(cmd + 3, "%d %d", &id, &val) == 2) {
      if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
      if (val) dxl.ledOn((uint8_t)id); else dxl.ledOff((uint8_t)id);
      CMD_SERIAL.print("OK LED "); CMD_SERIAL.print(id);
      CMD_SERIAL.print(" = "); CMD_SERIAL.println(val);
    } else {
      CMD_SERIAL.println("ERR usage: LED <id> <0|1>");
    }
  }
  else if (strncmp(cmd, "REBOOT", 6) == 0) {
    if (sscanf(cmd + 6, "%d", &id) == 1) {
      if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
      dxl.reboot((uint8_t)id);
      CMD_SERIAL.print("OK Reboot "); CMD_SERIAL.println(id);
    } else {
      CMD_SERIAL.println("ERR usage: REBOOT <id>");
    }
  }
  else if (strncmp(cmd, "FACTORY", 7) == 0) {
    if (sscanf(cmd + 7, "%d", &id) == 1) {
      if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
      CMD_SERIAL.print("OK Factory reset "); CMD_SERIAL.print(id);
      CMD_SERIAL.println(" — wait...");
      dxl.factoryReset((uint8_t)id, 0xFF);
      CMD_SERIAL.println("OK Factory reset complete");
    } else {
      CMD_SERIAL.println("ERR usage: FACTORY <id>");
    }
  }
  else if (strncmp(cmd, "MODE", 4) == 0) {
    if (sscanf(cmd + 4, "%d %d", &id, &val) == 2) {
      if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
      dxl.torqueOff((uint8_t)id);
      dxl.setOperatingMode((uint8_t)id, (uint8_t)val);
      dxl.torqueOn((uint8_t)id);
      CMD_SERIAL.print("OK Mode "); CMD_SERIAL.print(id);
      CMD_SERIAL.print(" = "); CMD_SERIAL.println(val);
    } else {
      CMD_SERIAL.println("ERR usage: MODE <id> <mode>");
    }
  }
  else if (strncmp(cmd, "GD", 2) == 0) {
    if (sscanf(cmd + 2, "%d %f", &id, &deg) == 2) {
      if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
      dxl.setGoalPosition((uint8_t)id, deg, UNIT_DEGREE);
      CMD_SERIAL.print("OK GoalDeg "); CMD_SERIAL.print(id);
      CMD_SERIAL.print(" -> "); CMD_SERIAL.println(deg);
    } else {
      CMD_SERIAL.println("ERR usage: GD <id> <degrees>");
    }
  }
  else if (strncmp(cmd, "SD", 2) == 0) {
    if (sscanf(cmd + 2, "%d", &id) == 1) {
      if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
      float pos = dxl.getPresentPosition((uint8_t)id, UNIT_DEGREE);
      CMD_SERIAL.print("OK PosDeg "); CMD_SERIAL.print(id);
      CMD_SERIAL.print(" = "); CMD_SERIAL.println(pos);
    } else {
      CMD_SERIAL.println("ERR usage: SD <id>");
    }
  }
  else if (strncmp(cmd, "HELP", 4) == 0 || strncmp(cmd, "help", 4) == 0) {
    CMD_SERIAL.println(F(
      "OK --- Commands ---\r\n"
      "P <id>              Ping motor\r\n"
      "T <id> <0|1>        Torque off/on\r\n"
      "G <id> <pos>        Set goal position (raw)\r\n"
      "GD <id> <deg>       Set goal position (degrees)\r\n"
      "V <id> <vel>        Set goal velocity\r\n"
      "S <id>              Read present position (raw)\r\n"
      "SD <id>             Read present position (degrees)\r\n"
      "MODE <id> <m>       Set operating mode\r\n"
      "R <id> <addr> <n>   Read n bytes from register\r\n"
      "W <id> <addr> <v>   Write 1 byte to register\r\n"
      "W2 <id> <addr> <v>  Write 2 bytes to register\r\n"
      "W4 <id> <addr> <v>  Write 4 bytes to register\r\n"
      "LED <id> <0|1>      LED off/on\r\n"
      "REBOOT <id>         Reboot motor\r\n"
      "FACTORY <id>        Factory reset (caution!)\r\n"
      "HELP                This message"
    ));
  }

  // ---- Single-character commands ----
  else {
    char type = cmd[0];
    const char *args = cmd + 1;

    switch (type) {

      case 'P':  // Ping
        if (sscanf(args, "%d", &id) == 1) {
          if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
          if (dxl.ping((uint8_t)id)) {
            uint16_t model = dxl.getModelNumber((uint8_t)id);
            CMD_SERIAL.print("OK Ping "); CMD_SERIAL.print(id);
            CMD_SERIAL.print(" model="); CMD_SERIAL.println(model);
          } else {
            CMD_SERIAL.print("ERR Ping "); CMD_SERIAL.print(id);
            CMD_SERIAL.println(" no response");
          }
        } else {
          CMD_SERIAL.println("ERR usage: P <id>");
        }
        break;

      case 'T':  // Torque
        if (sscanf(args, "%d %d", &id, &val) == 2) {
          if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
          if (val) dxl.torqueOn((uint8_t)id);
          else     dxl.torqueOff((uint8_t)id);
          CMD_SERIAL.print("OK Torque "); CMD_SERIAL.print(id);
          CMD_SERIAL.print(" = "); CMD_SERIAL.println(val);
        } else {
          CMD_SERIAL.println("ERR usage: T <id> <0|1>");
        }
        break;

      case 'G':  // Goal Position (raw)
        if (sscanf(args, "%d %ld", &id, &lval) == 2) {
          if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
          dxl.setGoalPosition((uint8_t)id, (int32_t)lval);
          CMD_SERIAL.print("OK Goal "); CMD_SERIAL.print(id);
          CMD_SERIAL.print(" -> "); CMD_SERIAL.println(lval);
        } else {
          CMD_SERIAL.println("ERR usage: G <id> <position>");
        }
        break;

      case 'V':  // Goal Velocity
        if (sscanf(args, "%d %ld", &id, &lval) == 2) {
          if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
          dxl.setGoalVelocity((uint8_t)id, (int32_t)lval);
          CMD_SERIAL.print("OK Velocity "); CMD_SERIAL.print(id);
          CMD_SERIAL.print(" -> "); CMD_SERIAL.println(lval);
        } else {
          CMD_SERIAL.println("ERR usage: V <id> <velocity>");
        }
        break;

      case 'S':  // Present Position (raw)
        if (sscanf(args, "%d", &id) == 1) {
          if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
          int32_t pos = dxl.getPresentPosition((uint8_t)id);
          CMD_SERIAL.print("OK Position "); CMD_SERIAL.print(id);
          CMD_SERIAL.print(" = "); CMD_SERIAL.println(pos);
        } else {
          CMD_SERIAL.println("ERR usage: S <id>");
        }
        break;

      case 'R':  // Read Register
        if (sscanf(args, "%d %d %d", &id, &addr, &len) == 3) {
          if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
          if (len > 64) len = 64;
          uint8_t data[64] = {0};
          int32_t err = dxl.read((uint8_t)id, (uint16_t)addr, 2, data, (uint16_t)len);
          if (err == 0) {
            CMD_SERIAL.print("OK Read "); CMD_SERIAL.print(id);
            CMD_SERIAL.print(" ["); CMD_SERIAL.print(addr);
            CMD_SERIAL.print("]:");
            for (int i = 0; i < len; i++) {
              CMD_SERIAL.print(" ");
              if (data[i] < 0x10) CMD_SERIAL.print("0");
              CMD_SERIAL.print(data[i], HEX);
            }
            CMD_SERIAL.println();
          } else {
            CMD_SERIAL.print("ERR Read "); CMD_SERIAL.print(id);
            CMD_SERIAL.print(" code="); CMD_SERIAL.println(err);
          }
        } else {
          CMD_SERIAL.println("ERR usage: R <id> <addr> <len>");
        }
        break;

      case 'W':  // Write Register (1, 2, or 4 bytes)
        if (cmd[1] == '2') {
          args = cmd + 2;
          if (sscanf(args, "%d %d %d", &id, &addr, &val) == 3) {
            if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
            uint8_t data[2] = { (uint8_t)(val & 0xFF), (uint8_t)((val >> 8) & 0xFF) };
            bool ok = dxl.write((uint8_t)id, (uint16_t)addr, data, 2);
            if (ok) {
              CMD_SERIAL.print("OK W2 "); CMD_SERIAL.print(id);
              CMD_SERIAL.print(" ["); CMD_SERIAL.print(addr);
              CMD_SERIAL.print("] = "); CMD_SERIAL.println(val);
            } else {
              CMD_SERIAL.print("ERR W2 "); CMD_SERIAL.print(id);
              CMD_SERIAL.println(" failed");
            }
          } else {
            CMD_SERIAL.println("ERR usage: W2 <id> <addr> <value>");
          }
        }
        else if (cmd[1] == '4') {
          args = cmd + 2;
          if (sscanf(args, "%d %d %ld", &id, &addr, &lval) == 3) {
            if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
            uint8_t data[4] = {
              (uint8_t)(lval & 0xFF),
              (uint8_t)((lval >> 8) & 0xFF),
              (uint8_t)((lval >> 16) & 0xFF),
              (uint8_t)((lval >> 24) & 0xFF)
            };
            bool ok = dxl.write((uint8_t)id, (uint16_t)addr, data, 4);
            if (ok) {
              CMD_SERIAL.print("OK W4 "); CMD_SERIAL.print(id);
              CMD_SERIAL.print(" ["); CMD_SERIAL.print(addr);
              CMD_SERIAL.print("] = "); CMD_SERIAL.println(lval);
            } else {
              CMD_SERIAL.print("ERR W4 "); CMD_SERIAL.print(id);
              CMD_SERIAL.println(" failed");
            }
          } else {
            CMD_SERIAL.println("ERR usage: W4 <id> <addr> <value>");
          }
        }
        else {
          // 1-byte write
          if (sscanf(args, "%d %d %d", &id, &addr, &val) == 3) {
            if (!validID(id)) { CMD_SERIAL.println("ERR invalid ID"); return; }
            uint8_t data[1] = { (uint8_t)(val & 0xFF) };
            bool ok = dxl.write((uint8_t)id, (uint16_t)addr, data, 1);
            if (ok) {
              CMD_SERIAL.print("OK W "); CMD_SERIAL.print(id);
              CMD_SERIAL.print(" ["); CMD_SERIAL.print(addr);
              CMD_SERIAL.print("] = "); CMD_SERIAL.println(val);
            } else {
              CMD_SERIAL.print("ERR W "); CMD_SERIAL.print(id);
              CMD_SERIAL.println(" failed");
            }
          } else {
            CMD_SERIAL.println("ERR usage: W <id> <addr> <value>");
          }
        }
        break;

      default:
        CMD_SERIAL.print("ERR unknown: "); CMD_SERIAL.println(cmd);
        CMD_SERIAL.println("OK Send HELP for command list");
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
  // USB Serial for debug output
  DBG_SERIAL.begin(115200);
  while (!DBG_SERIAL);

  // Command serial (connected to ESP32 bridge or direct UART)
  CMD_SERIAL.begin(CMD_BAUD);

  // Initialize DynamixelShield
  dxl.begin(DXL_BAUD);
  dxl.setPortProtocolVersion(DXL_PROTOCOL);

  DBG_SERIAL.println(F("=== Dynamixel Hat Controller ==="));
  DBG_SERIAL.print(F("DXL bus: ")); DBG_SERIAL.print(DXL_BAUD);
  DBG_SERIAL.println(F(" baud, Protocol 2.0"));
  DBG_SERIAL.print(F("Motors: ID "));
  for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
    if (i > 0) DBG_SERIAL.print(F(", "));
    DBG_SERIAL.print(MOTOR_IDS[i]);
  }
  DBG_SERIAL.println();
  DBG_SERIAL.print(F("Command serial: ")); DBG_SERIAL.print(CMD_BAUD);
  DBG_SERIAL.println(F(" baud on Serial2"));
  DBG_SERIAL.println(F("Send HELP for command list."));
  DBG_SERIAL.println(F("================================"));

  // Ping all configured motors on startup
  for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
    DBG_SERIAL.print(F("Motor ")); DBG_SERIAL.print(MOTOR_IDS[i]);
    if (dxl.ping(MOTOR_IDS[i])) {
      uint16_t model = dxl.getModelNumber(MOTOR_IDS[i]);
      DBG_SERIAL.print(F(" OK (model "));
      DBG_SERIAL.print(model);
      DBG_SERIAL.println(F(")"));
    } else {
      DBG_SERIAL.println(F(" NO RESPONSE"));
    }
  }
  DBG_SERIAL.println(F("Ready."));
  DBG_SERIAL.println();

  // Flush stale data from command serial
  while (CMD_SERIAL.available()) CMD_SERIAL.read();

  memset(cmdBuf, 0, CMD_BUF_SIZE);
  cmdIdx = 0;
}

// ---------------------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------------------

void loop() {
  while (CMD_SERIAL.available()) {
    char c = CMD_SERIAL.read();

    if (c == '\n' || c == '\r') {
      if (cmdIdx > 0) {
        cmdBuf[cmdIdx] = '\0';

        DBG_SERIAL.print("CMD: ");
        DBG_SERIAL.println(cmdBuf);

        processCommand(cmdBuf);

        cmdIdx = 0;
        memset(cmdBuf, 0, CMD_BUF_SIZE);
      }
    }
    else if (c == '\b' || c == 0x7F) {
      if (cmdIdx > 0) cmdIdx--;
    }
    else if (cmdIdx < CMD_BUF_SIZE - 1) {
      if (c >= 0x20 && c <= 0x7E) {
        cmdBuf[cmdIdx++] = c;
      }
    }
  }

  delay(1);
}
