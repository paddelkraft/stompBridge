// Local development transport. Web Serial carries the same v1 UI frames as GATT.
(() => {
  const HEADER = 12;
  const ATTRIBUTE = 20;
  const MAX_MESSAGE = 8192;
  const MAX_FRAGMENTS = 1024;
  const TIMEOUT_MS = 3000;

  class StompSerialTransport {
    constructor(callbacks = {}) {
      this.callbacks = callbacks;
      this.port = null;
      this.reader = null;
      this.readPipe = null;
      this.writer = null;
      this.transfer = 0;
      this.writeQueue = Promise.resolve();
      this.pendingFrame = null;
      this.fragmentTimer = 0;
      this.maxCommand = 256;
      this.infoResolve = null;
      this.protocolReceived = false;
      this.reading = false;
      const token = new Uint32Array(1);
      window.crypto.getRandomValues(token);
      this.sessionToken = token[0].toString(16).padStart(8, '0');
    }

    async connect() {
      if (!window.isSecureContext || !navigator.serial)
        throw new Error('USB development transport needs Web Serial in Chromium. Open this page from localhost.');
      this.port = await navigator.serial.requestPort();
      this.port.addEventListener('disconnect', () => this.callbacks.onDisconnect?.());
      await this.port.open({ baudRate: 115200 });
      this.writer = this.port.writable.getWriter();
      const decoder = new TextDecoderStream();
      this.readPipe = this.port.readable.pipeTo(decoder.writable).catch(() => {});
      this.reader = decoder.readable.getReader();
      this.reading = true;
      this.readLoop();
      this.protocolReceived = false;
      const protocolReady = new Promise((resolve, reject) => {
        this.infoResolve = resolve;
        window.setTimeout(() => reject(new Error('No StompBridge UI serial info received. Check the dev firmware and USB port.')), 5000);
      });
      for (let attempt = 0; attempt < 10 && !this.protocolReceived; attempt += 1) {
        await this.writeLine(`UI-SERIAL:OPEN:${this.sessionToken}`);
        await Promise.race([protocolReady.then(() => true),
          new Promise((resolve) => window.setTimeout(resolve, 350))]);
      }
      const protocol = await protocolReady;
      if (protocol.v !== 1 || !Number.isInteger(protocol.epoch) ||
          !Number.isInteger(protocol.generation))
        throw new Error('Bridge protocol information is invalid or unsupported.');
      this.maxCommand = protocol.maxCommand || 256;
      return protocol;
    }

    async disconnect() {
      window.clearTimeout(this.fragmentTimer);
      this.pendingFrame = null;
      this.reading = false;
      await this.writeQueue.catch(() => {});
      try { await this.writeLine(`UI-SERIAL:CLOSE:${this.sessionToken}`); } catch (_) {}
      try { await this.reader?.cancel(); } catch (_) {}
      await this.readPipe;
      try { this.reader?.releaseLock(); } catch (_) {}
      try { this.writer?.releaseLock(); } catch (_) {}
      try { await this.port?.close(); } catch (_) {}
      this.reader = null;
      this.readPipe = null;
      this.writer = null;
      this.port = null;
    }

    resetInput() {
      window.clearTimeout(this.fragmentTimer);
      this.fragmentTimer = 0;
      this.pendingFrame = null;
    }

    write(type, value) {
      if (!this.writer) return Promise.reject(new Error('USB serial is disconnected.'));
      const bytes = new TextEncoder().encode(JSON.stringify(value));
      if (bytes.length === 0 || bytes.length > this.maxCommand || bytes.length > MAX_MESSAGE)
        return Promise.reject(new Error('Message exceeds the bridge size limit.'));
      const chunkSize = ATTRIBUTE - HEADER;
      const count = Math.ceil(bytes.length / chunkSize);
      const transfer = (this.transfer = (this.transfer + 1) & 0xffff);
      if (count > MAX_FRAGMENTS) return Promise.reject(new Error('Message requires too many serial frames.'));
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
          await this.writeLine(`UI-SERIAL:FRAME:${this.hex(frame)}`);
          // Give the ESP32 loop task time to drain its serial receive queue.
          await new Promise((resolve) => window.setTimeout(resolve, 8));
        }
      });
      this.writeQueue = operation.catch(() => {});
      return operation;
    }

    async writeLine(line) {
      if (!this.writer) throw new Error('USB serial is disconnected.');
      await this.writer.write(new TextEncoder().encode(`${line}\n`));
    }

    hex(bytes) {
      return [...bytes].map((byte) => byte.toString(16).padStart(2, '0')).join('');
    }

    async readLoop() {
      let pending = '';
      try {
        while (this.reading && this.reader) {
          const { value, done } = await this.reader.read();
          if (done) {
            if (this.reading) this.callbacks.onDisconnect?.();
            break;
          }
          pending += value;
          let newline;
          while ((newline = pending.indexOf('\n')) >= 0) {
            const line = pending.slice(0, newline).replace(/\r$/, '');
            pending = pending.slice(newline + 1);
            if (line.startsWith('UI-SERIAL:INFO:')) {
              const protocol = JSON.parse(line.slice(15));
              this.protocolReceived = true;
              this.infoResolve?.(protocol);
              this.infoResolve = null;
            } else if (line.startsWith('UI-SERIAL:FRAME:')) {
              const hex = line.slice(16);
              if (hex.length % 2 || hex.length > 1028 || !/^[0-9a-f]*$/i.test(hex)) {
                this.reject('Malformed USB serial frame.');
                continue;
              }
              const frame = new Uint8Array(hex.length / 2);
              for (let i = 0; i < frame.length; i += 1)
                frame[i] = Number.parseInt(hex.slice(i * 2, i * 2 + 2), 16);
              this.receive(frame);
            } else if (line.startsWith('UI-SERIAL:DEBUG:')) {
              this.callbacks.onDiagnostic?.(line.slice(16));
            } else if (line) {
              console.debug('[StompBridge USB] board serial', line);
            }
          }
          if (pending.length > 1200) pending = '';
        }
      } catch (error) {
        if (this.reading) this.callbacks.onDisconnect?.(error);
      }
    }

    receive(frame) {
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

  window.StompSerialTransport = StompSerialTransport;
})();
