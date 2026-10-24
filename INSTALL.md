Installing the Harmony Board
============================

This guide is for anyone who wants to try the Harmony Board on their LinnStrument 200, no programming needed. It takes about 10 minutes.

> [!WARNING]
> **Use at your own risk.** The Harmony Board is unofficial, experimental firmware, provided as is, without warranty of any kind (see the [license](LICENSE.txt)). It isn't made, tested or supported by Roger Linn Design. Flashing replaces your LinnStrument's firmware; if it's interrupted, the LinnStrument won't work until it's flashed again. Read [Risks and recovery](#risks-and-recovery) before you start.

There are two flashers, and both keep your settings and calibration:

- **The browser flasher** at [rymorg.github.io/linnstrument-harmony-board/flash](https://rymorg.github.io/linnstrument-harmony-board/flash/) runs in Chrome or Edge on a computer. Nothing to download or install. Start here.
- **The downloadable flasher** is an app for macOS (Apple Silicon), Windows and Linux. Use it if you can't use Chrome or Edge, or if the browser flasher doesn't work for you.

You can go back to the official firmware at any time (see [Going back to the stock firmware](#going-back-to-the-stock-firmware)).

Risks and recovery
------------------

**What can go wrong.** Flashing erases the LinnStrument's firmware and writes a new one. If the power drops or the cable is pulled in the middle, the LinnStrument is left with no working firmware: its lights stay dark and it won't play. Weak USB power is the most common cause; Roger Linn Design's [FAQ](https://www.rogerlinndesign.com/support/support-linnstrument-faqs) notes that power dipping during writes to flash memory can cause failures.

**How to avoid it:**

- Plug the LinnStrument **straight into the computer**, not into a hub or adapter, and keep a laptop on its charger.
- Keep the computer awake and don't close the flasher's window until it says **Done**.
- Don't touch the LinnStrument while it flashes, and **don't unplug it, even when its lights are off**.

**If it does go wrong, it's almost always recoverable.** The LinnStrument's processor has a recovery bootloader built into its read-only memory, which flashing can't erase, so a LinnStrument with dark lights can still take new firmware. In order:

1. **Leave it plugged in and flash again.** In the browser flasher, click the flash button again, or choose **Recover: flash with a saved backup** and your backup file (it also keeps a copy in the browser). The downloadable flasher prints the exact command to use, with your backup. If you don't have a backup, choose **Recover: flash without a backup**, or run the downloadable flasher with `--latest --no-backup` (your settings and calibration are lost; you'll calibrate afterwards).
2. **Use Firmware Upgrade mode.** Unplug the LinnStrument, hold **Global Settings**, and plug it back in: it shows **FWUP** in red, a recovery mode built into the firmware ([community wiki](https://linnstrument.miraheze.org/wiki/Firmware)). Then use the browser flasher's **Recover: flash without a backup** (or with your backup), or run the downloadable flasher with `--latest --no-backup`.
3. **Use the official LinnStrument Updater** from [rogerlinndesign.com](https://www.rogerlinndesign.com), which installs the stock firmware. Roger Linn Design's FAQ says a LinnStrument with no lights "will still accept an OS update".
4. **If none of that works**, the hardware may need repair. Roger Linn Design offers repair options for LinnStrument hardware; tell them it was running unofficial firmware, and don't ask them for help with the Harmony Board itself.

**How often does this happen?** Reports are rare, and the two "bricked" LinnStrument threads on the official forum both ended well. In [one](https://www.kvraudio.com/forum/viewtopic.php?t=528453), an update froze with the lights off because the USB port gave too little power; a different port fixed it. In the [other](https://www.kvraudio.com/forum/viewtopic.php?t=468539) (2016), a LinnStrument that had already been resetting its settings on its own went dark during an update; Roger Linn Design sent a replacement main board and it was playing again within days. Neither involved the Harmony Board.

What you need
-------------

- A **LinnStrument 200** (the LinnStrument 128 isn't supported yet) and its USB cable.
- A computer with **Chrome or Edge** (macOS, Windows, Linux or ChromeOS) and an internet connection. For the downloadable flasher instead: macOS (Apple Silicon), Windows or Linux.
- About 10 minutes in which you don't unplug the LinnStrument.

1. Put the LinnStrument in OS Update mode
-----------------------------------------

1. Plug the LinnStrument straight into the computer, not into a hub.
2. Press **Global Settings** (the bottom button on the left edge).
3. Find **OS Update**: column 16, counting from the control buttons, third pad from the bottom. Briefly press it once, with one finger, so its light turns on. Don't hold it, and don't touch the bottom pad of that column: pressing both together resets the LinnStrument's settings.

Not sure which pad that is? Open the [companion app](https://rymorg.github.io/linnstrument-harmony-board/) in Chrome, click **Connect LinnStrument** before step 2, and open Global Settings: the app labels the OS Update pad **UPDT**.

2. Flash from your browser
--------------------------

1. Open the [browser flasher](https://rymorg.github.io/linnstrument-harmony-board/flash/) in **Chrome** or **Edge**.
2. Click **Connect and back up**, choose **LinnStrument** in the list Chrome shows, and click **Connect**. The page reads the settings, calibration, sequencer projects and Harmony Board data. That changes nothing on the LinnStrument.
3. Click **Save backup file** and keep the file until you're done. The page also keeps a copy in the browser.
4. The page shows the newest Harmony Board release. Tick the box that you've read the risks, and click **Erase and install**.
5. It shows **WAIT** on the LinnStrument, erases it, writes the new firmware and checks every byte, then restores your backup, about two minutes in all. Keep the tab open and in front until it says **Done**.

If the page asks you to **pick the LinnStrument again** partway through, click the button and choose it: the LinnStrument restarted, and Chrome wants your permission once more. Nothing is lost, and the flash carries on.

If anything goes wrong, the page says what happened and what to do; see also [Risks and recovery](#risks-and-recovery).

The downloadable flasher
------------------------

Use this instead of step 2 if you can't use Chrome or Edge, or if the browser flasher doesn't work for you. It does the same thing.

Go to the [Releases page](https://github.com/rymorg/linnstrument-harmony-board/releases) and, under **Assets** of the newest release, download the flasher for your computer:

| Computer | File |
|---|---|
| Mac with Apple Silicon (M1 or later) | `harmony-board-flasher-macos-apple-silicon` |
| Windows | `harmony-board-flasher-windows.exe` |
| Linux | `harmony-board-flasher-linux` |

You don't need to download the firmware file: the flasher fetches the newest release itself.

**Mac:** the flasher uses an Intel tool from Arduino to write the firmware, so your Mac needs Rosetta. If you've never installed it, open Terminal and run `softwareupdate --install-rosetta --agree-to-license`.

**Intel Mac:** there's no ready-made app; use the browser flasher, or follow [Flashing from source](HARMONY.md#flash).

With the LinnStrument in OS Update mode (step 1), run the flasher:

- **Mac:** the first time, macOS says the flasher is from an unidentified developer. Right-click (or Control-click) the file, choose **Open**, then **Open** again. If it still won't open, go to System Settings → Privacy & Security and click **Open Anyway**.
- **Windows:** if SmartScreen says it protected your PC, click **More info**, then **Run anyway**.
- **Linux:** make it executable (`chmod +x harmony-board-flasher-linux`) and run it from a terminal. If it can't open the serial port, add yourself to the `dialout` group (`sudo usermod -aG dialout $USER`) and log in again.

The flasher then:

1. finds the LinnStrument and backs up its settings, calibration, sequencer projects and Harmony Board data to the `linnstrument-backups` folder in your home folder;
2. downloads the newest Harmony Board firmware and the Arduino tool that writes it;
3. asks before erasing anything: type `y` and press Enter;
4. erases the LinnStrument, writes the new firmware and checks it, about a minute and a half;
5. restores your backup.

> [!IMPORTANT]
> **With either flasher, the lights go dark while it flashes, for a minute or two. That's expected.** Just before, the pads show a red **WAIT**. Then the LinnStrument is erased and rewritten, and nothing can light up until the new firmware starts. When it's finished, the pads show a green **OK**. (Updating from firmware older than 0.1.0, there's no WAIT, and from the stock firmware no OK.) Don't unplug it, don't touch it, and don't close the flasher; wait for **Done**. If the flasher stops with an error, leave the LinnStrument plugged in and see [Risks and recovery](#risks-and-recovery).

3. After flashing
-----------------

- The LinnStrument restarts in normal (MIDI) mode with your settings.
- If it shows the **calibration** screen instead (rows and columns of lights to slide along), calibrate it: slide a finger along each lit row, then each lit column, until everything turns green and it shows **OK**. This happens if it had never been calibrated or the backup couldn't be used.
- **Turn on MPE** if it isn't already: press **Per-Split Settings** and hold **Channel Per Note** (column 1, second pad from the top) until it changes color. Then press Per-Split Settings again.
- In your DAW, use an MPE instrument with its pitch bend range set to **48 semitones**. For left-hand chords (Perform mode), add a second instrument listening to **MIDI channel 16**.

You're done. The [cheat sheet](docs/cheatsheet.md) shows what every pad does; [HARMONY.md](HARMONY.md) has everything in detail.

Updating to a new version
-------------------------

Put the LinnStrument in OS Update mode and open the [browser flasher](https://rymorg.github.io/linnstrument-harmony-board/flash/) again; it always offers the newest release. (With the downloadable flasher, download the newest one if the release notes say to, and run it.) Your settings, calibration, harmony and memories are kept.

To check which version you're running: press **Global Settings**, then the **VER** pad (column 16, second from the bottom). It always opens on `234`, the version of the stock firmware the Harmony Board is built on. Tap the surface once to see the Harmony Board's own version, such as `H0.1`; each tap switches between the two. The screen only shows information, so nothing else changes. Press Global Settings to leave.

Going back to the stock firmware
--------------------------------

Download the official **LinnStrument Updater** from [rogerlinndesign.com](https://www.rogerlinndesign.com) and follow its instructions. It keeps your factory settings and calibration; the Harmony Board's own data is left behind.

Troubleshooting
---------------

**The browser flasher says this browser can't talk to the LinnStrument.** It needs the Web Serial API, which only Chrome and Edge on a computer have. Use one of those, or the [downloadable flasher](#the-downloadable-flasher).

**The flasher can't find the LinnStrument, or it isn't in Chrome's list.** The flashers only see the LinnStrument in OS Update mode. Check that the OS Update pad is lit, try another USB cable or port, and close any app that might use the LinnStrument's serial port (including another tab with the flasher). On Windows, if no COM port appears in Device Manager in OS Update mode, install the driver from [LinnStrument Windows Driver.zip](LinnStrument%20Windows%20Driver.zip) (the official one from Roger Linn Design). On Linux, if Chrome can't open the port, add yourself to the `dialout` group (`sudo usermod -aG dialout $USER`) and log in again.

**The flasher stopped partway, or the lights stayed dark.** Don't panic and don't unplug it: your backup is safe. A dark LinnStrument is usually waiting in its bootloader. In the browser flasher, click the flash button again, or choose **Recover: flash with a saved backup** with your backup file; it flashes and restores in one go. The downloadable flasher keeps the backup in the `linnstrument-backups` folder and prints the command to finish the job: run that, or run the flasher again from a terminal with the backup it names and `--latest`, for example `./harmony-board-flasher-macos-apple-silicon --restore ~/linnstrument-backups/linnstrument-<date>.lsbackup --latest`. Backups from either flasher work in both. If that doesn't work, follow [Risks and recovery](#risks-and-recovery).

**"bossac can't run" on a Mac.** The downloadable flasher needs Rosetta (see [The downloadable flasher](#the-downloadable-flasher)); install it and run the flasher again, or use the browser flasher, which doesn't need it. Nothing was erased.

**My DAW doesn't see the LinnStrument after flashing.** It's probably still in OS Update mode, where it shows up as a serial device instead of MIDI. Press Global Settings and briefly press OS Update so its light turns off.

**Notes bend only a little, or slides sound wrong.** Your instrument's pitch bend range doesn't match: set it to 48 semitones, and make sure MPE is on both on the LinnStrument (Per-Split Settings) and in your DAW.

**The melody instrument also plays the left-hand chords.** It's listening on all MIDI channels. Set the melody track to channels 1–15 if your DAW allows it, or let us know which DAW you use (see below).

**The harmony zone is in a confusing state.** Hold **Split** to open the Harmony page, tap the red pad in the bottom-left corner (column 1), then tap it again within 3 seconds: the harmony, memories and Harmony page settings go back to how a fresh install starts.

**Something else is wrong.** Please [open an issue](https://github.com/rymorg/linnstrument-harmony-board/issues/new/choose) with your Harmony Board version, computer and DAW. Don't contact Roger Linn Design about the Harmony Board: it's an unofficial project.
