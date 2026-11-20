const state = {
  bank: 0,
  preset: 0,
  presetName: 'Glass Horizon',
  connected: false,
  ble: false,
  usbDev: false,
  bridgeConnected: false,
  stale: false,
  presetCertainty: 'unknown',
  simulation: false,
  profile: 'pocket-master',
  pedalId: 'unknown',
  revision: 0,
  deviceHosted: false,
  effects: { drive: true, delay: false, reverb: true, tuner: false },
  effectCertainty: { drive: 'unknown', delay: 'unknown', reverb: 'unknown', tuner: 'unknown' },
  effectNames: { drive: 'Warm boost', delay: 'Tape echo', reverb: 'Wide hall', tuner: 'Utility' },
  bankPatchNames: [],
  visiblePresets: [],
  bleBlocks: [],
  presetCount: 0,
  bleSelectedBank: null,
  pedalPhase: 'unknown',
  syncPhase: 'unknown',
  syncResult: 'unknown',
};

const demoNames = [
  'Glass Horizon', 'Copper Room', 'Night Signal', 'Paper Moon',
  'Slow Current', 'Neon Static', 'Open Water', 'Redline',
  'Afterimage', 'Satellite', 'Dust & Chrome', 'Low Orbit',
];

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];
const localSerialAvailable = ['localhost', '127.0.0.1', '[::1]'].includes(location.hostname) &&
  window.isSecureContext && !!navigator.serial;
const usbDevButton = $('#connect-usb-dev');
if (!localSerialAvailable) usbDevButton.hidden = true;
else {
  usbDevButton.hidden = true;
  const serialScript = document.createElement('script');
  serialScript.src = 'serial_transport.js?v=6';
  serialScript.onload = () => { usbDevButton.hidden = false; };
  serialScript.onerror = () => log('USB dev unavailable', 'Could not load the local serial transport script.');
  document.head.append(serialScript);
}

function presetLabel(index) {
  if (state.ble) {
    const visible = state.visiblePresets.find((preset) => preset.index === index);
    return visible?.label || `#${index}`;
  }
  if (state.profile === 'gp5') return `P${String(index + 1).padStart(2, '0')}`;
  return index < 50
    ? `P${String(index + 1).padStart(2, '0')}`
    : `F${String(index - 49).padStart(2, '0')}`;
}

function pedalName(id = state.pedalId) {
  if (id === 'valeton_gp5') return 'Valeton GP-5';
  if (id === 'pocket_master') return 'Sonicake Pocket Master';
  return 'No pedal identified';
}

function timestamp() {
  return new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function log(message, detail = '') {
  const item = document.createElement('li');
  const time = document.createElement('time');
  time.textContent = timestamp();
  const body = document.createElement('span');
  const title = document.createElement('b');
  title.textContent = message;
  body.append(title);
  if (detail) body.append(document.createElement('br'), detail);
  item.append(time, body);
  $('#event-log').prepend(item);
  while ($('#event-log').children.length > 8) $('#event-log').lastElementChild.remove();
  $('#log-count').textContent = $('#event-log').children.length;
}

function cardName(index, slot) {
  if (!state.deviceHosted && !state.simulation && !state.ble) return '—';
  return state.ble
    ? (state.visiblePresets.find((preset) => preset.index === index)?.name ||
      (state.preset === index ? state.presetName : '—'))
    : state.deviceHosted ? state.bankPatchNames[slot] || `Patch ${'ABCD'[slot]}`
      : demoNames[index % demoNames.length];
}

function renderPresets() {
  const container = $('#presets');
  container.innerHTML = '';
  for (let slot = 0; slot < 4; slot += 1) {
    const index = state.bank * 4 + slot;
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `preset${(state.deviceHosted || state.simulation || state.ble) && index === state.preset ? ' active' : ''}`;
    button.dataset.index = index;
    button.disabled = !state.connected;
    button.innerHTML = `
      <span class="slot">${'ABCD'[slot]}</span>
      <span><strong></strong><small></small></span>`;
    button.querySelector('strong').textContent = cardName(index, slot);
    button.querySelector('small').textContent = presetLabel(index);
    button.addEventListener('click', () => selectPreset(index));
    container.append(button);
  }
}

function renderConnection() {
  $('#connection').classList.toggle('offline', !state.bridgeConnected);
  $('#connection').setAttribute('aria-pressed', state.bridgeConnected);
  $('#connection-label').textContent = state.simulation
    ? 'SIMULATION' : state.usbDev ? 'DISCONNECT USB' : state.ble ? 'DISCONNECT BLE' : state.deviceHosted
      ? (state.bridgeConnected ? 'BRIDGE ONLINE' : 'BRIDGE OFFLINE')
      : state.bridgeConnected ? 'BRIDGE ONLINE' : 'CONNECT BLE';
  const pedalLink = $('#pedal-link-status');
  const pedalPhase = state.simulation ? 'simulated' : state.ble
    ? `${state.pedalPhase}${state.stale ? ' stale' : ''}`
    : state.deviceHosted ? (state.connected ? 'online' : 'offline') : 'unknown';
  pedalLink.textContent = `PEDAL ${pedalPhase.toUpperCase()}`;
  pedalLink.className = `certainty ${state.connected ? 'confirmed' :
    state.ble && state.pedalPhase === 'degraded' ? 'assumed' : 'unknown'}`;
  const pedalStatus = state.simulation ? 'SIMULATED' : state.ble
    ? `${state.pedalPhase.toUpperCase()}${state.stale ? ' · STALE' : ''}`
    : state.connected ? 'ONLINE' : 'OFFLINE';
  $('#midi-status').textContent = pedalStatus;
  $('#midi-status').style.color = state.connected || state.simulation ? 'var(--lime)' : 'var(--muted)';
  $('#sync-status').textContent = state.ble
    ? `${state.syncPhase.toUpperCase()}${state.stale ? ' · STALE' : ''} · ${state.syncResult}`
    : state.simulation ? 'SIMULATED' : 'UNKNOWN';
  $('#sync-status').style.color = state.ble && state.syncPhase === 'confirmed'
    ? 'var(--lime)' : 'var(--muted)';
  $('#stale-indicator').hidden = !state.stale;
}

function render() {
  const liveView = state.deviceHosted || state.simulation || state.ble;
  $('#preset-number').textContent = liveView ? presetLabel(state.preset) : '—';
  $('#preset-name').textContent = liveView ? state.presetName : 'Connect to a bridge';
  const certainty = $('#preset-certainty');
  certainty.textContent = state.simulation ? 'SIMULATED' : (state.deviceHosted || state.ble)
    ? state.presetCertainty.toUpperCase() : 'UNKNOWN';
  certainty.className = `certainty ${state.simulation ? 'assumed' : state.presetCertainty}`;
  $('#bank-number').textContent = liveView ? String(state.bank + 1).padStart(2, '0') : '—';
  $('#bank-range').textContent = liveView
    ? `${presetLabel(state.bank * 4)} — ${presetLabel(state.bank * 4 + 3)}` : '—';
  $$('.effect').forEach((button) => {
    const effect = button.dataset.effect;
    const active = state.effects[effect] && (state.simulation ||
      ((state.deviceHosted || state.ble) && state.effectCertainty[effect] !== 'unknown'));
    button.classList.toggle('active', active);
    button.setAttribute('aria-pressed', active);
    button.disabled = !state.connected;
    button.querySelector('small').textContent = state.effectNames[button.dataset.effect];
  });
  $('#bank-down').disabled = !state.connected;
  $('#bank-up').disabled = !state.connected;
  renderConnection();
  renderPresets();
}

async function sendAction(action) {
  if (state.ble) return sendBleAction(action);
  if (!state.deviceHosted) return true;
  try {
    const response = await fetch('/api/v1/action', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action, request_id: String(Date.now()) }),
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    window.setTimeout(syncState, 180);
    return true;
  } catch (error) {
    log('Command rejected', error.message);
    window.setTimeout(syncState, 100);
    return false;
  }
}

function selectPreset(index) {
  if (!state.connected) return log('Command blocked', 'Connect the device or explicitly start the simulator.');
  if (state.ble) {
    if (index >= state.presetCount) { log('Command blocked', 'Preset is outside the pedal’s advertised range.'); return; }
    state.bleSelectedBank = null;
    sendBleCommand({ kind: 'setPreset', index });
    return;
  }
  if (state.simulation) {
    state.preset = index;
    state.bank = Math.floor(index / 4);
    state.presetName = `${state.profile === 'gp5' ? 'GP-5' : 'Pocket Master'} ${presetLabel(index)}`;
    render();
  }
  if (state.deviceHosted) sendAction(`preset_${'abcd'[index % 4]}`);
  log(`Selected ${presetLabel(index)}`, state.simulation ? 'Simulation result' : 'Request sent; awaiting device state');
}

function changeBank(direction) {
  if (!state.connected) return log('Command blocked', 'Connect the device or explicitly start the simulator.');
  const maxBank = state.ble ? Math.max(0, Math.ceil(state.presetCount / 4) - 1) : state.profile === 'gp5' ? 24 : 25;
  if (state.ble) {
    state.bleSelectedBank = Math.max(0, Math.min(maxBank, state.bank + direction));
    state.bank = state.bleSelectedBank;
    render();
    log(`Bank ${String(state.bank + 1).padStart(2, '0')} selected`, 'Choose preset A–D to send an absolute preset command.');
    return;
  }
  if (state.simulation) {
    state.bank = Math.max(0, Math.min(maxBank, state.bank + direction));
    render();
  }
  if (state.deviceHosted) sendAction(direction < 0 ? 'bank_down' : 'bank_up');
  log(`Bank ${String(state.bank + 1).padStart(2, '0')}`, 'Ready — choose preset A, B, C or D');
}

function toggleEffect(name) {
  if (!state.connected) return log('Command blocked', 'Connect the device or explicitly start the simulator.');
  if (state.ble) {
    const enabled = state.effects[name];
    if (state.effectCertainty[name] === 'unknown') { log('Command blocked', `${name} state is unknown; refresh bridge state first.`); return; }
    if (name === 'tuner') sendBleCommand({ kind: 'setTuner', enabled: !enabled });
    else {
      const block = state.bleBlocks.find((item) => item.uiName === name);
      if (!block || !block.set) { log('Command blocked', `${name} is not writable on this pedal.`); return; }
      sendBleCommand({ kind: 'setBlock', id: block.id, enabled: !enabled });
    }
    return;
  }
  if (state.simulation) {
    state.effects[name] = !state.effects[name];
    render();
  }
  if (state.deviceHosted) sendAction(`toggle_${name}`);
  const on = state.effects[name];
  if (name === 'tuner') {
    $('#tuner-overlay').hidden = !on;
    log(`Tuner request ${on ? 'on' : 'off'}`, state.simulation ? 'Simulation result' : 'Request sent; awaiting device state');
  } else {
    log(`${name.toUpperCase()} ${on ? 'on' : 'off'}`, state.simulation ? 'Simulation result' : 'Request sent; awaiting device state');
  }
}

function applySnapshot(snapshot) {
  if (!snapshot || typeof snapshot.revision !== 'number') return false;
  state.revision = snapshot.revision;
  state.deviceHosted = true;
  state.simulation = false;
  state.bridgeConnected = true;
  state.stale = false;
  $('#start-simulation').disabled = true;
  state.profile = snapshot.device === 'valeton_gp5' ? 'gp5' : 'pocket-master';
  state.connected = snapshot.connection === 'connected';
  state.presetCertainty = snapshot.preset?.certainty || 'unknown';
  state.bank = Number.isInteger(snapshot.bank) ? snapshot.bank : state.bank;
  if (snapshot.preset) {
    state.preset = snapshot.preset.absolute;
    state.presetName = snapshot.preset.name || `${state.profile === 'gp5' ? 'GP-5' : 'Pocket Master'} ${snapshot.preset.label}`;
  }
  state.bankPatchNames = (snapshot.bank_patches || []).map((patch) => patch.name || patch.label);
  for (const block of snapshot.blocks || []) {
    if (Object.prototype.hasOwnProperty.call(state.effects, block.id) && typeof block.enabled === 'boolean') {
      state.effects[block.id] = block.enabled;
      state.effectCertainty[block.id] = block.certainty || 'unknown';
      state.effectNames[block.id] = block.effect_name || block.label;
    }
  }
  $('#device-profile').value = state.profile;
  $('#device-profile').disabled = true;
  render();
  return true;
}

async function syncState() {
  try {
    const response = await fetch('/api/v1/state', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return applySnapshot(await response.json());
  } catch (_) {
    if (state.deviceHosted) {
      state.bridgeConnected = false;
      state.connected = false;
      state.stale = true;
      render();
    }
    return false;
  }
}

let liveConnectionPending = false;
let bleClient = null;
let bleTransport = null;
let bleHelloResolve = null;
let bleRefreshTimer = 0;
let bleFreshnessTimer = 0;

function updatePendingStatus() {
  const indicator = $('#live-request-status');
  if (!indicator) return;
  const requests = bleClient?.pendingRequests;
  const count = requests?.size || 0;
  indicator.textContent = count ? `${count} PENDING` : 'NO PENDING';
  indicator.className = `certainty ${count ? 'assumed' : 'unknown'}`;
  const phases = requests ? [...requests.values()].map((request) => request.phase) : [];
  indicator.title = count ? phases.join(', ') : 'No command is awaiting a result';
}

function disconnectBle() {
  window.clearInterval(bleRefreshTimer);
  window.clearInterval(bleFreshnessTimer);
  const transport = bleTransport;
  bleTransport = null;
  bleClient = null;
  bleHelloResolve = null;
  updatePendingStatus();
  transport?.disconnect();
  state.ble = false;
  state.usbDev = false;
  state.bridgeConnected = false;
  state.connected = false;
  state.stale = true;
  state.bleSelectedBank = null;
  $('#start-simulation').disabled = false;
  $('#device-profile').disabled = false;
  render();
  log('Live UI disconnected', 'Live state is stale until the next connection.');
}

function bleDisconnected() {
  if (!bleClient) return;
  window.clearInterval(bleRefreshTimer);
  window.clearInterval(bleFreshnessTimer);
  const transport = bleTransport;
  bleClient = null;
  bleTransport = null;
  transport?.disconnect();
  bleHelloResolve = null;
  updatePendingStatus();
  state.ble = false;
  state.usbDev = false;
  state.bridgeConnected = false;
  state.connected = false;
  state.stale = true;
  state.bleSelectedBank = null;
  $('#start-simulation').disabled = false;
  $('#device-profile').disabled = false;
  render();
  log('Live UI link lost', 'No command was replayed. Reconnect and read a fresh snapshot.');
}

async function connectLive(Transport, transportName, usbDev = false) {
  if (liveConnectionPending) return;
  liveConnectionPending = true;
  $('#connection').disabled = true;
  $('#connect-usb-dev').disabled = true;
  $('#start-simulation').disabled = true;
  $('#connection-label').textContent = `CONNECTING ${usbDev ? 'USB' : 'BLE'}…`;
  try {
    bleTransport = new Transport({
      onMessage: handleBleMessage,
      onDisconnect: bleDisconnected,
      onDiagnostic: usbDev ? (detail) => {
        console.info('[StompBridge USB]', detail);
        log('USB board', detail);
      } : undefined,
      onFrameError: (reason) => recoverBleSnapshot(reason),
    });
    const protocol = await bleTransport.connect();
    const client = {
      device: bleTransport.device, epoch: protocol.epoch,
      generation: protocol.generation, request: 0, negotiated: false,
      lastSnapshotAt: 0, lastRecoveryAt: 0, pendingRequests: new Map(),
      maxCommand: protocol.maxCommand || 256,
    };
    bleClient = client;
    state.ble = true;
    state.usbDev = usbDev;
    state.pedalId = 'unknown';
    state.profile = 'unknown';
    state.deviceHosted = false;
    state.simulation = false;
    state.bridgeConnected = true;
    state.connected = false;
    state.stale = true;
    state.bleSelectedBank = null;
    $('#device-profile').disabled = true;
    $('#device-profile').value = 'unknown';
    $('#start-simulation').disabled = true;
    updatePendingStatus();
    render();
    log(`${transportName} bridge connected`, protocol.window
      ? 'Writable window open · USB pedal state pending.'
      : usbDev ? 'Local USB development link · pedal state pending.'
        : 'Bridge linked; physical authorization window is closed.');
    const hello = new Promise((resolve, reject) => {
      bleHelloResolve = resolve;
      window.setTimeout(() => reject(new Error(usbDev
        ? 'Protocol negotiation timed out. Check the USB serial connection.'
        : 'Protocol negotiation timed out. Check the physical authorization window.')), 5000);
    });
    await writeBleMessage(1, { v: 1, kind: 'hello', min: 1, max: 1, epoch: client.epoch });
    await hello;
    await sendBleCommand({ kind: 'snapshot' }, true);
    bleRefreshTimer = window.setInterval(() => sendBleCommand({ kind: 'snapshot' }, true), 8000);
    bleFreshnessTimer = window.setInterval(() => {
      if (!bleClient) return;
      const now = Date.now();
      if (bleTransport?.pendingFrame && now - bleTransport.pendingFrame.touched > 3000)
        recoverBleSnapshot('Notification fragments timed out.');
      let expired = false;
      for (const [request, pending] of bleClient.pendingRequests) {
        if (now - pending.startedAt <= 20000) continue;
        bleClient.pendingRequests.delete(request);
        expired = true;
        log(`Request ${request} outcome unknown`, 'No correlated result arrived; the command was not replayed.');
      }
      if (expired) {
        updatePendingStatus();
        state.stale = true;
        state.connected = false;
        render();
        sendBleCommand({ kind: 'snapshot' }, true);
      }
      if (bleClient.lastSnapshotAt && now - bleClient.lastSnapshotAt >= 12000) {
        state.stale = true;
        state.connected = false;
        render();
      }
    }, 1000);
  } catch (error) {
    if (bleClient) disconnectBle();
    else {
      bleTransport?.disconnect();
      bleTransport = null;
    }
    $('#start-simulation').disabled = false;
    $('#device-profile').disabled = false;
    const detail = usbDev && error?.name === 'NotFoundError'
      ? 'No serial port was selected. Choose the board’s USB Single Serial (CH340) port in the browser chooser; canceling leaves the UI disconnected.'
      : error?.message || String(error);
    log(`${transportName} connection failed`, detail);
    if ($('#activity-panel').hidden) $('#toggle-log').click();
  } finally {
    liveConnectionPending = false;
    $('#connection').disabled = false;
    $('#connect-usb-dev').disabled = false;
    renderConnection();
  }
}

function connectBle() {
  return connectLive(window.StompBleTransport, 'BLE');
}

async function connectUsbDev() {
  if (!localSerialAvailable) {
    log('USB dev unavailable', 'Use Chromium from http://localhost with Web Serial support.');
    return;
  }
  if (!window.StompSerialTransport) { log('USB dev unavailable', 'Serial transport script is still loading.'); return; }
  return connectLive(window.StompSerialTransport, 'USB dev', true);
}

function writeBleMessage(type, value) {
  if (!bleTransport) return Promise.reject(new Error('Live UI transport is disconnected.'));
  return bleTransport.write(type, value);
}

async function sendBleCommand(command, internal = false) {
  if (!bleClient || (!internal && !state.connected)) {
    log('Command blocked', 'Connect to a ready pedal before sending commands.');
    return;
  }
  if (command.kind === 'setBlock') {
    const capability = state.bleBlocks.find((block) => block.id === command.id);
    if (!capability?.set) { log('Command blocked', 'This pedal block is read-only.'); return; }
  }
  const client = bleClient;
  const request = ++client.request;
  const isSnapshot = command.kind === 'snapshot';
  if (!internal && state.usbDev)
    console.info('[StompBridge USB] command requested', { request, command });
  if (!isSnapshot) {
    client.pendingRequests.set(request, { phase: 'sending', startedAt: Date.now() });
    updatePendingStatus();
  }
  const message = command.kind === 'snapshot'
    ? { v: 1, kind: 'snapshot', request, epoch: bleClient.epoch, generation: bleClient.generation }
    : { v: 1, kind: 'command', request, epoch: bleClient.epoch,
        generation: bleClient.generation, command };
  try {
    await writeBleMessage(2, message);
    if (!internal && state.usbDev)
      console.info('[StompBridge USB] command frame write completed', { request });
    if (client.pendingRequests.has(request)) {
      client.pendingRequests.get(request).phase = 'awaiting admission';
      updatePendingStatus();
    }
    if (!internal) log('Command sent', `Request ${request} · waiting for bridge and pedal result.`);
  } catch (error) {
    client.pendingRequests.delete(request);
    updatePendingStatus();
    log('Command write failed', error?.message || String(error));
    if (bleClient) { state.stale = true; render(); }
  }
}

function recoverBleSnapshot(reason) {
  if (!bleClient?.negotiated) return;
  const now = Date.now();
  bleTransport?.resetInput();
  state.stale = true;
  state.connected = false;
  render();
  if (now - bleClient.lastRecoveryAt < 3000) {
    log('Snapshot recovery pending', reason);
    return;
  }
  bleClient.lastRecoveryAt = now;
  log('Refreshing live state', reason);
  sendBleCommand({ kind: 'snapshot' }, true);
}

function handleBleMessage(frameType, message) {
  if (message.v !== 1) { log('Live message rejected', 'Unsupported protocol version.'); return; }
  if (message.kind === 'helloResult') {
    if (frameType !== 4 || message.epoch !== bleClient?.epoch || message.selected !== 1) return;
    bleClient.negotiated = true;
    bleHelloResolve?.();
    bleHelloResolve = null;
    log(`${state.usbDev ? 'USB' : 'BLE'} protocol ready`, `Version 1 · UI epoch ${message.epoch}`);
    return;
  }
  if (message.kind === 'snapshot') {
    if (frameType !== 3 || message.epoch !== bleClient?.epoch) {
      recoverBleSnapshot('Snapshot epoch or frame type did not match.');
      return;
    }
    applyBleSnapshot(message);
    return;
  }
  if (message.kind === 'result') {
    if (frameType !== 4 || message.epoch !== bleClient?.epoch) {
      recoverBleSnapshot('Result epoch or frame type did not match.');
      return;
    }
    const terminal = message.admission !== 'accepted' || message.phase === 'transportComplete' ||
      message.phase === 'observed' || message.phase === 'failed';
    if (terminal) bleClient.pendingRequests.delete(message.request);
    else if (bleClient.pendingRequests.has(message.request)) bleClient.pendingRequests.get(message.request).phase = message.phase;
    updatePendingStatus();
    if (state.usbDev)
      console.info('[StompBridge USB] command result', {
        request: message.request, phase: message.phase,
        admission: message.admission, result: message.result,
      });
    log(`Request ${message.request}: ${message.phase}`, `Admission ${message.admission} · ${message.result}`);
    return;
  }
  if (message.kind === 'error') {
    if (message.code === 'resultGap') {
      recoverBleSnapshot(`${message.lost || 0} result(s) could not be delivered (oldest retained ${message.oldest ?? 'unknown'}).`);
    } else if (message.code === 'badFrame') {
      recoverBleSnapshot('Bridge rejected a frame; requesting a fresh snapshot.');
    } else log('Bridge rejected message', message.code || 'unknown error');
  }
}

function uiBlockName(label) {
  const text = String(label || '').toLowerCase();
  if (/drive|boost|distort|overdrive/.test(text)) return 'drive';
  if (/delay|echo/.test(text)) return 'delay';
  if (/reverb|hall|room/.test(text)) return 'reverb';
  return null;
}

function applyBleSnapshot(snapshot) {
  if (bleClient) bleClient.lastSnapshotAt = Date.now();
  if (snapshot.generation !== bleClient?.generation) {
    bleClient.generation = snapshot.generation;
    log('Pedal session changed', 'Reading state for the new pedal generation.');
  }
  state.ble = true;
  state.deviceHosted = false;
  state.simulation = false;
  state.bridgeConnected = snapshot.bridge === 'connected';
  state.connected = snapshot.pedal?.phase === 'ready';
  state.stale = false;
  state.revision = snapshot.revision;
  state.presetCount = snapshot.capabilities?.presetCount || 0;
  state.pedalId = snapshot.device || 'unknown';
  state.profile = state.pedalId === 'valeton_gp5' ? 'gp5'
    : state.pedalId === 'pocket_master' ? 'pocket-master' : 'unknown';
  $('#device-profile').value = state.profile;
  state.pedalPhase = snapshot.pedal?.phase || 'unknown';
  state.syncPhase = snapshot.pedal?.sync || 'unknown';
  state.syncResult = snapshot.pedal?.syncResult || 'unknown';
  state.presetCertainty = snapshot.preset?.certainty || 'unknown';
  state.preset = snapshot.preset?.index || 0;
  state.bank = state.bleSelectedBank ?? snapshot.bank ?? 0;
  state.presetName = snapshot.preset?.name || 'Unnamed preset';
  state.visiblePresets = snapshot.visiblePresets || [];
  const blocks = snapshot.capabilities?.blocks || [];
  state.bleBlocks = blocks.map((block) => ({ ...block, uiName: uiBlockName(block.label) }));
  for (const name of ['drive', 'delay', 'reverb']) {
    state.effects[name] = false;
    state.effectCertainty[name] = 'unknown';
  }
  for (const block of snapshot.blocks || []) {
    const capability = state.bleBlocks.find((item) => item.id === block.id);
    const name = capability?.uiName;
    if (!name) continue;
    state.effects[name] = !!block.enabled;
    state.effectCertainty[name] = block.certainty || 'unknown';
    state.effectNames[name] = block.effect_name || capability.label || name;
  }
  state.effects.tuner = !!snapshot.tuner?.enabled;
  state.effectCertainty.tuner = snapshot.tuner?.certainty || 'unknown';
  state.effectNames.tuner = snapshot.capabilities?.tuner ? 'Tuner' : 'Unavailable';
  if (!snapshot.capabilities?.tuner) state.effectCertainty.tuner = 'unknown';
  $('#tuner-overlay').hidden = !state.effects.tuner || state.effectCertainty.tuner === 'unknown';
  render();
  log('Live state received', `Revision ${snapshot.revision} · ${pedalName()} · pedal ${snapshot.pedal?.phase || 'unknown'} · sync ${snapshot.pedal?.sync || 'unknown'}`);
}

function sendBleAction(action) {
  log('Command blocked', `Live UI accepts absolute semantic commands; legacy action “${action}” is not sent.`);
  return false;
}

$$('.effect').forEach((button) => button.addEventListener('click', () => toggleEffect(button.dataset.effect)));
$('#bank-down').addEventListener('click', () => changeBank(-1));
$('#bank-up').addEventListener('click', () => changeBank(1));
$('#clear-log').addEventListener('click', () => {
  $('#event-log').innerHTML = '';
  $('#log-count').textContent = '0';
});
$('#toggle-log').addEventListener('click', () => {
  const panel = $('#activity-panel');
  const opening = panel.hidden;
  panel.hidden = !opening;
  $('#toggle-log').setAttribute('aria-expanded', opening);
  $('#toggle-log').classList.toggle('active', opening);
});
$('#close-tuner').addEventListener('click', () => toggleEffect('tuner'));

$('#connection').addEventListener('click', () => {
  if (state.ble) { disconnectBle(); return; }
  if (state.deviceHosted) {
    log('MIDI transport', state.connected ? 'Connected' : 'Waiting for pedal connection');
    return;
  }
  if (state.simulation) {
    state.connected = !state.connected;
    state.bridgeConnected = state.connected;
    renderConnection();
    log(state.connected ? 'Simulator connected' : 'Simulator disconnected', 'Explicit simulation mode');
    return;
  }
  connectBle();
});

$('#connect-usb-dev').addEventListener('click', () => {
  if (state.usbDev) { disconnectBle(); return; }
  if (state.ble || state.deviceHosted || state.simulation) {
    log('USB dev connection blocked', 'Disconnect the current mode before opening the USB development link.');
    return;
  }
  connectUsbDev();
});

$('#start-simulation').addEventListener('click', () => {
  if (state.profile === 'unknown') state.profile = 'pocket-master';
  state.simulation = true;
  state.deviceHosted = false;
  state.connected = true;
  state.bridgeConnected = true;
  state.stale = false;
  $('#device-profile').disabled = false;
  render();
  log('Simulation started', 'Commands affect simulated state only.');
});

$('#device-profile').addEventListener('change', (event) => {
  if (state.deviceHosted) return;
  state.profile = event.target.value;
  state.bank = 0;
  state.preset = 0;
  render();
  log('Profile changed', event.target.selectedOptions[0].textContent);
});

document.addEventListener('keydown', (event) => {
  if (event.target.matches('select, button')) return;
  const key = event.key.toUpperCase();
  if (['1', '2', '3'].includes(key)) toggleEffect(['drive', 'delay', 'reverb'][Number(key) - 1]);
  if (key === 'T') toggleEffect('tuner');
  if ('ABCD'.includes(key)) selectPreset(state.bank * 4 + 'ABCD'.indexOf(key));
  if (event.key === 'ArrowLeft') changeBank(-1);
  if (event.key === 'ArrowRight') changeBank(1);
});

async function start() {
  render();
  if (document.documentElement.hasAttribute('data-device-hosted') && await syncState()) {
    log('Device UI online', `Revision ${state.revision} · HTTP v1 state`);
    window.setInterval(syncState, 2000);
  } else {
    log('Device offline', 'Connect over BLE or USB DEV, or explicitly start the simulator.');
  }
}

start();
