#!/usr/bin/env python3
"""Tests for flash_keep_settings.py against a simulated LinnStrument.

FakeLinnStrument sits on a pseudo-terminal and answers like the firmware's
handleSerialIO() and the serialSend*/serialRestore* functions in ls_serial.ino,
byte for byte, including their 2-second timeouts. A fake bossac stands in for
the bootloader. No hardware is touched.

  tools/.venv/bin/python tools/test_flash_keep_settings.py
"""

import contextlib
import io
import os
import pty
import random
import select
import stat
import struct
import sys
import tempfile
import threading
import time
import tty
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flash_keep_settings as fks  # noqa: E402

SETTINGS_SIZE = 11720       # sizeof(Configuration) in firmware 2.3.4, from the `config` symbol in the ELF
PROJECT_SIZE = 1000
VERSION = 17


def firmware_crc(data):
    """crc_byte_array() from ls_serial.ino, nibble table and all."""
    table = [0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
             0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c]
    crc = 0xFFFFFFFF
    for b in data:
        crc = table[(crc ^ b) & 0x0F] ^ (crc >> 4)
        crc = table[(crc ^ (b >> 4)) & 0x0F] ^ (crc >> 4)
    return crc ^ 0xFFFFFFFF


def random_blob(size, version=VERSION, seed=0):
    rnd = random.Random(seed)
    return bytes([version]) + bytes(rnd.randrange(256) for _ in range(size - 1))


class Abort(Exception):
    """The firmware gave up on a transfer (serialWaitForMaximumTwoSeconds timed out)."""


class FakeLinnStrument(threading.Thread):
    COUNTDOWN = b"5, 4, 3, 2, 1 ...\n"
    CONTROL = b"LC\n\0"

    def __init__(self, settings, projects, flash_marker=None, harmony=None):
        super().__init__(daemon=True)
        self.harmony = harmony                   # None: firmware without the Harmony Board commands
        self.master, self.slave = pty.openpty()
        tty.setraw(self.slave)                   # keep a slave fd open so the pty never hangs up
        self.port = os.ttyname(self.slave)
        self.settings = bytearray(settings)
        self.projects = [bytearray(p) for p in projects]
        self.serial_mode = True
        self.waiting_for_commands = False
        self.code_pos = 0
        self.flash_marker = flash_marker
        self.flashed = False
        self.corrupt_send_batches = set()        # batch numbers sent corrupted once (device -> host)
        self.reject_receive_batches = set()      # batch numbers answered with CRC wrong once (host -> device)
        self.events = []
        self.running = True

    def stop(self):
        self.running = False
        self.join(2)
        os.close(self.master)
        os.close(self.slave)

    # serial I/O
    def write(self, data):
        os.write(self.master, data)

    def read_byte(self, timeout=2.0):
        r, _, _ = select.select([self.master], [], [], timeout)
        if not r:
            self.waiting_for_commands = False
            raise Abort()
        return os.read(self.master, 1)[0]

    def read_bytes(self, n):
        return bytes(self.read_byte() for _ in range(n))

    # main loop
    def run(self):
        while self.running:
            if self.flash_marker and not self.flashed and os.path.exists(self.flash_marker):
                self.first_boot_after_flash()
            r, _, _ = select.select([self.master], [], [], 0.05)
            if not r:
                continue
            d = os.read(self.master, 1)[0]
            if not self.serial_mode:
                continue                         # in MIDI mode the serial port is gone
            try:
                self.handle_serial_io(d)
            except Abort:
                self.events.append("timeout")

    def first_boot_after_flash(self):
        # bossac -e erased everything; initializeStorage() writes defaults and starts in serial mode
        self.flashed = True
        self.settings = bytearray(random_blob(len(self.settings), seed=99))
        self.projects = [bytearray(len(p)) for p in self.projects]
        if self.harmony is not None:
            self.harmony = b""                   # the saved harmony block is erased too
        self.serial_mode = True
        self.waiting_for_commands = False
        self.code_pos = 0
        self.events.append("flashed")

    def handle_serial_io(self, d):
        if self.waiting_for_commands:
            command = {ord("s"): self.send_settings, ord("r"): self.restore_settings,
                       ord("p"): self.send_projects, ord("q"): self.restore_project}.get(d)
            if self.harmony is not None:
                command = command or {ord("x"): self.send_harmony, ord("y"): self.restore_harmony}.get(d)
            if command:
                command()
            else:
                self.waiting_for_commands = False
            return
        if d == self.COUNTDOWN[self.code_pos]:
            self.code_pos += 1
            if self.code_pos == len(self.COUNTDOWN):
                self.code_pos = 0
                self.waiting_for_commands = True
                self.write(b"LinnStruments are go!\n")
        elif self.code_pos < len(self.CONTROL) and d == self.CONTROL[self.code_pos]:
            self.code_pos += 1
        else:
            self.code_pos = 0

    # device -> host
    def send_batches(self, data, label):
        offset, batch_no = 0, 0
        while offset < len(data):
            batch = data[offset:offset + 96]
            if (label, batch_no) in self.corrupt_send_batches:
                self.corrupt_send_batches.discard((label, batch_no))
                self.events.append("corrupted %s batch %d" % (label, batch_no))
                self.write(bytes([batch[0] ^ 0xFF]) + batch[1:])
            else:
                self.write(batch)
            if self.read_byte() != ord("c"):
                return False
            self.write(struct.pack("<I", firmware_crc(batch)))
            if self.read_byte() == ord("w"):
                continue
            offset += len(batch)
            batch_no += 1
        return True

    def send_settings(self):
        self.write(b"ACK\n")
        self.write(struct.pack("<i", len(self.settings)))
        if self.send_batches(bytes(self.settings), "settings"):
            self.write(b"ACK\n")
            self.events.append("sent settings")

    def send_projects(self):
        self.write(b"ACK\n")
        self.write(bytes([len(self.projects)]))
        self.write(struct.pack("<i", PROJECT_SIZE))
        for p in self.projects:
            self.send_batches(bytes(p), "project")
        self.write(b"ACK\n")
        self.events.append("sent projects")

    # host -> device
    def receive_batches(self, size, label):
        data = bytearray()
        batch_no = 0
        while len(data) < size:
            batch = self.read_bytes(min(96, size - len(data)))
            self.write(b"c")
            (remote_crc,) = struct.unpack("<I", self.read_bytes(4))
            if (label, batch_no) in self.reject_receive_batches:
                self.reject_receive_batches.discard((label, batch_no))
                self.events.append("rejected %s batch %d" % (label, batch_no))
                self.write(b"w")
                continue
            if firmware_crc(batch) != remote_crc:
                self.write(b"w")
                continue
            self.write(b"o")
            data += batch
            batch_no += 1
        return bytes(data)

    def restore_settings(self):
        self.write(b"ACK\n")
        (size,) = struct.unpack("<i", self.read_bytes(4))
        self.write(b"ACK\n")
        data = self.receive_batches(size, "settings")
        # upgradeConfigurationSettings(): only the current format, at the current size, is taken as is
        if data[0] == VERSION and size == len(self.settings):
            self.settings = bytearray(data)
            self.events.append("restored settings")
        else:
            self.events.append("discarded settings")
        self.write(b"ACK\n")
        self.serial_mode = False                 # switchSerialMode(false)
        self.waiting_for_commands = False

    def send_harmony(self):
        self.write(b"ACK\n")
        self.write(struct.pack("<i", len(self.harmony)))
        if self.send_batches(bytes(self.harmony), "harmony"):
            self.write(b"ACK\n")
            self.events.append("sent harmony")

    def restore_harmony(self):
        self.write(b"ACK\n")
        (size,) = struct.unpack("<i", self.read_bytes(4))
        self.write(b"ACK\n")
        self.harmony = self.receive_batches(size, "harmony")
        self.write(b"ACK\n")
        self.events.append("restored harmony")

    def restore_project(self):
        self.write(b"ACK\n")
        version = self.read_byte()
        if version < 9:
            return
        self.write(b"ACK\n")
        (size,) = struct.unpack("<i", self.read_bytes(4))
        if size != PROJECT_SIZE:
            return
        self.write(b"ACK\n")
        p = self.read_byte()
        self.write(b"ACK\n")
        self.projects[p] = bytearray(self.receive_batches(size, "project"))
        self.write(b"ACK\n")
        self.events.append("restored project %d" % p)


def make_elf(symbol_sizes):
    """A minimal 32-bit little-endian ELF with a symbol table, enough for settings_size_in_elf()."""
    strtab = b"\0"
    symtab = b"\0" * 16
    for name, size in symbol_sizes.items():
        symtab += struct.pack("<IIIBBH", len(strtab), 0x20070000, size, 0x11, 0, 1)   # global object
        strtab += name.encode() + b"\0"
    shoff = 52 + len(symtab) + len(strtab)
    header = (b"\x7fELF\x01\x01\x01" + b"\0" * 9 +
              struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, 0, 0, shoff, 0, 52, 0, 0, 40, 3, 0))
    sections = (b"\0" * 40 +
                struct.pack("<10I", 0, 2, 0, 0, 52, len(symtab), 2, 1, 4, 16) +
                struct.pack("<10I", 0, 3, 0, 0, 52 + len(symtab), len(strtab), 0, 0, 1, 0))
    return header + symtab + strtab + sections


class FlashKeepSettingsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.user_settings = random_blob(SETTINGS_SIZE, seed=1)
        self.user_projects = [random_blob(PROJECT_SIZE, version=i, seed=10 + i) for i in range(16)]
        self.marker = os.path.join(self.tmp, "flashed")
        self.device = FakeLinnStrument(self.user_settings, self.user_projects, self.marker)
        self.device.start()

    def tearDown(self):
        self.device.stop()

    def run_script(self, *args):
        out = io.StringIO()
        argv = sys.argv
        sys.argv = ["flash_keep_settings.py"] + list(args)
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
                code = fks.main()
        finally:
            sys.argv = argv
        return code, out.getvalue()

    def fake_bossac(self, info_failures=0):
        path = os.path.join(self.tmp, "bossac")
        log = os.path.join(self.tmp, "bossac.log")
        count = os.path.join(self.tmp, "info-count")
        with open(path, "w") as f:
            f.write("""#!/bin/sh
echo "$@" >> '%(log)s'
case " $* " in
  *" -i "*)
    n=$(cat '%(count)s' 2>/dev/null || echo 0); echo $((n + 1)) > '%(count)s'
    if [ "$n" -lt %(failures)d ]; then echo "No device found on $1"; exit 1; fi
    echo "Device       : ATSAMX8E"; exit 0 ;;
  *" -e "*) touch '%(marker)s'; echo "Verify successful"; exit 0 ;;
  *" --help "*) echo "Usage: bossac"; exit 0 ;;
esac
exit 2
""" % {"log": log, "count": count, "failures": info_failures, "marker": self.marker})
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
        return path, log

    def fake_bin(self, settings_size=None):
        path = os.path.join(self.tmp, "linnstrument-firmware.ino.bin")
        with open(path, "wb") as f:
            f.write(b"\0" * 1024)
        if settings_size:
            with open(os.path.join(self.tmp, "linnstrument-firmware.ino.elf"), "wb") as f:
                f.write(make_elf({"Project": 4, "config": settings_size}))
        return path

    def test_crc_matches_firmware(self):
        rnd = random.Random(5)
        for n in (0, 1, 4, 95, 96, 97, 300):
            data = bytes(rnd.randrange(256) for _ in range(n))
            self.assertEqual(zlib.crc32(data), firmware_crc(data))

    def test_backup_only(self):
        self.device.corrupt_send_batches = {("settings", 3), ("project", 0)}
        out_path = os.path.join(self.tmp, "backup.lsbackup")
        code, out = self.run_script("--backup-only", "--port", self.device.port, "-o", out_path)
        self.assertEqual(code, 0, out)
        backup = fks.Backup.load(out_path)
        self.assertEqual(backup.settings, self.user_settings)
        self.assertEqual(backup.projects, self.user_projects)
        self.assertEqual(backup.project_size, PROJECT_SIZE)
        self.assertIn("corrupted settings batch 3", self.device.events)
        self.assertTrue(self.device.serial_mode)

    def test_restore(self):
        saved = random_blob(SETTINGS_SIZE, seed=2)
        saved_projects = [random_blob(PROJECT_SIZE, seed=20 + i) for i in range(16)]
        path = os.path.join(self.tmp, "saved.lsbackup")
        fks.Backup(saved, PROJECT_SIZE, saved_projects).save(path)
        self.device.reject_receive_batches = {("settings", 5), ("project", 1)}
        code, out = self.run_script("--restore", path, "--port", self.device.port)
        self.assertEqual(code, 0, out)
        self.assertEqual(bytes(self.device.settings), saved)
        self.assertEqual([bytes(p) for p in self.device.projects], saved_projects)
        self.assertIn("rejected settings batch 5", self.device.events)
        self.assertFalse(self.device.serial_mode)

    def test_restore_raw_blob_without_projects(self):
        saved = random_blob(SETTINGS_SIZE, seed=3)
        path = os.path.join(self.tmp, "raw.bin")
        with open(path, "wb") as f:
            f.write(saved)
        code, out = self.run_script("--restore", path, "--port", self.device.port)
        self.assertEqual(code, 0, out)
        self.assertEqual(bytes(self.device.settings), saved)
        self.assertEqual([bytes(p) for p in self.device.projects], self.user_projects)

    def test_flash_keeps_settings(self):
        bossac, log = self.fake_bossac(info_failures=2)
        bin_path = self.fake_bin(settings_size=SETTINGS_SIZE)
        backup_path = os.path.join(self.tmp, "auto.lsbackup")
        code, out = self.run_script(bin_path, "--port", self.device.port, "--bossac", bossac,
                                    "--yes", "-o", backup_path)
        self.assertEqual(code, 0, out)
        self.assertIn("same 11720-byte settings layout", out)
        self.assertEqual(fks.Backup.load(backup_path).settings, self.user_settings)
        name = os.path.basename(self.device.port)
        with open(log) as f:
            calls = f.read().splitlines()
        self.assertEqual(calls[0], "--help")                 # bossac has to run before anything is erased
        calls = calls[1:]
        self.assertEqual(calls[:3], ["--port=%s -U true -i" % name] * 3)
        self.assertEqual(calls[3], "--port=%s -U true -e -w -v -b %s -R" % (name, bin_path))
        self.assertIn("flashed", self.device.events)
        self.assertEqual(bytes(self.device.settings), self.user_settings)
        self.assertEqual([bytes(p) for p in self.device.projects], self.user_projects)
        self.assertFalse(self.device.serial_mode)

    def test_flash_from_existing_backup(self):
        bossac, log = self.fake_bossac()
        bin_path = self.fake_bin()
        path = os.path.join(self.tmp, "saved.lsbackup")
        fks.Backup(self.user_settings).save(path)
        code, out = self.run_script("--restore", path, bin_path, "--port", self.device.port,
                                    "--bossac", bossac, "--yes")
        self.assertEqual(code, 0, out)
        self.assertNotIn("sent settings", self.device.events)
        self.assertEqual(bytes(self.device.settings), self.user_settings)

    def test_size_mismatch_stops_before_flashing(self):
        bossac, log = self.fake_bossac()
        bin_path = self.fake_bin(settings_size=SETTINGS_SIZE + 8)
        code, out = self.run_script(bin_path, "--port", self.device.port, "--bossac", bossac, "--yes",
                                    "-o", os.path.join(self.tmp, "b.lsbackup"))
        self.assertEqual(code, 1)
        self.assertIn("--force", out)
        calls = open(log).read().splitlines() if os.path.exists(log) else []
        self.assertEqual([c for c in calls if c != "--help"], [])   # nothing but the pre-check ran
        self.assertTrue(self.device.serial_mode)

    def use_harmony_device(self, harmony):
        self.device.stop()
        self.device = FakeLinnStrument(self.user_settings, self.user_projects, self.marker, harmony=harmony)
        self.device.start()

    def test_flash_keeps_harmony_block(self):
        harmony = random_blob(136, seed=7)
        self.use_harmony_device(harmony)
        bossac, _ = self.fake_bossac()
        backup = os.path.join(self.tmp, "b.lsbackup")
        code, out = self.run_script(self.fake_bin(), "--port", self.device.port, "--bossac", bossac,
                                    "--yes", "--skip-projects", "-o", backup)
        self.assertEqual(code, 0, out)
        self.assertIn("restored harmony", self.device.events)
        self.assertEqual(self.device.harmony, harmony)
        self.assertEqual(fks.Backup.load(backup).harmony, harmony)

    def test_backup_without_harmony_support(self):
        backup = os.path.join(self.tmp, "b.lsbackup")
        code, out = self.run_script("--backup-only", "--port", self.device.port, "--skip-projects", "-o", backup)
        self.assertEqual(code, 0, out)
        self.assertEqual(fks.Backup.load(backup).harmony, b"")

    def test_format_1_backup_still_loads(self):
        settings = random_blob(SETTINGS_SIZE, seed=3)
        payload = settings
        path = os.path.join(self.tmp, "old.lsbackup")
        with open(path, "wb") as f:
            f.write(fks.BACKUP_HEADER_1.pack(fks.BACKUP_MAGIC, 1, settings[0], 0, len(settings), 0, zlib.crc32(payload)) + payload)
        b = fks.Backup.load(path)
        self.assertEqual(b.settings, settings)
        self.assertEqual(b.harmony, b"")

    def test_bossac_that_cannot_run_stops_before_erasing(self):
        broken = os.path.join(self.tmp, "bossac")
        with open(broken, "wb") as f:
            f.write(b"\xca\xfe\xba\xbe not a program")   # like an Intel bossac without Rosetta
        os.chmod(broken, 0o755)
        code, out = self.run_script(self.fake_bin(), "--port", self.device.port, "--bossac", broken, "--yes",
                                    "--skip-projects", "-o", os.path.join(self.tmp, "b.lsbackup"))
        self.assertEqual(code, 1)
        self.assertIn("Nothing was erased", out)
        self.assertNotIn("flashed", self.device.events)

    def test_restore_with_latest_flashes_then_restores(self):
        bossac, log = self.fake_bossac()
        latest = self.fake_bin()
        path = os.path.join(self.tmp, "saved.lsbackup")
        fks.Backup(self.user_settings).save(path)
        original = fks.fetch_latest_firmware
        fks.fetch_latest_firmware = lambda: latest
        try:
            code, out = self.run_script("--restore", path, "--latest", "--port", self.device.port,
                                        "--bossac", bossac, "--yes")
        finally:
            fks.fetch_latest_firmware = original
        self.assertEqual(code, 0, out)
        self.assertIn("flashed", self.device.events)
        self.assertEqual(bytes(self.device.settings), self.user_settings)

    def test_port_lookup(self):
        self.assertTrue(fks.port_present(self.device.port))
        self.assertFalse(fks.port_present(self.device.port + "-gone"))

    def test_elf_symbol_size(self):
        path = os.path.join(self.tmp, "x.elf")
        with open(path, "wb") as f:
            f.write(make_elf({"configuration": 1, "config": 1234}))
        self.assertEqual(fks.settings_size_in_elf(path), 1234)
        repo_elf = os.path.join(os.path.dirname(__file__), "..", "build", "linnstrument-firmware.ino.elf")
        if os.path.exists(repo_elf):
            self.assertGreater(fks.settings_size_in_elf(repo_elf), 0)

    def test_damaged_backup_is_refused(self):
        path = os.path.join(self.tmp, "saved.lsbackup")
        fks.Backup(self.user_settings).save(path)
        with open(path, "r+b") as f:
            f.seek(100)
            f.write(b"\x00\x01")
        code, out = self.run_script("--restore", path, "--port", self.device.port)
        self.assertEqual(code, 1)
        self.assertIn("damaged", out)
        self.assertTrue(self.device.serial_mode)


if __name__ == "__main__":
    unittest.main(verbosity=2)
