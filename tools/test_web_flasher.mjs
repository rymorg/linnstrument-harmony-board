// Tests for the browser flasher (companion/flash/) against the simulated LinnStrument in
// web_flasher_sim.mjs. No hardware is touched.
//
//   node --test tools/test_web_flasher.mjs

import { afterEach, test } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

import { ascii, crc16, crc32, equal, u32le } from "../companion/flash/bytes.js";
import { Backup, NoAnswer, TIMING, settingsSizeInElf } from "../companion/flash/linnstrument.js";
import { SambaError, VerifyError, WrongChip, checkImage } from "../companion/flash/samba.js";
import { backupDevice, flashAndRestore, restoreOnly, settingsSizeProblem } from "../companion/flash/flow.js";
import { FakeWebSerial, PROJECT_SIZE, SimLinnStrument, SimProvider, fakeImage, firmwareCrc, randomBlob } from "./web_flasher_sim.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const SETTINGS_SIZE = 11720;   // sizeof(Configuration) in firmware 2.3.4, from the `config` symbol in the ELF
const SCALE = 0.1;
const FAST = { ...TIMING, firstBootSettle: 150, restartSettle: 200, deviceIdle: 200, waitShown: 50, touchSettle: 50, bootloaderSettle: 100,
  bootloaderFirstTry: 2500, bootloaderTimeout: 4000, bootTimeout: 6000, portTimeout: 4000, scale: SCALE };

const userSettings = randomBlob(SETTINGS_SIZE, 17, 1);
const userProjects = Array.from({ length: 16 }, (_, i) => randomBlob(PROJECT_SIZE, i, 10 + i));
const userHarmony = randomBlob(310, 3, 5);
const oldImage = fakeImage(30000, 1);
const newImage = fakeImage(40000 + 77, 2);     // not a whole number of pages

const devices = [];
afterEach(() => {
  while (devices.length) devices.pop().stop();
});

function makeDevice(options = {}) {
  const sim = new SimLinnStrument({ settings: userSettings, projects: userProjects, harmony: userHarmony,
    oldImage, newImage, scale: SCALE, ...options });
  devices.push(sim);
  return { sim, provider: new SimProvider(sim) };
}

// the firmware leaves serial mode a second (scaled) after it confirms the settings
const settle = () => new Promise((resolve) => setTimeout(resolve, 2000 * SCALE));

function quietLog() {
  const lines = [];
  const log = (line) => lines.push(line);
  log.lines = lines;
  return log;
}

function assertFlashed(sim) {
  assert.deepEqual(sim.errors, []);
  assert.ok(sim.flashHolds(newImage), "the flash holds the new firmware");
  assert.ok(sim.gpnvm & 0b010, "boots from flash");
  assert.equal(sim.running, "new");
}

function assertRestored(sim, { harmony = true } = {}) {
  assert.ok(equal(sim.data.settings, userSettings), "settings restored");
  for (let i = 0; i < 16; i++) assert.ok(equal(sim.data.projects[i], userProjects[i]), `project ${i} restored`);
  if (harmony) assert.ok(equal(sim.data.harmony, userHarmony), "harmony restored");
  assert.ok(sim.events.includes("restored settings"));
  assert.equal(sim.events.lastIndexOf("restored settings"), sim.events.findLastIndex((e) => e.startsWith("restored")),
    "settings come last");
}

function allOpensAt(provider, ...speeds) {
  for (const port of provider.ports) for (const baud of port.opens) assert.ok(speeds.includes(baud), `opened at ${baud}`);
}

// ------------------------------------------------------------------------------------------------

test("CRC-32 matches the firmware's nibble-table CRC, CRC-16 is XMODEM's", () => {
  for (const n of [0, 1, 4, 95, 96, 97, 300]) {
    const data = randomBlob(n, 0, n);
    assert.equal(crc32(data), firmwareCrc(data));
  }
  assert.equal(crc16(ascii("123456789")), 0x31c3);
});

test("backup files round-trip and match the Python helper's format", () => {
  const backup = new Backup(userSettings, PROJECT_SIZE, userProjects, userHarmony);
  const bytes = backup.toBytes();
  assert.equal(bytes.length, 28 + SETTINGS_SIZE + 16 * PROJECT_SIZE + userHarmony.length);
  const back = Backup.fromBytes(bytes);
  assert.ok(equal(back.settings, userSettings));
  assert.equal(back.projects.length, 16);
  assert.ok(equal(back.harmony, userHarmony));
  assert.match(back.describe(), /11720 bytes of settings \(format version 17\), 16 sequencer projects, and the Harmony/);

  const damaged = bytes.slice();
  damaged[100] ^= 1;
  assert.throws(() => Backup.fromBytes(damaged), /damaged/);
  assert.throws(() => Backup.fromBytes(new Uint8Array(0)), /neither/);
  const raw = Backup.fromBytes(userSettings);
  assert.equal(raw.projects.length, 0);

  // the Python helper reads what this writes, and the other way round (pyserial stubbed out)
  const dir = mkdtempSync(join(tmpdir(), "lsbackup-"));
  const fromJs = join(dir, "js.lsbackup");
  const fromPy = join(dir, "py.lsbackup");
  writeFileSync(fromJs, bytes);
  const script = `
import sys, types
serial = types.ModuleType("serial"); serial.SerialException = OSError
tools = types.ModuleType("serial.tools"); ports = types.ModuleType("serial.tools.list_ports")
tools.list_ports = ports; serial.tools = tools
sys.modules.update({"serial": serial, "serial.tools": tools, "serial.tools.list_ports": ports})
sys.path.insert(0, sys.argv[1])
import flash_keep_settings as f
b = f.Backup.load(sys.argv[2])
print(len(b.settings), len(b.projects), b.project_size, len(b.harmony), b.version)
f.Backup(b.settings, b.project_size, b.projects, b.harmony).save(sys.argv[3])
`;
  const out = execFileSync("python3", ["-I", "-B", "-c", script, here, fromJs, fromPy], { encoding: "utf8" });
  assert.equal(out.trim(), `${SETTINGS_SIZE} 16 ${PROJECT_SIZE} ${userHarmony.length} 17`);
  assert.ok(equal(new Uint8Array(readFileSync(fromPy)), bytes), "byte-identical to the Python helper's file");
});

test("the settings size comes from the config symbol in an .elf", () => {
  // a minimal 32-bit little-endian ELF with a symbol table, like make_elf() in the Python tests
  const strtab = ascii("\0Project\0config\0");
  const sym = (name, size) => {
    const s = new Uint8Array(16);
    const v = new DataView(s.buffer);
    v.setUint32(0, name, true); v.setUint32(4, 0x20070000, true); v.setUint32(8, size, true); s[12] = 0x11; v.setUint16(14, 1, true);
    return s;
  };
  const symtab = new Uint8Array([...new Uint8Array(16), ...sym(1, 4), ...sym(9, SETTINGS_SIZE)]);
  const shoff = 52 + symtab.length + strtab.length;
  const elf = new Uint8Array(shoff + 40 * 3);
  elf.set([0x7f, 0x45, 0x4c, 0x46, 1, 1, 1]);
  const v = new DataView(elf.buffer);
  v.setUint32(0x20, shoff, true); v.setUint16(0x2e, 40, true); v.setUint16(0x30, 3, true);
  elf.set(symtab, 52);
  elf.set(strtab, 52 + symtab.length);
  const section = (i, type, offset, size, link) => {
    const b = shoff + i * 40;
    v.setUint32(b + 4, type, true); v.setUint32(b + 16, offset, true); v.setUint32(b + 20, size, true); v.setUint32(b + 24, link, true);
  };
  section(1, 2, 52, symtab.length, 2);
  section(2, 3, 52 + symtab.length, strtab.length, 0);
  assert.equal(settingsSizeInElf(elf), SETTINGS_SIZE);
  assert.equal(settingsSizeInElf(ascii("not an elf")), null);

  const real = join(here, "..", "build", "linnstrument-firmware.ino.elf");
  if (existsSync(real)) assert.equal(settingsSizeInElf(new Uint8Array(readFileSync(real))), SETTINGS_SIZE);

  const backup = new Backup(userSettings);
  assert.equal(settingsSizeProblem(backup, SETTINGS_SIZE), null);
  assert.equal(settingsSizeProblem(backup, null), null);
  assert.match(settingsSizeProblem(backup, SETTINGS_SIZE + 8), /11720 bytes of settings .* 11728 bytes/);
});

test("images that aren't Due firmware are refused", () => {
  checkImage(newImage);
  assert.throws(() => checkImage(new Uint8Array(0)), /empty/);
  assert.throws(() => checkImage(ascii("<!doctype html><html>not found</html>")), /doesn't look like LinnStrument firmware/);
  assert.throws(() => checkImage(new Uint8Array(600000)), /flash holds 524288/);
});

test("backs up settings, projects and the Harmony block, despite corrupted batches", async () => {
  const { sim, provider } = makeDevice({ faults: { corruptSendBatches: new Set(["settings:3", "project:0"]) } });
  const backup = await backupDevice(provider, { timing: FAST, log: quietLog() });
  assert.ok(equal(backup.settings, userSettings));
  assert.equal(backup.projectSize, PROJECT_SIZE);
  assert.equal(backup.projects.length, 16);
  for (let i = 0; i < 16; i++) assert.ok(equal(backup.projects[i], userProjects[i]));
  assert.ok(equal(backup.harmony, userHarmony));
  assert.ok(sim.events.includes("corrupted settings:3"));
  assert.ok(sim.events.includes("corrupted project:0"));
  assert.ok(!sim.events.includes("erased"));
  assert.deepEqual(sim.errors, []);
});

test("a LinnStrument that isn't in OS Update mode isn't touched", async () => {
  const { sim, provider } = makeDevice();
  sim.data.serialMode = false;
  await assert.rejects(backupDevice(provider, { timing: FAST, log: quietLog() }), NoAnswer);
  assert.ok(!sim.events.includes("erased"));
});

test("backs up, flashes and restores Harmony Board firmware", async () => {
  const { sim, provider } = makeDevice({ faults: { rejectReceiveBatches: new Set(["settings:5", "project:2"]) } });
  const log = quietLog();
  const backup = await backupDevice(provider, { timing: FAST, log });
  const steps = [];
  const progress = [];
  await flashAndRestore(provider, newImage, Backup.fromBytes(backup.toBytes()), {
    timing: FAST, log, step: (s) => steps.push(s), progress: (what, done, total) => progress.push([what, done, total]) });
  assertFlashed(sim);
  assertRestored(sim);
  await settle();
  assert.deepEqual(steps, ["reset", "write", "restore"]);
  const order = ["showed wait", "erased", "ea 0", "ea 1", "sgpb 1", "rstc reset", "first boot", "restored harmony", "restored settings", "midi mode"];
  let at = -1;
  for (const e of order) {
    const i = sim.events.indexOf(e, at + 1);
    assert.ok(i > at, `${e} after the previous step (${sim.events.join(", ")})`);
    at = i;
  }
  const pages = Math.ceil(newImage.length / 256);
  assert.equal(sim.events.filter((e) => e.startsWith("wp ")).length, pages);
  assert.deepEqual(progress.filter(([w]) => w === "write").at(-1), ["write", pages, pages]);
  assert.deepEqual(progress.filter(([w]) => w === "verify").at(-1), ["verify", pages, pages]);
  assert.ok(log.lines.some((l) => /xmodem transfers/.test(l)), "talked XMODEM to the ROM");
  allOpensAt(provider, 115200, 1200);
});

test("updates from stock firmware, which has no WAIT and no Harmony block", async () => {
  const { sim, provider } = makeDevice({ harmony: null });
  const log = quietLog();
  const backup = await backupDevice(provider, { timing: FAST, log });
  assert.equal(backup.harmony.length, 0);
  assert.ok(log.lines.some((l) => /doesn't answer the harmony backup command/.test(l)));
  await flashAndRestore(provider, newImage, backup, { timing: FAST, log });
  assertFlashed(sim);
  assertRestored(sim, { harmony: false });
  assert.ok(!sim.events.includes("showed wait"));
});

test("XMODEM blocks corrupted in both directions are sent again", async () => {
  const { sim, provider } = makeDevice({ faults: { corruptToHost: 3, corruptFromHost: 3 } });
  const backup = await backupDevice(provider, { timing: FAST, log: quietLog() });
  await flashAndRestore(provider, newImage, backup, { timing: FAST, log: quietLog() });
  assertFlashed(sim);
  assert.equal(sim.events.filter((e) => e === "sent corrupt block").length, 3);
  assert.equal(sim.events.filter((e) => e === "nak block from host").length, 3);
});

test("a page that doesn't write fails the check, leaves the boot bit clear, and a second run fixes it", async () => {
  const { sim, provider } = makeDevice({ faults: { badPage: 17 } });
  const backup = await backupDevice(provider, { timing: FAST, log: quietLog() });
  const error = await flashAndRestore(provider, newImage, backup, { timing: FAST, log: quietLog() }).then(() => null, (e) => e);
  assert.ok(error instanceof VerifyError, String(error));
  assert.equal(error.stage, "flash");
  assert.match(error.message, /first: page 17/);
  assert.equal(sim.gpnvm & 0b010, 0, "still boots the ROM bootloader");
  assert.ok(!sim.events.includes("sgpb 1"));
  assert.equal(sim.program, "rom");
  assert.deepEqual(sim.errors, []);

  // the recovery path: flash again with the saved backup
  sim.faults.badPage = null;
  await flashAndRestore(provider, newImage, backup, { timing: FAST, log: quietLog() });
  assertFlashed(sim);
  assertRestored(sim);
});

test("if the first 1200-baud touch doesn't take, it touches again", async () => {
  const { sim, provider } = makeDevice({ faults: { ignoreTouches: 1 } });
  const log = quietLog();
  const backup = await backupDevice(provider, { timing: FAST, log });
  await flashAndRestore(provider, newImage, backup, { timing: FAST, log });
  assertFlashed(sim);
  assertRestored(sim);
  assert.ok(sim.events.includes("touch ignored"));
  assert.ok(log.lines.some((l) => /resetting again/.test(l)));
});

test("USB dropping out after the port opens is waited out", async () => {
  const { sim, provider } = makeDevice({ faults: { dropOnOpen: 3 } });
  const log = quietLog();
  const backup = await backupDevice(provider, { timing: FAST, log });
  await flashAndRestore(provider, newImage, backup, { timing: FAST, log });
  assertFlashed(sim);
  assertRestored(sim);
  assert.ok(sim.events.filter((e) => e === "usb dropped").length >= 3);
  assert.ok(provider.ports.length >= 3, "picked up the port again after each drop");
});

test("a bootloader that answers with raw bytes is written in binary", async () => {
  const { sim, provider } = makeDevice({ faults: { transfer: "binary" } });
  const log = quietLog();
  const backup = await backupDevice(provider, { timing: FAST, log });
  await flashAndRestore(provider, newImage, backup, { timing: FAST, log });
  assertFlashed(sim);
  assertRestored(sim);
  assert.ok(log.lines.some((l) => /binary transfers/.test(l)));
});

test("stray bytes before the bootloader's XMODEM data don't throw it off", async () => {
  const { sim, provider } = makeDevice({ faults: { junkBeforeRead: true } });
  const log = quietLog();
  await flashAndRestore(provider, newImage, null, { timing: FAST, log });
  assertFlashed(sim);
  assert.ok(log.lines.some((l) => /unexpected byte/.test(l)));
  assert.ok(log.lines.some((l) => /xmodem transfers/.test(l)));
});

test("another chip in the bootloader is never written", async () => {
  const { sim, provider } = makeDevice({ faults: { chipId: 0x284e0a60 } });
  const backup = await backupDevice(provider, { timing: FAST, log: quietLog() });
  const error = await flashAndRestore(provider, newImage, backup, { timing: FAST, log: quietLog() }).then(() => null, (e) => e);
  assert.ok(error instanceof WrongChip, String(error));
  assert.ok(!sim.events.some((e) => /^(ea|wp|sgpb|S )/.test(e)), "no erase, write or upload");
});

test("a bad image stops before anything is reset", async () => {
  const { sim, provider } = makeDevice();
  const error = await flashAndRestore(provider, ascii("<html>404</html>"), null, { timing: FAST, log: quietLog() }).then(() => null, (e) => e);
  assert.ok(error instanceof SambaError);
  assert.equal(error.stage, "start");
  assert.ok(!sim.events.includes("erased"));
  assert.deepEqual(provider.ports.flatMap((p) => p.opens), []);
});

test("flashes without a backup (FWUP mode: the firmware doesn't answer)", async () => {
  const { sim, provider } = makeDevice();
  sim.data.serialMode = false;          // the countdown gets no answer, but the 1200-baud touch still works
  await flashAndRestore(provider, newImage, null, { timing: FAST, log: quietLog() });
  assertFlashed(sim);
  await settle();
  assert.ok(sim.events.includes("first boot"));
  assert.ok(!sim.events.includes("showed wait"));
});

test("a LinnStrument already waiting in its bootloader is flashed and restored from a saved backup", async () => {
  const { sim, provider } = makeDevice();
  const backup = new Backup(userSettings, PROJECT_SIZE, userProjects, userHarmony);
  sim.eraseChip();                      // an earlier run stopped after the erase: the lights are dark
  await flashAndRestore(provider, newImage, backup, { timing: FAST, log: quietLog() });
  assertFlashed(sim);
  assertRestored(sim);
});

test("restores a backup on its own, settings last", async () => {
  const { sim, provider } = makeDevice();
  sim.data.settings = randomBlob(SETTINGS_SIZE, 17, 42);
  sim.data.projects = userProjects.map(() => new Uint8Array(PROJECT_SIZE));
  await restoreOnly(provider, new Backup(userSettings, PROJECT_SIZE, userProjects, userHarmony), { timing: FAST, log: quietLog() });
  assertRestored(sim);
  assert.deepEqual(sim.errors, []);
});

test("an image larger than one flash plane is written to both", async () => {
  const big = fakeImage(300 * 1024 + 5, 4);
  const { sim, provider } = makeDevice({ newImage: big });
  await flashAndRestore(provider, big, null, { timing: FAST, log: quietLog() });
  assert.deepEqual(sim.errors, []);
  assert.ok(sim.flashHolds(big));
  assert.ok(sim.events.includes("wp 1024") && sim.events.includes("wp 1200"), "pages on plane 1");
});

test("a full-size image writes and checks in reasonable time", async () => {
  const big = fakeImage(182000, 3);
  const { sim, provider } = makeDevice({ newImage: big });
  const started = Date.now();
  await flashAndRestore(provider, big, null, { timing: FAST, log: quietLog() });
  assert.deepEqual(sim.errors, []);
  assert.ok(sim.flashHolds(big));
  assert.ok(Date.now() - started < 60000);
});

// ------------------------------------------------------------------------------------------------
// the browser layer, through a fake navigator.serial

test("the Web Serial layer backs up, flashes and restores", async () => {
  const { WebSerialProvider } = await import("../companion/flash/webserial.js");
  const { sim } = makeDevice({ faults: { dropOnOpen: 1 } });
  const serial = new FakeWebSerial(sim);
  const provider = new WebSerialProvider({ serial });
  await provider.choose();
  const log = quietLog();
  const backup = await backupDevice(provider, { timing: FAST, log });
  await flashAndRestore(provider, newImage, backup, { timing: FAST, log });
  assertFlashed(sim);
  assertRestored(sim);
  assert.equal(serial.requests, 1);
});

test("when Chrome forgets the port across a reconnect, the user is asked to pick it again", async () => {
  const { WebSerialProvider } = await import("../companion/flash/webserial.js");
  const { sim } = makeDevice();
  const serial = new FakeWebSerial(sim, { keepPermission: false });
  let asked = 0;
  const provider = new WebSerialProvider({ serial, pickAgain: async () => { asked++; return serial.requestPort({ filters: [{ usbVendorId: 0xf055 }] }); } });
  await provider.choose();
  const backup = await backupDevice(provider, { timing: FAST, log: quietLog() });
  sim.faults.dropOnOpen = 1;            // the next open drops USB, and the permission with it
  await flashAndRestore(provider, newImage, backup, { timing: { ...FAST, portTimeout: 500 }, log: quietLog() });
  assertFlashed(sim);
  assertRestored(sim);
  assert.ok(asked >= 1);
});
