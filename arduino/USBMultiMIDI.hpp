#ifndef USBMULTIMIDI_HPP
#define USBMULTIMIDI_HPP

#include <stdint.h>
#include <Arduino.h>

#if ARDUINO < 10606
#error "USB MIDI requires Arduino IDE 1.6.6 or greater. Please update your IDE."
#endif
#ifndef USBCON
#error "USB MIDI can only be used with an USB MCU."
#endif

typedef struct
{
	union
	{
		uint8_t header;
		struct
		{
			uint8_t cin : 4;	// Code Index Number: command type
			uint8_t cn  : 4;	// Cable Number: number of the Embedded MIDI Jack
		} hdr;
	};
	uint8_t data[3];
} midiEventPacket_t;

#if defined(ARDUINO_ARCH_AVR)

#include <PluggableUSB.h>
#define EPTYPE_DESCRIPTOR_SIZE		uint8_t

#elif defined(ARDUINO_ARCH_SAM)

#include <USB/PluggableUSB.h>
#define EPTYPE_DESCRIPTOR_SIZE		uint32_t

#elif defined(ARDUINO_ARCH_SAMD)

#if defined(ARDUINO_API_VERSION)
#include <api/PluggableUSB.h>
#define EPTYPE_DESCRIPTOR_SIZE		unsigned int
#else
#include <USB/PluggableUSB.h>
#define EPTYPE_DESCRIPTOR_SIZE		uint32_t
#endif

#else

#error "Unsupported architecture"

#endif


class USBMultiMIDI : public PluggableUSBModule
{
public:
	USBMultiMIDI(uint8_t portsRX, uint8_t portsTX);
	uint32_t available(void);
	midiEventPacket_t read(void);
	void flush(void);
	void sendMIDI(midiEventPacket_t event);
	size_t write(const uint8_t *buffer, size_t size);
protected:
	int getInterface(uint8_t* interfaceNum);
	int getDescriptor(USBSetup& setup);
	bool setup(USBSetup& setup);
	uint8_t getShortName(char* name);
private:
	void accept(void);
	
	EPTYPE_DESCRIPTOR_SIZE _epTypes[2];	// OUT and IN
	uint8_t _epMidiRX;
	uint8_t _epMidiTX;
	uint8_t _jCntRX;
	uint8_t _jCntTX;
	uint8_t _jidRX[0x10];
	uint8_t _jidTX[0x10];
	
	uint8_t _portsRX;
	uint8_t _portsTX;
};

#endif	// USBMULTIMIDI_HPP
