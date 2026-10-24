// A simulated LinnStrument 200 for testing the browser flasher (companion/flash/) in Node.
//
// It models the parts the flasher talks to:
//   - the USB-to-serial bridge: opening the port (DTR up) restarts the SAM3X; a 1200-baud open with DTR
//     dropped erases the whole chip and starts the ROM bootloader; at any speed but 115200 the bytes turn
//     to junk, because the bridge passes the speed on to the SAM3X's UART;
//   - the firmware's serial protocol, byte for byte like handleSerialIO() and the serialSend*/serialRestore*
//     functions in ls_serial.ino, with their 2-second timeouts (a port of FakeLinnStrument in
//     tools/test_flash_keep_settings.py);
//   - the SAM3X8E's ROM SAM-BA monitor on the UART: the 0x80 0x80 # identification, N#/V#/w/W/o/R/S/G
//     commands with XMODEM-CRC file transfers, SRAM, the two EEFC flash controllers with page latches,
//     busy flags, lock bits and GPNVM bits, the bossac word-copy applet run by G, and RSTC resets. Reading
//     flash directly returns zeros, like the real ROM, and anything outside the memory map stops the chip.
// No hardware is touched.

import { ByteQueue, PortLost, TimeoutError, ascii, concat, crc16, crc32, equal, i32le, readI32le, readU32le, sleep, u32le } from "../companion/flash/bytes.js";
import { WORD_COPY, SAM3X8E } from "../companion/flash/samba.js";

const COUNTDOWN = ascii("5, 4, 3, 2, 1 ...\n");
const CONTROL = ascii("LC\n\0");
const GO = ascii("LinnStruments are go!\n");
const ACKS = ascii("ACK\n");
export const PROJECT_SIZE = 1000;
export const VERSION = 17;
const FLASH_BASE = 0x80000;
const FLASH_SIZE = 0x80000;
const PAGE = 256;
const PLANE_PAGES = 1024;

class Abort extends Error {}            // the firmware gave up on a transfer (serialWaitForMaximumTwoSeconds)
class Halt extends Error {}             // the chip was reset or stopped; the running program ends

export function randomBlob(size, version = VERSION, seed = 0) {
  let x = seed * 2654435761 + 1;
  const out = new Uint8Array(size);
  for (let i = 0; i < size; i++) {
    x = (x * 1103515245 + 12345) >>> 0;
    out[i] = x >>> 24;
  }
  out[0] = version;
  return out;
}

// a plausible Arduino Due image: stack pointer in SRAM, reset handler in flash, then noise
export function fakeImage(size, seed = 7) {
  const img = randomBlob(size, 0, seed);
  img.set(u32le(0x20088000), 0);
  img.set(u32le(0x000800f5), 4);
  return img;
}

// the firmware's crc_byte_array(), nibble table and all
export function firmwareCrc(data) {
  const table = [0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c];
  let crc = 0xffffffff;
  for (const b of data) {
    crc = (table[(crc ^ b) & 0x0f] ^ (crc >>> 4)) >>> 0;
    crc = (table[(crc ^ (b >> 4)) & 0x0f] ^ (crc >>> 4)) >>> 0;
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export class SimLinnStrument {
  // oldImage: what's in flash at the start; newImage: the firmware the test flashes. Both "run" when the
  // flash holds exactly them. oldHarmony: the old firmware's harmony block (null: it doesn't know x/y/u).
  constructor({ settings, projects, harmony = null, oldImage, newImage, settingsSize, newSettingsSize, faults = {}, scale = 1 }) {
    this.scale = scale;
    this.faults = { transfer: "xmodem", junkBeforeRead: false, dropOnOpen: 0, ignoreTouches: 0, corruptToHost: 0, corruptFromHost: 0,
      badPage: null, chipId: SAM3X8E.chipId, corruptSendBatches: new Set(), rejectReceiveBatches: new Set(), ...faults };
    this.events = [];
    this.errors = [];
    this.settingsSize = settingsSize ?? settings.length;
    this.newSettingsSize = newSettingsSize ?? this.settingsSize;
    this.oldImage = oldImage;
    this.newImage = newImage;
    // the SAM3X's flash: the firmware image, plus the firmware's stored data, modelled separately
    this.flash = new Uint8Array(FLASH_SIZE).fill(0xff);
    this.flash.set(oldImage);
    this.data = { settings: new Uint8Array(settings), projects: projects.map((p) => new Uint8Array(p)),
      harmony: harmony === null ? null : new Uint8Array(harmony), serialMode: true, firstBoot: false };
    this.gpnvm = 0b010;                  // boots from flash
    this.locks = [0, 0];
    this.sram = new Uint8Array(0x10000);
    this.sram1 = new Uint8Array(0x8000);
    this.eefc = [0, 1].map(() => ({ fmr: 0, busy: 0, frr: 0, fsrErr: 0, latch: new Uint8Array(PAGE).fill(0xff) }));
    this.present = true;
    this.port = null;                    // the host's open SimPort
    this.baud = 0;
    this.dtr = false;
    this.inbox = new ByteQueue();
    this.gen = 0;
    this.program = "firmware";
    this.boot();
  }

  log(event) {
    this.events.push(event);
  }

  fail(message) {
    this.errors.push(message);
    this.program = "dead";
    this.halt();
  }

  // ---------------------------------------------------------------- the bridge, as the host sees it

  hostOpen(port, baud) {
    this.port = port;
    this.baud = baud;
    this.setDTR(true);
  }

  hostClose() {
    this.setDTR(false);
    this.port = null;
  }

  setDTR(value) {
    const was = this.dtr;
    this.dtr = value;
    if (value && !was) this.reset("port opened");
    if (!value && was && this.baud === 1200) {
      if (this.faults.ignoreTouches > 0) {
        this.faults.ignoreTouches--;
        this.log("touch ignored");
        return;
      }
      this.eraseChip();
    }
  }

  hostWrite(bytes) {
    if (this.baud !== 115200) bytes = bytes.map((b) => (b * 37 + 11) & 0xff);   // wrong speed: junk
    this.inbox.push(new Uint8Array(bytes));
  }

  emit(bytes) {
    if (!this.port) return;
    if (this.baud !== 115200) bytes = bytes.map((b) => (b * 53 + 7) & 0xff);
    this.port.queue.push(new Uint8Array(bytes));
  }

  // the USB device disappears for a while (the bridge switching modes); the open port is lost
  dropUsb(ms) {
    this.present = false;
    if (this.port) {
      this.port.lost = true;
      this.port.queue.fail(new PortLost());
      this.port = null;
    }
    this.dtr = false;
    this.log("usb dropped");
    if (ms !== Infinity) setTimeout(() => { this.present = true; this.log("usb back"); }, ms * this.scale);
  }

  eraseChip() {
    this.flash.fill(0xff);
    this.gpnvm = 0;
    this.locks = [0, 0];
    this.data = { settings: null, projects: null, harmony: null, serialMode: true, firstBoot: true };
    this.log("erased");
    this.halt();
    this.program = "rom";
    this.startProgram();
  }

  // ---------------------------------------------------------------- the chip

  // end the test: no program keeps waiting for input
  stop() {
    this.stopped = true;
    this.halt();
  }

  halt() {
    this.gen++;
    this.inbox.fail(new Halt());
    this.inbox = new ByteQueue();
  }

  reset(why) {
    if (this.stopped) return;
    this.halt();
    this.boot();
    this.log(`reset (${why}) -> ${this.program}`);
    if (this.program === "firmware" && this.faults.dropOnOpen > 0 && why === "port opened") {
      this.faults.dropOnOpen--;
      setTimeout(() => this.dropUsb(1500), 350 * this.scale);
    }
  }

  boot() {
    if (this.program === "dead" && this.errors.length) return;
    if (!(this.gpnvm & 0b010)) {
      this.program = "rom";
    } else if (this.newImage ? this.flashHolds(this.newImage) : this.flashHasVectors()) {
      this.program = "firmware";
      this.running = "new";
    } else if (this.flashHolds(this.oldImage)) {
      this.program = "firmware";
      this.running = "old";
    } else {
      this.program = "dead";
      this.log("booted garbage");
    }
    this.startProgram();
  }

  // without a newImage, any Due image counts as the new firmware (for trying the page in a browser)
  flashHasVectors() {
    const sp = readU32le(this.flash, 0);
    const pc = readU32le(this.flash, 4);
    return sp >= 0x20000000 && sp <= 0x20100000 && (pc & 1) === 1 && pc >= FLASH_BASE && pc < FLASH_BASE + FLASH_SIZE;
  }

  flashHolds(image) {
    if (!image) return false;
    return equal(this.flash.subarray(0, image.length), image);
  }

  startProgram() {
    if (this.stopped) return;
    const gen = this.gen;
    const inbox = this.inbox;
    const run = this.program === "rom" ? this.runRom(gen, inbox) : this.program === "firmware" ? this.runFirmware(gen, inbox) : null;
    if (run) run.catch((e) => { if (!(e instanceof Halt)) this.errors.push(`${this.program}: ${e.stack}`); });
  }

  // ---------------------------------------------------------------- firmware (ls_serial.ino)

  async runFirmware(gen, inbox) {
    await sleep(100 * this.scale);       // booting
    if (gen !== this.gen) return;
    const harmonyFirmware = this.running === "new" || this.data.harmony !== null;
    if (this.data.firstBoot) {
      // initializeStorage() after a flash: defaults, serial mode, calibration screen
      this.data = { settings: randomBlob(this.newSettingsSize, VERSION, 99), projects: Array.from({ length: 16 }, () => new Uint8Array(PROJECT_SIZE)),
        harmony: new Uint8Array(0), serialMode: true, firstBoot: false };
      this.log("first boot");
    }
    const fw = { inbox, waiting: false, codePos: 0, harmonyFirmware };
    for (;;) {
      const d = (await inbox.read(1, 1e9))[0];
      if (!this.data.serialMode) continue;
      try {
        await this.handleSerialIO(fw, d);
      } catch (e) {
        if (!(e instanceof Abort)) throw e;
        this.log("timeout");
      }
    }
  }

  async readByte(fw) {
    try {
      return (await fw.inbox.read(1, 2000 * this.scale))[0];
    } catch (e) {
      if (e instanceof TimeoutError) {
        fw.waiting = false;
        throw new Abort();
      }
      throw e;
    }
  }

  async readBytes(fw, n) {
    const out = new Uint8Array(n);
    for (let i = 0; i < n; i++) out[i] = await this.readByte(fw);
    return out;
  }

  async handleSerialIO(fw, d) {
    if (fw.waiting) {
      const commands = { s: this.sendSettings, r: this.restoreSettings, p: this.sendProjects, q: this.restoreProject };
      if (fw.harmonyFirmware) Object.assign(commands, { x: this.sendHarmony, y: this.restoreHarmony, u: this.prepareUpdate });
      const command = commands[String.fromCharCode(d)];
      if (command) await command.call(this, fw);
      else fw.waiting = false;
      return;
    }
    if (d === COUNTDOWN[fw.codePos]) {
      fw.codePos++;
      if (fw.codePos === COUNTDOWN.length) {
        fw.codePos = 0;
        fw.waiting = true;
        this.emit(GO);
      }
    } else if (fw.codePos < CONTROL.length && d === CONTROL[fw.codePos]) {
      fw.codePos++;
    } else {
      fw.codePos = 0;
    }
  }

  async sendBatches(fw, data, label) {
    let offset = 0;
    let n = 0;
    while (offset < data.length) {
      const batch = data.subarray(offset, offset + 96);
      const key = `${label}:${n}`;
      if (this.faults.corruptSendBatches.has(key)) {
        this.faults.corruptSendBatches.delete(key);
        this.log(`corrupted ${key}`);
        this.emit(concat(new Uint8Array([batch[0] ^ 0xff]), batch.subarray(1)));
      } else {
        this.emit(batch);
      }
      if ((await this.readByte(fw)) !== 0x63) return false;     // 'c'
      this.emit(u32le(firmwareCrc(batch)));
      if ((await this.readByte(fw)) === 0x77) continue;          // 'w'
      offset += batch.length;
      n++;
    }
    return true;
  }

  async receiveBatches(fw, size, label) {
    const data = new Uint8Array(size);
    let filled = 0;
    let n = 0;
    while (filled < size) {
      const batch = await this.readBytes(fw, Math.min(96, size - filled));
      this.emit(ascii("c"));
      const remote = readU32le(await this.readBytes(fw, 4));
      const key = `${label}:${n}`;
      if (this.faults.rejectReceiveBatches.has(key)) {
        this.faults.rejectReceiveBatches.delete(key);
        this.log(`rejected ${key}`);
        this.emit(ascii("w"));
        continue;
      }
      if (firmwareCrc(batch) !== remote) {
        this.emit(ascii("w"));
        continue;
      }
      this.emit(ascii("o"));
      data.set(batch, filled);
      filled += batch.length;
      n++;
    }
    return data;
  }

  async sendSettings(fw) {
    this.emit(ACKS);
    this.emit(i32le(this.data.settings.length));
    if (await this.sendBatches(fw, this.data.settings, "settings")) {
      this.emit(ACKS);
      this.log("sent settings");
    }
  }

  async sendProjects(fw) {
    this.emit(ACKS);
    this.emit(new Uint8Array([this.data.projects.length]));
    this.emit(i32le(PROJECT_SIZE));
    for (const p of this.data.projects) await this.sendBatches(fw, p, "project");
    this.emit(ACKS);
    this.log("sent projects");
  }

  async restoreSettings(fw) {
    this.emit(ACKS);
    const size = readI32le(await this.readBytes(fw, 4));
    this.emit(ACKS);
    const data = await this.receiveBatches(fw, size, "settings");
    const current = this.running === "new" ? this.newSettingsSize : this.settingsSize;
    // upgradeConfigurationSettings(): only the current format, at the current size, is taken as is
    if (data[0] === VERSION && size === current) {
      this.data.settings = data;
      this.log("restored settings");
    } else {
      this.log("discarded settings");
    }
    this.emit(ACKS);
    await sleep(1000 * this.scale);
    // switchSerialMode(false): the bridge goes back to MIDI, and the serial port disappears
    this.data.serialMode = false;
    fw.waiting = false;
    this.log("midi mode");
    this.dropUsb(Infinity);
  }

  async prepareUpdate(fw) {
    this.emit(ACKS);
    this.log("showed wait");
  }

  async sendHarmony(fw) {
    this.emit(ACKS);
    this.emit(i32le(this.data.harmony.length));
    if (await this.sendBatches(fw, this.data.harmony, "harmony")) {
      this.emit(ACKS);
      this.log("sent harmony");
    }
  }

  async restoreHarmony(fw) {
    this.emit(ACKS);
    const size = readI32le(await this.readBytes(fw, 4));
    if (size <= 0 || size > 4096) return;
    this.emit(ACKS);
    this.data.harmony = await this.receiveBatches(fw, size, "harmony");
    this.emit(ACKS);
    this.log("restored harmony");
  }

  async restoreProject(fw) {
    this.emit(ACKS);
    const version = await this.readByte(fw);
    if (version < 9) return;
    this.emit(ACKS);
    const size = readI32le(await this.readBytes(fw, 4));
    if (size !== PROJECT_SIZE) return;
    this.emit(ACKS);
    const p = await this.readByte(fw);
    this.emit(ACKS);
    this.data.projects[p] = await this.receiveBatches(fw, size, "project");
    this.emit(ACKS);
    this.log(`restored project ${p}`);
    await sleep(500 * this.scale);
  }

  // ---------------------------------------------------------------- the ROM bootloader (SAM-BA)

  async runRom(gen, inbox) {
    const rom = { inbox, identified: false, terminal: true, seq: [], cmd: "" };
    for (;;) {
      const b = (await inbox.read(1, 1e9))[0];
      if (!rom.identified) {
        // the UART interface is picked once 0x80 0x80 # arrives
        rom.seq = [...rom.seq, b].slice(-3);
        if (rom.seq[0] === 0x80 && rom.seq[1] === 0x80 && rom.seq[2] === 0x23) {
          rom.identified = true;
          this.emit(ascii("\n\r>"));
          this.log("rom identified uart");
        }
        continue;
      }
      if (b !== 0x23) {                  // '#'
        rom.cmd += String.fromCharCode(b);
        if (rom.cmd.length > 40) rom.cmd = rom.cmd.slice(-40);
        continue;
      }
      const cmd = rom.cmd;
      rom.cmd = "";
      await this.romCommand(rom, cmd);
      if (rom.terminal) this.emit(ascii("\n\r>"));
    }
  }

  async romCommand(rom, cmd) {
    const letter = cmd[0];
    const args = cmd.slice(1).split(",").map((h) => parseInt(h, 16) >>> 0);
    const [addr, value] = args;
    switch (letter) {
      case "N":
        rom.terminal = false;
        this.emit(ascii("\n\r"));
        return;
      case "T":
        rom.terminal = true;
        this.emit(ascii("\n\r"));
        return;
      case "V":
        this.emit(ascii("v1.1 Dec 15 2010 19:25:04\n\r"));
        return;
      case "w":
        if (rom.terminal) return this.emit(ascii(`0x${this.read32(addr, true).toString(16)}`));
        return this.emit(u32le(this.read32(addr, true)));
      case "o":
        return this.emit(new Uint8Array([this.read8(addr, true)]));
      case "W":
        return this.write32(addr, value);
      case "O":
        return this.write8(addr, value);
      case "R": {
        const data = new Uint8Array(value);
        for (let i = 0; i < value; i++) data[i] = this.read8(addr + i, true);
        if (this.faults.transfer === "binary") {
          if (value > 32 && !(value & (value - 1))) return;   // the USB power-of-two bug: nothing useful comes out
          return this.emit(data);
        }
        if (this.faults.junkBeforeRead) this.emit(ascii("\n\r"));
        return this.xmodemSend(rom, data);
      }
      case "S": {
        const data = this.faults.transfer === "binary" ? await rom.inbox.read(value, 2000) : await this.xmodemReceive(rom, value);
        if (!data) return;
        for (let i = 0; i < data.length; i++) this.write8(addr + i, data[i]);
        this.log(`S ${addr.toString(16)} ${data.length}`);
        return;
      }
      case "G":
        return this.go(addr);
      case "":
        return;
      default:
        this.log(`rom ignored ${JSON.stringify(cmd)}`);
    }
  }

  async romByte(rom, ms) {
    try {
      return (await rom.inbox.read(1, ms))[0];
    } catch (e) {
      if (e instanceof TimeoutError) return -1;
      throw e;
    }
  }

  // receive an XMODEM-CRC transfer: 'C' until the first block, full 128-byte blocks written as received
  async xmodemReceive(rom, size) {
    const blocks = [];
    let expect = 1;
    let b = -1;
    for (let i = 0; i < 50 && b !== 0x01; i++) {
      this.emit(ascii("C"));
      b = await this.romByte(rom, 200);
    }
    if (b !== 0x01) return null;
    for (;;) {
      if (b === 0x04) {                // EOT
        this.emit(new Uint8Array([0x06]));
        break;
      }
      if (b !== 0x01) {
        b = await this.romByte(rom, 2000);
        if (b === -1) return null;
        continue;
      }
      let rest;
      try {
        rest = await rom.inbox.read(132, 1000);
      } catch (e) {
        if (!(e instanceof TimeoutError)) throw e;
        this.emit(new Uint8Array([0x15]));
        b = await this.romByte(rom, 2000);
        continue;
      }
      const num = rest[0];
      const crcOk = crc16(rest.subarray(2, 130)) === ((rest[130] << 8) | rest[131]);
      if (this.faults.corruptFromHost > 0 && num === expect) {
        this.faults.corruptFromHost--;
        this.log("nak block from host");
        this.emit(new Uint8Array([0x15]));
      } else if (crcOk && num === (expect & 0xff) && rest[1] === (~expect & 0xff)) {
        blocks.push(rest.slice(2, 130));
        expect++;
        this.emit(new Uint8Array([0x06]));
      } else if (crcOk && num === ((expect - 1) & 0xff)) {
        this.emit(new Uint8Array([0x06]));   // a repeat of the last block: its ACK got lost
      } else {
        this.emit(new Uint8Array([0x15]));
      }
      b = await this.romByte(rom, 2000);
      if (b === -1) return null;
    }
    const data = concat(...blocks);
    if (data.length < size) this.fail(`S got ${data.length} bytes, wanted ${size}`);
    return data;
  }

  async xmodemSend(rom, data) {
    let b;
    do b = await this.romByte(rom, 10000); while (b !== 0x43 && b !== -1);
    if (b === -1) return;
    for (let n = 1, offset = 0; offset < data.length; n++, offset += 128) {
      const blk = new Uint8Array(133);
      blk[0] = 0x01;
      blk[1] = n & 0xff;
      blk[2] = ~n & 0xff;
      blk.set(data.subarray(offset, offset + 128), 3);
      const crc = crc16(blk.subarray(3, 131));
      blk[131] = crc >> 8;
      blk[132] = crc & 0xff;
      for (let tries = 0; ; tries++) {
        if (tries > 10) return;
        if (this.faults.corruptToHost > 0) {
          this.faults.corruptToHost--;
          this.log("sent corrupt block");
          const bad = blk.slice();
          bad[50] ^= 0xff;
          this.emit(bad);
        } else {
          this.emit(blk);
        }
        const reply = await this.romByte(rom, 2000);
        if (reply === 0x06) break;
      }
    }
    for (let tries = 0; tries < 10; tries++) {
      this.emit(new Uint8Array([0x04]));
      if ((await this.romByte(rom, 2000)) === 0x06) return;
    }
  }

  // G: SAM-BA loads the stack pointer and entry point from addr, like a vector table
  go(addr) {
    const sp = this.read32(addr);
    const pc = this.read32(addr + 4);
    const base = SAM3X8E.user;
    const code = new Uint8Array(WORD_COPY.code.length);
    for (let i = 0; i < code.length; i++) code[i] = this.read8(base + i);
    // the applet's literal words live inside the code area; compare only the instructions
    if (pc !== base + 1 || !equal(code.subarray(0, 0x20), WORD_COPY.code.subarray(0, 0x20)) || sp !== SAM3X8E.stack) {
      return this.fail(`G ${addr.toString(16)} ran something that isn't the word-copy applet (sp ${sp.toString(16)}, pc ${pc.toString(16)})`);
    }
    const dst = this.read32(base + WORD_COPY.dstAddr);
    const src = this.read32(base + WORD_COPY.srcAddr);
    const words = this.read32(base + WORD_COPY.words);
    for (let i = 0; i < words; i++) this.write32(dst + i * 4, this.read32(src + i * 4));
  }

  // ---------------------------------------------------------------- memory map

  region(addr) {
    if (addr >= 0x20000000 && addr < 0x20010000) return [this.sram, addr - 0x20000000];
    if (addr >= 0x20080000 && addr < 0x20088000) return [this.sram1, addr - 0x20080000];
    return null;
  }

  read8(addr, direct = false) {
    if (addr < 4) return (0x20001ff0 >>> (addr * 8)) & 0xff;   // ROM, seen at 0 while booting from ROM: its stack pointer
    if (addr < 0x10000) return (addr * 7 + 3) & 0xff;
    const r = this.region(addr);
    if (r) return r[0][r[1]];
    if (addr >= FLASH_BASE && addr < FLASH_BASE + FLASH_SIZE) return direct ? 0 : this.flash[addr - FLASH_BASE];
    const word = this.read32(addr & ~3, direct);
    return (word >>> ((addr & 3) * 8)) & 0xff;
  }

  read32(addr, direct = false) {
    if (addr < 0x10000 || this.region(addr) || (addr >= FLASH_BASE && addr < FLASH_BASE + FLASH_SIZE)) {
      if (addr & 3) this.fail(`unaligned read at ${addr.toString(16)}`);
      let v = 0;
      for (let i = 3; i >= 0; i--) v = (v << 8) | this.read8(addr + i, direct);
      return v >>> 0;
    }
    if (addr === 0xe000ed00) return 0x412fc230;               // CPUID: Cortex-M3 r2p0
    if (addr === 0x400e0740) return 0;
    if (addr === 0x400e0940) return this.faults.chipId;       // CHIPID_CIDR
    for (const plane of [0, 1]) {
      const base = 0x400e0a00 + plane * 0x200;
      const e = this.eefc[plane];
      if (addr === base) return e.fmr;
      if (addr === base + 8) {                                // FSR: reading clears the error flags
        let v = e.busy > 0 ? 0 : 1;
        if (e.busy > 0) e.busy--;
        v |= e.fsrErr;
        e.fsrErr = 0;
        return v;
      }
      if (addr === base + 0xc) return e.frr;
    }
    this.fail(`read from unmapped address ${addr.toString(16)}`);
    return 0;
  }

  write8(addr, value) {
    const r = this.region(addr);
    if (r) {
      r[0][r[1]] = value;
      return;
    }
    this.fail(`byte write to ${addr.toString(16)}`);
  }

  write32(addr, value) {
    value >>>= 0;
    const r = this.region(addr);
    if (r) {
      for (let i = 0; i < 4; i++) r[0][r[1] + i] = (value >>> (i * 8)) & 0xff;
      return;
    }
    if (addr >= FLASH_BASE && addr < FLASH_BASE + FLASH_SIZE) {
      const plane = addr >= FLASH_BASE + FLASH_SIZE / 2 ? 1 : 0;
      const e = this.eefc[plane];
      if (e.busy > 0) this.fail(`flash latch written while plane ${plane} was busy`);
      const offset = (addr - FLASH_BASE) % PAGE;
      for (let i = 0; i < 4; i++) e.latch[offset + i] = (value >>> (i * 8)) & 0xff;
      return;
    }
    for (const plane of [0, 1]) {
      const base = 0x400e0a00 + plane * 0x200;
      if (addr === base) {
        this.eefc[plane].fmr = value;
        return;
      }
      if (addr === base + 4) return this.flashCommand(plane, value);
    }
    if (addr === 0x400e1a00) {
      if (value >>> 24 !== 0xa5 || !(value & 1)) return this.fail(`bad reset request ${value.toString(16)}`);
      this.log("rstc reset");
      setTimeout(() => this.reset("rstc"), 20 * this.scale);
      return;
    }
    this.fail(`write to unmapped address ${addr.toString(16)}`);
  }

  flashCommand(plane, value) {
    const e = this.eefc[plane];
    if (value >>> 24 !== 0x5a) {
      e.fsrErr |= 2;                                          // FCMDE
      return;
    }
    if (e.busy > 0) return this.fail(`flash command sent to busy plane ${plane}`);
    if ((e.fmr >> 8 & 0xf) !== 6) this.fail(`flash command before FWS was set to 6 on plane ${plane}`);
    const cmd = value & 0xff;
    const arg = (value >>> 8) & 0xffff;
    const planeBase = plane * FLASH_SIZE / 2;
    switch (cmd) {
      case 0x1: case 0x3: {                                   // WP, EWP
        if (arg >= PLANE_PAGES) return this.fail(`page ${arg} out of range`);
        const page = plane * PLANE_PAGES + arg;
        if (this.locks[plane] & (1 << Math.floor(arg / 64))) {
          e.fsrErr |= 4;                                      // FLOCKE
          return;
        }
        const at = planeBase + arg * PAGE;
        for (let i = 0; i < PAGE; i++) {
          // a plain write can only clear bits; erase-and-write starts from 0xFF
          this.flash[at + i] = cmd === 0x3 ? e.latch[i] : this.flash[at + i] & e.latch[i];
        }
        if (this.faults.badPage === page) this.flash[at + 10] ^= 0x5a;
        e.latch.fill(0xff);
        e.busy = 2;
        this.log(`wp ${page}`);
        return;
      }
      case 0x5:                                               // EA
        this.flash.fill(0xff, planeBase, planeBase + FLASH_SIZE / 2);
        e.busy = 6;
        this.log(`ea ${plane}`);
        return;
      case 0xa:                                               // GLB
        e.frr = this.locks[plane];
        return;
      case 0xb:                                               // SGPB
        this.gpnvm |= 1 << arg;
        e.busy = 1;
        this.log(`sgpb ${arg}`);
        return;
      case 0xc:                                               // CGPB
        this.gpnvm &= ~(1 << arg);
        return;
      case 0xd:                                               // GGPB
        e.frr = this.gpnvm;
        return;
      default:
        this.fail(`unexpected flash command ${cmd.toString(16)}`);
    }
  }
}

// ---------------------------------------------------------------- the host side

export class SimPort {
  constructor(sim) {
    this.sim = sim;
    this.queue = new ByteQueue();
    this.isOpen = false;
    this.lost = false;
    this.opens = [];
  }

  async open(baudRate) {
    if (this.lost || !this.sim.present) throw new Error("Failed to open serial port.");
    if (this.isOpen) throw new Error("The port is already open.");
    this.queue.reset();
    this.isOpen = true;
    this.opens.push(baudRate);
    this.sim.hostOpen(this, baudRate);
  }

  async close() {
    if (!this.isOpen) return;
    this.isOpen = false;
    if (!this.lost && this.sim.port === this) this.sim.hostClose();
  }

  async setDTR(value) {
    if (this.lost) throw new PortLost();
    this.sim.setDTR(value);
  }

  async write(bytes) {
    if (this.lost) throw new PortLost();
    if (!this.isOpen) throw new Error("The port is closed.");
    this.sim.hostWrite(bytes);
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
}

// Hands out the LinnStrument's port; after a USB drop, a new port object, as Web Serial does
export class SimProvider {
  constructor(sim) {
    this.sim = sim;
    this.current = new SimPort(sim);
    this.ports = [this.current];
  }

  async waitForPort(timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    while (!this.sim.present) {
      if (Date.now() > deadline) throw new Error("The LinnStrument's serial port didn't come back.");
      await sleep(20);
    }
    if (this.current.lost) {
      this.current = new SimPort(this.sim);
      this.ports.push(this.current);
    }
    return this.current;
  }
}

// ---------------------------------------------------------------- a fake navigator.serial

// The Web Serial API over the simulator, enough for companion/flash/webserial.js: requestPort and
// getPorts, ports with readable and writable streams, setSignals, close (which refuses locked streams,
// as Chrome does), and disconnect events. After a USB drop the port comes back as a new object; with
// keepPermission false, it stays out of getPorts() until it's picked again, as Chrome does for devices
// without a serial number.
export class FakeWebSerial extends EventTarget {
  constructor(sim, { keepPermission = true } = {}) {
    super();
    this.sim = sim;
    this.keepPermission = keepPermission;
    this.port = null;
    this.permitted = false;
    this.requests = 0;
    const dropUsb = sim.dropUsb.bind(sim);
    sim.dropUsb = (ms) => {
      const port = this.port;
      dropUsb(ms);
      if (port) {
        port.simPort.queue.fail(new PortLost());
        const event = new Event("disconnect");
        Object.defineProperty(event, "target", { value: port });
        this.dispatchEvent(event);
      }
      this.port = null;
      if (!this.keepPermission) this.permitted = false;
    };
  }

  current() {
    if (!this.sim.present) return null;
    if (!this.port) this.port = new FakeSerialPort(this.sim);
    return this.port;
  }

  async requestPort({ filters }) {
    this.requests++;
    if (!filters.some((f) => f.usbVendorId === 0xf055)) throw new DOMException("No port selected by the user.", "NotFoundError");
    const port = this.current();
    if (!port) throw new DOMException("No port selected by the user.", "NotFoundError");
    this.permitted = true;
    return port;
  }

  async getPorts() {
    const port = this.permitted ? this.current() : null;
    return port ? [port] : [];
  }
}

class FakeSerialPort extends EventTarget {
  constructor(sim) {
    super();
    this.simPort = new SimPort(sim);
    this.readable = null;
    this.writable = null;
  }

  getInfo() {
    return { usbVendorId: 0xf055, usbProductId: 0x0070 };
  }

  async open({ baudRate }) {
    if (this.readable) throw new DOMException("The port is already open.", "InvalidStateError");
    try {
      await this.simPort.open(baudRate);
    } catch (e) {
      throw new DOMException("Failed to open serial port.", "NetworkError");
    }
    const lost = () => new DOMException("The device has been lost.", "NetworkError");
    const simPort = this.simPort;
    this.readable = new ReadableStream({
      async pull(controller) {
        try {
          controller.enqueue(await simPort.queue.readSome(1e9));
        } catch (e) {
          if (e instanceof PortLost) controller.error(lost());
          else controller.close();
        }
      },
      cancel() {
        simPort.queue.fail(new Error("cancelled"));
      },
    });
    this.writable = new WritableStream({
      async write(chunk) {
        if (simPort.lost) throw lost();
        await simPort.write(chunk);
      },
    });
  }

  async setSignals({ dataTerminalReady }) {
    if (!this.readable) throw new DOMException("The port is closed.", "InvalidStateError");
    if (this.simPort.lost) throw new DOMException("The device has been lost.", "NetworkError");
    if (dataTerminalReady !== undefined) await this.simPort.setDTR(dataTerminalReady);
  }

  async close() {
    if (!this.readable) throw new DOMException("The port is already closed.", "InvalidStateError");
    if (this.readable.locked || this.writable.locked) throw new TypeError("Cannot cancel a locked stream");
    this.readable = null;
    this.writable = null;
    await this.simPort.close();
  }
}

// for companion/flash/index.html?simulate: a LinnStrument with Harmony Board firmware in OS Update mode,
// as window.sim for poking at from the console (sim.faults, sim.events)
export function simulatedSerial(options = {}) {
  const sim = new SimLinnStrument({
    settings: randomBlob(11720, VERSION, 1),
    projects: Array.from({ length: 16 }, (_, i) => randomBlob(PROJECT_SIZE, i, 10 + i)),
    harmony: randomBlob(310, 3, 5),
    oldImage: fakeImage(30000, 1),
    newImage: null,
    ...options,
  });
  globalThis.sim = sim;
  return new FakeWebSerial(sim);
}
