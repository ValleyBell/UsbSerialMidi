# Serial MIDI PC Tools

## COM-Port MIDI Player

This is a small program that plays back MIDI files over the Serial port.
I used this to test the CTS/RTS flow control settings and multi-port mode.

Usage:
- `comMidiPlay.exe COM1 "file.mid"`
- `comMidiPlay.exe \\.\COM50 "file.mid"` (You need a special prefix for COM ports >=10 because Windows.)

There are only very basic playback controls.
- `Space` pauses/resumes. (It is very basic and will just freeze playback with hanging notes.)
- `ESC` / `Q` quits.
