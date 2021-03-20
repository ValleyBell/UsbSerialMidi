# Arduino USB Serial MIDI Bridge

This is a small project that turns an Arduino into a USB-MIDI device on one side,
bridged to Serial MIDI on the other side.

## Basic build instructions

What you need:

- an Arduino that is supported by the "PluggableUSB" library (I used an Arduino Leonardo.)
- an RS-232 Interface IC 3.0V to 5.5V (e.g. a MAX3232)
- a few resistors
- an 8-pin MINI DIN ↔ D-SUB 9-pin cable (see [cable wiring diagram](cable-wiring.png), image sourced from a Yamaha manual)  

The wiring on the Arduino side is shown in [schematic.pdf](schematic.pdf).

The Arduino project `UsbSerialMidi.ino` contains the firmware for the USB Serial MIDI Bridge.
It uses the `USBMultiMIDI` class, which the Arduino IDE should automatically include in the project.

On the MIDI device, you need to move the `COMPUTER` (Roland) / `TO HOST` (Yamaha) select switch to "PC-2".

### Notes

- The firmware defaults to 1x MIDI In (device → host) and 4x MIDI out (host → device) ports.  
  You can change these values by editing `PORTS_IN` and `PORTS_OUT` in `UsbSerialMidi.ino`.
- Arduino pin usage layout:
  - pin 0: RS232 RX
  - pin 1: RS232 TX
  - pin 2: RS232 CTS
  - pin 3: RS232 RTS


Thanks a lot to:
- my father for doing all the soldering and wiring (I only wrote the firmware)
- the developers of the [Arduino MIDIUSB Library](https://github.com/arduino-libraries/MIDIUSB), which helped me a lot with getting USB MIDI to work


## Multi-Port USB MIDI driver

`USBMultiMIDI.cpp/hpp` is an USB MIDI library I wrote for the project.  
It allows you to setup your Arduino as USB MIDI device with up to 16 input and output ports.

Even though it is based on the Arduino MIDIUSB Library,
I rewrote most parts of it in order to allow an arbitrary number of USB MIDI ports.
("arbitrary" within the limit of 16 ports per USB endpoint, of course)

All configuration, which is the most complex stuff here, is done in `USBMultiMIDI::getInterface`.
I hope that the few comments help to get a basic understanding of how the setup of the USB MIDI interface works.

btw: It was really disappointing to see that the Arduino MIDIUSB library just copy-pasted the USB configuration example from the USB MIDI specifciation.
