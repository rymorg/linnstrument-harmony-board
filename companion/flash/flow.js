// The steps of a flash, in the order tools/flash_keep_settings.py runs them: back up, show WAIT and
// reset into the bootloader, write the firmware, wait for it to start, restore.
//
// Everything here talks to a port provider, so the same code runs on Web Serial in the browser and on
// the simulated LinnStrument in the tests. A provider has:
//   waitForPort(timeoutMs) -> Port     the LinnStrument's port, once it's connected (after a USB drop too)
// and a Port has:
//   open(baudRate), close(), setDTR(bool), write(bytes), read(n, timeoutMs), readSome(timeoutMs, max),
//   flushInput(), and `lost`, true once the USB device went away while the port was open.

import { sleep, now, PortLost } from "./bytes.js";
import {
  Backup, Connection, LinnStrumentError, NoAnswer, MIN_PROJECT_VERSION, SERIAL_BAUD, TIMING, withRetries,
} from "./linnstrument.js";
import { Samba, SambaError, WrongChip, checkImage, writeFirmware } from "./samba.js";

export async function backupDevice(provider, { timing = TIMING, log, progress, skipProjects = false } = {}) {
  const conn = await Connection.open(provider, { timing, log });
  try {
    const settings = await withRetries(() => conn.readSettings((done, total) => progress?.("settings", done, total)), conn);
    if (settings[0] < 1) throw new LinnStrumentError("the settings don't start with a format version");
    let projectSize = 0;
    let projects = [];
    if (!skipProjects) {
      try {
        ({ size: projectSize, projects } = await withRetries(
          () => conn.readProjects((done, total) => progress?.("projects", done, total)), conn));
      } catch (e) {
        if (!(e instanceof LinnStrumentError || e instanceof PortLost)) throw e;
        log?.(`Couldn't read the sequencer projects (${e.message}); continuing with settings only.`);
      }
    }
    let harmony = new Uint8Array(0);
    try {
      const got = await conn.readHarmony();
      if (got === null) log?.("No Harmony Board data: this firmware doesn't answer the harmony backup command.");
      else if (!got.length) log?.("No Harmony Board data: the LinnStrument reports nothing saved yet.");
      else harmony = got;
    } catch (e) {
      if (!(e instanceof LinnStrumentError || e instanceof PortLost)) throw e;
      log?.(`Couldn't read the Harmony Board data (${e.message}); continuing without it.`);
    }
    return new Backup(settings, projectSize, projects, harmony);
  } finally {
    await conn.close();
  }
}

// Open the port at 1200 baud and drop DTR, like arduino-cli's use_1200bps_touch. The LinnStrument's USB
// bridge then erases the SAM3X, firmware, settings and all, and starts its SAM-BA bootloader.
export async function touch1200(provider, { timing = TIMING, log } = {}) {
  const port = await provider.waitForPort(timing.portTimeout);
  await port.open(1200);
  try {
    await port.setDTR(false);
  } catch (e) {
    log?.(`Couldn't drop DTR (${e.message}).`);
  } finally {
    await port.close();
  }
  await sleep(timing.touchSettle);
}

// Show WAIT on Harmony Board firmware, then the 1200-baud touch. Web Serial can't change the speed of an
// open port, so unlike the Python helper this reopens it, which restarts the LinnStrument just before the
// erase; WAIT shows until then.
export async function resetIntoBootloader(provider, { timing = TIMING, log } = {}) {
  let conn = null;
  try {
    conn = await Connection.open(provider, { timing, log });
    if (await conn.prepareUpdate()) {
      log?.("The LinnStrument shows WAIT.");
      await sleep(timing.waitShown);   // long enough to read before the lights go out
    }
  } catch (e) {
    if (!(e instanceof LinnStrumentError || e instanceof PortLost)) throw e;
  } finally {
    if (conn) await conn.close();
  }
  await touch1200(provider, { timing, log });
}

async function tryBootloader(provider, until, { timing, log }) {
  while (now() < until) {
    const port = await provider.waitForPort(timing.portTimeout);
    try {
      await port.open(SERIAL_BAUD);
    } catch (e) {
      await sleep(1000);
      continue;
    }
    await sleep(timing.bootloaderSettle);
    if (!port.lost) {
      const samba = new Samba(port, { log });
      try {
        await samba.init();
        return samba;
      } catch (e) {
        if (e instanceof WrongChip) {
          await port.close();
          throw e;
        }
        if (!(e instanceof SambaError || e instanceof PortLost)) {
          await port.close();
          throw e;
        }
      }
    }
    await port.close();
    await sleep(1000);
  }
  return null;
}

// Poll until SAM-BA answers; if it doesn't, touch again (the usual way) and poll some more
export async function connectBootloader(provider, { timing = TIMING, log } = {}) {
  let samba = await tryBootloader(provider, now() + timing.bootloaderFirstTry, { timing, log });
  if (!samba) {
    log?.("The bootloader didn't answer yet; resetting again.");
    await touch1200(provider, { timing, log });
    samba = await tryBootloader(provider, now() + timing.bootloaderTimeout, { timing, log });
  }
  if (!samba) throw new LinnStrumentError("The LinnStrument's bootloader didn't answer.");
  log?.(`Bootloader answered: ${samba.version || "SAM-BA"}, chip ${samba.chip.toString(16)}, ${samba.mode} transfers.`);
  return samba;
}

export async function flashFirmware(provider, image, { timing = TIMING, log, progress } = {}) {
  checkImage(image);
  const samba = await connectBootloader(provider, { timing, log });
  try {
    return await writeFirmware(samba, image, { log, progress });
  } finally {
    await samba.port.close();
  }
}

// wait for the freshly flashed firmware, which starts in serial mode, to answer the countdown
export async function connectAfterBoot(provider, { timing = TIMING, log } = {}) {
  const deadline = now() + timing.bootTimeout;
  for (;;) {
    let conn = null;
    try {
      conn = await Connection.open(provider, { timing, log });
      if (await conn.startSession()) return conn;
    } catch (e) {
      if (!(e instanceof NoAnswer || e instanceof PortLost || e instanceof LinnStrumentError)) {
        if (conn) await conn.close();
        throw e;
      }
    }
    if (conn) await conn.close();
    if (now() > deadline) {
      throw new NoAnswer(`The new firmware didn't answer within ${Math.round(timing.bootTimeout / 1000)} seconds.`);
    }
    await sleep(1000);
  }
}

export async function restoreDevice(conn, backup, { log, progress, skipProjects = false } = {}) {
  if (backup.projects.length && !skipProjects) {
    if (backup.version < MIN_PROJECT_VERSION) {
      log?.(`Skipping sequencer projects: settings version ${backup.version} is too old for the firmware to accept them.`);
    } else {
      try {
        for (let i = 0; i < backup.projects.length; i++) {
          await withRetries(() => conn.writeProject(backup.version, i, backup.projects[i]), conn);
          progress?.("projects", (i + 1) * backup.projectSize, backup.projects.length * backup.projectSize);
        }
      } catch (e) {
        if (!(e instanceof LinnStrumentError || e instanceof PortLost)) throw e;
        log?.(`Couldn't restore the sequencer projects (${e.message}); restoring settings anyway.`);
      }
    }
  }
  if (backup.harmony.length) {
    try {
      await withRetries(() => conn.writeHarmony(backup.harmony), conn);
      log?.("Restored the Harmony Board's harmony, memories and page settings.");
    } catch (e) {
      if (!(e instanceof LinnStrumentError || e instanceof PortLost)) throw e;
      log?.(`Couldn't restore the Harmony Board data (${e.message}); restoring settings anyway.`);
    }
  }
  // this has to come last: once the settings are in, the firmware leaves serial mode
  await withRetries(() => conn.writeSettings(backup.settings, (done, total) => progress?.("settings", done, total)), conn);
}

// Compare the backup's settings size with the new firmware's (from its .elf); null when it can't tell
export function settingsSizeProblem(backup, expected) {
  if (!backup || !expected || expected === backup.settings.length) return null;
  return `The backup holds ${backup.settings.length} bytes of settings (format version ${backup.version}), but this ` +
    `firmware's settings are ${expected} bytes. Unless the firmware knows how to convert that older format, it will ` +
    "discard them and start from defaults, and you'll calibrate again.";
}

// The part of a flash that changes the LinnStrument, after the backup is saved. Errors carry `stage`:
// "start" (nothing changed), "flash" (it may be erased and waiting in its bootloader) or "restore" (the new firmware runs, the
// settings aren't back yet).
export async function flashAndRestore(provider, image, backup, { timing = TIMING, log, progress, step, skipProjects } = {}) {
  let stage = "start";
  try {
    checkImage(image);
    stage = "flash";
    step?.("reset");
    await resetIntoBootloader(provider, { timing, log });
    step?.("write");
    await flashFirmware(provider, image, { timing, log, progress });
    if (!backup) return;
    stage = "restore";
    step?.("restore");
    log?.("Waiting for the new firmware to start.");
    await sleep(timing.firstBootSettle);
    const conn = await connectAfterBoot(provider, { timing, log });
    try {
      await restoreDevice(conn, backup, { log, progress, skipProjects });
    } finally {
      await conn.close();
    }
  } catch (e) {
    e.stage = stage;
    throw e;
  }
}

export async function restoreOnly(provider, backup, { timing = TIMING, log, progress, skipProjects } = {}) {
  const conn = await Connection.open(provider, { timing, log });
  try {
    await restoreDevice(conn, backup, { log, progress, skipProjects });
  } finally {
    await conn.close();
  }
}
