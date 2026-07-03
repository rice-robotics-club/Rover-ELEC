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
const DEFAULT_MOVE_BAUD = 115200;
const DEFAULT_DRILL_BAUD = 115200;
const PORT = 3000;

// --- Serial Port State ---
let movePort = null;
let drillPort = null;
let movePortPath = DEFAULT_MOVE_PORT;
let drillPortPath = DEFAULT_DRILL_PORT;
let moveBaudRate = DEFAULT_MOVE_BAUD;
let drillBaudRate = DEFAULT_DRILL_BAUD;

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
      isAck: trimmed.startsWith('ACK:'),
      isError: trimmed.includes('STALLED') || trimmed.includes('ERROR')
    });
  });

  port.on('close', () => {
    console.log(`[${label}] Port closed: ${portPath}`);
    io.emit('port-status', { label, connected: false, path: portPath, error: 'Port closed' });
  });

  port.on('error', (err) => {
    console.error(`[${label}] Error: ${err.message}`);
    io.emit('port-status', { label, connected: false, path: portPath, error: err.message });
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

// --- Open both serial ports ---
function openAllPorts() {
  closeSerialPort(movePort, 'Movement', movePortPath);
  closeSerialPort(drillPort, 'Drill', drillPortPath);
  movePort = openSerialPort(movePortPath, moveBaudRate, 'Movement');
  drillPort = openSerialPort(drillPortPath, drillBaudRate, 'Drill');
}

openAllPorts();

// --- Helper: write command to serial port ---
function writeCommand(port, portPath, label, command, speed) {
  if (!port || !port.isOpen) {
    console.warn(`[${label}] Cannot write: port ${portPath} not open`);
    return false;
  }
  const msg = `${command},${speed}\n`;
  console.log(`[${label}] TX: ${msg.trim()}`);
  port.write(msg, (err) => {
    if (err) {
      console.error(`[${label}] Write error: ${err.message}`);
    }
  });
  return true;
}

// --- Socket.IO connection handling ---
io.on('connection', (socket) => {
  console.log(`Client connected: ${socket.id}`);

  // Send current port status on connect
  socket.emit('port-status', {
    label: 'Movement',
    connected: movePort && movePort.isOpen,
    path: movePortPath,
    error: null
  });
  socket.emit('port-status', {
    label: 'Drill',
    connected: drillPort && drillPort.isOpen,
    path: drillPortPath,
    error: null
  });
  socket.emit('config', {
    movePort: movePortPath,
    drillPort: drillPortPath,
    moveBaud: moveBaudRate,
    drillBaud: drillBaudRate
  });

  // --- Movement commands (sent to movement serial port) ---
  socket.on('move', (data) => {
    // data: { command: 0-4, speed: 0-65535 }
    const cmd = parseInt(data.command);
    const spd = Math.max(0, Math.min(65535, parseInt(data.speed) || 30000));
    writeCommand(movePort, movePortPath, 'Movement', cmd, spd);
  });

  // --- Individual motor commands (for drivetrain motors 1-4) ---
  socket.on('motor', (data) => {
    // data: { motor: 1-4, direction: 'forward'|'backward'|'stop', speed: 0-65535 }
    const motor = parseInt(data.motor);
    const spd = Math.max(0, Math.min(65535, parseInt(data.speed) || 30000));
    let cmd;
    if (data.direction === 'forward') {
      cmd = 6 + motor * 2;   // 8, 10, 12, 14 for motors 1-4 forward
    } else if (data.direction === 'backward') {
      cmd = 7 + motor * 2;   // 9, 11, 13, 15 for motors 1-4 backward
    } else {
      cmd = 8 + motor * 2;   // 10, 12, 14, 16 for motors 1-4 stop
    }
    writeCommand(movePort, movePortPath, 'Movement', cmd, spd);
  });

  // --- Drill commands (motor0, sent to drill serial port) ---
  socket.on('drill', (data) => {
    // data: { direction: 'forward'|'reverse'|'stop', speed: 0-65535 }
    const spd = Math.max(0, Math.min(65535, parseInt(data.speed) || 30000));
    let cmd;
    if (data.direction === 'forward') {
      cmd = 5;  // motor0 forward
    } else if (data.direction === 'reverse') {
      cmd = 6;  // motor0 backward
    } else {
      cmd = 7;  // motor0 stop
    }
    writeCommand(drillPort, drillPortPath, 'Drill', cmd, spd);
  });

  // --- Sample collection commands (motor4, sent to drill serial port) ---
  socket.on('sample', (data) => {
    // data: { direction: 'extend'|'retract'|'stop', speed: 0-65535 }
    const spd = Math.max(0, Math.min(65535, parseInt(data.speed) || 30000));
    let cmd;
    if (data.direction === 'extend') {
      cmd = 17; // motor4 forward
    } else if (data.direction === 'retract') {
      cmd = 18; // motor4 backward
    } else {
      cmd = 19; // motor4 stop
    }
    writeCommand(drillPort, drillPortPath, 'Drill', cmd, spd);
  });

  // --- Emergency stop (stops everything on both ports) ---
  socket.on('emergency-stop', () => {
    console.log('[Emergency] EMERGENCY STOP triggered!');
    writeCommand(movePort, movePortPath, 'Movement', 0, 0);
    writeCommand(drillPort, drillPortPath, 'Drill', 7, 0);
    writeCommand(drillPort, drillPortPath, 'Drill-Sample', 19, 0);
  });

  // --- Reconfigure serial ports ---
  socket.on('set-config', (config) => {
    if (config.movePort) movePortPath = config.movePort;
    if (config.drillPort) drillPortPath = config.drillPort;
    if (config.moveBaud) moveBaudRate = parseInt(config.moveBaud);
    if (config.drillBaud) drillBaudRate = parseInt(config.drillBaud);
    console.log(`[Config] Updated. Reconnecting ports...`);
    openAllPorts();
    socket.emit('config', {
      movePort: movePortPath,
      drillPort: drillPortPath,
      moveBaud: moveBaudRate,
      drillBaud: drillBaudRate
    });
  });

  // --- Request port refresh ---
  socket.on('refresh-ports', () => {
    openAllPorts();
    socket.emit('port-status', {
      label: 'Movement',
      connected: movePort && movePort.isOpen,
      path: movePortPath,
      error: null
    });
    socket.emit('port-status', {
      label: 'Drill',
      connected: drillPort && drillPort.isOpen,
      path: drillPortPath,
      error: null
    });
  });

  socket.on('disconnect', () => {
    console.log(`Client disconnected: ${socket.id}`);
  });
});

// --- Start server ---
server.listen(PORT, () => {
  console.log(`=== Rover Web Controller ===`);
  console.log(`Server running at http://localhost:${PORT}`);
  console.log(`Movement serial port: ${movePortPath} @ ${moveBaudRate} baud`);
  console.log(`Drill serial port:    ${drillPortPath} @ ${drillBaudRate} baud`);
  console.log(`=============================`);
});
