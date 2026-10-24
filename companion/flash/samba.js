// Writing the firmware: the SAM-BA protocol of the SAM3X8E's ROM bootloader, and its EEFC flash
// controller. A port of what Arduino's bossac 1.6.1 does for `bossac -e -w -v -b FILE -R` on an
// ATSAM3X8E (Samba.cpp, EefcFlash.cpp, Flash.cpp, Flasher.cpp, FlashFactory.cpp, WordCopyApplet).
//
// The LinnStrument's USB goes through a bridge chip to the SAM3X's UART, so this talks to SAM-BA the
// way bossac does on a serial link: the 0x80 0x80 # identification sequence at 115200 baud, then
// binary mode (N#), with file transfers (S and R commands) in XMODEM-CRC. In case the bootloader sends
// raw data instead, as it does on USB, a harmless read of four bytes decides which one it speaks.

import { ascii, concat, crc16, hex8, readU32le, sleep, now, text, u32le } from "./bytes.js";

const SOH = 0x01;
const EOT = 0x04;
const ACK = 0x06;
const NAK = 0x15;
const CAN = 0x18;
const START = 0x43;                  // 'C': the XMODEM receiver asks for CRC mode
const BLK_SIZE = 128;
const MAX_RETRIES = 5;
const TIMEOUT_QUICK = 100;
const TIMEOUT_NORMAL = 1000;

// bossac's FlashFactory entry for chip ID 0x285e0a60:
// EefcFlash(samba, "ATSAM3X8", 0x80000, 2048, 256, 2, 32, 0x20001000, 0x20010000, 0x400e0a00, false)
export const SAM3X8E = {
  chipId: 0x285e0a60,
  name: "ATSAM3X8E",
  flashAddr: 0x80000,
  pages: 2048,
  pageSize: 256,
  planes: 2,
  lockRegions: 32,
  user: 0x20001000,                  // where the copy applet goes, in SRAM
  stack: 0x20010000,
  regs: 0x400e0a00,                  // EEFC0; EEFC1 is 0x200 above
  bootFlashBit: 1,                   // GPNVM1: boot from flash (no brownout bits on the SAM3X)
};
const APPLET_MAX_SIZE = 0x1000;      // the page buffers sit right after the applet's space
const RSTC_CR = 0x400e1a00;
const RSTC_RESET = 0xa500000d;       // key, processor, peripheral and external reset (Samba::reset for the SAM3X8E)

const EEFC_KEY = 0x5a;
const FCMD_WP = 0x1;
const FCMD_EA = 0x5;
const FCMD_GLB = 0xa;
const FCMD_SGPB = 0xb;
const FCMD_GGPB = 0xd;
const FSR_FRDY = 1 << 0;
const FSR_FCMDE = 1 << 1;
const FSR_FLOCKE = 1 << 2;

// bossac's WordCopyArm applet: copies `words` words from src_addr to dst_addr, then returns to SAM-BA
export const WORD_COPY = {
  stack: 0x20, reset: 0x24, dstAddr: 0x28, srcAddr: 0x2c, words: 0x30, start: 0x00,
  code: new Uint8Array([
    0x09, 0x48, 0x0a, 0x49, 0x0a, 0x4a, 0x02, 0xe0, 0x08, 0xc9, 0x08, 0xc0, 0x01, 0x3a, 0x00, 0x2a,
    0xfa, 0xd1, 0x04, 0x48, 0x00, 0x28, 0x01, 0xd1, 0x01, 0x48, 0x85, 0x46, 0x70, 0x47, 0xc0, 0x46,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
  ]),
};

export class SambaError extends Error {
  constructor(message) {
    super(message);
    this.name = "SambaError";
  }
}

// Something other than a LinnStrument 200's processor answered: never write to it
export class WrongChip extends SambaError {}

export class VerifyError extends SambaError {}

// Check a firmware image before anything is erased: it has to fit, and start with a Cortex-M3 vector
// table for code linked at the start of the SAM3X's flash.
export function checkImage(image, dev = SAM3X8E) {
  if (!(image instanceof Uint8Array) || image.length < 8) throw new SambaError("The firmware file is empty.");
  const max = dev.pages * dev.pageSize;
  if (image.length > max) throw new SambaError(`The firmware file is ${image.length} bytes; the flash holds ${max}.`);
  const sp = readU32le(image, 0);
  const reset = readU32le(image, 4);
  const spOk = sp >= 0x20000000 && sp <= 0x20100000;
  const resetOk = (reset & 1) === 1 && reset >= dev.flashAddr && reset < dev.flashAddr + max;
  if (!spOk || !resetOk) {
    throw new SambaError("This doesn't look like LinnStrument firmware (a .bin built for the Arduino Due): " +
      `its first words are 0x${hex8(sp)} 0x${hex8(reset)}.`);
  }
}

export class Samba {
  constructor(port, { log } = {}) {
    this.port = port;
    this.log = log;
    this.mode = "xmodem";
    this.chip = 0;
    this.version = "";
  }

  async write(bytes) {
    await this.port.write(typeof bytes === "string" ? ascii(bytes) : bytes);
  }

  // up to n bytes, whatever arrives before the deadline (like bossac's SerialPort::read)
  async readUpTo(n, timeoutMs) {
    const deadline = now() + timeoutMs;
    let got = new Uint8Array(0);
    while (got.length < n) {
      const left = deadline - now();
      if (left <= 0) break;
      got = concat(got, await this.port.readSome(left, n - got.length));
    }
    return got;
  }

  async readExactly(n, timeoutMs, what) {
    const got = await this.readUpTo(n, timeoutMs);
    if (got.length !== n) throw new SambaError(`no answer from the bootloader (${what}: got ${got.length} of ${n} bytes)`);
    return got;
  }

  async getByte(timeoutMs) {
    const got = await this.port.readSome(timeoutMs, 1);
    return got.length ? got[0] : -1;
  }

  async drain(quietMs) {
    while ((await this.port.readSome(quietMs)).length) { /* discard */ }
  }

  // Samba::init: identify the interface, binary mode, then the chip. Throws WrongChip for a processor
  // that isn't a SAM3X8E, SambaError when nothing sensible answers.
  async init() {
    this.port.flushInput();
    await this.drain(TIMEOUT_QUICK);
    // the serial auto-baud sequence, which also tells SAM-BA it's talking over the UART
    await this.write(new Uint8Array([0x80]));
    await this.readUpTo(1, TIMEOUT_QUICK);
    await this.write(new Uint8Array([0x80]));
    await this.readUpTo(1, TIMEOUT_QUICK);
    await this.write("#");
    await this.readUpTo(3, TIMEOUT_QUICK);
    await this.write("N#");
    await this.readUpTo(2, TIMEOUT_QUICK);
    await this.drain(50);

    this.chip = await this.chipId();
    // bossac reads the version after the chip ID: "sam-ba did not answer correctly on some devices when used from UART" otherwise
    this.version = await this.readVersion();
    if (this.chip !== SAM3X8E.chipId) {
      throw new WrongChip(`The bootloader reports chip ID 0x${hex8(this.chip)}, not a LinnStrument 200's ` +
        `ATSAM3X8E (0x${hex8(SAM3X8E.chipId)}). Nothing was written.`);
    }
    await this.detectTransferMode();
    return true;
  }

  async readWord(addr) {
    await this.write(`w${hex8(addr)},4#`);
    return readU32le(await this.readExactly(4, TIMEOUT_NORMAL, `reading 0x${hex8(addr)}`));
  }

  async writeWord(addr, value) {
    await this.write(`W${hex8(addr)},${hex8(value)}#`);
  }

  async readByte(addr) {
    await this.write(`o${hex8(addr)},4#`);
    return (await this.readExactly(1, TIMEOUT_NORMAL, `reading 0x${hex8(addr)}`))[0];
  }

  async go(addr) {
    await this.write(`G${hex8(addr)}#`);
  }

  async chipId() {
    this.vector = await this.readWord(0x0);
    if ((this.vector & 0xff000000) >>> 0 === 0xea000000) {
      throw new WrongChip("An ARM7 SAM-BA bootloader answered, not a LinnStrument 200's. Nothing was written.");
    }
    const cpuid = await this.readWord(0xe000ed00);
    if ((cpuid & 0x0000fff0) === 0xc600) {
      throw new WrongChip("A Cortex-M0+ bootloader answered, not a LinnStrument 200's. Nothing was written.");
    }
    let cid = await this.readWord(0x400e0740);
    if (cid === 0) cid = await this.readWord(0x400e0940);
    return cid;
  }

  async readVersion() {
    await this.write("V#");
    let got = new Uint8Array(0);
    while (got.length < 255) {
      const chunk = await this.port.readSome(TIMEOUT_QUICK, 255 - got.length);
      if (!chunk.length) break;
      got = concat(got, chunk);
    }
    let end = 0;
    while (end < got.length && got[end] >= 0x20 && got[end] < 0x7f) end++;
    return text(got.subarray(0, end));
  }

  // Read the first word of memory with R, which SAM-BA answers in XMODEM on its UART and with raw
  // bytes on USB, and use whichever it does for the rest.
  async detectTransferMode() {
    await this.write(`R${hex8(0)},${hex8(4)}#`);
    const raw = await this.readUpTo(4, 300);
    let data;
    if (raw.length === 4 && readU32le(raw) === this.vector) {
      this.mode = "binary";
      data = raw;
    } else {
      if (raw.length) {
        // not the data: something else came first; an XMODEM sender waits for our 'C' regardless
        this.log?.(`The bootloader sent ${raw.length} unexpected byte(s) before the transfer check.`);
        await this.drain(TIMEOUT_QUICK);
      }
      this.mode = "xmodem";
      data = await this.readXmodem(4);
    }
    if (readU32le(data) !== this.vector) {
      throw new SambaError(`The bootloader's transfer check didn't match (0x${hex8(readU32le(data))}, expected ` +
        `0x${hex8(this.vector)}). Nothing was written.`);
    }
  }

  // Samba::write: S command, then the data
  async writeMemory(addr, data) {
    await this.write(`S${hex8(addr)},${hex8(data.length)}#`);
    if (this.mode === "binary") await this.write(data);
    else await this.writeXmodem(data);
  }

  // Samba::read: R command, then the data
  async readMemory(addr, size) {
    if (this.mode === "binary") {
      // SAM-BA over USB mis-sends powers of 2 over 32 bytes: read the first byte on its own
      let first = null;
      if (size > 32 && !(size & (size - 1))) {
        first = await this.readByte(addr);
        addr++;
        size--;
      }
      await this.write(`R${hex8(addr)},${hex8(size)}#`);
      const rest = await this.readExactly(size, TIMEOUT_NORMAL * 2, `reading 0x${hex8(addr)}`);
      return first === null ? rest : concat(new Uint8Array([first]), rest);
    }
    await this.write(`R${hex8(addr)},${hex8(size)}#`);
    return this.readXmodem(size);
  }

  // XMODEM-CRC sender (Samba::writeXmodem)
  async writeXmodem(data) {
    let started = false;
    for (let i = 0; i < MAX_RETRIES && !started; i++) started = (await this.getByte(TIMEOUT_NORMAL)) === START;
    if (!started) throw new SambaError("the bootloader didn't start the transfer");
    this.port.flushInput();          // more 'C's may follow the first one
    let blkNum = 1;
    for (let offset = 0; offset < data.length; offset += BLK_SIZE, blkNum++) {
      const blk = new Uint8Array(BLK_SIZE + 5);
      blk[0] = SOH;
      blk[1] = blkNum & 0xff;
      blk[2] = ~blkNum & 0xff;
      blk.set(data.subarray(offset, offset + BLK_SIZE), 3);   // the rest stays 0, as bossac pads it
      const crc = crc16(blk.subarray(3, 3 + BLK_SIZE));
      blk[BLK_SIZE + 3] = crc >> 8;
      blk[BLK_SIZE + 4] = crc & 0xff;
      let acked = false;
      for (let i = 0; i < MAX_RETRIES && !acked; i++) {
        await this.write(blk);
        const reply = await this.awaitReply(TIMEOUT_NORMAL);
        if (reply === CAN) throw new SambaError("the bootloader cancelled the transfer");
        acked = reply === ACK;
      }
      if (!acked) throw new SambaError(`the bootloader didn't accept block ${blkNum}`);
    }
    let done = false;
    for (let i = 0; i < MAX_RETRIES && !done; i++) {
      await this.write(new Uint8Array([EOT]));
      done = (await this.awaitReply(TIMEOUT_NORMAL)) === ACK;
    }
    if (!done) throw new SambaError("the bootloader didn't confirm the end of the transfer");
  }

  // the receiver's answer to a block: ACK, NAK or CAN, skipping any late 'C'
  async awaitReply(timeoutMs) {
    const deadline = now() + timeoutMs;
    for (;;) {
      const b = await this.getByte(Math.max(0, deadline - now()));
      if (b !== START) return b;
    }
  }

  // one XMODEM block, skipping anything before its SOH
  async readBlock(timeoutMs) {
    const deadline = now() + timeoutMs;
    for (;;) {
      const b = await this.getByte(Math.max(0, deadline - now()));
      if (b === -1) return new Uint8Array(0);
      if (b === SOH) break;
    }
    return concat(new Uint8Array([SOH]), await this.readUpTo(BLK_SIZE + 4, Math.max(0, deadline - now())));
  }

  // XMODEM-CRC receiver (Samba::readXmodem)
  async readXmodem(size) {
    const out = new Uint8Array(size);
    let blkNum = 1;
    for (let offset = 0; offset < size; offset += BLK_SIZE, blkNum++) {
      let blk = null;
      for (let i = 0; i < MAX_RETRIES && !blk; i++) {
        if (blkNum === 1) await this.write(new Uint8Array([START]));
        const got = await this.readBlock(TIMEOUT_NORMAL);
        if (got.length === BLK_SIZE + 5 && got[0] === SOH && got[1] === (blkNum & 0xff) &&
            crc16(got.subarray(3, 3 + BLK_SIZE)) === ((got[BLK_SIZE + 3] << 8) | got[BLK_SIZE + 4])) {
          blk = got;
          break;
        }
        await this.drain(TIMEOUT_QUICK);   // let the rest of a broken block pass before asking again
        if (blkNum !== 1) await this.write(new Uint8Array([NAK]));
      }
      if (!blk) throw new SambaError(`couldn't read block ${blkNum} from the bootloader`);
      await this.write(new Uint8Array([ACK]));
      out.set(blk.subarray(3, 3 + Math.min(BLK_SIZE, size - offset)), offset);
    }
    let done = false;
    for (let i = 0; i < MAX_RETRIES && !done; i++) {
      if ((await this.getByte(TIMEOUT_NORMAL)) === EOT) {
        await this.write(new Uint8Array([ACK]));
        done = true;
      } else {
        await this.write(new Uint8Array([NAK]));
      }
    }
    if (!done) throw new SambaError("the bootloader didn't end the transfer");
    return out;
  }

  // Samba::reset for the SAM3X8E
  async reset() {
    await this.writeWord(RSTC_CR, RSTC_RESET);
    await sleep(100);
  }
}

// The SAM3X8E's two flash planes behind the EEFC controllers (EefcFlash), written through bossac's
// word-copy applet and two page buffers in SRAM (Flash).
export class Sam3xFlash {
  constructor(samba, dev = SAM3X8E) {
    this.samba = samba;
    this.dev = dev;
    this.bufferA = dev.user + APPLET_MAX_SIZE;
    this.bufferB = this.bufferA + dev.pageSize;
    this.onBufferA = true;
  }

  // load the applet and set it up (the Flash and EefcFlash constructors)
  async setup() {
    const { samba, dev } = this;
    await samba.writeMemory(dev.user, WORD_COPY.code);
    await samba.writeWord(dev.user + WORD_COPY.words, dev.pageSize / 4);
    await samba.writeWord(dev.user + WORD_COPY.stack, dev.stack);
    // SAM3 errata: FWS must be 6
    await samba.writeWord(dev.regs + 0x00, 0x6 << 8);
    await samba.writeWord(dev.regs + 0x200, 0x6 << 8);
  }

  async waitFSR(timeoutMs = 2000) {
    const deadline = now() + timeoutMs;
    for (;;) {
      const fsr0 = await this.samba.readWord(this.dev.regs + 0x08);
      if (fsr0 & FSR_FLOCKE) throw new SambaError("the flash reported a write to a locked region");
      const fsr1 = await this.samba.readWord(this.dev.regs + 0x208);
      if (fsr1 & FSR_FLOCKE) throw new SambaError("the flash reported a write to a locked region");
      if ((fsr0 | fsr1) & FSR_FCMDE) this.samba.log?.("The flash controller flagged a command error.");
      if (fsr0 & fsr1 & FSR_FRDY) return;
      // no sleep between polls: each one is a serial round trip already, and Chrome stretches timers in
      // background tabs to a second
      if (now() > deadline) throw new SambaError("the flash controller stayed busy");
    }
  }

  async writeFCR(plane, cmd, arg) {
    await this.samba.writeWord(this.dev.regs + plane * 0x200 + 0x04, ((EEFC_KEY << 24) | (arg << 8) | cmd) >>> 0);
  }

  async readFRR(plane) {
    return this.samba.readWord(this.dev.regs + plane * 0x200 + 0x0c);
  }

  // lock bits of both planes; any set bit means a region bossac would fail to write
  async lockBits() {
    const bits = [];
    for (let plane = 0; plane < this.dev.planes; plane++) {
      await this.waitFSR();
      await this.writeFCR(plane, FCMD_GLB, 0);
      await this.waitFSR();
      bits.push(await this.readFRR(plane));
    }
    return bits;
  }

  async eraseAll() {
    for (let plane = 0; plane < this.dev.planes; plane++) {
      await this.waitFSR(10000);
      await this.writeFCR(plane, FCMD_EA, 0);
    }
    await this.waitFSR(10000);
  }

  // Applet::runv: SAM-BA's G command takes a vector table: the stack, then the entry point (Thumb)
  async runApplet() {
    await this.samba.writeWord(this.dev.user + WORD_COPY.reset, this.dev.user + WORD_COPY.start + 1);
    await this.samba.go(this.dev.user + WORD_COPY.stack);
  }

  // Flash::loadBuffer and EefcFlash::writePage (after an erase, so a plain write, not erase-and-write)
  async writePage(page, data) {
    const { samba, dev } = this;
    const buffer = this.onBufferA ? this.bufferA : this.bufferB;
    await samba.writeMemory(buffer, data);
    await samba.writeWord(dev.user + WORD_COPY.dstAddr, dev.flashAddr + page * dev.pageSize);
    await samba.writeWord(dev.user + WORD_COPY.srcAddr, buffer);
    this.onBufferA = !this.onBufferA;
    await this.waitFSR();
    await this.runApplet();
    const perPlane = dev.pages / dev.planes;
    const plane = page >= perPlane ? 1 : 0;
    await this.writeFCR(plane, FCMD_WP, page - plane * perPlane);
  }

  // EefcFlash::readPage: SAM-BA reads flash as zeros, so copy the page to SRAM and read it from there
  async readPage(page) {
    const { samba, dev } = this;
    const buffer = this.onBufferA ? this.bufferA : this.bufferB;
    await samba.writeWord(dev.user + WORD_COPY.dstAddr, buffer);
    await samba.writeWord(dev.user + WORD_COPY.srcAddr, dev.flashAddr + page * dev.pageSize);
    await this.waitFSR();
    await this.runApplet();
    return samba.readMemory(buffer, dev.pageSize);
  }

  async gpnvm() {
    await this.waitFSR();
    await this.writeFCR(0, FCMD_GGPB, 0);
    await this.waitFSR();
    return this.readFRR(0);
  }

  async setBootFlash() {
    await this.waitFSR();
    await this.writeFCR(0, FCMD_SGPB, this.dev.bootFlashBit);
    await this.waitFSR();
    await sleep(10);
  }
}

// Erase, write, read back and compare every page, then set boot-from-flash and reset: bossac's
// -e -w -v -b -R. The boot bit is only set once every byte matches, so a failed run leaves the
// LinnStrument in its ROM bootloader, ready for another try.
export async function writeFirmware(samba, image, { log, progress } = {}) {
  checkImage(image);
  const flash = new Sam3xFlash(samba);
  const { pageSize } = flash.dev;
  const pages = Math.ceil(image.length / pageSize);
  const padded = new Uint8Array(pages * pageSize).fill(0xff);
  padded.set(image);

  await flash.setup();
  const locks = await flash.lockBits();
  if (locks.some((bits) => bits !== 0)) {
    throw new SambaError(`Some flash regions are locked (0x${locks.map(hex8).join(", 0x")}); not erasing.`);
  }

  log?.("Erasing the flash.");
  await flash.eraseAll();

  log?.(`Writing ${image.length} bytes (${pages} pages).`);
  for (let page = 0; page < pages; page++) {
    await flash.writePage(page, padded.subarray(page * pageSize, (page + 1) * pageSize));
    progress?.("write", page + 1, pages);
  }

  log?.("Reading it back to check every byte.");
  const bad = [];
  for (let page = 0; page < pages; page++) {
    const got = await flash.readPage(page);
    const want = padded.subarray(page * pageSize, (page + 1) * pageSize);
    for (let i = 0; i < pageSize; i++) {
      if (got[i] !== want[i]) {
        bad.push(page);
        break;
      }
    }
    progress?.("verify", page + 1, pages);
  }
  if (bad.length) {
    throw new VerifyError(`The check found ${bad.length} page${bad.length > 1 ? "s" : ""} that didn't write correctly ` +
      `(first: page ${bad[0]}). The LinnStrument stays in its bootloader; flash again.`);
  }

  await flash.setBootFlash();
  const bits = await flash.gpnvm();
  if (!(bits & (1 << flash.dev.bootFlashBit))) {
    throw new SambaError("The boot-from-flash setting didn't stick (GPNVM 0x" + hex8(bits) + "); flash again.");
  }
  log?.("Firmware written and checked; starting it.");
  await samba.reset();
  return { pages, bytes: image.length };
}
