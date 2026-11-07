// Setting the robot up and updating it with no USB console (the USB port is the servo bus):
//  - a web page on port 80 (status, Wi-Fi / server / device settings, restart) and /update for a
//    firmware .bin, login user "xelulu" + Config.h NET_ADMIN_PASS (none needed on the hotspot);
//  - the Arduino IDE network port (ArduinoOTA), same password;
//  - a setup hotspot named netHost() while no Wi-Fi works (none saved, or 30 s without it);
//  - a few status lines on the face while the robot is not connected to the server.
#pragma once
#include <Arduino.h>

void portalBegin();   // after netBegin() and otaBegin()
