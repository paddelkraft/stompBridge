const state = {
  bank: 0,
  preset: 0,
  presetName: 'Glass Horizon',
  connected: true,
  profile: 'pocket-master',
  revision: 0,
  deviceHosted: false,
  effects: { drive: true, delay: false, reverb: true, tuner: false },
  effectNames: { drive: 'Warm boost', delay: 'Tape echo', reverb: 'Wide hall', tuner: 'Utility' },
  bankPatchNames: [],
};

const demoNames = [
  'Glass Horizon', 'Copper Room', 'Night Signal', 'Paper Moon',
  'Slow Current', 'Neon Static', 'Open Water', 'Redline',
  'Afterimage', 'Satellite', 'Dust & Chrome', 'Low Orbit',
];

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];

function presetLabel(index) {
  if (state.profile === 'gp5') return `P${String(index + 1).padStart(2, '0')}`;
  return index < 50
    ? `P${String(index + 1).padStart(2, '0')}`
    : `F${String(index - 49).padStart(2, '0')}`;
}

function timestamp() {
  return new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function log(message, detail = '') {
  const item = document.createElement('li');
  item.innerHTML = `<time>${timestamp()}</time><span><b>${message}</b>${detail ? `<br>${detail}` : ''}</span>`;
  $('#event-log').prepend(item);
  while ($('#event-log').children.length > 8) $('#event-log').lastElementChild.remove();
  $('#log-count').textContent = $('#event-log').children.length;
}

function cardName(index, slot) {
  return state.deviceHosted
    ? state.bankPatchNames[slot] || `Patch ${'ABCD'[slot]}`
    : demoNames[index % demoNames.length];
}

function renderPresets() {
  const container = $('#presets');
  container.innerHTML = '';
  for (let slot = 0; slot < 4; slot += 1) {
    const index = state.bank * 4 + slot;
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `preset${index === state.preset ? ' active' : ''}`;
    button.dataset.index = index;
    button.innerHTML = `
      <span class="slot">${'ABCD'[slot]}</span>
      <span><strong>${cardName(index, slot)}</strong><small>${presetLabel(index)}</small></span>`;
    button.addEventListener('click', () => selectPreset(index));
    container.append(button);
  }
}

function renderConnection() {
  $('#connection').classList.toggle('offline', !state.connected);
  $('#connection').setAttribute('aria-pressed', state.connected);
  $('#connection-label').textContent = state.connected ? 'CONNECTED' : 'OFFLINE';
  $('#midi-status').textContent = state.connected ? 'ONLINE' : 'OFFLINE';
  $('#midi-status').style.color = state.connected ? 'var(--lime)' : 'var(--muted)';
}

function render() {
  $('#preset-number').textContent = presetLabel(state.preset);
  $('#preset-name').textContent = state.deviceHosted
    ? state.presetName
    : demoNames[state.preset % demoNames.length];
  $('#preset-meta').textContent = state.deviceHosted
    ? `${state.profile === 'gp5' ? 'Valeton GP-5' : 'Pocket Master'} · Slot ${'ABCD'[state.preset % 4]}`
    : `${state.preset % 3 === 0 ? 'Clean ambient' : state.preset % 3 === 1 ? 'Dynamic drive' : 'Modulated space'} · Slot ${'ABCD'[state.preset % 4]}`;
  $('#bank-number').textContent = String(state.bank + 1).padStart(2, '0');
  $('#bank-range').textContent = `${presetLabel(state.bank * 4)} — ${presetLabel(state.bank * 4 + 3)}`;
  $$('.effect').forEach((button) => {
    const active = state.effects[button.dataset.effect];
    button.classList.toggle('active', active);
    button.setAttribute('aria-pressed', active);
    button.querySelector('small').textContent = state.effectNames[button.dataset.effect];
  });
  renderConnection();
  renderPresets();
}

async function sendAction(action) {
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
  if (!state.connected && !state.deviceHosted) return log('Command blocked', 'Connect a MIDI device first.');
  state.preset = index;
  state.bank = Math.floor(index / 4);
  state.presetName = `${state.profile === 'gp5' ? 'GP-5' : 'Pocket Master'} ${presetLabel(index)}`;
  render();
  sendAction(`preset_${'abcd'[index % 4]}`);
  log(`Selected ${presetLabel(index)}`, state.deviceHosted ? 'Command queued' : 'Demo state updated');
}

function changeBank(direction) {
  const maxBank = state.profile === 'gp5' ? 24 : 25;
  state.bank = Math.max(0, Math.min(maxBank, state.bank + direction));
  render();
  sendAction(direction < 0 ? 'bank_down' : 'bank_up');
  log(`Bank ${String(state.bank + 1).padStart(2, '0')}`, 'Ready — choose preset A, B, C or D');
}

function toggleEffect(name) {
  if (!state.connected && !state.deviceHosted) return log('Command blocked', 'Connect a MIDI device first.');
  state.effects[name] = !state.effects[name];
  render();
  sendAction(`toggle_${name}`);
  const on = state.effects[name];
  if (name === 'tuner') {
    $('#tuner-overlay').hidden = !on;
    log(`Tuner ${on ? 'on' : 'off'}`, 'Locally assumed · no reliable MIDI readback');
  } else {
    log(`${name.toUpperCase()} ${on ? 'on' : 'off'}`, state.deviceHosted ? 'Command queued' : 'Demo state updated');
  }
}

function applySnapshot(snapshot) {
  if (!snapshot || typeof snapshot.revision !== 'number') return false;
  state.revision = snapshot.revision;
  state.deviceHosted = true;
  state.profile = snapshot.device === 'valeton_gp5' ? 'gp5' : 'pocket-master';
  state.connected = snapshot.connection === 'connected';
  state.bank = Number.isInteger(snapshot.bank) ? snapshot.bank : state.bank;
  if (snapshot.preset) {
    state.preset = snapshot.preset.absolute;
    state.presetName = snapshot.preset.name || `${state.profile === 'gp5' ? 'GP-5' : 'Pocket Master'} ${snapshot.preset.label}`;
  }
  state.bankPatchNames = (snapshot.bank_patches || []).map((patch) => patch.name || patch.label);
  for (const block of snapshot.blocks || []) {
    if (Object.prototype.hasOwnProperty.call(state.effects, block.id) && typeof block.enabled === 'boolean') {
      state.effects[block.id] = block.enabled;
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
    if (!response.ok) return false;
    return applySnapshot(await response.json());
  } catch (_) {
    return false;
  }
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
  if (state.deviceHosted) {
    log('MIDI transport', state.connected ? 'Connected' : 'Waiting for pedal connection');
    return;
  }
  state.connected = !state.connected;
  renderConnection();
  log(state.connected ? 'Device connected' : 'Device disconnected', 'Local prototype mode');
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
  if (await syncState()) {
    log('Device UI online', `Revision ${state.revision} · live firmware state`);
    window.setInterval(syncState, 2000);
  } else {
    log('Prototype mode', 'No device API detected');
    log('State synchronized', 'Interactive browser simulation');
  }
}

start();
