// The LinnStrument's serial port through the Web Serial API (Chrome and Edge on desktop), as the port
// provider flow.js expects.

import { ByteQueue, PortLost, sleep, now } from "./bytes.js";
import { LINNSTRUMENT_USB_VID } from "./linnstrument.js";

export const FILTERS = [{ usbVendorId: LINNSTRUMENT_USB_VID }];

export class WebSerialPort {
  constructor(serialPort) {
    this.sp = serialPort;
    this.queue = new ByteQueue();
    this.isOpen = false;
    this.lost = false;
    this.reader = null;
    this.writer = null;
  }

  async open(baudRate) {
    if (this.lost) throw new PortLost();
    await this.sp.open({ baudRate, bufferSize: 65536 });
    this.isOpen = true;
    this.closing = false;
    this.queue.reset();
    this.writer = this.sp.writable.getWriter();
    this.pumping = this.pump();
  }

  // move received bytes into the queue until the port closes or the device goes away
  async pump() {
    while (this.isOpen && this.sp.readable) {
      const reader = this.sp.readable.getReader();
      this.reader = reader;
      try {
        for (;;) {
          const { value, done } = await reader.read();
          if (done) return;
          if (value) this.queue.push(value);
        }
      } catch (e) {
        if (this.closing) return;
        if (e.name === "NetworkError" || !this.sp.readable) {
          this.markLost();
          return;
        }
        // a framing, parity or overrun error: the stream restarts, keep reading
      } finally {
        try {
          reader.releaseLock();
        } catch { /* already released */ }
        this.reader = null;
      }
    }
  }

  markLost() {
    this.lost = true;
    this.queue.fail(new PortLost());
  }

  async write(bytes) {
    if (this.lost) throw new PortLost();
    try {
      await this.writer.write(bytes);
    } catch (e) {
      if (this.lost || e.name === "NetworkError") {
        this.markLost();
        throw new PortLost();
      }
      throw e;
    }
  }

  async setDTR(value) {
    await this.sp.setSignals({ dataTerminalReady: value });
  }

  read(n, timeoutMs) {
    return this.queue.read(n, timeoutMs);
  }

  readSome(timeoutMs, max) {
    return this.queue.readSome(timeoutMs, max);
  }

  flushInput() {
    this.queue.clear();
  }

  async close() {
    if (!this.isOpen) return;
    this.closing = true;
    this.isOpen = false;
    try {
      await this.reader?.cancel();
    } catch { /* the device may be gone */ }
    try {
      await this.pumping;
    } catch { /* ignore */ }
    try {
      this.writer?.releaseLock();
    } catch { /* ignore */ }
    this.writer = null;
    try {
      await this.sp.close();
    } catch { /* the port goes away when the firmware leaves serial mode */ }
  }
}

// Keeps track of the LinnStrument's port. The user picks it once; when the USB device drops out and
// comes back, Chrome hands out a new port object, which this finds again. If Chrome didn't keep the
// permission across the reconnect, pickAgain() asks the user to pick it (it needs a click).
export class WebSerialProvider {
  constructor({ serial = navigator.serial, pickAgain = null, log = null } = {}) {
    this.serial = serial;
    this.pickAgain = pickAgain;
    this.log = log;
    this.current = null;
    this.productId = null;
    serial.addEventListener("disconnect", (event) => {
      if (this.current && event.target === this.current.sp) this.current.markLost();
    });
  }

  // must run from a click
  async choose() {
    const sp = await this.serial.requestPort({ filters: FILTERS });
    this.use(sp);
    return this.current;
  }

  use(sp) {
    if (this.current?.sp !== sp || this.current.lost) this.current = new WebSerialPort(sp);
    this.productId = sp.getInfo().usbProductId ?? null;
  }

  get chosen() {
    return !!this.current;
  }

  async findPort() {
    const ports = (await this.serial.getPorts()).filter((p) => p.getInfo().usbVendorId === LINNSTRUMENT_USB_VID);
    if (this.current && !this.current.lost && ports.includes(this.current.sp)) return this.current.sp;
    return ports.find((p) => p.getInfo().usbProductId === this.productId) || ports[0] || null;
  }

  async waitForPort(timeoutMs) {
    const deadline = now() + timeoutMs;
    for (;;) {
      const sp = await this.findPort();
      if (sp) {
        this.use(sp);
        return this.current;
      }
      if (now() > deadline) break;
      await sleep(250);
    }
    if (this.pickAgain) {
      this.log?.("The LinnStrument's port came back, but Chrome needs you to pick it again.");
      this.use(await this.pickAgain());
      return this.current;
    }
    throw new PortLost("The LinnStrument's serial port didn't come back.");
  }
}
