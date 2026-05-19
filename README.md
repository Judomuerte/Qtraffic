# Qtraffic

Displays live WAN traffic from OPNsense on the Arduino UNO Q LED matrix.

RX shows on the bottom rows, TX on the top rows. Traffic is compared against a rolling baseline so the display stays calm during normal use and lights up on unexpected spikes.

Spike events are logged to `/var/log/arduino_traffic.log` on the board.

## Requirements

- Arduino UNO Q
- OPNsense with API access enabled

## Setup

Edit the config block at the top of `ArduinoApps/wifi/python/main.py`:

```python
OPNSENSE_HOST   = "Opnsense_IP"
OPNSENSE_KEY    = "YOUR_API_KEY"
OPNSENSE_SECRET = "YOUR_API_SECRET"
OPNSENSE_IFACE  = "wan_interface"
```

To get API credentials go to **System → Access → Users** in OPNsense, edit your user, scroll to **API keys** and click **+**. Make sure the user has Diagnostics privileges.

Open the app in Arduino App Lab and hit Run.

## File structure

```
ArduinoApps/wifi/
├── app.yaml
├── python/
│   └── main.py      # runs on the Linux side, polls OPNsense
└── sketch/
    └── sketch.ino   # runs on the MCU, drives the LED matrix
```
