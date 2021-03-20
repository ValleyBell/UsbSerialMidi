# Arduino USB ↔ Serial MIDI

This project turns an Arduino into a "USB MIDI to Serial" bridge.  
This way you can use USB to send MIDI data to the Serial port present on many 1990s MIDI modules.

This project also serves as a documentation of how Serial MIDI works.

## Folders and files

- `arduino` - USB ↔ Serial MIDI bridge for Arduino Leonardo  
  It includes schematics and the Arduino project.
  It also contains a USB MIDI library supporting multiple MIDI input/output ports.
- `pc-tools` - tools for Windows that I wrote while researching/testing MIDI playback via my PC's COM port
- `SerialMIDI.txt` - documentation on how Serial MIDI works (port settings, protocol, etc.)
