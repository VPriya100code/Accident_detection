ACCIDENT DETECTION 
========================

FILES
-----
1. accident_detection_esp32.ino
2. accident_dashboard.py

HARDWARE PINS
-------------
SW-420 DO       -> GPIO 19
KY-038 AO       -> GPIO 34
HC-SR04 TRIG    -> GPIO 5
HC-SR04 ECHO    -> GPIO 18 through 1k/2k voltage divider
OLED SDA        -> GPIO 21
OLED SCL        -> GPIO 22
LED             -> GPIO 25 through 220 ohm resistor
Buzzer          -> GPIO 23
Push Button     -> GPIO 4 to GND
SIM800L TX      -> GPIO 16
SIM800L RX      -> GPIO 17

IMPORTANT POWER
---------------
- HC-SR04 VCC: 5V
- OLED, SW-420, KY-038: 3.3V
- All modules must share common GND.
- Do NOT power SIM800L from ESP32 3.3V.
- SIM800L needs a suitable high-current power arrangement.
- TP4056 is for Li-ion charging/protection; it is not a regulated 5V supply.

ARDUINO IDE
-----------
1. Install ESP32 board support.
2. Install:
   - Adafruit GFX Library
   - Adafruit SSD1306
3. Open accident_detection_esp32.ino.
4. Replace PHONE_NUMBER with your real number.
5. Select the ESP32 DevKit V1 board.
6. Select the correct COM port.
7. Upload.
8. Open Serial Monitor at 115200 baud.

FIRST SENSOR TEST
-----------------
Run the uploaded code.
Watch:
- Vibration
- Sound
- Distance
- Confidence

CALIBRATION
-----------
The default prototype values are:
- SOUND_THRESHOLD = 1800
- PROXIMITY_THRESHOLD_CM = 20
- Accident threshold = 60
- Required consecutive readings = 3

These are not universal safety standards. Adjust them after observing your own sensors.

PYTHON DASHBOARD
----------------
Install pyserial:

    python -m pip install pyserial

Run:

    python accident_dashboard.py

Select the ESP32 COM port (for example COM7) and click Connect.

CONTROLS
--------
RUN DEMO ALERT
- Sends command D.
- Generates a clearly marked software demo event.
- Can send an SMS.

RESET SYSTEM
- Sends command R.
- Clears the event and returns to monitoring.

GET STATUS
- Sends command S.
- Prints current status.

