# CYD Chess  (ESP32-2432S028 "Cheap Yellow Display")

Touch-screen chess with a green chess.com-style board, **pass & play** for two people, and a
**built-in engine** (4 levels) - no Wi-Fi or phone needed.

## Option A - no tools on your PC (recommended): get a ready-to-install link
1. Create a free GitHub repository and upload this whole folder to it (branch `main`).
2. Repo **Settings -> Pages -> Source: GitHub Actions**.
3. The *Build and publish flasher* action builds the firmware and publishes the flasher page
   (a few minutes; see the **Actions** tab). Your link: `https://<you>.github.io/<repo>/`
4. Open that link in Chrome/Edge, press **CONNECT & FLASH**, pick the CYD's serial port, tick
   **Erase device**, press **Install**. Nothing to install locally, and the link keeps working.
The merged `.bin` is also downloadable from the action run's artifacts.

## Option B - build on your own PC

1. Install Python and PlatformIO once:  `pip install platformio`
2. Plug the CYD in with a USB **data** cable.
3. Run **flash.bat** (Windows) / `./flash.sh` (Mac/Linux) / `python flash.py`.
   It builds the firmware, then opens a page in your browser (use **Chrome or Edge**).
4. Press **CONNECT & FLASH** -> the browser asks which serial port to connect to -> pick the CYD
   (CH340 / USB Serial).
5. Tick **Erase device** (wipes the flash first), press **Install**.

The first build downloads the ESP32 toolchain (a few minutes). The page loads the flashing
tool from the internet (unpkg), so you need to be online. Press Ctrl+C in the terminal when done.

No browser? `python flash.py --cli` does it in the terminal instead: lists the serial ports,
you pick one, then it erases and flashes (`--port COM5`, `--list`, `--no-erase`, `--monitor`).

If it says it can't connect: hold the **BOOT** button on the CYD while pressing Install, and make
sure the CH340 driver is installed on Windows/Mac.

## First boot
After a fresh erase the board asks you to tap 3 crosshairs (touch calibration, saved to flash).
You can redo it any time from the main menu -> CALIBRATE TOUCH.

## Playing
* **Pass & Play** - two players, one screen. `FLIP` turns the board; `ROT` auto-turns it so the
  side to move is always at the bottom.
* **Vs Computer** - pick your colour and level: 1 Beginner (makes random mistakes), 2 Easy,
  3 Medium (~2.5 s/move), 4 Hard (~5.5 s/move).
* Tap a piece, tap a destination (dots = moves, ring = capture). Pawn promotion shows a picker.
* `UNDO` takes back a move (in vs-computer mode: your move plus the reply). `MENU` returns to the menu.
* Rules implemented: castling, en passant, promotion, check/checkmate/stalemate, 50-move rule,
  threefold repetition, insufficient material.

## Troubleshooting
* Colours look inverted -> in `platformio.ini` uncomment `-DCYD_INVERT_DISPLAY=1`.
* Red/blue swapped -> uncomment `-DTFT_RGB_ORDER=TFT_BGR`.
* Blank/garbled screen on some newer CYD revisions (the ones with two USB ports) -> change
  `-DILI9341_2_DRIVER=1` to `-DST7789_DRIVER=1` (and try the invert flag above).
* Touch is off -> recalibrate from the menu.

## What was tested (on a PC, not on your board)
`tools/host-tests/`:
* `test_engine.cpp` - move generator checked against the published *perft* node counts for the
  standard test positions (start, Kiwipete, positions 3-6; up to 4.8 million nodes), plus rule
  tests, tactics, and full engine self-play games.
* `ui_sim.cpp` - runs the real `main.cpp` with simulated taps (menus, moves, promotion, undo,
  engine replies).
Run: `g++ -O2 -std=gnu++11 -I../../src test_engine.cpp -o te && ./te`
