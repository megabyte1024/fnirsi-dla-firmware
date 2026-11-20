# Open source firmware and bootloader for FNIRSI DLA-16 Plus

## Features

Firmware:

- More stable work: long, fast captures complete more often than with FNIRSI firmware.

Bootloader:

- Robust updates: everything written is checked, interrupted updates are retried, and a hung firmware can always be
  replaced.
- FNIRSI firmware can be installed again at any time.

## Limits

- Streaming only. The buffered mode is not supported.
- USB 3 only.

## Compatibility

FNIRSI firmware file holds MCU firmware and FPGA firmware. Project firmware is MCU firmware only; it works with FNIRSI
FPGA firmware.

Files that script `dla16_flash.py` writes through each bootloader:

| File | FNIRSI bootloader | Project bootloader |
|---|---|---|
| FNIRSI firmware file (`DLA32_V1.1_26052116.bin`) | works | works |
| Combined firmware file: project MCU firmware and FNIRSI FPGA firmware (made with script `dla16_pack.py`) | **never tested**; if it fails, repair needs case open (see "Optional: project bootloader with case open") | works |
| Project firmware (`build/dla16_firmware.bin`, MCU firmware only) | refused: FNIRSI bootloader installs MCU firmware only when FPGA firmware comes with it | works |
| FNIRSI MCU firmware alone (separate file, taken out of FNIRSI firmware file with script `dla16_pack.py`) | refused | works |
| FPGA firmware alone | refused | works |
| FNIRSI firmware file of DLA-32 Plus | refused | refused by `full` and `fpga`; `mcu` takes only its MCU firmware, which is the same as in DLA-16 Plus file |
| File with unknown FPGA firmware (for example newer FNIRSI version) | refused unless `--force` | refused unless `--force` |
| Unknown MCU firmware alone (for example firmware from another project) | refused | works |

"Works" means tested on an analyzer. FNIRSI bootloader column is based on earlier tests: no analyzer with FNIRSI
bootloader is available for tests now.

## Before you start

- Download FNIRSI firmware file for the DLA-16 Plus (for example `DLA32_V1.1_26052116.bin`) from FNIRSI download page.
  It is needed to restore FNIRSI firmware and to install project firmware through FNIRSI bootloader. FNIRSI names the
  files of both models `DLA32_...`: the one for the DLA-16 Plus ends with `16`, the one for the DLA-32 Plus with `32`.
- **Installing project bootloader is optional.** It replaces FNIRSI bootloader for good. FNIRSI bootloader cannot be
  read out of the chip first, so there is no way back to it. FNIRSI firmware can always be installed again (see "Back
  to FNIRSI firmware"). Ways to install project bootloader:
  - in place of FNIRSI bootloader, case open, by bridging SW4 (see "Optional: project bootloader with case open");
  - in place of FNIRSI bootloader, case closed, with the bootloader installer: **never tried** (see "With FNIRSI
    bootloader: never tried");
  - in place of an older project bootloader, case closed, with the bootloader installer (see "Install project
    bootloader without opening the case").
- Project firmware installed through FNIRSI bootloader has **never been tested** on an analyzer. If you try it, be ready
  to install project bootloader with case open (see "Optional: project bootloader with case open").
- Opening the case may void your warranty. Disconnect probes and signals, and touch something grounded before you
  touch the board.

## Prerequisites

Common to every platform:

- CMake 3.30 or newer and Ninja. KDE neon's own CMake is new enough; Ubuntu 24.04's own `cmake` package (3.28) is not,
  there install a newer one, for example with `sudo snap install cmake --classic`
- xPack GNU RISC-V Embedded GCC (built and tested with 15.2.0), its `bin` folder on `PATH`
- Git, to fetch the two USB library submodules in `third_party/`
- Python 3.10 or newer, to install the firmware on the analyzer (packages: see "Install")

## How to compile

The compiler is the [xPack GNU RISC-V Embedded GCC](https://xpack-dev-tools.github.io/riscv-none-elf-gcc-xpack/).
Unpack it in any folder and add its `bin` folder to your `PATH`. Instead of `PATH`, the folder can also be named
in the first `cmake` command: add `-Dxpack_bin=<its bin folder>`, in PowerShell in quotes: `"-Dxpack_bin=<its bin folder>"`.

Linux and macOS:

```sh
git clone https://github.com/megabyte1024/fnirsi-dla-firmware.git
cd fnirsi-dla-firmware
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-none-elf.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Windows, Command Prompt:

```bat
git clone https://github.com/megabyte1024/fnirsi-dla-firmware.git
cd fnirsi-dla-firmware
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-none-elf.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Windows, PowerShell:

```powershell
git clone https://github.com/megabyte1024/fnirsi-dla-firmware.git
cd fnirsi-dla-firmware
git submodule update --init
cmake -S . -B build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=cmake/riscv-none-elf.cmake" "-DCMAKE_BUILD_TYPE=Release"
cmake --build build
```

In an existing clone run only `git submodule update --init`. Do not add `--recursive` (neither to `git clone` nor to
`git submodule update`): it also fetches test repositories of the libraries, which the build does not need, and one of
them can only be fetched over SSH with GitHub key.

### Result

The build makes the three files to install:

- `build/dla16_boot.bin`: project bootloader;
- `build/dla16_firmware.bin`: project firmware (MCU firmware);
- `build/dla16_installer.bin`: the bootloader installer (see "Install project bootloader without opening the case").

## Install

### What you need

- Python 3.10 or later and two packages:

  ```
  pip install pyusb libusb-package
  ```

- scripts `tools/dla16_flash.py` and `tools/dla16_pack.py` from this repository (on Linux, if `pip`
  refuses to install into the system Python, use a virtual environment: `python3 -m venv venv`, then
  `venv/bin/pip install pyusb libusb-package` and run the scripts with `venv/bin/python`);
- to install project bootloader with case open: a small screwdriver and tweezers (or a short wire).

`python tools/dla16_flash.py status` shows what runs on the analyzer at any time.

### Windows drivers

Script `dla16_flash.py` needs the WinUSB driver, once for each of these devices:

| When | Name in Device Manager | USB ID |
|---|---|---|
| firmware running | WCH USB3.0 DEVICE | 1A86 5537 |
| bootloader running | WCH USB2.0 DEVICE | 1A86 5537 |
| WCH's chip bootloader (SW4 bridged, bootloader install only) | USB CH372/CH375 | 4348 55E0 |

To install WinUSB: start [Zadig](https://zadig.akeo.ie/), choose Options > List All Devices, pick the device (check
the USB ID), select WinUSB and press Install Driver (or Replace Driver).

If Device Manager shows the analyzer as "USB CH372/CH375", that is WCH's driver (installed with FNIRSI PC software).
Replace it with WinUSB the same way, or, once WinUSB was installed, in Device Manager: Update driver > Browse my
computer > Let me pick from a list > "WCH USB3.0 DEVICE". Windows can switch the analyzer back to WCH's driver when
firmware changes between FNIRSI firmware and project firmware; script `dla16_flash.py` then says so.

If WCH's chip bootloader disappears before Zadig has installed WinUSB, unplug, bridge SW4 and plug in again until the
driver is installed. Then unplug and start "Optional: project bootloader with case open".

### Linux permissions

Allow your user to access the analyzer and the chip bootloader, for example with a file
`/etc/udev/rules.d/60-dla16.rules`:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="5537", TAG+="uaccess"
SUBSYSTEM=="usb", ATTR{idVendor}=="4348", ATTR{idProduct}=="55e0", TAG+="uaccess"
```

then `sudo udevadm control --reload` and replug the analyzer.

### Project firmware

With project bootloader:

```
python tools/dla16_flash.py mcu build/dla16_firmware.bin
```

The update takes about 10 seconds.

With FNIRSI bootloader, project firmware is first packed with FNIRSI FPGA firmware into combined firmware file,
then installed with `full` (**never tested**, see "Before you start"):

```
python tools/dla16_pack.py pack --mcu build/dla16_firmware.bin --vendor DLA32_V1.1_26052116.bin -o combined.bin
python tools/dla16_flash.py full combined.bin
```

This takes a few minutes. At the end, unplug the analyzer and plug it in again.

### Back to FNIRSI firmware

With project bootloader:

```
python tools/dla16_flash.py mcu DLA32_V1.1_26052116.bin
```

The script installs MCU firmware from FNIRSI firmware file; FPGA firmware on the analyzer stays as it is. FNIRSI
firmware then runs as before; the bootloader stays, and `mcu` installs project firmware again at any time.

With FNIRSI bootloader:

```
python tools/dla16_flash.py full DLA32_V1.1_26052116.bin
```

At the end, unplug the analyzer and plug it in again.

### MCU firmware and FPGA firmware: `full`

```
python tools/dla16_flash.py full DLA32_V1.1_26052116.bin
```

`full` installs whole firmware file (FNIRSI firmware file, or combined firmware file made by script `dla16_pack.py`):
MCU firmware, then FPGA firmware.

- With project bootloader, the script checks both parts after writing them. If the analyzer holds the FPGA firmware
  already, only MCU firmware is written; `--rewrite` writes FPGA firmware anyway.
- With FNIRSI bootloader, both parts are written in one go. FPGA firmware cannot be read back there, so the script
  cannot check it.
- With an older build of project bootloader, the script says so and writes nothing.
- FPGA firmware of FNIRSI firmware file for this model is written without questions; the one of DLA-32 Plus is
  refused. Any other FPGA firmware is refused unless you add `--force`.
- At the end, unplug the analyzer and plug it in again: the FPGA loads its firmware only at power-up.
- If the transfer stops half way, the script says that FPGA firmware is incomplete. Do not unplug the analyzer then: it
  cannot capture until you run the command again. If the script says that the analyzer does not answer, unplug it,
  plug it in again and run the command again.

### FPGA firmware only: `fpga`

```
python tools/dla16_flash.py fpga DLA32_V1.1_26052116.bin
```

`fpga` writes FPGA firmware only; MCU firmware on the analyzer stays as it is. Its file is FNIRSI firmware file, combined
firmware file, or a file with FPGA firmware alone.

- Only with project bootloader. With FNIRSI bootloader the script says so and writes nothing; use `full` there.
- The script checks FPGA firmware after writing it. If the analyzer holds it already, nothing is written; `--rewrite`
  writes it anyway.
- With an older build of project bootloader, the script says so and writes nothing.
- FPGA firmware of FNIRSI firmware file for this model is written without questions; the one of DLA-32 Plus is
  refused. Any other FPGA firmware is refused unless you add `--force`.
- At the end, unplug the analyzer and plug it in again: the FPGA loads its firmware only at power-up.
- If the transfer stops half way, the script says that FPGA firmware is incomplete. Do not unplug the analyzer then: it
  cannot capture until you run the command again. If the script says that the analyzer does not answer, unplug it,
  plug it in again and run the command again.

`python tools/dla16_pack.py info FILE` shows what a firmware file contains; `unpack FILE -d DIR` writes its parts as
files, for example FPGA firmware alone.

### Optional: project bootloader with case open

1. Unplug the analyzer. Remove the screws visible on the back and lift the back plate off.
2. Find SW4 on the bottom side of the board: a button footprint with two pads and no button fitted. One pad is
   ground. Hold the board with the USB socket on the right: SW4 is in the top right corner, above the USB
   socket and a little to the left of it.

   ```
   bottom side of the board, USB socket on the right

   +----------------------------------------------------+
   |  O                                         _    O  |
   |                      o o o o o o        S | |      |
   |                                         W | |      |
   |                                         4  -       |
   |                                                  o |
   |                                                  o |
   |                                                  o |
   |  |||||               X1                            |
   |  |||||                                             |
   |  |||||                                     +-------|
   |  |||||                                     |  USB  |
   |  |||||                                     +-------|
   |  |||||                                             |
   |  |||||                                             |
   |  |||||                                             |
   |  |||||                                             |
   |                                                    |
   |                                                    |
   |                                                    |
   |            U17                                 O   |
   |  O                                                 |
   |                                                    |
   +----------------------------------------------------+
   ```

   The row on the left is the input connector, the six dots at the top are a 6-pin header, O marks the screw posts
   and the three small dots on the right edge are pads.

3. Start the script:

   ```
   python tools/dla16_flash.py bootloader build/dla16_boot.bin build/dla16_firmware.bin
   ```

4. When the script asks: bridge the two SW4 pads (tweezers or a short wire) and, while they are bridged, plug the
   USB cable in. The script waits up to 5 minutes. The SW4 pads are usually oxidized: **press the tweezers firmly onto
   both pads, a light touch often makes no contact.**
5. As soon as the script says "found", remove the tweezers. The script writes the bootloader, checks it, starts it
   and installs the firmware.
6. It ends with a message that the firmware is running. Put the back plate on again.

If it stops before "Bootloader written and verified", start over: bridging SW4 always works.

If it stops after that line, the bootloader is in place and only the firmware is missing. The analyzer then waits in
its bootloader (on Windows it may first need WinUSB as "WCH USB2.0 DEVICE"); continue with "Project firmware".

### Install project bootloader without opening the case

On an analyzer that has project bootloader, the bootloader installer puts the bootloader of your build in place, for
example a newer one. The build makes `build/dla16_installer.bin` next to the bootloader and the firmware. Do not unplug
the analyzer while the script runs.

1. Install the installer like MCU firmware:

   ```
   python tools/dla16_flash.py mcu build/dla16_installer.bin
   ```

2. The script waits until the analyzer answers from the new bootloader and ends with "Bootloader installed. Now
   install MCU firmware: python tools/dla16_flash.py mcu FILE" and exit code 0.
3. Install the firmware:

   ```
   python tools/dla16_flash.py mcu build/dla16_firmware.bin
   ```

If the status light alternates red and blue, the installer failed. The script then ends with an error and exit code 1
because the analyzer does not come back on USB. Unplug the analyzer and plug it in again; if it answers, run the
installer again. If it does not answer, install the bootloader as in "Optional: project bootloader with case open".

#### With FNIRSI bootloader: never tried

The installer may also replace FNIRSI bootloader with the case closed. **This has never been done on an analyzer.** If it
fails, the analyzer can always be repaired by bridging SW4 with the case open (see "Optional: project bootloader with
case open").

1. Pack the installer with FNIRSI FPGA firmware into combined firmware file (FNIRSI bootloader installs MCU firmware
   only together with FPGA firmware):

   ```
   python tools/dla16_pack.py pack --mcu build/dla16_installer.bin --vendor DLA32_V1.1_26052116.bin -o installer_combined.bin
   ```

2. Install it:

   ```
   python tools/dla16_flash.py full installer_combined.bin
   ```

3. If it works, the script ends with "Bootloader installed. Now install MCU firmware: ...". Unplug the analyzer and plug it
   in again.
4. Install project firmware (on Windows project bootloader may first need WinUSB as "WCH USB2.0 DEVICE"):

   ```
   python tools/dla16_flash.py mcu build/dla16_firmware.bin
   ```

If FNIRSI bootloader refuses the file or the installer does not start, FNIRSI bootloader stays in place: install FNIRSI
firmware file with `full` (see "Back to FNIRSI firmware"). If the analyzer does not answer at all, install project
bootloader with the case open (see "Optional: project bootloader with case open").

### If something goes wrong

- "no DLA-16 found on USB": check the cable, and that the analyzer is plugged into a USB 3 port.
- "cannot open analyzer": on Windows the analyzer has no WinUSB driver (see "Windows drivers"); on Linux see
  "Linux permissions".
- "Windows switched analyzer to WCH's driver": the update worked; switch the driver back to WinUSB before the next
  update (see "Windows drivers").
- "analyzer runs FNIRSI's bootloader" after `mcu` or `fpga`: nothing was written; use `full` (see "Project firmware").
- A firmware that does not start: the analyzer stays in its bootloader, and `mcu` installs it again.
- Nothing answers at all: install the bootloader again (see "Optional: project bootloader with case open").

## Repository layout

- `mcu/`: MCU firmware, bootloader and bootloader installer (C for the CH569; built by the root `CMakeLists.txt`).
- `fpga/`: FPGA design for the analyzer's Pango PGL22G (not written yet; see `fpga/README.md`).
- `tools/`: install scripts `dla16_flash.py` and `dla16_pack.py`.
- `cmake/`: toolchain file and build job limit.
- `third_party/`: the two USB libraries (git submodules) and the patch the build applies to one of them.

## Licence

The firmware, the bootloader and the scripts are MIT licensed (`LICENSE`). The libraries in `third_party/`
(`wch-ch56x-lib` and `wch-ch56x-bsp`, from the HydraUSB3 project) keep their Apache-2.0 licence (see `third_party/README.md`).
Binaries are distributed with the licence texts of both libraries and with `third_party/README.md`.
