const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('serialport');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

// --- Configuration ---
const DEFAULT_MOVE_PORT = '/dev/ttyUSB0';
const DEFAULT_DRILL_PORT = '/dev/ttyUSB1';
const DEFAULT_ARM_PORT = '/dev/ttyAMA0';
const DEFAULT_MOVE_BAUD = 115200;
const DEFAULT_DRILL_BAUD = 115200;
const DEFAULT_ARM_BAUD = 57600;   // Arduino Hat uses 57600 baud
const HTTP_PORT = 3000;

// --- Arm command queue (rate-limited, ACK-gated) ---
const ARM_CMD_TIMEOUT = 500;   // ms to wait for OK/ERR before sending next
const ARM_CMD_GAP = 30;        // minimum ms gap between commands
const armQueue = [];
let armBusy = false;
let armTimer = null;

function flushArmQueue() {
  armQueue.length = 0;
  if (armTimer) { clearTimeout(armTimer); armTimer = null; }
  armBusy = false;
}

function processArmQueue() {
  if (armBusy || armQueue.length === 0) return;
  if (!armPort || !armPort.isOpen) {
    flushArmQueue();
    return;
  }

  armBusy = true;
  const cmd = armQueue.shift();
  const msg = cmd + '\n';
  console.log(`[Arm] TX: ${msg.trim()}`);
  armPort.write(msg, (err) => {
    if (err) console.error(`[Arm] Write error: ${err.message}`);
  });

  // Safety timeout: if Arduino doesn't respond, proceed anyway
  armTimer = setTimeout(() => {
    console.warn(`[Arm] Timeout waiting for response to: ${cmd}`);
    armBusy = false;
    armTimer = null;
    setTimeout(() => processArmQueue(), ARM_CMD_GAP);
  }, ARM_CMD_TIMEOUT);
}

function onArmResponse() {
  if (armBusy && armTimer) {
    clearTimeout(armTimer);
    armTimer = null;
    armBusy = false;
    setTimeout(() => processArmQueue(), ARM_CMD_GAP);
  }
}

function queueArmCommand(command) {
  if (!armPort || !armPort.isOpen) {
    console.warn(`[Arm] Port not open, dropping: ${command}`);
    return;
  }

  // Dedup: replace any queued command with the same type+servo (first two tokens)
  const dedupKey = command.split(' ').slice(0, 2).join(' ');  // e.g. "V 1"
  for (let i = armQueue.length - 1; i >= 0; i--) {
    const existingKey = armQueue[i].split(' ').slice(0, 2).join(' ');
    if (existingKey === dedupKey) {
      console.log(`[Arm] Dedup: replacing "${armQueue[i]}" → "${command}"`);
      armQueue[i] = command;
      return;  // replaced in place, no need to push
    }
  }

  armQueue.push(command);
  processArmQueue();
}

// --- Serial Port State ---
let movePort = null;
let drillPort = null;
let armPort = null;
let movePortPath = DEFAULT_MOVE_PORT;
let drillPortPath = DEFAULT_DRILL_PORT;
let armPortPath = DEFAULT_ARM_PORT;
let moveBaudRate = DEFAULT_MOVE_BAUD;
let drillBaudRate = DEFAULT_DRILL_BAUD;
let armBaudRate = DEFAULT_ARM_BAUD;

// --- Serve static files from public/ ---
app.use(express.static('public'));

// --- Helper: open a serial port ---
function openSerialPort(portPath, baudRate, label) {
  console.log(`[${label}] Opening ${portPath} at ${baudRate} baud...`);
  const port = new SerialPort({ path: portPath, baudRate }, (err) => {
    if (err) {
      console.error(`[${label}] Failed to open ${portPath}: ${err.message}`);
      io.emit('port-status', { label, connected: false, path: portPath, error: err.message });
      return;
    }
    console.log(`[${label}] Connected to ${portPath}`);
    io.emit('port-status', { label, connected: true, path: portPath, error: null });
  });

  const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));

  parser.on('data', (line) => {
    const trimmed = line.trim();
    if (!trimmed) return;
    const timestamp = new Date().toISOString();
    console.log(`[${label}] RX: ${trimmed}`);
    io.emit('serial-data', {
      port: label,
      timestamp,
      data: trimmed,
      isAck: trimmed.startsWith('ACK:') || trimmed.startsWith('OK'),
      isError: trimmed.includes('STALLED') || trimmed.includes('ERROR') || trimmed.startsWith('ERR')
    });

    // Arm port: detect OK/ERR response → unblock queue
    if (label === 'Arm' && (trimmed.startsWith('OK') || trimmed.startsWith('ERR'))) {
      onArmResponse();
    }
  });

  port.on('close', () => {
    console.log(`[${label}] Port closed: ${portPath}`);
    io.emit('port-status', { label, connected: false, path: portPath, error: 'Port closed' });
    if (label === 'Arm') flushArmQueue();
  });

  port.on('error', (err) => {
    console.error(`[${label}] Error: ${err.message}`);
    io.emit('port-status', { label, connected: false, path: portPath, error: err.message });
    if (label === 'Arm') flushArmQueue();
  });

  return port;
}

// --- Helper: close a serial port ---
function closeSerialPort(port, label, path) {
  if (port && port.isOpen) {
    port.close((err) => {
      if (err) {
        console.error(`[${label}] Error closing port: ${err.message}`);
      } else {
        console.log(`[${label}] Port closed: ${path}`);
      }
    });
  }
}

// --- Open all serial ports ---
function openAllPorts() {
  flushArmQueue();
  closeSerialPort(movePort, 'Movement', movePortPath);
  closeSerialPort(drillPort, 'Drill', drillPortPath);
  closeSerialPort(armPort, 'Arm', armPortPath);
  movePort = openSerialPort(movePortPath, moveBaudRate, 'Movement');
  drillPort = openSerialPort(drillPortPath, drillBaudRate, 'Drill');
  armPort = openSerialPort(armPortPath, armBaudRate, 'Arm');
}

openAllPorts();

function clampSpeed(val) {
  const s = parseInt(val);
  return Math.max(0, Math.min(65535, isNaN(s) ? 30000 : s));
}

// --- Helper: write comma-separated movement command ---
function writeCommand(port, portPath, label, command, speed) {
  if (!port || !port.isOpen) {
    console.warn(`[${label}] Cannot write: port ${portPath} not open`);
    return false;
  }
  const msg = `${command},${speed}\n`;
  console.log(`[${label}] TX: ${msg.trim()}`);
  port.write(msg, (err) => {
    if (err) console.error(`[${label}] Write error: ${err.message}`);
  });
  return true;
}

// --- Socket.IO connection handling ---
io.on('connection', (socket) => {
  console.log(`Client connected: ${socket.id}`);

  // Send current port status on connect
  const ports = [
    { label: 'Movement', port: movePort, path: movePortPath },
    { label: 'Drill', port: drillPort, path: drillPortPath },
    { label: 'Arm', port: armPort, path: armPortPath },
  ];
  ports.forEach(p => {
    socket.emit('port-status', {
      label: p.label,
      connected: p.port && p.port.isOpen,
      path: p.path,
      error: null
    });
  });
  socket.emit('config', {
    movePort: movePortPath, drillPort: drillPortPath, armPort: armPortPath,
    moveBaud: moveBaudRate, drillBaud: drillBaudRate, armBaud: armBaudRate
  });

  // --- Movement commands (sent to movement serial port) ---
  socket.on('move', (data) => {
    const cmd = parseInt(data.command);
    const spd = clampSpeed(data.speed);
    writeCommand(movePort, movePortPath, 'Movement', cmd, spd);
  });

  // --- Drill commands (motor0, sent to drill serial port) ---
  socket.on('drill', (data) => {
    const spd = clampSpeed(data.speed);
    let cmd;
    if (data.direction === 'forward') cmd = 5;
    else if (data.direction === 'reverse') cmd = 6;
    else cmd = 7;
    writeCommand(drillPort, drillPortPath, 'Drill', cmd, spd);
  });

  // --- Sample collection commands (motor4, sent to drill serial port) ---
  socket.on('sample', (data) => {
    const spd = clampSpeed(data.speed);
    let cmd;
    if (data.direction === 'extend') cmd = 17;
    else if (data.direction === 'retract') cmd = 18;
    else cmd = 19;
    writeCommand(drillPort, drillPortPath, 'Drill', cmd, spd);
  });

  // --- Arm: initialise servos into velocity mode (queued, one at a time) ---
  socket.on('arm-init', (data) => {
    const ids = data && data.servoId ? [parseInt(data.servoId)] : [1, 2];
    ids.forEach(id => {
      if (id < 1 || id > 4) return;
      queueArmCommand(`T ${id} 1`);
      queueArmCommand(`MODE ${id} 1`);
    });
  });

  // --- Arm servo velocity commands (Dynamixel Hat, velocity mode) ---
  socket.on('arm-velocity', (data) => {
    const id = parseInt(data.servoId);
    if (isNaN(id) || id < 1 || id > 4) {
      console.warn(`[Arm] Invalid servo ID: ${data.servoId}`);
      return;
    }
    let vel = parseInt(data.velocity);
    if (isNaN(vel)) vel = 0;
    vel = Math.max(-1023, Math.min(1023, vel));
    queueArmCommand(`V ${id} ${vel}`);
  });

  socket.on('arm-torque', (data) => {
    const id = parseInt(data.servoId);
    const val = data.enable ? '1' : '0';
    queueArmCommand(`T ${id} ${val}`);
  });

  socket.on('arm-raw', (data) => {
    queueArmCommand(data.command);
  });

  // --- Emergency stop (stops everything on all three ports) ---
  socket.on('emergency-stop', () => {
    console.log('[Emergency] EMERGENCY STOP triggered!');
    writeCommand(movePort, movePortPath, 'Movement', 0, 0);
    writeCommand(drillPort, drillPortPath, 'Drill', 7, 0);
    writeCommand(drillPort, drillPortPath, 'Drill', 19, 0);
    // Flush pending arm commands, send stop immediately
    flushArmQueue();
    for (let id = 1; id <= 2; id++) {
      queueArmCommand(`V ${id} 0`);
    }
  });

  // --- Reconfigure serial ports ---
  socket.on('set-config', (config) => {
    if (config.movePort) movePortPath = config.movePort;
    if (config.drillPort) drillPortPath = config.drillPort;
    if (config.armPort) armPortPath = config.armPort;
    if (config.moveBaud) moveBaudRate = parseInt(config.moveBaud);
    if (config.drillBaud) drillBaudRate = parseInt(config.drillBaud);
    if (config.armBaud) armBaudRate = parseInt(config.armBaud);
    console.log('[Config] Updated. Reconnecting ports...');
    openAllPorts();
    socket.emit('config', {
      movePort: movePortPath, drillPort: drillPortPath, armPort: armPortPath,
      moveBaud: moveBaudRate, drillBaud: drillBaudRate, armBaud: armBaudRate
    });
  });

  // --- Request port refresh ---
  socket.on('refresh-ports', () => {
    openAllPorts();
    ports.forEach(p => {
      socket.emit('port-status', {
        label: p.label,
        connected: p.port && p.port.isOpen,
        path: p.path,
        error: null
      });
    });
  });

  socket.on('disconnect', () => {
    console.log(`Client disconnected: ${socket.id}`);
  });
});

// --- Start server ---
server.listen(HTTP_PORT, () => {
  console.log('=== Rover Web Controller ===');
  console.log(`Server running at http://localhost:${HTTP_PORT}`);
  console.log(`Movement port: ${movePortPath} @ ${moveBaudRate} baud`);
  console.log(`Drill port:    ${drillPortPath} @ ${drillBaudRate} baud`);
  console.log(`Arm port:      ${armPortPath} @ ${armBaudRate} baud`);
  console.log('=============================');
});
