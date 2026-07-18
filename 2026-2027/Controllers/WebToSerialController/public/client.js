// === WebSocket Connection ===
const socket = io();

// === State ===
let currentSpeed = 30000;
let servoTorque = { 1: true, 2: true }; // Track torque state
let servoPos = { 1: 180, 2: 180 };      // Track current position

// === DOM References ===
const $speedSlider = document.getElementById('speed-slider');
const $speedValue = document.getElementById('speed-value');
const $debugLog = document.getElementById('debug-log');
const $moveStatus = document.getElementById('move-status');
const $drillStatus = document.getElementById('drill-status');
const $armStatus = document.getElementById('arm-status');
const $wsStatus = document.getElementById('ws-status');
const $settingsPanel = document.getElementById('settings-panel');

// === Speed Slider ===
$speedSlider.addEventListener('input', () => {
  currentSpeed = parseInt($speedSlider.value);
  $speedValue.textContent = currentSpeed;
});

// === Logging ===
function addLogEntry(text, className = 'system') {
  const entry = document.createElement('div');
  entry.className = `log-entry ${className}`;
  const ts = new Date().toLocaleTimeString();
  entry.innerHTML = `<span class="timestamp">[${ts}]</span>${text}`;
  $debugLog.appendChild(entry);
  $debugLog.scrollTop = $debugLog.scrollHeight;
  while ($debugLog.children.length > 500) {
    $debugLog.removeChild($debugLog.firstChild);
  }
}

// === Send helpers ===
function sendMove(command) {
  socket.emit('move', { command, speed: currentSpeed });
  const names = { 0: 'STOP', 1: 'FWD', 2: 'BACK', 3: 'RIGHT', 4: 'LEFT' };
  addLogEntry(`Move: ${names[command] || command} @ ${currentSpeed}`, 'system');
}

function sendDrill(direction) {
  socket.emit('drill', { direction, speed: currentSpeed });
  addLogEntry(`Drill: ${direction} @ ${currentSpeed}`, 'system');
}

function sendSample(direction) {
  socket.emit('sample', { direction, speed: currentSpeed });
  addLogEntry(`Sample: ${direction} @ ${currentSpeed}`, 'system');
}

function sendArmPosition(servoId, degrees) {
  socket.emit('arm-position', { servoId, degrees });
  addLogEntry(`Arm: Servo ${servoId} → ${degrees}°`, 'system');
}

function sendArmTorque(servoId, enable) {
  socket.emit('arm-torque', { servoId, enable });
  addLogEntry(`Arm: Servo ${servoId} torque ${enable ? 'ON' : 'OFF'}`, 'system');
}

// === Hold-to-move (mouse + touch) ===
function setupHoldButton(button, onPress, onRelease) {
  const start = (e) => {
    e.preventDefault();
    button.classList.add('pressed');
    if (onPress) onPress();
  };
  const end = (e) => {
    e.preventDefault();
    button.classList.remove('pressed');
    if (onRelease) onRelease();
  };
  button.addEventListener('mousedown', start);
  button.addEventListener('mouseup', end);
  button.addEventListener('mouseleave', end);
  button.addEventListener('touchstart', start, { passive: false });
  button.addEventListener('touchend', end);
  button.addEventListener('touchcancel', end);
}

// Movement buttons: hold sends move, release sends stop
document.querySelectorAll('.move-btn').forEach(btn => {
  const command = parseInt(btn.dataset.command);
  setupHoldButton(btn,
    () => { sendMove(command); },
    () => { sendMove(0); }
  );
});

// Drill buttons
document.getElementById('btn-drill-fwd').addEventListener('click', () => sendDrill('forward'));
document.getElementById('btn-drill-rev').addEventListener('click', () => sendDrill('reverse'));
document.getElementById('btn-drill-stop').addEventListener('click', () => sendDrill('stop'));

// Sample buttons
document.getElementById('btn-sample-extend').addEventListener('click', () => sendSample('extend'));
document.getElementById('btn-sample-retract').addEventListener('click', () => sendSample('retract'));
document.getElementById('btn-sample-stop').addEventListener('click', () => sendSample('stop'));

// Stop button in D-pad
document.getElementById('btn-stop').addEventListener('click', () => {
  sendMove(0);
  document.querySelectorAll('.move-btn.pressed').forEach(b => b.classList.remove('pressed'));
});

// === Arm Servo Sliders (send on release to avoid flooding serial) ===
function setupServoSlider(sliderId, valId, servoId) {
  const slider = document.getElementById(sliderId);
  const valSpan = document.getElementById(valId);

  slider.addEventListener('input', () => {
    valSpan.textContent = slider.value + '°';
  });

  // Send on mouse/touch release
  const send = () => {
    const deg = parseInt(slider.value);
    servoPos[servoId] = deg;
    sendArmPosition(servoId, deg);
  };
  slider.addEventListener('change', send);      // mouse release
  slider.addEventListener('touchend', send);     // touch release
}

setupServoSlider('servo1-slider', 'servo1-val', 1);
setupServoSlider('servo2-slider', 'servo2-val', 2);

// === Arm Torque Toggles ===
function setupTorqueButton(btnId, servoId) {
  const btn = document.getElementById(btnId);
  btn.addEventListener('click', () => {
    servoTorque[servoId] = !servoTorque[servoId];
    sendArmTorque(servoId, servoTorque[servoId]);
    btn.textContent = servoTorque[servoId] ? 'Torque' : 'Off';
    btn.className = 'torque-btn ' + (servoTorque[servoId] ? 'on' : 'off');
  });
}
setupTorqueButton('btn-servo1-torque', 1);
setupTorqueButton('btn-servo2-torque', 2);

// === Arm Home Buttons ===
document.querySelectorAll('.home-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const servoId = parseInt(btn.dataset.servo);
    const deg = parseInt(btn.dataset.deg);
    const slider = document.getElementById('servo' + servoId + '-slider');
    const valSpan = document.getElementById('servo' + servoId + '-val');
    slider.value = deg;
    valSpan.textContent = deg + '°';
    servoPos[servoId] = deg;
    sendArmPosition(servoId, deg);
  });
});

// === Arm Preset Buttons ===
document.querySelectorAll('.preset-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const s1 = parseInt(btn.dataset.s1);
    const s2 = parseInt(btn.dataset.s2);
    [ [1, s1, 'servo1-slider', 'servo1-val'],
      [2, s2, 'servo2-slider', 'servo2-val'] ].forEach(([id, deg, sliderId, valId]) => {
      document.getElementById(sliderId).value = deg;
      document.getElementById(valId).textContent = deg + '°';
      servoPos[id] = deg;
      sendArmPosition(id, deg);
    });
  });
});

// === Emergency Stop ===
document.getElementById('btn-emergency').addEventListener('click', () => {
  socket.emit('emergency-stop');
  document.querySelectorAll('.pressed').forEach(b => b.classList.remove('pressed'));
  addLogEntry('🛑 EMERGENCY STOP — all halted', 'error');
});

// Clear log
document.getElementById('btn-clear-log').addEventListener('click', () => {
  $debugLog.innerHTML = '<div class="log-entry system">Log cleared.</div>';
});

// Toggle settings
document.getElementById('btn-toggle-settings').addEventListener('click', () => {
  $settingsPanel.classList.toggle('hidden');
});

// Apply settings
document.getElementById('btn-apply-settings').addEventListener('click', () => {
  const config = {
    movePort: document.getElementById('move-port-path').value,
    drillPort: document.getElementById('drill-port-path').value,
    armPort: document.getElementById('arm-port-path').value,
    moveBaud: parseInt(document.getElementById('move-port-baud').value),
    drillBaud: parseInt(document.getElementById('drill-port-baud').value),
    armBaud: parseInt(document.getElementById('arm-port-baud').value)
  };
  socket.emit('set-config', config);
  addLogEntry('Config updated & reconnecting...', 'system');
});

// Refresh ports
document.getElementById('btn-refresh-ports').addEventListener('click', () => {
  socket.emit('refresh-ports');
  addLogEntry('Port refresh requested', 'system');
});

// === Keyboard Controls ===
const keyState = {};
window.addEventListener('keydown', (e) => {
  if (e.target.tagName === 'INPUT') return;
  if (keyState[e.key]) return;
  keyState[e.key] = true;

  switch (e.key) {
    case 'ArrowUp': case 'w': case 'W':
      e.preventDefault(); sendMove(1);
      document.getElementById('btn-forward').classList.add('pressed'); break;
    case 'ArrowDown': case 's': case 'S':
      e.preventDefault(); sendMove(2);
      document.getElementById('btn-backward').classList.add('pressed'); break;
    case 'ArrowLeft': case 'a': case 'A':
      e.preventDefault(); sendMove(4);
      document.getElementById('btn-left').classList.add('pressed'); break;
    case 'ArrowRight': case 'd': case 'D':
      e.preventDefault(); sendMove(3);
      document.getElementById('btn-right').classList.add('pressed'); break;
    case ' ': case 'Escape':
      e.preventDefault(); socket.emit('emergency-stop');
      document.querySelectorAll('.pressed').forEach(b => b.classList.remove('pressed'));
      addLogEntry('🛑 KEYBOARD E-STOP', 'error'); break;
    case 'q': case 'Q':
      e.preventDefault(); sendDrill('forward');
      document.getElementById('btn-drill-fwd').classList.add('pressed'); break;
    case 'e': case 'E':
      e.preventDefault(); sendDrill('reverse');
      document.getElementById('btn-drill-rev').classList.add('pressed'); break;
    case 'z': case 'Z':
      e.preventDefault(); sendDrill('stop'); break;
    case 'r': case 'R':
      e.preventDefault(); sendSample('extend');
      document.getElementById('btn-sample-extend').classList.add('pressed'); break;
    case 'f': case 'F':
      e.preventDefault(); sendSample('retract');
      document.getElementById('btn-sample-retract').classList.add('pressed'); break;
    case 'v': case 'V':
      e.preventDefault(); sendSample('stop'); break;
    case '1':
      e.preventDefault();
      servoPos[1] = Math.max(0, servoPos[1] - 10);
      document.getElementById('servo1-slider').value = servoPos[1];
      document.getElementById('servo1-val').textContent = servoPos[1] + '°';
      sendArmPosition(1, servoPos[1]); break;
    case '2':
      e.preventDefault();
      servoPos[1] = Math.min(360, servoPos[1] + 10);
      document.getElementById('servo1-slider').value = servoPos[1];
      document.getElementById('servo1-val').textContent = servoPos[1] + '°';
      sendArmPosition(1, servoPos[1]); break;
    case '3':
      e.preventDefault();
      servoPos[2] = Math.max(0, servoPos[2] - 10);
      document.getElementById('servo2-slider').value = servoPos[2];
      document.getElementById('servo2-val').textContent = servoPos[2] + '°';
      sendArmPosition(2, servoPos[2]); break;
    case '4':
      e.preventDefault();
      servoPos[2] = Math.min(360, servoPos[2] + 10);
      document.getElementById('servo2-slider').value = servoPos[2];
      document.getElementById('servo2-val').textContent = servoPos[2] + '°';
      sendArmPosition(2, servoPos[2]); break;
  }
});

window.addEventListener('keyup', (e) => {
  keyState[e.key] = false;
  switch (e.key) {
    case 'ArrowUp': case 'w': case 'W':
      sendMove(0);
      document.getElementById('btn-forward').classList.remove('pressed'); break;
    case 'ArrowDown': case 's': case 'S':
      sendMove(0);
      document.getElementById('btn-backward').classList.remove('pressed'); break;
    case 'ArrowLeft': case 'a': case 'A':
      sendMove(0);
      document.getElementById('btn-left').classList.remove('pressed'); break;
    case 'ArrowRight': case 'd': case 'D':
      sendMove(0);
      document.getElementById('btn-right').classList.remove('pressed'); break;
    case 'q': case 'Q':
      sendDrill('stop');
      document.getElementById('btn-drill-fwd').classList.remove('pressed'); break;
    case 'e': case 'E':
      sendDrill('stop');
      document.getElementById('btn-drill-rev').classList.remove('pressed'); break;
    case 'r': case 'R':
      sendSample('stop');
      document.getElementById('btn-sample-extend').classList.remove('pressed'); break;
    case 'f': case 'F':
      sendSample('stop');
      document.getElementById('btn-sample-retract').classList.remove('pressed'); break;
  }
});

// === Socket.IO Events ===
socket.on('connect', () => {
  $wsStatus.textContent = 'WS ✓';
  $wsStatus.className = 'status-badge connected';
  addLogEntry('Connected to server', 'system');
});

socket.on('disconnect', () => {
  $wsStatus.textContent = 'WS ✗';
  $wsStatus.className = 'status-badge disconnected';
  addLogEntry('WebSocket disconnected', 'error');
});

socket.on('serial-data', (msg) => {
  const labels = { Movement: 'MOV', Drill: 'DRL', Arm: 'ARM' };
  const portLabel = labels[msg.port] || msg.port;
  const className = msg.isError ? 'error' : (msg.isAck ? 'ack' : 'system');
  addLogEntry(`[${portLabel}] ${msg.data}`, className);
});

socket.on('port-status', (status) => {
  const badgeMap = {
    Movement: $moveStatus,
    Drill: $drillStatus,
    Arm: $armStatus
  };
  const shortLabel = { Movement: 'MOV', Drill: 'DRL', Arm: 'ARM' };
  const badge = badgeMap[status.label];
  const label = shortLabel[status.label] || status.label;
  if (!badge) return;

  if (status.connected) {
    badge.textContent = label + ' ✓';
    badge.className = 'status-badge connected';
  } else {
    badge.textContent = label + ' ✗';
    badge.className = 'status-badge disconnected';
    if (status.error) {
      addLogEntry(`[${label}] ${status.error}`, 'error');
    }
  }
});

socket.on('config', (config) => {
  document.getElementById('move-port-path').value = config.movePort || '';
  document.getElementById('drill-port-path').value = config.drillPort || '';
  document.getElementById('arm-port-path').value = config.armPort || '';
  document.getElementById('move-port-baud').value = config.moveBaud || 115200;
  document.getElementById('drill-port-baud').value = config.drillBaud || 115200;
  document.getElementById('arm-port-baud').value = config.armBaud || 57600;
});
