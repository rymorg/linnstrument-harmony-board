// The LinnStrument's settings transfer protocol (ls_serial.ino), the .lsbackup file format, and the
// settings-size check from a firmware .elf. A port of the Connection and Backup classes in
// tools/flash_keep_settings.py; keep the two in step.

import {
  ascii, concat, crc32, equal, i32le, indexOf, readI32le, readU32le, show, sleep, now, u32le, TimeoutError, PortLost,
} from "./bytes.js";

export const COUNTDOWN = ascii("5, 4, 3, 2, 1 ...\n"); // host -> device: open a command session
export const GO = ascii("LinnStruments are go!\n");     // device -> host: ready for one command
export const ACK = ascii("ACK\n");
const SEND_SETTINGS = ascii("s");
const RESTORE_SETTINGS = ascii("r");
const SEND_PROJECTS = ascii("p");
const RESTORE_PROJECT = ascii("q");
const SEND_HARMONY = ascii("x");        // Harmony Board firmware only: its saved harmony block
const RESTORE_HARMONY = ascii("y");
const PREPARE_UPDATE = ascii("u");      // Harmony Board firmware only: show WAIT just before the erase
const CRC_CHECK = ascii("c");           // asks for the CRC of the batch just sent
const CRC_WRONG = ascii("w");           // the batch gets sent again
const CRC_OK = ascii("o");
const BATCH_SIZE = 96;                  // data travels in batches of 96 bytes, each with a CRC
const RESYNC = new Uint8Array([0xff]);  // not a command: ends a stale session and resets the countdown match
export const MIN_PROJECT_VERSION = 9;   // serialRestoreProject() ignores projects from older settings versions
export const SERIAL_BAUD = 115200;
export const LINNSTRUMENT_USB_VID = 0xf055;

// Delays, in milliseconds. The tests shrink them.
export const TIMING = {
  restartSettle: 2000,   // opening the port restarts the LinnStrument, which sometimes drops USB for a second or two
  deviceIdle: 2000,      // the firmware abandons a transfer after 2 s without data
  waitShown: 2000,       // how long WAIT stays up before the lights go out
  touchSettle: 500,      // after the 1200-baud touch
  bootloaderSettle: 1000, // after opening the port to the bootloader (bossac waits 500 ms)
  bootloaderFirstTry: 8000,
  bootloaderTimeout: 20000,
  firstBootSettle: 1500, // after the flash, before opening the port (which restarts it): the first start sets up storage
  bootTimeout: 60000,    // for the new firmware to answer after the flash
  portTimeout: 15000,    // for the USB device to come back after a drop
  scale: 1,              // multiplies the receive timeouts below (tests use less)
};

export const OS_UPDATE_HINT = "Turn on OS Update: press Global Settings, then briefly press OS Update " +
  "(column 16, third pad from the bottom) so its light turns on.";

export class LinnStrumentError extends Error {
  constructor(message) {
    super(message);
    this.name = "LinnStrumentError";
  }
}

// The firmware didn't answer the countdown: it isn't in serial mode, or isn't running
export class NoAnswer extends LinnStrumentError {}

// The firmware doesn't know the command, so there's no point retrying it
export class NotSupported extends NoAnswer {}

// Open the LinnStrument's port and wait out the restart that opening it causes. The firmware comes
// back in serial mode, but sometimes the USB device drops out while it restarts, so wait, and open
// the port again once it's back.
export async function openSettled(provider, timing, log, attempts = 4) {
  for (let i = 0; i < attempts; i++) {
    const port = await provider.waitForPort(timing.portTimeout);
    try {
      await port.open(SERIAL_BAUD);
    } catch (e) {
      log?.(`Couldn't open the port (${e.message}); trying again.`);
      await sleep(1000);
      continue;
    }
    const deadline = now() + timing.restartSettle;
    while (now() < deadline && !port.lost) await sleep(50);
    if (!port.lost) {
      port.flushInput();
      return port;
    }
    log?.("The LinnStrument restarted and its USB connection dropped for a moment; reconnecting.");
    await port.close();
  }
  throw new NoAnswer("The LinnStrument's serial port kept dropping out.");
}

export class Connection {
  constructor(provider, { timing = TIMING, log } = {}) {
    this.provider = provider;
    this.timing = timing;
    this.log = log;
    this.port = null;
  }

  static async open(provider, options) {
    const conn = new Connection(provider, options);
    conn.port = await openSettled(provider, conn.timing, conn.log);
    return conn;
  }

  async reopen() {
    await this.close();
    this.port = await openSettled(this.provider, this.timing, this.log);
  }

  async close() {
    if (this.port) await this.port.close();
  }

  async write(bytes) {
    await this.port.write(bytes);
  }

  async read(n, what, timeout = 3000) {
    try {
      return await this.port.read(n, timeout * this.timing.scale);
    } catch (e) {
      if (e instanceof TimeoutError) {
        throw new LinnStrumentError(`timed out waiting for ${what} (got ${e.partial.length} of ${n} bytes)`);
      }
      throw e;
    }
  }

  async expect(token, what, timeout = 3000) {
    const got = await this.read(token.length, what, timeout);
    if (!equal(got, token)) throw new LinnStrumentError(`expected ${what} ${show(token)}, got ${show(got)}`);
  }

  // send the countdown and wait for the go reply (handleSerialIO in ls_serial.ino)
  async startSession(timeout = 2000) {
    this.port.flushInput();
    await this.write(concat(RESYNC, COUNTDOWN));
    const deadline = now() + timeout * this.timing.scale;
    let seen = new Uint8Array(0);
    while (now() < deadline) {
      seen = concat(seen, await this.port.readSome(Math.max(1, deadline - now())));
      if (indexOf(seen, GO) >= 0) return true;
    }
    return false;
  }

  async handshake(attempts = 3) {
    for (let i = 0; i < attempts; i++) {
      if (await this.startSession()) return;
      // let any transfer the firmware is still stuck in time out before trying again
      await sleep(this.timing.deviceIdle + 300);
    }
    throw new NoAnswer(`The LinnStrument didn't answer. ${OS_UPDATE_HINT}`);
  }

  // receive size bytes in CRC-checked batches (the host side of serialSendSettings)
  async receive(size, progress) {
    const data = new Uint8Array(size);
    let filled = 0;
    let resends = 0;
    while (filled < size) {
      const batch = await this.read(Math.min(BATCH_SIZE, size - filled), "data");
      await this.write(CRC_CHECK);
      const remote = readU32le(await this.read(4, "batch CRC"));
      if (crc32(batch) !== remote) {
        if (++resends > 10) throw new LinnStrumentError("too many corrupted batches");
        await this.write(CRC_WRONG);
        continue;
      }
      await this.write(CRC_OK);
      data.set(batch, filled);
      filled += batch.length;
      progress?.(filled, size);
    }
    return data;
  }

  // send data in CRC-checked batches (the host side of serialRestoreSettings)
  async send(data, progress) {
    let offset = 0;
    let resends = 0;
    while (offset < data.length) {
      const batch = data.subarray(offset, offset + BATCH_SIZE);
      await this.write(batch);
      await this.expect(CRC_CHECK, "CRC request");
      await this.write(u32le(crc32(batch)));
      const reply = await this.read(1, "CRC verdict");
      if (equal(reply, CRC_WRONG)) {
        if (++resends > 10) throw new LinnStrumentError("too many corrupted batches");
        continue;
      }
      if (!equal(reply, CRC_OK)) throw new LinnStrumentError(`unexpected CRC verdict ${show(reply)}`);
      offset += batch.length;
      progress?.(offset, data.length);
    }
  }

  async readSettings(progress) {
    await this.handshake();
    await this.write(SEND_SETTINGS);
    await this.expect(ACK, "acknowledgement");
    const size = readI32le(await this.read(4, "settings size"));
    if (!(size > 0 && size <= 64 * 1024)) throw new LinnStrumentError(`implausible settings size ${size}`);
    const data = await this.receive(size, progress);
    await this.expect(ACK, "end of settings");
    return data;
  }

  async readProjects(progress) {
    await this.handshake();
    await this.write(SEND_PROJECTS);
    await this.expect(ACK, "acknowledgement");
    const count = (await this.read(1, "project count"))[0];
    const size = readI32le(await this.read(4, "project size"));
    if (!(size > 0 && size <= 64 * 1024)) throw new LinnStrumentError(`implausible project size ${size}`);
    const projects = [];
    for (let i = 0; i < count; i++) {
      projects.push(await this.receive(size));
      progress?.(projects.length * size, count * size);
    }
    await this.expect(ACK, "end of projects");
    return { size, projects };
  }

  async writeProject(version, index, data) {
    await this.handshake();
    await this.write(RESTORE_PROJECT);
    await this.expect(ACK, "acknowledgement");
    await this.write(new Uint8Array([version]));
    await this.expect(ACK, "acceptance of the project version");
    await this.write(i32le(data.length));
    try {
      await this.expect(ACK, "acceptance of the project size");
    } catch (e) {
      if (!(e instanceof LinnStrumentError)) throw e;
      throw new LinnStrumentError(`the firmware rejected the ${data.length}-byte project: its sequencer format differs`);
    }
    await this.write(new Uint8Array([index]));
    await this.expect(ACK, "acknowledgement of the project number");
    await this.send(data);
    await this.expect(ACK, "end of project");
  }

  // the Harmony Board's saved block (ls_serial.ino): empty when nothing is saved, null when the firmware
  // doesn't know the command
  async readHarmony() {
    await this.handshake();
    await this.write(SEND_HARMONY);
    try {
      await this.expect(ACK, "acknowledgement", 1000);
    } catch (e) {
      if (e instanceof LinnStrumentError) return null;
      throw e;
    }
    const size = readI32le(await this.read(4, "harmony size"));
    if (!(size >= 0 && size <= 4096)) throw new LinnStrumentError(`implausible harmony size ${size}`);
    const data = size ? await this.receive(size) : new Uint8Array(0);
    await this.expect(ACK, "end of harmony");
    return data;
  }

  // ask Harmony Board firmware to show WAIT on its pads; false if it doesn't answer (stock firmware, or a
  // LinnStrument in FWUP mode). One quick try, since this is only a courtesy.
  async prepareUpdate() {
    try {
      if (!(await this.startSession())) return false;
      await this.write(PREPARE_UPDATE);
      await this.expect(ACK, "acknowledgement", 1000);
      return true;
    } catch (e) {
      if (e instanceof LinnStrumentError || e instanceof PortLost) return false;
      throw e;
    }
  }

  async writeHarmony(data) {
    await this.handshake();
    await this.write(RESTORE_HARMONY);
    try {
      await this.expect(ACK, "acknowledgement", 1000);
    } catch (e) {
      if (!(e instanceof LinnStrumentError)) throw e;
      throw new NotSupported("this firmware doesn't take Harmony Board data");
    }
    await this.write(i32le(data.length));
    await this.expect(ACK, "acknowledgement of the harmony size");
    await this.send(data);
    await this.expect(ACK, "confirmation", 5000);
  }

  async writeSettings(data, progress) {
    await this.handshake();
    await this.write(RESTORE_SETTINGS);
    await this.expect(ACK, "acknowledgement");
    await this.write(i32le(data.length));
    await this.expect(ACK, "acknowledgement of the settings size");
    await this.send(data, progress);
    // the firmware applies the settings, then answers and leaves serial mode
    await this.expect(ACK, "confirmation", 10000);
  }
}

// run a transfer again, on a fresh connection, if it fails midway; a missing device isn't retried
export async function withRetries(operation, conn, attempts = 3) {
  for (let attempt = 0; ; attempt++) {
    try {
      return await operation();
    } catch (e) {
      if (e instanceof NoAnswer) throw e;
      if (!(e instanceof LinnStrumentError || e instanceof PortLost) || attempt === attempts - 1) throw e;
      conn.log?.(`Transfer failed (${e.message}), trying again...`);
      await sleep(conn.timing.deviceIdle + 500);
      await conn.reopen();
    }
  }
}

// ------------------------------------------------------------------------------------------------
// Backup files
// ------------------------------------------------------------------------------------------------

const BACKUP_MAGIC = ascii("LSBACKUP");
const BACKUP_FORMAT = 2;
const HEADER_1_SIZE = 24;   // magic, format, settings version, project count, settings size, project size, CRC-32
const HEADER_2_SIZE = 28;   // format 2 adds the size of the Harmony Board block, stored after the projects

// The settings blob exactly as the firmware sends it (a struct Configuration), plus projects
export class Backup {
  constructor(settings, projectSize = 0, projects = [], harmony = new Uint8Array(0)) {
    this.settings = settings;
    this.projectSize = projectSize;
    this.projects = projects;
    this.harmony = harmony || new Uint8Array(0);
  }

  get version() {
    return this.settings[0];   // DeviceSettings.version, the settings format version
  }

  describe() {
    let out = `${this.settings.length} bytes of settings (format version ${this.version})`;
    if (this.projects.length) out += `, ${this.projects.length} sequencer projects`;
    if (this.harmony.length) out += ", and the Harmony Board's harmony, memories and page settings";
    return out;
  }

  toBytes() {
    const payload = concat(this.settings, ...this.projects, this.harmony);
    const header = new Uint8Array(HEADER_2_SIZE);
    const view = new DataView(header.buffer);
    header.set(BACKUP_MAGIC, 0);
    view.setUint16(8, BACKUP_FORMAT, true);
    view.setUint8(10, this.version);
    view.setUint8(11, this.projects.length);
    view.setUint32(12, this.settings.length, true);
    view.setUint32(16, this.projectSize, true);
    view.setUint32(20, this.harmony.length, true);
    view.setUint32(24, crc32(payload), true);
    return concat(header, payload);
  }

  static fromBytes(data, name = "the file") {
    if (!equal(data.subarray(0, 8), BACKUP_MAGIC)) {
      // a raw settings blob, as saved by other tools
      if (!data.length || !(data[0] >= 1 && data[0] <= 64) || data.length > 64 * 1024) {
        throw new LinnStrumentError(`${name} is neither a LinnStrument backup nor a raw settings blob`);
      }
      return new Backup(data.slice());
    }
    if (data.length < HEADER_1_SIZE) throw new LinnStrumentError(`${name} is truncated`);
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const format = view.getUint16(8, true);
    const count = view.getUint8(11);
    const settingsSize = view.getUint32(12, true);
    const projectSize = view.getUint32(16, true);
    let harmonySize, crc, payload;
    if (format === 1) {
      harmonySize = 0;
      crc = view.getUint32(20, true);
      payload = data.subarray(HEADER_1_SIZE);
    } else if (format === 2 && data.length >= HEADER_2_SIZE) {
      harmonySize = view.getUint32(20, true);
      crc = view.getUint32(24, true);
      payload = data.subarray(HEADER_2_SIZE);
    } else {
      throw new LinnStrumentError(`${name} uses backup format ${format}; this flasher reads formats 1 and ${BACKUP_FORMAT}`);
    }
    const projectsEnd = settingsSize + count * projectSize;
    if (payload.length !== projectsEnd + harmonySize || crc32(payload) !== crc) {
      throw new LinnStrumentError(`${name} is damaged (size or checksum mismatch)`);
    }
    const projects = [];
    for (let i = 0; i < count; i++) {
      projects.push(payload.slice(settingsSize + i * projectSize, settingsSize + (i + 1) * projectSize));
    }
    return new Backup(payload.slice(0, settingsSize), projectSize, projects, payload.slice(projectsEnd));
  }
}

export function backupFileName(date = new Date()) {
  const p = (n) => String(n).padStart(2, "0");
  return `linnstrument-${date.getFullYear()}${p(date.getMonth() + 1)}${p(date.getDate())}-` +
    `${p(date.getHours())}${p(date.getMinutes())}${p(date.getSeconds())}.lsbackup`;
}

// sizeof(Configuration) in a firmware build, from the size of its `config` symbol; null if it can't tell
export function settingsSizeInElf(data) {
  try {
    if (data[0] !== 0x7f || data[1] !== 0x45 || data[2] !== 0x4c || data[3] !== 0x46) return null;
    if (data[4] !== 1 || data[5] !== 1) return null;   // 32-bit little-endian only
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const shoff = view.getUint32(0x20, true);
    const shentsize = view.getUint16(0x2e, true);
    const shnum = view.getUint16(0x30, true);
    const section = (i) => {
      const base = shoff + i * shentsize;
      return { type: view.getUint32(base + 4, true), offset: view.getUint32(base + 16, true),
        size: view.getUint32(base + 20, true), link: view.getUint32(base + 24, true) };
    };
    const want = ascii("config\0");
    for (let i = 0; i < shnum; i++) {
      const s = section(i);
      if (s.type !== 2) continue;                       // SHT_SYMTAB
      const strtab = section(s.link).offset;
      for (let sym = s.offset; sym < s.offset + s.size; sym += 16) {
        const name = view.getUint32(sym, true);
        const size = view.getUint32(sym + 8, true);
        const info = view.getUint8(sym + 12);
        if ((info & 0xf) !== 1) continue;               // STT_OBJECT
        const at = strtab + name;
        if (equal(data.subarray(at, at + want.length), want)) return size;
      }
    }
  } catch (e) {
    if (!(e instanceof RangeError)) throw e;
  }
  return null;
}
