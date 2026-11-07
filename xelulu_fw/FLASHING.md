# Flashing Xelulu firmware 0.3.0 with the Arduino IDE

You need the robot with its **battery ON** (USB alone does not power the screen and sensors), a USB cable
in the board's **native USB port**, and Arduino IDE 2.x.

## 1. One-time setup

1. **Board package:** Tools > Board > Boards Manager, search `esp32`, install **esp32 by Espressif Systems,
   version 3.3.10** (pick 3.3.10 in the version list, not the newest).
2. **Libraries:** copy the five folders in this package's `libraries/` folder into your Arduino libraries
   folder (File > Preferences shows the *Sketchbook location*; `libraries` is inside it, usually
   `Documents\Arduino\libraries`). Replace them if asked, then restart the IDE.
   They are TFT_eSPI 2.5.43, ArduinoJson 7.4.3, VL53L0X 1.3.1 (Pololu), WebSockets 2.7.2 (Markus Sattler)
   and libopus. libopus is not in the Library Manager, so it must come from this package.

## 2. Open the sketch and set the Tools menu

1. File > Open > `xelulu_fw/xelulu_fw.ino`. Keep the folder whole: `partitions.csv` and `build_opt.h`
   must stay next to the `.ino`.
2. Optional: in the `Config.h` tab, fill in `NET_WIFI_SSID`, `NET_WIFI_PASS` and `NET_SERVER` (the IP of
   the PC that runs the Xelulu stack). You can leave them empty and set them from a phone later (step 4).
3. Tools menu:

| Menu | Choose |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| Flash Size | 16MB (128Mb) |
| Partition Scheme | Custom |
| PSRAM | OPI PSRAM |
| Flash Mode | QIO 80MHz |
| Upload Mode | UART0 / Hardware CDC |
| Erase All Flash Before Sketch Upload | Disabled |
| Port | the COM port that appears when the robot is plugged in |

If one of these is wrong, the build stops with an error that names the menu item to change.

## 3. Upload

Sketch > Upload. **The first build takes 10 to 15 minutes** (the voice codec library is large); later
builds take a minute or two.

If no port appears, or the upload cannot connect: hold the board's **BOOT** button, plug the USB in,
release BOOT, then Upload again. When it finishes, press **RESET** (or switch the robot off and on).

## 4. Connect the robot to the stack (the developer console)

**On the PC that runs the Xelulu stack:**

- In the stack's root `.env`, set `DEVICE_BIND_HOST=0.0.0.0`, then `docker compose up --build -d`.
- **Stop the simulator:** `docker compose stop simulator`. It logs in as the same device (`xlu_000123`), so
  the broker would keep kicking the robot off.
- Let ports **1883** and **8081** in through the Windows firewall (or set the Wi-Fi network to Private).
- Find the PC's IP with `ipconfig` (the IPv4 Address of its Wi-Fi adapter).
- Console: `pnpm dev:admin:device`, then open `http://127.0.0.1:5173/#/devices`.

**On the robot:** if the Wi-Fi in `Config.h` was empty or does not work, then within 30 s the screen shows
`Wi-Fi setup / Join Wi-Fi xelulu-xxxxxx / then open 192.168.4.1`.

1. On a phone, join the Wi-Fi `xelulu-xxxxxx`. The password is `xelulu123` (`NET_ADMIN_PASS` in `Config.h`).
2. The setup page opens by itself (otherwise browse to `http://192.168.4.1`).
3. Pick your Wi-Fi (2.4 GHz only), type its password, and set **Server** to the PC's IP (for example
   `192.168.1.50`). Leave the device id and token as they are: they match the stack's defaults. Then Save.
   The robot restarts and joins.

**What the robot's screen tells you:**

| Screen | Meaning |
|---|---|
| `Connected <ip>` / `pairing XXXXXX` | Done. The Devices tab shows `xlu_000123` with the same pairing code. |
| `Server <ip>` / `no answer` | Wrong PC IP, the stack is not running, `DEVICE_BIND_HOST` is not `0.0.0.0`, or the firewall. |
| `token refused` | The device token is not the stack's `DEVICE_TOKEN`. |
| `kicked off (simulator on?)` | Something else uses `xlu_000123`: stop the simulator. |

If the Devices tab says **Unpaired** or **Offline** while the robot said `Connected`, the gateway was
started after the robot and missed its hello. Restart the robot (its web page has a Restart button).

The robot's own page (status and settings): `http://<robot IP>/` or `http://xelulu-xxxxxx.local/`,
user `xelulu`, password `xelulu123`.

The phone **parent app** cannot connect to the robot yet: in the xelulu repo it is screens with mock data
only (pairing is planned for a later phase). The robot shows up in the developer console.

## 5. Update over Wi-Fi (no USB)

The robot and the PC must be on the same Wi-Fi.

- **Arduino IDE:** Tools > Port > the network port `xelulu-xxxxxx at 192.168.x.y`, then Upload. The password
  is `xelulu123`.
- **Browser:** Sketch > Export Compiled Binary (it writes `build/.../xelulu_fw.ino.bin`), then open
  `http://<robot IP>/update`, log in, choose `xelulu_fw.ino.bin` (not the `.merged.bin`) and press Update
  Firmware.

The robot shows `Updating firmware`, stops moving and restarts into the new firmware. If the new firmware
cannot join the Wi-Fi, switch the robot off and on: it goes back to the previous firmware.

## Good to know

- With USB plugged in at boot, the console is on and the **servos are off** (they share the USB pins). For
  servos, boot without USB. After this first flash, updates can go over Wi-Fi.
- Change `NET_ADMIN_PASS` in `Config.h` before flashing if the robot leaves a trusted network.
- Cannot build at all? `prebuilt/` in this package holds the same firmware (Config.h network values empty)
  as the four images the Arduino upload writes. Flash them with https://espressif.github.io/esptool-js/ over
  the native USB port at these addresses: `0x0` xelulu_fw.ino.bootloader.bin, `0x8000`
  xelulu_fw.ino.partitions.bin, `0xe000` boot_app0.bin, `0x10000` xelulu_fw.ino.bin. Then set the Wi-Fi with
  step 4. Later Wi-Fi updates use `xelulu_fw.ino.bin`.
