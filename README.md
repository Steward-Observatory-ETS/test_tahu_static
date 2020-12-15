This is a proof of concept project that provides Sparkplug B functionallity to
a Teensy 4.1 with Arduino.

protobufs (pb) and tahu source is copied from https://github.com/eclipse/tahu
tahu.c has been modified to allow it to be compiled in Arduino

test_client.c is used to command the LED state, builds/runs in Debian 9.9

MMQT.fx can be used to view MMQT traffic ( http://www.mqttfx.jensd.de/ )

Mosquitto is used as the MQTT broker ( https://mosquitto.org/ )
