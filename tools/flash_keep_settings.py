#!/usr/bin/env python3
"""Flash LinnStrument firmware without losing its settings and calibration.

Resetting a LinnStrument into its bootloader erases the whole flash, settings
and calibration included, so a plain bossac or arduino-cli upload always starts
from factory defaults. The official LinnStrument Updater avoids that by reading
the settings over serial before flashing and sending them back once the new
firmware boots. This script does the same, using the firmware's own settings
transfer protocol (ls_serial.ino):

  1. Back up the settings (and the 16 sequencer projects) from a LinnStrument
     in OS Update mode, and save them to a file.
  2. Reset it into the bootloader with a 1200-baud touch and flash a .bin with
     the Arduino bossac.
  3. Send the backup to the new firmware, which waits for it in serial mode on
     its first boot after a flash.

Usage:
  flash_keep_settings.py build/linnstrument-firmware.ino.bin
  flash_keep_settings.py --backup-only [-o FILE]
  flash_keep_settings.py --restore FILE
  flash_keep_settings.py --restore FILE build/linnstrument-firmware.ino.bin

Before running it, turn on OS Update: press Global Settings, then briefly press
OS Update (column 16, third pad from the bottom) so its light turns on.

Needs pyserial:
  python3 -m venv tools/.venv
  tools/.venv/bin/pip install -r tools/requirements.txt
"""

import argparse
import datetime
import glob
import os
import shlex
import struct
import subprocess
import sys
import termios
import time
import zlib

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("This script needs pyserial. Set it up once with:\n"
             "  python3 -m venv tools/.venv\n"
             "  tools/.venv/bin/pip install -r tools/requirements.txt\n"
             "then run it with tools/.venv/bin/python tools/flash_keep_settings.py")


# Settings transfer protocol, from ls_serial.ino
COUNTDOWN = b"5, 4, 3, 2, 1 ...\n"   # host -> device: open a command session
GO = b"LinnStruments are go!\n"      # device -> host: ready for one command
ACK = b"ACK\n"
SEND_SETTINGS = b"s"
RESTORE_SETTINGS = b"r"
SEND_PROJECTS = b"p"
RESTORE_PROJECT = b"q"
SEND_HARMONY = b"x"                  # Harmony Board firmware only: its saved harmony block
RESTORE_HARMONY = b"y"
CRC_CHECK = b"c"                     # asks for the CRC of the batch just sent
CRC_WRONG = b"w"                     # the batch gets sent again
CRC_OK = b"o"
BATCH_SIZE = 96                      # data travels in batches of 96 bytes, each with a CRC
RESYNC = b"\xff"                     # not a command: ends a stale session and resets the countdown match
DEVICE_IDLE_TIMEOUT = 2.0            # the firmware abandons a transfer after 2 s without data
MIN_PROJECT_VERSION = 9              # serialRestoreProject() ignores projects from older settings versions
SERIAL_BAUD = 115200
RESTART_SETTLE = 2.0                 # opening the port restarts the LinnStrument, which sometimes drops USB for a second or two
PORT_ERRORS = (serial.SerialException, termios.error, OSError)

BACKUP_DIR = os.path.expanduser("~/linnstrument-backups")
BACKUP_MAGIC = b"LSBACKUP"
BACKUP_FORMAT = 2
# magic, format, settings version, project count, settings size, project size, CRC-32 of the payload (format 1)
BACKUP_HEADER_1 = struct.Struct("<8sHBBIII")
# format 2 adds the size of the Harmony Board block, stored after the projects
BACKUP_HEADER = struct.Struct("<8sHBBIIII")

BOSSAC_GLOB = "~/Library/Arduino15/packages/arduino/tools/bossac/*/bossac"

OS_UPDATE_HINT = ("Turn on OS Update: press Global Settings, then briefly press OS Update "
                  "(column 16, third pad from the bottom) so its light turns on.")


class LinnStrumentError(Exception):
    pass


class NoAnswer(LinnStrumentError):
    """The firmware didn't answer the countdown: it isn't in serial mode, or isn't running."""


class NotSupported(NoAnswer):
    """The firmware doesn't know the command, so there's no point retrying it."""


# ------------------------------------------------------------------------------------------------
# Serial protocol
# ------------------------------------------------------------------------------------------------

class Connection:
    """The settings transfer protocol over an open serial port."""

    def __init__(self, port):
        self.port = port
        self.ser = None
        self.open()

    def open(self, attempts=4):
        """Open the port and wait out the restart that opening it causes.

        The USB bridge restarts the LinnStrument whenever the port is opened. The firmware comes back
        in serial mode, but sometimes the USB device drops out while it restarts, so wait, and open
        the port again once it's back."""
        for _ in range(attempts):
            wait_for_port(self.port, time.monotonic() + 15)
            try:
                self.ser = serial.Serial(self.port, SERIAL_BAUD, timeout=0.1, write_timeout=5)
            except serial.SerialException:
                time.sleep(1.0)
                continue
            deadline = time.monotonic() + RESTART_SETTLE
            while time.monotonic() < deadline and os.path.exists(self.port):
                time.sleep(0.05)
            if os.path.exists(self.port):
                try:
                    self.ser.reset_input_buffer()
                    return
                except PORT_ERRORS:
                    pass
            self.close()
        raise NoAnswer("The LinnStrument's serial port on %s kept dropping out." % self.port)

    def reopen(self):
        self.close()
        self.open()

    def close(self):
        try:
            if self.ser:
                self.ser.close()
        except PORT_ERRORS:
            pass                         # the port goes away when the firmware leaves serial mode

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _write(self, data):
        self.ser.write(data)
        self.ser.flush()

    def _read(self, n, what, timeout=3.0):
        deadline = time.monotonic() + timeout
        buf = bytearray()
        while len(buf) < n:
            chunk = self.ser.read(n - len(buf))
            if chunk:
                buf += chunk
            elif time.monotonic() > deadline:
                raise LinnStrumentError("timed out waiting for %s (got %d of %d bytes)" % (what, len(buf), n))
        return bytes(buf)

    def _expect(self, token, what, timeout=3.0):
        got = self._read(len(token), what, timeout)
        if got != token:
            raise LinnStrumentError("expected %s %r, got %r" % (what, token, got))

    def start_session(self, timeout=2.0):
        """Send the countdown and wait for the go reply (handleSerialIO in ls_serial.ino)."""
        self.ser.reset_input_buffer()
        self._write(RESYNC + COUNTDOWN)
        deadline = time.monotonic() + timeout
        seen = bytearray()
        while time.monotonic() < deadline:
            seen += self.ser.read(64)
            if GO in seen:
                return True
        return False

    def handshake(self, attempts=3):
        for _ in range(attempts):
            if self.start_session():
                return
            # let any transfer the firmware is still stuck in time out before trying again
            time.sleep(DEVICE_IDLE_TIMEOUT + 0.3)
        raise NoAnswer("The LinnStrument didn't answer on %s. %s" % (self.port, OS_UPDATE_HINT))

    def _receive(self, size, progress=None):
        """Receive size bytes in CRC-checked batches (the host side of serialSendSettings)."""
        data = bytearray()
        resends = 0
        while len(data) < size:
            batch = self._read(min(BATCH_SIZE, size - len(data)), "data")
            self._write(CRC_CHECK)
            (remote_crc,) = struct.unpack("<I", self._read(4, "batch CRC"))
            if zlib.crc32(batch) != remote_crc:
                resends += 1
                if resends > 10:
                    raise LinnStrumentError("too many corrupted batches")
                self._write(CRC_WRONG)
                continue
            self._write(CRC_OK)
            data += batch
            if progress:
                progress.update(len(data))
        return bytes(data)

    def _send(self, data, progress=None):
        """Send data in CRC-checked batches (the host side of serialRestoreSettings)."""
        offset = 0
        resends = 0
        while offset < len(data):
            batch = data[offset:offset + BATCH_SIZE]
            self._write(batch)
            self._expect(CRC_CHECK, "CRC request")
            self._write(struct.pack("<I", zlib.crc32(batch)))
            reply = self._read(1, "CRC verdict")
            if reply == CRC_WRONG:
                resends += 1
                if resends > 10:
                    raise LinnStrumentError("too many corrupted batches")
                continue
            if reply != CRC_OK:
                raise LinnStrumentError("unexpected CRC verdict %r" % reply)
            offset += len(batch)
            if progress:
                progress.update(offset)

    def read_settings(self):
        self.handshake()
        self._write(SEND_SETTINGS)
        self._expect(ACK, "acknowledgement")
        (size,) = struct.unpack("<i", self._read(4, "settings size"))
        if not 0 < size <= 64 * 1024:
            raise LinnStrumentError("implausible settings size %d" % size)
        progress = Progress("Reading settings", size)
        data = self._receive(size, progress)
        progress.finish()
        self._expect(ACK, "end of settings")
        return data

    def read_projects(self):
        self.handshake()
        self._write(SEND_PROJECTS)
        self._expect(ACK, "acknowledgement")
        count = self._read(1, "project count")[0]
        (size,) = struct.unpack("<i", self._read(4, "project size"))
        if not 0 < size <= 64 * 1024:
            raise LinnStrumentError("implausible project size %d" % size)
        progress = Progress("Reading sequencer projects", count * size)
        projects = []
        for _ in range(count):
            projects.append(self._receive(size))
            progress.update(len(projects) * size)
        progress.finish()
        self._expect(ACK, "end of projects")
        return size, projects

    def write_project(self, version, index, data):
        self.handshake()
        self._write(RESTORE_PROJECT)
        self._expect(ACK, "acknowledgement")
        self._write(bytes([version]))
        self._expect(ACK, "acceptance of the project version")
        self._write(struct.pack("<i", len(data)))
        try:
            self._expect(ACK, "acceptance of the project size")
        except LinnStrumentError:
            raise LinnStrumentError("the firmware rejected the %d-byte project: its sequencer format differs" % len(data))
        self._write(bytes([index]))
        self._expect(ACK, "acknowledgement of the project number")
        self._send(data)
        self._expect(ACK, "end of project")

    def read_harmony(self):
        """The Harmony Board's saved block (ls_serial.ino), b"" when nothing is saved, None when the firmware
        doesn't know the command."""
        self.handshake()
        self._write(SEND_HARMONY)
        try:
            self._expect(ACK, "acknowledgement", timeout=1.0)
        except LinnStrumentError:
            return None
        (size,) = struct.unpack("<i", self._read(4, "harmony size"))
        if not 0 <= size <= 4096:
            raise LinnStrumentError("implausible harmony size %d" % size)
        data = self._receive(size) if size else b""
        self._expect(ACK, "end of harmony")
        return data

    def write_harmony(self, data):
        self.handshake()
        self._write(RESTORE_HARMONY)
        try:
            self._expect(ACK, "acknowledgement", timeout=1.0)
        except LinnStrumentError:
            raise NotSupported("this firmware doesn't take Harmony Board data")
        self._write(struct.pack("<i", len(data)))
        self._expect(ACK, "acknowledgement of the harmony size")
        self._send(data)
        self._expect(ACK, "confirmation", timeout=5)

    def write_settings(self, data):
        self.handshake()
        self._write(RESTORE_SETTINGS)
        self._expect(ACK, "acknowledgement")
        self._write(struct.pack("<i", len(data)))
        self._expect(ACK, "acknowledgement of the settings size")
        progress = Progress("Restoring settings", len(data))
        self._send(data, progress)
        progress.finish()
        # the firmware applies the settings, then answers and leaves serial mode
        self._expect(ACK, "confirmation", timeout=10)


def with_retries(operation, conn, attempts=3):
    """Run a transfer again, on a fresh connection, if it fails midway; a missing device isn't retried."""
    for attempt in range(attempts):
        try:
            return operation()
        except NoAnswer:
            raise
        except (LinnStrumentError,) + PORT_ERRORS as e:
            if attempt == attempts - 1:
                raise
            print("    Transfer failed (%s), trying again..." % e)
            time.sleep(DEVICE_IDLE_TIMEOUT + 0.5)
            conn.reopen()


class Progress:
    def __init__(self, label, total):
        self.label = label
        self.total = total
        self.tty = sys.stdout.isatty()

    def update(self, done):
        if self.tty:
            print("\r    %s: %d/%d bytes" % (self.label, done, self.total), end="", flush=True)

    def finish(self):
        print("\r    %s: %d/%d bytes" % (self.label, self.total, self.total))


# ------------------------------------------------------------------------------------------------
# Backup files
# ------------------------------------------------------------------------------------------------

class Backup:
    """The settings blob exactly as the firmware sends it (a struct Configuration), plus projects."""

    def __init__(self, settings, project_size=0, projects=(), harmony=b""):
        self.settings = settings
        self.project_size = project_size
        self.projects = list(projects)
        self.harmony = harmony or b""

    @property
    def version(self):
        return self.settings[0]          # DeviceSettings.version, the settings format version

    def describe(self):
        text = "%d bytes of settings (format version %d)" % (len(self.settings), self.version)
        if self.projects:
            text += ", %d sequencer projects" % len(self.projects)
        if self.harmony:
            text += ", and the Harmony Board's harmony, memories and page settings"
        return text

    def save(self, path):
        payload = self.settings + b"".join(self.projects) + self.harmony
        header = BACKUP_HEADER.pack(BACKUP_MAGIC, BACKUP_FORMAT, self.version, len(self.projects),
                                    len(self.settings), self.project_size, len(self.harmony), zlib.crc32(payload))
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        tmp = path + ".part"
        with open(tmp, "wb") as f:
            f.write(header + payload)
        os.replace(tmp, path)

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            data = f.read()
        if not data.startswith(BACKUP_MAGIC):
            # a raw settings blob, as saved by other tools
            if not data or not 1 <= data[0] <= 64 or len(data) > 64 * 1024:
                raise LinnStrumentError("%s is neither a backup from this script nor a raw settings blob" % path)
            return cls(data)
        if len(data) < BACKUP_HEADER_1.size:
            raise LinnStrumentError("%s is truncated" % path)
        (fmt,) = struct.unpack_from("<H", data, 8)
        if fmt == 1:
            magic, fmt, _, count, settings_size, project_size, crc = BACKUP_HEADER_1.unpack_from(data)
            harmony_size, payload = 0, data[BACKUP_HEADER_1.size:]
        elif fmt == 2 and len(data) >= BACKUP_HEADER.size:
            magic, fmt, _, count, settings_size, project_size, harmony_size, crc = BACKUP_HEADER.unpack_from(data)
            payload = data[BACKUP_HEADER.size:]
        else:
            raise LinnStrumentError("%s uses backup format %d, this script reads formats 1 and %d" % (path, fmt, BACKUP_FORMAT))
        projects_end = settings_size + count * project_size
        if len(payload) != projects_end + harmony_size or zlib.crc32(payload) != crc:
            raise LinnStrumentError("%s is damaged (size or checksum mismatch)" % path)
        settings = payload[:settings_size]
        projects = [payload[settings_size + i * project_size:settings_size + (i + 1) * project_size]
                    for i in range(count)]
        return cls(settings, project_size, projects, payload[projects_end:])


def default_backup_path():
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    return os.path.join(BACKUP_DIR, "linnstrument-%s.lsbackup" % stamp)


# ------------------------------------------------------------------------------------------------
# Ports, bootloader and bossac
# ------------------------------------------------------------------------------------------------

def candidate_ports():
    ports = list(list_ports.comports())
    named = [p for p in ports
             if "linnstrument" in " ".join(filter(None, (p.description, p.product, p.manufacturer))).lower()]
    if named:
        return named
    return [p for p in ports if os.path.basename(p.device).startswith("cu.usbmodem")]


def find_port(requested):
    if requested:
        return requested if requested.startswith("/") else "/dev/" + requested
    found = candidate_ports()
    if len(found) == 1:
        return found[0].device
    if not found:
        raise NoAnswer("No LinnStrument serial port found. " + OS_UPDATE_HINT)
    raise LinnStrumentError("Several serial ports could be the LinnStrument; pick one with --port:\n" +
                            "\n".join("  %s  %s" % (p.device, p.description) for p in found))


def wait_for_port(requested, deadline):
    while True:
        try:
            port = find_port(requested)
            if os.path.exists(port):
                return port
        except NoAnswer:
            pass
        if time.monotonic() > deadline:
            raise NoAnswer("The LinnStrument's serial port didn't come back.")
        time.sleep(0.5)


def connect_after_boot(requested, previous, timeout=60.0):
    """Wait for the freshly flashed firmware, which starts in serial mode, to answer the countdown."""
    deadline = time.monotonic() + timeout
    while True:
        port = previous if not requested and os.path.exists(previous) else wait_for_port(requested, deadline)
        try:
            conn = Connection(port)
        except (NoAnswer,) + PORT_ERRORS:
            conn = None
        if conn:
            try:
                if conn.start_session():
                    return conn
            except PORT_ERRORS:
                pass
            conn.close()
        if time.monotonic() > deadline:
            raise NoAnswer("The new firmware didn't answer on %s within %d seconds." % (port, timeout))
        time.sleep(1.0)


def touch_1200(port):
    """Open and close the port at 1200 baud with DTR dropped, like arduino-cli's use_1200bps_touch.

    The LinnStrument's USB bridge then erases the SAM3X, firmware, settings and all, and it
    starts its SAM-BA bootloader."""
    with serial.Serial(port, 1200) as s:
        try:
            s.dtr = False
        except (OSError, serial.SerialException) as e:
            print("    Warning: couldn't drop DTR (%s)" % e)
    time.sleep(0.5)


def find_bossac(requested):
    if requested:
        if not os.access(requested, os.X_OK):
            raise LinnStrumentError("bossac not found at %s" % requested)
        return requested
    matches = sorted(glob.glob(os.path.expanduser(BOSSAC_GLOB)))
    if not matches:
        raise LinnStrumentError("bossac not found. Install the Arduino Due core with "
                                "`arduino-cli core install arduino:sam@1.6.12`, or pass --bossac.")
    return matches[-1]


def bossac_port(port):
    # like arduino-cli's {serial.port.file}: bossac adds /dev/ itself
    return os.path.basename(port) if port.startswith("/dev/") else port


def wait_for_bootloader(bossac, port, timeout=20.0):
    """Poll the read-only `bossac -i` until SAM-BA answers."""
    deadline = time.monotonic() + timeout
    cmd = [bossac, "--port=%s" % bossac_port(port), "-U", "true", "-i"]
    while True:
        try:
            result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    universal_newlines=True, timeout=15)
            output = result.stdout.strip()
            if result.returncode == 0:
                return output
        except subprocess.TimeoutExpired:
            output = "bossac -i timed out"
        if time.monotonic() > deadline:
            raise LinnStrumentError("The bootloader didn't answer on %s:\n%s" % (port, output))
        time.sleep(1.0)


def run_bossac(bossac, port, bin_path):
    cmd = [bossac, "--port=%s" % bossac_port(port), "-U", "true", "-e", "-w", "-v", "-b", bin_path, "-R"]
    print("    $ " + " ".join(shlex.quote(c) for c in cmd))
    sys.stdout.flush()
    try:
        result = subprocess.run(cmd, timeout=300)
    except subprocess.TimeoutExpired:
        raise LinnStrumentError("bossac didn't finish within 5 minutes")
    if result.returncode != 0:
        raise LinnStrumentError("bossac failed with exit code %d" % result.returncode)


def settings_size_in_elf(elf_path):
    """sizeof(Configuration) in a firmware build, from the size of its `config` symbol."""
    try:
        with open(elf_path, "rb") as f:
            data = f.read()
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:    # 32-bit little-endian only
            return None
        (shoff,) = struct.unpack_from("<I", data, 0x20)
        shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
        sections = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
        for _, sh_type, _, _, offset, size, link, _, _, _ in sections:
            if sh_type != 2:                                           # SHT_SYMTAB
                continue
            strtab = sections[link][4]
            for sym in range(offset, offset + size, 16):
                name, _, sym_size, info = struct.unpack_from("<IIIB", data, sym)
                if info & 0xF == 1:                                    # STT_OBJECT
                    end = data.index(b"\0", strtab + name)
                    if data[strtab + name:end] == b"config":
                        return sym_size
    except (OSError, struct.error, ValueError, IndexError):
        pass
    return None


# ------------------------------------------------------------------------------------------------
# Steps
# ------------------------------------------------------------------------------------------------

def step(text):
    print("\n==> " + text)


def backup_device(port, skip_projects):
    with Connection(port) as conn:
        settings = with_retries(conn.read_settings, conn)
        if settings[0] < 1:
            raise LinnStrumentError("the settings don't start with a format version")
        project_size, projects = 0, []
        if not skip_projects:
            try:
                project_size, projects = with_retries(conn.read_projects, conn)
            except LinnStrumentError as e:
                print("    Warning: couldn't read the sequencer projects (%s); continuing with settings only." % e)
        harmony = b""
        try:
            harmony = conn.read_harmony()
            if harmony is None:
                harmony = b""                    # firmware without Harmony Board saving
        except (LinnStrumentError,) + PORT_ERRORS as e:
            print("    Warning: couldn't read the Harmony Board data (%s); continuing without it." % e)
            harmony = b""
    return Backup(settings, project_size, projects, harmony)


def restore_device(conn, backup, skip_projects):
    if backup.projects and not skip_projects:
        if backup.version < MIN_PROJECT_VERSION:
            print("    Skipping sequencer projects: settings version %d is too old for the firmware to accept them."
                  % backup.version)
        else:
            progress = Progress("Restoring sequencer projects", len(backup.projects) * backup.project_size)
            try:
                for index, project in enumerate(backup.projects):
                    with_retries(lambda: conn.write_project(backup.version, index, project), conn)
                    progress.update((index + 1) * backup.project_size)
                progress.finish()
            except LinnStrumentError as e:
                print("\n    Warning: couldn't restore the sequencer projects (%s); restoring settings anyway." % e)
    if backup.harmony:
        try:
            with_retries(lambda: conn.write_harmony(backup.harmony), conn)
            print("    Restored the Harmony Board's harmony, memories and page settings.")
        except (LinnStrumentError,) + PORT_ERRORS as e:
            print("    Warning: couldn't restore the Harmony Board data (%s); restoring settings anyway." % e)
    # this has to come last: once the settings are in, the firmware leaves serial mode
    with_retries(lambda: conn.write_settings(backup.settings), conn)


def check_settings_size(backup, bin_path, force):
    elf_path = os.path.splitext(bin_path)[0] + ".elf"
    expected = settings_size_in_elf(elf_path) if os.path.exists(elf_path) else None
    if expected is None:
        print("    No %s next to the .bin, so the settings size can't be checked." % os.path.basename(elf_path))
        return
    if expected == len(backup.settings):
        print("    The new firmware uses the same %d-byte settings layout as the backup." % expected)
        return
    message = ("The backup holds %d bytes of settings (format version %d), but this firmware's settings are "
               "%d bytes. Unless the firmware knows how to convert that older format, it will discard them "
               "and start from defaults with calibration." % (len(backup.settings), backup.version, expected))
    if not force:
        raise LinnStrumentError(message + "\nRun again with --force to flash anyway.")
    print("    Warning: " + message)


def confirm(question, assume_yes):
    if assume_yes:
        return
    if not sys.stdin.isatty():
        raise LinnStrumentError("Not flashing without confirmation; pass --yes to skip the question.")
    answer = input("    %s [y/N] " % question).strip().lower()
    if answer not in ("y", "yes"):
        raise LinnStrumentError("Cancelled; nothing was flashed.")


def main():
    parser = argparse.ArgumentParser(
        description="Flash LinnStrument firmware and keep its settings and calibration, like the official Updater.",
        epilog=OS_UPDATE_HINT)
    parser.add_argument("bin", nargs="?", help="firmware image to flash, e.g. build/linnstrument-firmware.ino.bin")
    parser.add_argument("--port", help="serial port, e.g. /dev/cu.usbmodem2101 (found automatically if left out)")
    parser.add_argument("--backup-only", action="store_true", help="only save the settings to a file")
    parser.add_argument("-o", "--output", help="where to save the backup (default: %s/linnstrument-<time>.lsbackup)"
                        % BACKUP_DIR)
    parser.add_argument("--restore", metavar="FILE",
                        help="send FILE to the LinnStrument instead of backing it up first; "
                             "with a .bin, flash it and then restore FILE")
    parser.add_argument("--skip-projects", action="store_true", help="leave the sequencer projects out")
    parser.add_argument("--bossac", help="path to bossac (default: the Arduino IDE's, %s)" % BOSSAC_GLOB)
    parser.add_argument("--yes", action="store_true", help="flash without asking for confirmation")
    parser.add_argument("--force", action="store_true",
                        help="flash even if the backup's settings size doesn't match the new firmware's")
    args = parser.parse_args()

    if args.backup_only and (args.bin or args.restore):
        parser.error("--backup-only can't be combined with a .bin or --restore")
    if not args.bin and not args.backup_only and not args.restore:
        parser.error("give a .bin to flash, --backup-only or --restore FILE")
    if args.output and args.restore:
        parser.error("--output names a new backup; --restore uses an existing one")

    backup_path = args.restore
    stage = "start"
    try:
        backup = None
        if args.restore:
            backup = Backup.load(args.restore)
            print("Loaded %s: %s." % (args.restore, backup.describe()))

        # restore only: the LinnStrument is waiting in serial mode, after a flash or with OS Update on
        if not args.bin and args.restore:
            step("Restoring settings")
            port = find_port(args.port)
            with Connection(port) as conn:
                restore_device(conn, backup, args.skip_projects)
            print("\nDone. The LinnStrument is back in MIDI mode with the restored settings.")
            return 0

        if args.bin:
            bin_path = os.path.abspath(args.bin)
            if not os.path.isfile(bin_path):
                raise LinnStrumentError("%s not found" % args.bin)
            bossac = find_bossac(args.bossac)

        port = find_port(args.port)
        total = 1 if args.backup_only else (2 if args.restore else 3)
        number = 0

        if not backup:
            number += 1
            step("Step %d/%d: backing up settings from %s" % (number, total, port))
            stage = "backup"
            backup = backup_device(port, args.skip_projects)
            backup_path = args.output or default_backup_path()
            backup.save(backup_path)
            print("    Saved %s to %s" % (backup.describe(), backup_path))
            if args.backup_only:
                print("\nDone. Turn OS Update off (the same pad) to get MIDI back.")
                return 0

        number += 1
        shown = os.path.relpath(bin_path)
        step("Step %d/%d: flashing %s" % (number, total, bin_path if shown.startswith("..") else shown))
        check_settings_size(backup, bin_path, args.force)
        confirm("Erase the LinnStrument (firmware, settings and calibration) and flash %s? "
                "The settings come back from %s afterwards." % (os.path.basename(bin_path), backup_path), args.yes)
        stage = "flash"
        print("    Resetting into the bootloader (1200-baud touch on %s)..." % port)
        time.sleep(RESTART_SETTLE)
        port = wait_for_port(args.port, time.monotonic() + 15)
        touch_1200(port)
        info = wait_for_bootloader(bossac, port)
        device = [line for line in info.splitlines() if line.lower().startswith("device")]
        print("    Bootloader answered%s." % (": " + device[0].split(":", 1)[-1].strip() if device else ""))
        run_bossac(bossac, port, bin_path)
        stage = "restore"

        number += 1
        step("Step %d/%d: restoring settings" % (number, total))
        print("    Waiting for the new firmware to start...")
        conn = connect_after_boot(args.port, port)
        try:
            restore_device(conn, backup, args.skip_projects)
        finally:
            conn.close()
        print("\nDone. The LinnStrument is running the new firmware in MIDI mode, with its settings and "
              "calibration. If it shows the calibration screen instead, the firmware couldn't use the saved "
              "settings; the backup is still in %s." % backup_path)
        return 0

    except (LinnStrumentError, serial.SerialException, termios.error, OSError, KeyboardInterrupt) as e:
        message = "Interrupted." if isinstance(e, KeyboardInterrupt) else str(e)
        print("\nError: " + message, file=sys.stderr)
        script = "tools/flash_keep_settings.py"
        if stage == "flash":
            print("\nThe LinnStrument may now be erased and waiting in its bootloader (all lights off). "
                  "Your settings are safe in %s. To flash and restore them, run:\n  %s --restore %s %s"
                  % (backup_path, script, shlex.quote(backup_path), shlex.quote(args.bin)), file=sys.stderr)
        elif stage == "restore":
            print("\nThe new firmware is installed but the settings weren't restored. Your settings are safe "
                  "in %s. Restore them before calibrating (calibrating on first boot leaves serial mode; "
                  "if that happened, turn OS Update on again) with:\n  %s --restore %s"
                  % (backup_path, script, shlex.quote(backup_path)), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
