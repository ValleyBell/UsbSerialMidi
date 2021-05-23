// USB-Serial MIDI driver
// Valley Bell, 2021-02-27

// Tested successfully with:
//	- Roland SC-88VL (2 Ports: A, B)
//	- Roland SC-88Pro (2 Ports. A, B)
//	- Roland SC-8820 (2 Ports. A, B)
//	- Yamaha MU128 (4 Ports: A, B, C, D)
//	- Korg NS5R (3 Ports: MIDI Out, A, B)

#include <stdint.h>
#include "USBMultiMIDI.hpp"


#define CTS_FLOW_CONTROL	0
#define PIN_CTS	2
#define PIN_RTS	3

#define PORTS_IN	1	// host-side MIDI in (USB TX)
#define PORTS_OUT	4	// host-side MIDI out (USB RX)

static const uint8_t USB_EVT_LEN[0x10] =
{
	0, 0, 2, 3, 3, 1, 2, 3,
	3, 3, 3, 3, 2, 2, 3, 1,
};
static const uint8_t MIDI_USB_EVT_TYPE_MAIN[0x10] =
{
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,	// for invalid commands, just assume "single byte" USB command
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x00,	// MIDI 8x..Ex = USB 08..0E, MIDI Fx is special
};
static const uint8_t MIDI_USB_EVT_TYPE_FX[0x10] =
{
	0xF0, 0x02, 0x03, 0x02, 0x00, 0x02, 0x05, 0xF0,	// System Common messages
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,	// System Real Time messages
};

static void FlushSrlUsbData(void);
static void ProcessSerialData(uint8_t data);


static USBMultiMIDI midiMod(PORTS_OUT, PORTS_IN);
static int lastPort = -1;
static unsigned long ledOffTime = 0;

// status variables for Serial -> USB
static uint8_t suCurCmd = 0x00;
static uint8_t suRunStatus = 0x00;
static uint8_t suRemLen = 0x00;
static uint8_t suBufPos = 0x00;
static midiEventPacket_t suPkt;

void setup()
{
	Serial.begin(115200);
	pinMode(LED_BUILTIN, OUTPUT);
	
	// Pin layout
	//	Pin 0: RS232 RX (in)
	//	Pin 1: RS232 TX (out)
	//	Pin 2: RS232 CTS (in)
	//	Pin 3: RS232 RTS (out)
	pinMode(PIN_CTS, INPUT);
	pinMode(PIN_RTS, OUTPUT);
	// CTS/RTS flow control:
	//	CTS: This is usually kept LOW.
	//	RTS: LOW = can send data, HIGH = suspend data stream
	digitalWrite(PIN_RTS, LOW);	// Roland SC devices wait for it to go LOW before sending data
	Serial1.begin(38400);
	
	// When there are more than 1 port, enforce sending Port Select before the first actual command.
	lastPort = (PORTS_OUT > 1) ? -1 : 0;
	suPkt.hdr.cn = 0;	// default to first port
	
	return;
}

static void WriteSerial(const uint8_t* data, uint8_t len)
{
#if ! CTS_FLOW_CONTROL
	Serial1.write(data, len);
	int state = digitalRead(PIN_CTS);
	if (state == HIGH)
	{
		//Serial.print("CTS state: ");	Serial.println(state);
		if (! ledOffTime)	// don't need to turn on when it's already on
			digitalWrite(LED_BUILTIN, state);
		ledOffTime = millis() + 100;	// keep on for 100 ms
	}
#else
	// send while taking CTS into account
	// TODO: Can this be done more efficiently? (I don't like the explicit flushing here.)
	uint8_t pos;
	for (pos = 0; pos < len; pos ++)
	{
		Serial1.flush();
		int state = digitalRead(PIN_CTS);
		if (state == HIGH)
		{
			if (! ledOffTime)	// don't need to turn on when it's already on
				digitalWrite(LED_BUILTIN, state);
			ledOffTime = millis() + 100;	// keep on for 100 ms
		}

		// poll port until CTS gets LOW (== ready)
		while(state == HIGH)
			state = digitalRead(PIN_CTS);
		// then send the data
		Serial1.write(data[pos]);
	}
#endif
	return;
}

static void FlushSrlUsbData(void)	// flush Serial -> USB data packet
{
	midiMod.sendMIDI(suPkt);
	midiMod.flush();
	
	suBufPos = 0x00;
	memset(suPkt.data, 0x00, 0x03);
	return;
}

static void ProcessSerialData(uint8_t data)
{
	if (suCurCmd == 0xF0)
	{
		if (data < 0xF0 || data == 0xF7)
		{
			suPkt.data[suBufPos] = data;
			suBufPos ++;
			if (data == 0xF7)	// SysEx End
			{
				suRemLen = 0x00;
				suCurCmd = 0x00;	// stop special SysEx handling
				suPkt.hdr.cin = 0x04 + suBufPos;	// SysEx End: command 5/6/7, depending on length
				FlushSrlUsbData();
			}
			else if (suBufPos >= 0x03)
			{
				suPkt.hdr.cin = 0x04;	// SysEx start/continue
				FlushSrlUsbData();
			}
			return;
		}
		
		// handle transition from SysEx to other commands
		if (suBufPos > 0)
		{
			suPkt.hdr.cin = 0x04 + suBufPos;	// SysEx End: command 5/6/7, depending on length
			FlushSrlUsbData();
		}
		suRemLen = 0x00;
		// then fall through
	}
	
	if (suRemLen)
	{
		// process remaining bytes of Channel message
		suPkt.data[suBufPos] = data;
		suBufPos ++;
		suRemLen --;
		if (! suRemLen)
		{
			if (suPkt.data[0] == 0xF5)
			{
				Serial.print("Serial In: Port = ");	Serial.println(suPkt.data[1], DEC);
				suPkt.hdr.cn = 0x00;	// all on port 0 for now
				suBufPos = 0x00;
				memset(suPkt.data, 0x00, 0x03);
				return;
			}
			FlushSrlUsbData();
		}
		return;
	}
	
	if (data >= 0xF8)	// quick handling of System Real Time messages
	{
		// They are all single-byte and don't change any status bytes.
		suPkt.hdr.cin = MIDI_USB_EVT_TYPE_FX[data & 0x0F];
		suPkt.data[0] = data;
		FlushSrlUsbData();
		return;
	}
	
	suBufPos = 0x00;
	if (data & 0x80)
	{
		if (data < 0xF0)
			suRunStatus = data;
		else if (data < 0xF8)
			suRunStatus = 0x00;
		// keep suRunStatus for F8..FF
		
		suCurCmd = MIDI_USB_EVT_TYPE_MAIN[data >> 4];
		if (! suCurCmd)	// handle Fx commands
			suCurCmd = MIDI_USB_EVT_TYPE_FX[data & 0x0F];
		suRemLen = USB_EVT_LEN[suCurCmd & 0x0F];
	}
	else if (suRunStatus)
	{
		// handle running staus for 80..EF events
		suCurCmd = MIDI_USB_EVT_TYPE_MAIN[suRunStatus >> 4];
		suRemLen = USB_EVT_LEN[suCurCmd];
		
		// in USB MIDI, there doesn't seem to be a "Running Status", so insert the missing command byte
		suPkt.data[suBufPos] = suRunStatus;
		suBufPos ++;
		suRemLen --;	// all events are assumed to have at least 1 parameter byte here
	}
	else
	{
		suCurCmd = MIDI_USB_EVT_TYPE_MAIN[0x00];
		suRemLen = USB_EVT_LEN[suCurCmd];
	}
	
	suPkt.hdr.cin = suCurCmd;
	suPkt.data[suBufPos] = data;
	suBufPos ++;
	suRemLen --;
	if (suRemLen == 0)
		FlushSrlUsbData();
	return;
}

void loop()
{
	midiEventPacket_t usPkt;
	
	while(Serial1.available())
	{
		uint8_t data = Serial1.read();
		//if (data >= 0xF0)
		//{
		//	Serial.print("IN: ");	Serial.println(data, HEX);
		//}
		ProcessSerialData(data);
	}
	
	usPkt = midiMod.read();
	while(usPkt.header != 0x00)
	{
		if (usPkt.hdr.cn != lastPort)
		{
			uint8_t portSel[2] = {0xF5, (uint8_t)(1 + usPkt.hdr.cn)};	// yes, it's 1-based
			//char portSel[2] = {0xF5, (uint8_t)usPkt.hdr.cn};	// TODO: has port 0 a special meaning?
			WriteSerial(portSel, 2);
			lastPort = usPkt.hdr.cn;
		}
		WriteSerial(usPkt.data, USB_EVT_LEN[usPkt.hdr.cin]);
		//Serial.print("OUT Cmd: ");	Serial.println(usPkt.hdr.cin, HEX);
		
		usPkt = midiMod.read();
	}

	// turn CTS LED off after timeout
	if (ledOffTime)
	{
		unsigned long timeMS = millis();
		if (timeMS >= ledOffTime)
		{
			ledOffTime = 0;
			digitalWrite(LED_BUILTIN, LOW);
		}
	}
	
	return;
}
