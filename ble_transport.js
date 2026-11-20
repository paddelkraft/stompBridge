// Small browser boundary for the StompBridge BLE UI GATT transport.
(() => {
  const SERVICE = '9e0d0001-7f4a-4b86-9c31-6a8b7d4c2000';
  const INFO = '9e0d0001-7f4a-4b86-9c31-6a8b7d4c2001';
  const COMMAND = '9e0d0001-7f4a-4b86-9c31-6a8b7d4c2002';
  const EVENTS = '9e0d0001-7f4a-4b86-9c31-6a8b7d4c2003';
  const HEADER = 12;
  const ATTRIBUTE = 20;
  const MAX_MESSAGE = 8192;
  const MAX_FRAGMENTS = 1024;
  const TIMEOUT_MS = 3000;

  class StompBleTransport {
    constructor(callbacks = {}) {
      this.callbacks = callbacks;
      this.device = null;
      this.command = null;
      this.events = null;
      this.transfer = 0;
      this.writeQueue = Promise.resolve();
      this.pendingFrame = null;
      this.fragmentTimer = 0;
      this.maxCommand = 256;
      this.onDisconnected = () => this.callbacks.onDisconnect?.();
      this.onNotification = (event) => this.receive(event);
    }

    async connect() {
      if (!window.isSecureContext)
        throw new Error('Open the UI from http://localhost on this computer or from HTTPS.');
      if (!navigator.bluetooth?.requestDevice)
        throw new Error('Web Bluetooth is unavailable. Try Chrome on desktop or Bluefy on iPhone/iPad.');
      try {
        this.device = await navigator.bluetooth.requestDevice({
          // Chromium matches name prefixes after discovery, avoiding the
          // native scan filter on Linux while keeping the chooser scoped.
          filters: [{ services: [SERVICE] }, { namePrefix: 'StompBridge UI' }],
          optionalServices: [SERVICE],
        });
      } catch (error) {
        if (error?.message?.includes('Web Bluetooth API globally disabled')) {
          throw new Error('Web Bluetooth is disabled in Brave. Enable Web Bluetooth API in brave://flags and relaunch.');
        }
        throw error;
      }
      this.device.addEventListener('gattserverdisconnected', this.onDisconnected);
      const server = await this.device.gatt.connect();
      const service = await server.getPrimaryService(SERVICE);
      const info = await service.getCharacteristic(INFO);
      let protocol;
      for (let attempt = 0; attempt < 8; attempt += 1) {
        protocol = JSON.parse(new TextDecoder().decode(await info.readValue()));
        if (protocol.epoch) break;
        await new Promise((resolve) => window.setTimeout(resolve, 100));
      }
      if (protocol.v !== 1 || !Number.isInteger(protocol.epoch) ||
          !Number.isInteger(protocol.generation))
        throw new Error('Bridge protocol information is invalid or unsupported.');
      this.maxCommand = protocol.maxCommand || 256;
      this.command = await service.getCharacteristic(COMMAND);
      this.events = await service.getCharacteristic(EVENTS);
      this.events.addEventListener('characteristicvaluechanged', this.onNotification);
      await this.events.startNotifications();
      return protocol;
    }

    disconnect() {
      window.clearTimeout(this.fragmentTimer);
      this.pendingFrame = null;
      if (this.events) this.events.removeEventListener('characteristicvaluechanged', this.onNotification);
      if (this.device?.gatt?.connected) this.device.gatt.disconnect();
    }

    resetInput() {
      window.clearTimeout(this.fragmentTimer);
      this.fragmentTimer = 0;
      this.pendingFrame = null;
    }

    write(type, value) {
      if (!this.command) return Promise.reject(new Error('BLE is disconnected.'));
      const bytes = new TextEncoder().encode(JSON.stringify(value));
      if (bytes.length === 0 || bytes.length > this.maxCommand || bytes.length > MAX_MESSAGE)
        return Promise.reject(new Error('Message exceeds the bridge size limit.'));
      const chunkSize = ATTRIBUTE - HEADER;
      const count = Math.ceil(bytes.length / chunkSize);
      const transfer = (this.transfer = (this.transfer + 1) & 0xffff);
      if (count > MAX_FRAGMENTS) return Promise.reject(new Error('Message requires too many BLE fragments.'));
      const frames = [];
      for (let index = 0; index < count; index += 1) {
        const offset = index * chunkSize;
        const data = bytes.slice(offset, Math.min(bytes.length, offset + chunkSize));
        const frame = new Uint8Array(HEADER + data.length);
        const view = new DataView(frame.buffer);
        frame[0] = 0x53; frame[1] = 0x42; frame[2] = 1; frame[3] = type;
        view.setUint16(4, transfer, true);
        view.setUint16(6, index, true);
        view.setUint16(8, count, true);
        view.setUint16(10, bytes.length, true);
        frame.set(data, HEADER);
        frames.push(frame);
      }
      const operation = this.writeQueue.then(async () => {
        for (const frame of frames) {
          if (!this.command || !this.device?.gatt?.connected)
            throw new Error('BLE connection changed during write.');
          if (this.command.writeValueWithResponse)
            await this.command.writeValueWithResponse(frame);
          else await this.command.writeValue(frame);
        }
      });
      this.writeQueue = operation.catch(() => {});
      return operation;
    }

    receive(event) {
      const value = event.target.value;
      const frame = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
      const now = Date.now();
      if (frame.length < HEADER || frame[0] !== 0x53 || frame[1] !== 0x42 || frame[2] !== 1) {
        this.reject('Malformed notification header or protocol version.');
        return;
      }
      const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
      const type = frame[3];
      const transfer = view.getUint16(4, true);
      const index = view.getUint16(6, true);
      const count = view.getUint16(8, true);
      const totalLength = view.getUint16(10, true);
      if (type < 1 || type > 5 || !count || count > MAX_FRAGMENTS ||
          totalLength > MAX_MESSAGE || index >= count ||
          frame.length > 514 || frame.length - HEADER > totalLength ||
          (index + 1 < count && frame.length === HEADER)) {
        this.reject('Fragment bounds did not match the declared transfer.');
        return;
      }
      let pending = this.pendingFrame;
      if (!pending) {
        if (index !== 0) { this.reject('Missing first fragment.'); return; }
        pending = { type, transfer, count, totalLength,
          chunkLength: frame.length - HEADER, next: 0, parts: [], touched: now };
        const expectedCount = pending.chunkLength === 0
          ? 1 : Math.max(1, Math.ceil(totalLength / pending.chunkLength));
        if (expectedCount !== count) { this.reject('Fragment count did not match the declared length.'); return; }
      }
      if (pending.type !== type || pending.transfer !== transfer || pending.count !== count ||
          pending.totalLength !== totalLength || pending.next !== index ||
          now - pending.touched > TIMEOUT_MS ||
          (index + 1 < count && frame.length - HEADER !== pending.chunkLength)) {
        this.reject('Out of order, duplicate, or timed-out fragment.');
        return;
      }
      pending.parts.push(frame.slice(HEADER));
      pending.next += 1;
      pending.touched = now;
      this.pendingFrame = pending.next === count ? null : pending;
      window.clearTimeout(this.fragmentTimer);
      if (pending.next !== count) {
        this.fragmentTimer = window.setTimeout(() => {
          if (this.pendingFrame === pending) this.reject('Notification fragments timed out.');
        }, TIMEOUT_MS + 1);
        return;
      }
      const assembledLength = pending.parts.reduce((sum, part) => sum + part.length, 0);
      if (assembledLength !== totalLength) { this.reject('Fragment lengths did not match the message.'); return; }
      const bytes = new Uint8Array(totalLength);
      let offset = 0;
      for (const part of pending.parts) { bytes.set(part, offset); offset += part.length; }
      try {
        const message = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes));
        this.callbacks.onMessage?.(type, message);
      } catch (error) {
        this.reject(error?.message || 'Invalid UTF-8 JSON.');
      }
    }

    reject(reason) {
      this.resetInput();
      this.callbacks.onFrameError?.(reason);
    }
  }

  window.StompBleTransport = StompBleTransport;
})();
