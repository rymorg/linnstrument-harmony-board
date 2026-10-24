// Byte helpers shared by the flasher: text and number encoding, CRC-32 and CRC-16, and ByteQueue,
// the receive buffer every serial port (Web Serial or the test simulator) reads from.

export const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
export const now = () => (typeof performance !== "undefined" ? performance.now() : Date.now());

export function ascii(text) {
  const out = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) out[i] = text.charCodeAt(i) & 0xff;
  return out;
}

export function text(bytes) {
  return String.fromCharCode(...bytes);
}

export function concat(...parts) {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let offset = 0;
  for (const p of parts) {
    out.set(p, offset);
    offset += p.length;
  }
  return out;
}

export function equal(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

export function indexOf(haystack, needle) {
  outer: for (let i = 0; i + needle.length <= haystack.length; i++) {
    for (let j = 0; j < needle.length; j++) if (haystack[i + j] !== needle[j]) continue outer;
    return i;
  }
  return -1;
}

export function u32le(value) {
  const out = new Uint8Array(4);
  new DataView(out.buffer).setUint32(0, value >>> 0, true);
  return out;
}

export function i32le(value) {
  const out = new Uint8Array(4);
  new DataView(out.buffer).setInt32(0, value, true);
  return out;
}

export function readU32le(bytes, offset = 0) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(offset, true);
}

export function readI32le(bytes, offset = 0) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getInt32(offset, true);
}

export const hex8 = (n) => (n >>> 0).toString(16).toUpperCase().padStart(8, "0");
export const show = (bytes) => JSON.stringify(text(bytes.slice(0, 40)));

// CRC-32 as zlib computes it, which equals the firmware's nibble-table crc_byte_array() (ls_serial.ino)
const CRC32_TABLE = new Uint32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  CRC32_TABLE[n] = c >>> 0;
}

export function crc32(bytes) {
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) crc = CRC32_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

// CRC-16/XMODEM (polynomial 0x1021, initial 0), as bossac's Samba::crc16Calc
export function crc16(bytes) {
  let crc = 0;
  for (let i = 0; i < bytes.length; i++) {
    crc ^= bytes[i] << 8;
    for (let k = 0; k < 8; k++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}

export async function sha256hex(bytes) {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

export class TimeoutError extends Error {
  constructor(message, partial = new Uint8Array(0)) {
    super(message);
    this.name = "TimeoutError";
    this.partial = partial;
  }
}

// The USB device went away while the port was open
export class PortLost extends Error {
  constructor(message = "the LinnStrument's USB connection dropped") {
    super(message);
    this.name = "PortLost";
  }
}

// Bytes received from a port, waiting to be read. push() comes from the port's receive loop.
export class ByteQueue {
  constructor() {
    this.chunks = [];
    this.length = 0;
    this.waiter = null;
    this.error = null;
  }

  push(bytes) {
    if (!bytes.length) return;
    this.chunks.push(bytes);
    this.length += bytes.length;
    this.wake();
  }

  fail(error) {
    this.error = error;
    this.wake();
  }

  reset() {
    this.chunks = [];
    this.length = 0;
    this.error = null;
  }

  clear() {
    this.chunks = [];
    this.length = 0;
  }

  wake() {
    if (this.waiter) {
      const w = this.waiter;
      this.waiter = null;
      w();
    }
  }

  take(n) {
    const out = new Uint8Array(n);
    let filled = 0;
    while (filled < n) {
      const chunk = this.chunks[0];
      const used = Math.min(chunk.length, n - filled);
      out.set(chunk.subarray(0, used), filled);
      filled += used;
      if (used === chunk.length) this.chunks.shift();
      else this.chunks[0] = chunk.subarray(used);
    }
    this.length -= n;
    return out;
  }

  // wait until data arrives, the port fails, or the deadline passes
  waitForData(deadline) {
    return new Promise((resolve) => {
      const ms = deadline - now();
      if (ms <= 0) return resolve();
      const timer = setTimeout(() => {
        if (this.waiter === done) this.waiter = null;
        resolve();
      }, ms);
      const done = () => {
        clearTimeout(timer);
        resolve();
      };
      this.waiter = done;
    });
  }

  // exactly n bytes, or TimeoutError carrying what did arrive
  async read(n, timeoutMs) {
    const deadline = now() + timeoutMs;
    while (this.length < n) {
      if (this.error) throw this.error;
      if (now() >= deadline) throw new TimeoutError(`timed out (got ${this.length} of ${n} bytes)`, this.take(this.length));
      await this.waitForData(deadline);
    }
    return this.take(n);
  }

  // whatever is there, up to max bytes, waiting up to timeoutMs for the first byte; empty on timeout
  async readSome(timeoutMs, max = Infinity) {
    const deadline = now() + timeoutMs;
    while (this.length === 0) {
      if (this.error) throw this.error;
      if (now() >= deadline) return new Uint8Array(0);
      await this.waitForData(deadline);
    }
    return this.take(Math.min(max, this.length));
  }
}
