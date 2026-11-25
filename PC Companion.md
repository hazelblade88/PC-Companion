# 🖥️ PC Companion

**A DIY smart desktop companion that brings PC monitoring, music controls, animations, and system power management to a compact OLED display.**

PC Companion is an ESP8266-based desktop companion that communicates with a Windows PC through USB serial communication. It uses a 0.96-inch OLED display and a touch sensor to provide quick access to hardware monitoring, music controls, screensavers, clock, and PC power operations.

Instead of switching between applications to check system information or control media playback, you can access these features directly from a small display on your desk.

## ✨ Features

### 📊 Hardware Monitoring
- Real-time CPU temperature monitoring.
- Real-time GPU temperature monitoring.
- Live FPS monitoring.
- Displays PC connection status.

### 🎵 Music Control
- Displays currently playing song title and artist.
- Shows playback progress.
- Play/Pause control.
- Next-track control.
- Animated rotating vinyl record interface.

### 🎨 Animated Screensavers
Includes five built-in animations:
- Stars
- Matrix
- Cube
- Radar
- Waves

### ⚡ PC Power Management
- Put the Windows PC into Sleep mode.
- Shut down the PC.
- Confirmation interface to prevent accidental actions.

### 🕒 Digital Clock
- Displays current time.
- Displays date.
- Automatically synchronizes with Windows through serial communication.

## 🛠️ Hardware Requirements

| Component | Description |
|---|---|
| Microcontroller | ESP8266 NodeMCU / Wemos D1 Mini |
| Display | 0.96-inch SSD1306 OLED |
| Input | Touch sensor module |
| Connection | USB cable |
| PC | Windows computer |

## 🔌 Hardware Connections

### OLED Display (I2C)

| OLED Pin | ESP8266 Pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | D2 (GPIO4) |
| SCL | D1 (GPIO5) |

### Touch Sensor

| Touch Sensor Pin | ESP8266 Pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| OUT | D5 (GPIO14) |

## 💻 Software Requirements

### ESP8266
- Arduino IDE
- ESP8266 Board Package
- Adafruit SSD1306 Library
- Adafruit GFX Library

### Windows PC
- Python 3
- LibreHardwareMonitor
- MSI Afterburner
- RivaTuner Statistics Server (RTSS)

### Python Dependencies

Install the required packages:

```bash
pip install pyserial requests
```

For Windows media session integration:

```bash
pip install winrt-runtime winrt-Windows.Media.Control winrt-Windows.Foundation winrt-Windows.Foundation.Collections
```

## ⚙️ Installation and Setup

### 1. Upload ESP8266 Firmware

1. Install Arduino IDE.
2. Install the ESP8266 board package.
3. Install Adafruit SSD1306 and Adafruit GFX libraries.
4. Open `pc_companion.ino`.
5. Select your ESP8266 board and COM port.
6. Upload the firmware.

### 2. Configure Windows Hardware Monitoring

**CPU and GPU temperatures:**

1. Install LibreHardwareMonitor.
2. Run it with administrator privileges.
3. Enable the Remote Web Server.
4. Use the default port `8085`.

**FPS monitoring:**

1. Install MSI Afterburner (with RTSS).
2. Keep RTSS running in the background.

### 3. Configure Python Application

Open the Python file and configure the serial port:

```python
COM_PORT = "COM7"
BAUD = 9600
```

Change `COM7` to the COM port assigned to your ESP8266.

Install the required Python dependencies and run:

```bash
python pc_companion.py
```

## 🎮 Menu Navigation

The OLED interface contains five main pages.

| Menu | Function |
|---|---|
| MONITOR | CPU, GPU and FPS monitoring |
| MUSIC | Music information and playback controls |
| SCREENSAVER | Five animated screensavers |
| POWER | Sleep and shutdown controls |
| CLOCK | Digital clock and date |

### Touch Controls

| Action | Function |
|---|---|
| Tap on normal pages | Move to next page |
| Tap on Music | Play/Pause |
| Hold on Music | Next track |
| Hold on Screensaver | Change animation |
| Tap on Power | Change selection |
| Hold on Power | Select / Confirm |
| Long hold on locked pages | Exit the page |

## 🔄 Communication Architecture

The project uses USB serial communication between the ESP8266 and Windows PC.

**Windows PC → ESP8266**

The Python application sends the following information:

```text
FPS|CPU_TEMP|GPU_TEMP|TIME|DATE|PLAYING|TITLE|ARTIST|POSITION|DURATION
```

Data is transmitted approximately every 500 milliseconds.

**ESP8266 → Windows PC**

The ESP8266 sends commands when the touch interface is used:

```text
CMD:PLAY
CMD:NEXT
CMD:SLEEP
CMD:SHUTDOWN
```

The Python application receives these commands and performs the corresponding Windows operations.

## 📁 Project Structure

```text
PC-Companion/
│
├── pc_companion.ino
│
├── pc_companion.py
│
└── README.md
```

## 🚀 Future Improvements

- More OLED interface themes.
- Customizable screensaver animations.
- Additional PC monitoring features.
- Improved interface and navigation.
- Wireless communication support.
- More desktop control functions.

## 👨‍💻 Project

**PC Companion** is a personal DIY hardware and software project combining embedded systems, Python programming, Windows integration, and OLED interface design.

Built using ESP8266, Arduino IDE, Python, and Windows APIs.

---

⭐ If you like this project, consider giving the repository a star!

