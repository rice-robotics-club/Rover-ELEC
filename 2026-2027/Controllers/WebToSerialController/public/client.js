// === WebSocket Connection ===
const socket = io();

// === State ===
let currentSpeed = 30000;
let activeMoveCommand = null; // Track which movement button is held

// === DOM References ===
const $speedSlider = document.getElementById('speed-slider');
const $speedValue = document.getElementById('speed-value');
const $debugLog = document.getElementById('debug-log');
const $moveStatus = document.getElementById('move-status');
const $drillStatus = document.getElementById('drill-status');
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

  // Keep log from growing unbounded
  while ($debugLog.children.length > 500) {
    $debugLog.removeChild($debugLog.firstChild);
  }
}

// === Send command helpers ===
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

// === Hold-to-move pattern (mouse + touch) ===
function setupHoldButton(button, onPress, onRelease) {
  let intervalId = null;

  const start = (e) => {
    e.preventDefault();
    button.classList.add('pressed');
    if (onPress) onPress();
    // Don't repeat — single fire; release sends stop
  };

  const end = (e) => {
    e.preventDefault();
    button.classList.remove('pressed');
    if (intervalId) clearInterval(intervalId);
    intervalId = null;
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
    () => { sendMove(command); activeMoveCommand = command; },
    () => { sendMove(0); activeMoveCommand = null; }
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

// Stop button (in D-pad) — one-shot
document.getElementById('btn-stop').addEventListener('click', () => {
  sendMove(0);
  activeMoveCommand = null;
  document.querySelectorAll('.move-btn.pressed').forEach(b => b.classList.remove('pressed'));
});

// Emergency stop
document.getElementById('btn-emergency').addEventListener('click', () => {
  socket.emit('emergency-stop');
  activeMoveCommand = null;
  document.querySelectorAll('.pressed').forEach(b => b.classList.remove('pressed'));
  addLogEntry('🛑 EMERGENCY STOP — all motors halted', 'error');
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
    moveBaud: parseInt(document.getElementById('move-port-baud').value),
    drillBaud: parseInt(document.getElementById('drill-port-baud').value)
  };
  socket.emit('set-config', config);
  addLogEntry(`Config updated: Move=${config.movePort}:${config.moveBaud}, Drill=${config.drillPort}:${config.drillBaud}`, 'system');
});

// Refresh ports
document.getElementById('btn-refresh-ports').addEventListener('click', () => {
  socket.emit('refresh-ports');
  addLogEntry('Port refresh requested', 'system');
});

// === Keyboard Controls ===
const keyState = {};

window.addEventListener('keydown', (e) => {
  // Ignore if user is typing in an input
  if (e.target.tagName === 'INPUT') return;

  if (keyState[e.key]) return; // Prevent repeat fire
  keyState[e.key] = true;

  switch (e.key) {
    case 'ArrowUp': case 'w': case 'W':
      e.preventDefault();
      sendMove(1);
      document.getElementById('btn-forward').classList.add('pressed');
      break;
    case 'ArrowDown': case 's': case 'S':
      e.preventDefault();
      sendMove(2);
      document.getElementById('btn-backward').classList.add('pressed');
      break;
    case 'ArrowLeft': case 'a': case 'A':
      e.preventDefault();
      sendMove(4);
      document.getElementById('btn-left').classList.add('pressed');
      break;
    case 'ArrowRight': case 'd': case 'D':
      e.preventDefault();
      sendMove(3);
      document.getElementById('btn-right').classList.add('pressed');
      break;
    case ' ': case 'Escape':
      e.preventDefault();
      socket.emit('emergency-stop');
      document.querySelectorAll('.pressed').forEach(b => b.classList.remove('pressed'));
      addLogEntry('🛑 KEYBOARD EMERGENCY STOP', 'error');
      break;
    case 'q': case 'Q':
      e.preventDefault();
      sendDrill('forward');
      document.getElementById('btn-drill-fwd').classList.add('pressed');
      break;
    case 'e': case 'E':
      e.preventDefault();
      sendDrill('reverse');
      document.getElementById('btn-drill-rev').classList.add('pressed');
      break;
    case 'z': case 'Z':
      e.preventDefault();
      sendDrill('stop');
      break;
    case 'r': case 'R':
      e.preventDefault();
      sendSample('extend');
      document.getElementById('btn-sample-extend').classList.add('pressed');
      break;
    case 'f': case 'F':
      e.preventDefault();
      sendSample('retract');
      document.getElementById('btn-sample-retract').classList.add('pressed');
      break;
    case 'v': case 'V':
      e.preventDefault();
      sendSample('stop');
      break;
  }
});

window.addEventListener('keyup', (e) => {
  keyState[e.key] = false;

  switch (e.key) {
    case 'ArrowUp': case 'w': case 'W':
      sendMove(0);
      document.getElementById('btn-forward').classList.remove('pressed');
      break;
    case 'ArrowDown': case 's': case 'S':
      sendMove(0);
      document.getElementById('btn-backward').classList.remove('pressed');
      break;
    case 'ArrowLeft': case 'a': case 'A':
      sendMove(0);
      document.getElementById('btn-left').classList.remove('pressed');
      break;
    case 'ArrowRight': case 'd': case 'D':
      sendMove(0);
      document.getElementById('btn-right').classList.remove('pressed');
      break;
    case 'q': case 'Q':
      sendDrill('stop');
      document.getElementById('btn-drill-fwd').classList.remove('pressed');
      break;
    case 'e': case 'E':
      sendDrill('stop');
      document.getElementById('btn-drill-rev').classList.remove('pressed');
      break;
    case 'r': case 'R':
      sendSample('stop');
      document.getElementById('btn-sample-extend').classList.remove('pressed');
      break;
    case 'f': case 'F':
      sendSample('stop');
      document.getElementById('btn-sample-retract').classList.remove('pressed');
      break;
  }
});

// === Socket.IO Event Handlers ===
socket.on('connect', () => {
  $wsStatus.textContent = 'WS: Connected';
  $wsStatus.className = 'status-badge connected';
  addLogEntry('WebSocket connected to server', 'system');
});

socket.on('disconnect', () => {
  $wsStatus.textContent = 'WS: Disconnected';
  $wsStatus.className = 'status-badge disconnected';
  addLogEntry('WebSocket disconnected', 'error');
});

socket.on('serial-data', (msg) => {
  const portLabel = msg.port === 'Movement' ? 'MOV' : 'DRL';
  const className = msg.isError ? 'error' : (msg.isAck ? 'ack' : 'system');
  addLogEntry(`[${portLabel}] ${msg.data}`, className);
});

socket.on('port-status', (status) => {
  const badge = status.label === 'Movement' ? $moveStatus : $drillStatus;
  const label = status.label === 'Movement' ? 'MOV' : 'DRL';
  if (status.connected) {
    badge.textContent = `${label}: ${status.path.split('/').pop()}`;
    badge.className = 'status-badge connected';
  } else {
    badge.textContent = `${label}: Disconnected`;
    badge.className = 'status-badge disconnected';
    if (status.error) {
      addLogEntry(`[${label}] ${status.error}`, 'error');
    }
  }
});

socket.on('config', (config) => {
  document.getElementById('move-port-path').value = config.movePort;
  document.getElementById('drill-port-path').value = config.drillPort;
  document.getElementById('move-port-baud').value = config.moveBaud;
  document.getElementById('drill-port-baud').value = config.drillBaud;
});
