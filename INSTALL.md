Installing the Harmony Board
============================

This guide is for anyone who wants to try the Harmony Board on their LinnStrument 200, no programming needed. It takes about 10 minutes.

> **Before you start:** the Harmony Board is experimental and changes often. It replaces your LinnStrument's firmware. The flasher keeps your settings and calibration, and you can go back to the official firmware at any time (see [Going back to the stock firmware](#going-back-to-the-stock-firmware)).

What you need
-------------

- A **LinnStrument 200** (the LinnStrument 128 isn't supported yet) and its USB cable.
- A computer with macOS (Apple Silicon), Windows or Linux, and an internet connection.
- About 10 minutes in which you don't unplug the LinnStrument.

1. Download the flasher
-----------------------

Go to the [Releases page](https://github.com/rymorg/linnstrument-harmony-board/releases) and, under **Assets** of the newest release, download the flasher for your computer:

| Computer | File |
|---|---|
| Mac with Apple Silicon (M1 or later) | `harmony-board-flasher-macos-apple-silicon` |
| Windows | `harmony-board-flasher-windows.exe` |
| Linux | `harmony-board-flasher-linux` |

You don't need to download the firmware file: the flasher fetches the newest release itself.

**Mac:** the flasher uses an Intel tool from Arduino to write the firmware, so your Mac needs Rosetta. If you've never installed it, open Terminal and run `softwareupdate --install-rosetta --agree-to-license`.

**Intel Mac:** there's no ready-made flasher yet; follow [Flashing from source](HARMONY.md#flash) instead.

2. Put the LinnStrument in OS Update mode
-----------------------------------------

1. Plug the LinnStrument into the computer.
2. Press **Global Settings** (the bottom button on the left edge).
3. Find **OS Update**: column 16, counting from the control buttons, third pad from the bottom. Briefly press it once, with one finger, so its light turns on. Don't hold it, and don't touch the pad below it: pressing both together resets the LinnStrument's settings.

Not sure which pad that is? Open the [companion app](https://rymorg.github.io/linnstrument-harmony-board/) in Chrome, click **Connect LinnStrument** before step 2, and open Global Settings: the app labels the OS Update pad **UPDT**.

3. Run the flasher
------------------

- **Mac:** the first time, macOS says the flasher is from an unidentified developer. Right-click (or Control-click) the file, choose **Open**, then **Open** again. If it still won't open, go to System Settings → Privacy & Security and click **Open Anyway**.
- **Windows:** if SmartScreen says it protected your PC, click **More info**, then **Run anyway**.
- **Linux:** make it executable (`chmod +x harmony-board-flasher-linux`) and run it from a terminal. If it can't open the serial port, add yourself to the `dialout` group (`sudo usermod -aG dialout $USER`) and log in again.

The flasher then:

1. finds the LinnStrument and backs up its settings, calibration, sequencer projects and Harmony Board data to the `linnstrument-backups` folder in your home folder;
2. downloads the newest Harmony Board firmware and the Arduino tool that writes it;
3. asks before erasing anything: type `y` and press Enter;
4. flashes and checks the firmware (about a minute);
5. restores your backup.

Leave the LinnStrument alone until the flasher says **Done**. Its lights go dark while it flashes; that's normal.

4. After flashing
-----------------

- The LinnStrument restarts in normal (MIDI) mode with your settings.
- If it shows the **calibration** screen instead (rows and columns of lights to slide along), calibrate it: slide a finger along each lit row, then each lit column, until everything turns green and it shows **OK**. This happens if it had never been calibrated or the backup couldn't be used.
- **Turn on MPE** if it isn't already: press **Per-Split Settings** and hold **Channel Per Note** (column 1, second pad from the top) until it changes color. Then press Per-Split Settings again.
- In your DAW, use an MPE instrument with its pitch bend range set to **48 semitones**. For left-hand chords (Perform mode), add a second instrument listening to **MIDI channel 16**.

You're done. The [cheat sheet](docs/cheatsheet.md) shows what every pad does; [HARMONY.md](HARMONY.md) has everything in detail.

Updating to a new version
-------------------------

Download the newest flasher if the release notes say to, put the LinnStrument in OS Update mode, and run the flasher again. Your settings, calibration, harmony and memories are kept.

To check which version you're running: press **Global Settings**, then the **VER** pad (column 16, second from the bottom). It shows `234` (the stock firmware it's based on); tap it again for the Harmony Board version, such as `H0.1`.

Going back to the stock firmware
--------------------------------

Download the official **LinnStrument Updater** from [rogerlinndesign.com](https://www.rogerlinndesign.com) and follow its instructions. It keeps your factory settings and calibration; the Harmony Board's own data is left behind.

Troubleshooting
---------------

**The flasher says it can't find the LinnStrument.** It only sees the LinnStrument in OS Update mode. Check that the OS Update pad is lit, try another USB cable or port, and close any app that might be using the LinnStrument's serial port. On Windows, if no COM port appears in Device Manager in OS Update mode, install the driver from [LinnStrument Windows Driver.zip](LinnStrument%20Windows%20Driver.zip) (the official one from Roger Linn Design).

**The flasher stopped partway.** Don't panic: your backup is safe in the `linnstrument-backups` folder, and the flasher prints the command to finish the job. If the LinnStrument is dark, it's waiting in its bootloader: run the command the flasher printed, or run the flasher again from a terminal with the backup it names and `--latest`, for example `./harmony-board-flasher-macos-apple-silicon --restore ~/linnstrument-backups/linnstrument-<date>.lsbackup --latest`. It flashes and restores in one go.

**"bossac can't run" on a Mac.** Install Rosetta (see step 1) and run the flasher again. Nothing was erased.

**My DAW doesn't see the LinnStrument after flashing.** It's probably still in OS Update mode, where it shows up as a serial device instead of MIDI. Press Global Settings and briefly press OS Update so its light turns off.

**Notes bend only a little, or slides sound wrong.** Your instrument's pitch bend range doesn't match: set it to 48 semitones, and make sure MPE is on both on the LinnStrument (Per-Split Settings) and in your DAW.

**The melody instrument also plays the left-hand chords.** It's listening on all MIDI channels. Set the melody track to channels 1–15 if your DAW allows it, or let us know which DAW you use (see below).

**The harmony zone is in a confusing state.** Hold **Split** to open the Harmony page, tap the red pad in the bottom-left corner (column 1), then tap it again within 3 seconds: the harmony, memories and Harmony page settings go back to how a fresh install starts.

**Something else is wrong.** Please [open an issue](https://github.com/rymorg/linnstrument-harmony-board/issues/new/choose) with your Harmony Board version, computer and DAW. Don't contact Roger Linn Design about the Harmony Board: it's an unofficial project.
