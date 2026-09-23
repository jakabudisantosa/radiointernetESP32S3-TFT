System Overview

This program is an ESP32-S3-based Internet Radio system designed to receive radio broadcasts via the internet and output audio through an I2S audio system. The system features an ST7735 TFT display as the user interface.

Key functions include:

1. Internet Radio
The ESP32 can receive various radio stations via streaming URLs and supports audio formats such as MP3 and AAC.
2. Radio Database
The radio list includes station names, streaming addresses, and codec types. Radio data is stored persistently, ensuring it remains available after the device is powered off.
3. Web-based Radio Management
Users can access the ESP32 via a web browser to add, edit, delete, reorder, and select radio stations.
4. WiFi Connection
The device stores WiFi configurations and automatically attempts to reconnect if the connection is lost.
5. Captive Portal
If WiFi is unavailable or configuration changes are required, the ESP32 can create its own WiFi network, allowing users to configure WiFi settings via a browser.
6. PSRAM Buffering
PSRAM is used as an audio buffer to ensure smooth streaming, particularly during unstable internet connections or when using HTTPS.
7. Audio Management
The system handles stream reception, audio decoding, transmission to the amplifier via I2S, as well as stream stopping and reconnection.
8. Volume Control
Volume can be adjusted using physical buttons or the web interface. Volume settings are saved persistently.
9. ST7735 TFT Display
The display shows information such as:
radio station name, streaming status, volume level, time, date, alarm status,  alarm settings, WiFi configuration details.
10. Radio and Clock Modes
The device has two primary functions:
Radio Mode for listening to streams.Clock/NTP Mode for displaying internet time.NTP Clock Time is obtained from an internet server, allowing the device clock to synchronize automatically and serve as the basis for alarms.
11. Radio Alarm
Users can set an alarm time and select a radio station to play. When the scheduled time arrives, the device switches to radio mode and begins playing the selected station.
12. Button Controls
The buttons support both short and long presses, allowing various functions to be controlled without needing a web browser.
13. Display Backlight
The system manages TFT backlight activity to ensure the screen does not remain on when not in use.
14  Automatic Reconnection
If the WiFi or radio stream disconnects, the program includes a mechanism to attempt reconnection.
15 Simplified Overview

The ESP32-S3 serves as the central controller for the entire system:

WiFi/Internet → Radio Streaming → PSRAM Buffer → MP3/AAC Decoder → I2S → Amplifier/Speaker

Users control the system via:
1. Buttons + TFT + Web Browser
2. Configuration settings—such as WiFi, radio stations, volume, and alarms—are saved, allowing the system to resume operation after a restart.

In summary: the program creates a comprehensive, standalone Internet Radio system featuring audio streaming, multi-station management, WiFi configuration, web-based control, a TFT display, an NTP clock, a radio alarm, configuration storage, and automatic connection recovery.

# radiointernetESP32S3-TFT
