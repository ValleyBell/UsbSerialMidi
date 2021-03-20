// Multi-Port USB MIDI driver
// Valley Bell, 2020-12-28

/*
    USB MIDI Layout
    ---------------

+----------------+   +-------------------------------------------------------------+
|  AudioControl  |   |  MIDIStream Interface                                       |
|   Interface *<-----|                                                             |
+----------------+   | +---------------------------+ +---------------------------+ |
                     | |     MIDI Out Endpoint     | |      MIDI In Endpoint     | |
                     | |                           | |                           | |
                     | |   /-----\       /-----\   | |   /-----\       /-----\   | |
                     | |  / Embed.\     / Embed.\  | |  / Embed.\     / Embed.\  | |
                     | | |   Jack  |...|   Jack  | | | |   Jack  |...|   Jack  | | |
                     | |  \  In 1 /     \  In n /  | |  \ Out 1 /     \ Out n /  | |
                     | |   \-----/       \-----/   | |   \-----/       \-----/   | |
                     | |                           | |                           | |
                     | +---------------------------+ +---------------------------+ |
                     |                                                             |
                     +-------------------------------------------------------------+
 */

#include <stdint.h>
#include "USBMultiMIDI.hpp"


#if defined(ARDUINO_ARCH_AVR)

#include <PluggableUSB.h>

//#define EPTYPE_DESCRIPTOR_SIZE		uint8_t
#define EP_TYPE_BULK_IN_MIDI 		EP_TYPE_BULK_IN
#define EP_TYPE_BULK_OUT_MIDI 		EP_TYPE_BULK_OUT
#define MIDI_BUFFER_SIZE			USB_EP_SIZE
#define is_write_enabled(x)			(1)

#elif defined(ARDUINO_ARCH_SAM)

#include <USB/PluggableUSB.h>

//#define EPTYPE_DESCRIPTOR_SIZE		uint32_t
#define EP_TYPE_BULK_IN_MIDI		(UOTGHS_DEVEPTCFG_EPSIZE_512_BYTE | \
									UOTGHS_DEVEPTCFG_EPDIR_IN |         \
									UOTGHS_DEVEPTCFG_EPTYPE_BLK |       \
									UOTGHS_DEVEPTCFG_EPBK_1_BANK |      \
									UOTGHS_DEVEPTCFG_NBTRANS_1_TRANS |  \
									UOTGHS_DEVEPTCFG_ALLOC)
#define EP_TYPE_BULK_OUT_MIDI		(UOTGHS_DEVEPTCFG_EPSIZE_512_BYTE | \
									UOTGHS_DEVEPTCFG_EPTYPE_BLK |       \
									UOTGHS_DEVEPTCFG_EPBK_1_BANK |      \
									UOTGHS_DEVEPTCFG_NBTRANS_1_TRANS |  \
									UOTGHS_DEVEPTCFG_ALLOC)
#define MIDI_BUFFER_SIZE			EPX_SIZE
#define USB_SendControl				USBD_SendControl
#define USB_Available				USBD_Available
#define USB_Recv					USBD_Recv
#define USB_Send					USBD_Send
#define USB_Flush					USBD_Flush
#define is_write_enabled(x)			Is_udd_write_enabled(x)

#elif defined(ARDUINO_ARCH_SAMD)

#if defined(ARDUINO_API_VERSION)
#include <api/PluggableUSB.h>
//#define EPTYPE_DESCRIPTOR_SIZE		unsigned int
#else
#include <USB/PluggableUSB.h>
//#define EPTYPE_DESCRIPTOR_SIZE		uint32_t
#endif
#define EP_TYPE_BULK_IN_MIDI 		USB_ENDPOINT_TYPE_BULK | USB_ENDPOINT_IN(0);
#define EP_TYPE_BULK_OUT_MIDI 		USB_ENDPOINT_TYPE_BULK | USB_ENDPOINT_OUT(0);
#define MIDI_BUFFER_SIZE			EPX_SIZE
#define USB_SendControl				USBDevice.sendControl
#define USB_Available				USBDevice.available
#define USB_Recv					USBDevice.recv
#define USB_Send					USBDevice.send
#define USB_Flush					USBDevice.flush
#define is_write_enabled(x)			(1)

#else

#error "Unsupported architecture"

#endif


// --- USB MIDI structures ---

_Pragma("pack(1)")
typedef struct	// MIDI Adapter Class-specific AC (Audio Control) Interface Descriptor
{
	uint8_t bLength;			// Size of this descriptor, in bytes.
	uint8_t bDescriptorType;	// CS_INTERFACE.
	uint8_t bDescriptorSubtype;	// HEADER subtype.
	uint16_t bcdADC;			// Revision of class specification - 1.0
	uint16_t wTotalLength;		// Total size of class specific descriptors.
	uint8_t bInCollection;		// Number of streaming interfaces.
	uint8_t baInterfaceNr;		// MIDIStreaming interface 1 belongs to this AudioControl interface.
} MIDI_ACInterfaceDescriptor;

typedef struct	// Class-Specific MS (MIDIStreaming) Interface Header Descriptor (USB MIDI Spec. 1.0: Table 6-2)
{
	uint8_t bLength;			// Size of this descriptor, in bytes: 7.
	uint8_t bDescriptorType;	// CS_INTERFACE descriptor type.
	uint8_t bDescriptorSubtype;	// MS_HEADER descriptor subtype.
	uint16_t bcdMSC;			// MIDIStreaming SubClass Spec. Release Number in BCD: 01.00.
	uint16_t wTotalLength;		// Total size for the class-specific MIDIStreaming interface descriptor. Includes the combined length of this descriptor header and all Jack and Element descriptors.
} MIDI_MSInterfaceDescriptor;

typedef struct	// MIDI IN Jack Descriptor (USB MIDI Spec. 1.0: Table 6-3)
{
	uint8_t bLength;			// Size of this descriptor, in bytes: 6
	uint8_t bDescriptorType;	// CS_INTERFACE descriptor type.
	uint8_t bDescriptorSubtype;	// MIDI_IN_JACK descriptor subtype.
	uint8_t bJackType;			// EMBEDDED or EXTERNAL
	uint8_t bJackID;			// Constant uniquely identifying the MIDI IN Jack within the USB-MIDI function.
	uint8_t iJack;				// Index of a string descriptor, describing the MIDI IN Jack
} MIDIJackInDescriptor;

typedef struct	// MIDI OUT Jack Descriptor (USB MIDI Spec. 1.0: Table 6-4)
{
	uint8_t bLength;			// Size of this descriptor, in bytes: 6+2*p
	uint8_t bDescriptorType;	// CS_INTERFACE descriptor type.
	uint8_t bDescriptorSubtype;	// MIDI_OUT_JACK descriptor subtype.
	uint8_t bJackType;			// EMBEDDED or EXTERNAL
	uint8_t bJackID;			// Constant uniquely identifying the MIDI OUT Jack within the USB-MIDI function.
	uint8_t bNrInputPins;		// Number of Input Pins of this MIDI OUT Jack: p
//	uint8_t baSourceID[1];		// ID of the Entity to which the last Input Pin of this MIDI OUT Jack is connected.
//	uint8_t baSourcePin[1];		// Output Pin number of the Entity to which the last Input Pin of this MIDI OUT Jack is connected.
	uint8_t iJack;				// Index of a string descriptor, describing the MIDI OUT Jack.
} MIDIJackOutDescriptor;

typedef struct	// Standard MS Bulk Data Endpoint Descriptor [for MIDI Jacks] (USB MIDI Spec. 1.0: Table 6-6)
{
	EndpointDescriptor len;	// see USBCore.h
	uint8_t refresh;		// Reset to 0.
	uint8_t sync;			// The address of the endpoint used to communicate synchronization information if required by this endpoint. Reset to zero.
} MIDI_StdEPDescriptor;

typedef struct	// Class-specific MS Bulk Data Endpoint Descriptor [for MIDI Jacks] (USB MIDI Spec. 1.0: Table 6-7)
{
	uint8_t bLength;			// Size of this descriptor, in bytes: 4+n
	uint8_t bDescriptorType;	// CS_ENDPOINT
	uint8_t bDescriptorSubType;	// MS_GENERAL
	uint8_t bNumEmbMIDIJack;	// Number of Embedded MIDI Jacks: n.
	uint8_t baAssocJackIDs[];	// IDs of the first..last Embedded Jack that is associated with this endpoint.
} MIDI_CsEPDescriptor;


// --- USB MIDI constants ---
#define USB_IC_AUDIO							0x01	// bInterfaceClass: AUDIO
#define USB_ISC_AUDIO_CONTROL					0x01	// bInterfaceSubclass: AUDIO_CONTROL
#define AC_IDS_HEADER							0x01	// bDescriptorSubtype: HEADER

#define USB_ICS_MIDISTREAMING					0x03	// bInterfaceSubclass: MIDISTREAMING
#define MS_CS_INTERFACE							0x24	// bDescriptorType: CS_INTERFACE
#define MS_CS_ENDPOINT							0x25	// bDescriptorType: CS_ENDPOINT

// MS Class-Specific Interface Descriptor Subtype (USB MIDI Spec. 1.0: A.1)
#define MS_IDS_UNDEFINED						0x00	// MS_DESCRIPTOR_UNDEFINED
#define MS_IDS_HEADER							0x01	// MS_HEADER
#define MS_IDS_IN_JACK							0x02	// MIDI_IN_JACK
#define MS_IDS_OUT_JACK							0x03	// MIDI_OUT_JACK
#define MS_IDS_ELEMENT							0x04	// ELEMENT
// MS Class-Specific Endpoint Descriptor Subtypes (USB MIDI Spec. 1.0: A.2)
#define MS_EDS_UNDEFINED						0x00	// DESCRIPTOR_UNDEFINED
#define MS_EDS_GENERAL							0x01	// MS_GENERAL
// MS MIDI IN and OUT Jack types (USB MIDI Spec. 1.0: A.3)
#define MS_JT_UNDEFINED							0x00	// JACK_TYPE_UNDEFINED
#define MS_JT_EMBEDDED							0x01	// EMBEDDED
#define MS_JT_EXTERNAL							0x02	// EXTERNAL


// --- helper macros for filling in USB structures ---
#define D_AC_INTERFACE(numIntfs, msIntf) \
	{ 0x09, MS_CS_INTERFACE, AC_IDS_HEADER, 0x0100, 0x0009, numIntfs, msIntf }
#define D_MS_INTERFACE() \
	{ 0x07, MS_CS_INTERFACE, MS_IDS_HEADER, 0x0100, 0x0000 }
#define D_MIDI_INJACK(jackProp, jackID) \
	{ 0x06, MS_CS_INTERFACE, MS_IDS_IN_JACK, jackProp, jackID, 0 }
#define D_MIDI_OUTJACK(jackProp, jackID) \
	{ 0x07, MS_CS_INTERFACE, MS_IDS_OUT_JACK, jackProp, jackID, 0, 0 }
#define D_MIDI_JACK_EP(addr, attr, packetSize) \
	{ 0x09, 5, addr, attr, packetSize, 0, 0, 0 }
#define D_MIDI_AC_JACK_EP() \
	{ 0x04, MS_CS_ENDPOINT, MS_EDS_GENERAL, 0 }


// --- implementation ---
USBMultiMIDI::USBMultiMIDI(uint8_t portsRX, uint8_t portsTX)
	: PluggableUSBModule(2, 2, _epTypes)	// numEndpoints: 2, numInterfaces: 2, endpointType = _epTypes
	, _portsRX(portsRX)
	, _portsTX(portsTX)
{
	_epTypes[0] = EP_TYPE_BULK_OUT_MIDI;	// USB -> host
	_epTypes[1] = EP_TYPE_BULK_IN_MIDI;		// host -> USB
	PluggableUSB().plug(this);
}

// macro for easily adding USB structures to the data buffer
#define STRUCT_ADD(data, pos, structName, varName)	structName* varName = (structName*)&data[pos];	pos += sizeof(structName);

static void AddJackEpDesc(uint8_t* data, uint16_t& pos, uint8_t jackCount, const uint8_t* jackIDs)
{
	MIDI_CsEPDescriptor* jEpDesc = (MIDI_CsEPDescriptor*)&data[pos];
	
	*jEpDesc = D_MIDI_AC_JACK_EP();
	memcpy(jEpDesc->baAssocJackIDs, jackIDs, jackCount);
	jEpDesc->bNumEmbMIDIJack = jackCount;
	jEpDesc->bLength += jEpDesc->bNumEmbMIDIJack;
	
	pos += jEpDesc->bLength;
	return;
}

int USBMultiMIDI::getInterface(uint8_t* interfaceNum)
{
	// required buffer size:
	//	8 [IADDescriptor] +
	//	9 [InterfaceDescriptor AC Intf] + 9 [MIDI_ACInterfaceDescriptor] +
	//	9 [InterfaceDescriptor MS Intf] + 7 [MIDI_MSInterfaceDescriptor] +
	//	_portsRX * 6 [MIDIJackInDescriptor] + _portsTX * (6+2) [MIDIJackOutDescriptor] +
	//	9 [MIDI_StdEPDescriptor] + (4+_portsRX) [MIDI_CsEPDescriptor RX jacks] +
	//	9 [MIDI_StdEPDescriptor] + (4+_portsTX) [MIDI_CsEPDescriptor TX jacks]
	// Summarized, this is:
	//	8+9+9+9+7+9+9+4+4 + (nRX*6 + nRX) + (nTX*8 + nTX) = 68 + 7*nRX + 9*nTX
	// With up to 16 MIDI in + out jacks each, this would require 324 bytes. (0x144)
	uint8_t data[0x180];
	uint16_t pos;
	uint16_t msIntfDescPos;
	uint8_t midiAcIntfID = pluggedInterface + 0;	// "pluggedInterface" is inherited from PluggableUSBModule
	uint8_t midiStrmIntfID = pluggedInterface + 1;
	_epMidiRX = pluggedEndpoint + 0;
	_epMidiTX = pluggedEndpoint + 1;
	
	// Many of these field are taken from the "Example: Simple MIDI Adapter" from the USB MIDI Spec. 1.0.
	// Comments denote which table was used as reference.
	
	pos = 0x00;
	(*interfaceNum) += 2;	// We use 2 interfaces: AudioControl and MIDIStream
	STRUCT_ADD(data, pos, IADDescriptor, iad);
	//     D_IAD(firstIntf, numIntfs, class,         subClass,     protocol)
	*iad = D_IAD(midiAcIntfID, 2, USB_IC_AUDIO, USB_ISC_AUDIO_CONTROL, 0);

	// --- AudioControl Interface ---
	STRUCT_ADD(data, pos, InterfaceDescriptor, acIntf);
	// The AudioControl interface doesn't have any endpoints.
	//        D_INTERFACE(intfIndex, numEndpts, class,         subClass,   protocol)
	*acIntf = D_INTERFACE(midiAcIntfID, 0, USB_IC_AUDIO, USB_ISC_AUDIO_CONTROL, 0);	// see Table B-3
	
	STRUCT_ADD(data, pos, MIDI_ACInterfaceDescriptor, acIDesc);
	// Here we assign our MIDIStreaming interface to this AudioControl interface.
	//   D_AC_INTERFACE(numStreamIntfs, acIntfID)
	*acIDesc = D_AC_INTERFACE(1, midiStrmIntfID);	// see Table B-4

	// --- MIDIStreaming Interface ---
	STRUCT_ADD(data, pos, InterfaceDescriptor, msIntf);
	// We have 2 endpoints: one for MIDI In and Out each.
	//        D_INTERFACE(intfIndex, numEndpts, class,         subClass,      protocol)
	*msIntf = D_INTERFACE(midiStrmIntfID, 2, USB_IC_AUDIO, USB_ICS_MIDISTREAMING, 0);

	msIntfDescPos = pos;
	STRUCT_ADD(data, pos, MIDI_MSInterfaceDescriptor, msIDesc);
	*msIDesc = D_MS_INTERFACE();	// USB-MIDI Spec. B-6

	// Embedded MIDI Jack = associated with USB MIDI endpoint (up to 16 embedded jacks per endpoint)
	// External MIDI Jack = physical MIDI connections built into the "USB-MIDI function" -> optional
	uint8_t jackID = 1;	// Note: bJackID value 0 is reserved, IDs begin with 1
	uint8_t port;
	
	// create MIDI In Jacks for Host Output Endpoint (host -> MIDI interface)
	_jCntRX = _portsRX;
	for (port = 0; port < _jCntRX; port ++)
	{
		STRUCT_ADD(data, pos, MIDIJackInDescriptor, jInEmb);
		//        D_MIDI_INJACK(   jackType,    jackID)
		*jInEmb = D_MIDI_INJACK(MS_JT_EMBEDDED, jackID);	// see Table B-7
		_jidRX[port] = jackID;
		jackID ++;
	}
	
	// create MIDI Out Jacks for Host Input Endpoint (MIDI interface -> host)
	_jCntTX = _portsTX;
	for (port = 0; port < _jCntTX; port ++)
	{
		STRUCT_ADD(data, pos, MIDIJackOutDescriptor, jOutEmb);
		//         D_MIDI_OUTJACK(  jackType,     jackID)
		*jOutEmb = D_MIDI_OUTJACK(MS_JT_EMBEDDED, jackID);	// see Table B-9
		_jidTX[port] = jackID;
		jackID ++;
	}
	
	// external jacks aren't used for now
	//STRUCT_ADD(data, pos, MIDIJackInDescriptor, jInExt);
	////        D_MIDI_INJACK(   jackType,  jackID)
	//*jInExt = D_MIDI_INJACK(MS_JT_EXTERNAL, jackID);	// see Table B-8
	//jackID ++;
	//STRUCT_ADD(data, pos, MIDIJackOutDescriptor, jOutExt);
	////      D_MIDI_OUTJACK(  jackType,  jackID, numPins, srcID, srcPin)
	//*jOutExt = D_MIDI_OUTJACK(MS_JT_EXTERNAL, jackID, 1, 1, 1);	// see Table B-10
	//jackID ++;
	
	// MIDI Out Endpoint (host -> MIDI interface)
	STRUCT_ADD(data, pos, MIDI_StdEPDescriptor, jEpRX);
	//       D_MIDI_JACK_EP(     bEndpointAddress,            bmAttributes,       wMaxPacketSize)
	*jEpRX = D_MIDI_JACK_EP(USB_ENDPOINT_OUT(_epMidiRX), USB_ENDPOINT_TYPE_BULK, MIDI_BUFFER_SIZE);	// see Table B-11
	AddJackEpDesc(data, pos, _jCntRX, _jidRX);
	
	// MIDI In Endpoint (MIDI interface -> host)
	STRUCT_ADD(data, pos, MIDI_StdEPDescriptor, jEpTX);
	//       D_MIDI_JACK_EP(     bEndpointAddress,           bmAttributes,       wMaxPacketSize)
	*jEpTX = D_MIDI_JACK_EP(USB_ENDPOINT_IN(_epMidiTX), USB_ENDPOINT_TYPE_BULK, MIDI_BUFFER_SIZE);	// see Table B-13
	AddJackEpDesc(data, pos, _jCntTX, _jidTX);
	
	msIDesc->wTotalLength = pos - msIntfDescPos;	// sizeof(Stream Intf Descriptor) + sizeof(all Jack descriptors) + sizeof(all Endpoint descriptors)
	
	return USB_SendControl(0, data, pos);
}

int USBMultiMIDI::getDescriptor(USBSetup& setup)
{
	return 0;	// just return 0 for now
}

// interface usb setup callback (optional)
bool USBMultiMIDI::setup(USBSetup& setup)
{
	return false;
}

// short MIDI Device name (TODO: return something better here)
uint8_t USBMultiMIDI::getShortName(char* name)
{
	// Note: The buffer "name" is ISERIAL_MAX_LEN large, but stuff is added by PluggableUSB_ as well.
	// In practise, anything > 7 characters doesn't seem to work.
	strcpy(name, "MIDI");
	return strlen(name);	// return length without \0 terminator
}


struct ring_bufferMIDI
{
	midiEventPacket_t midiEvent[MIDI_BUFFER_SIZE];
	volatile uint32_t head;
	volatile uint32_t tail;
};

ring_bufferMIDI midi_rx_buffer = {{0,0,0,0 }, 0, 0};

void USBMultiMIDI::accept(void)
{
	ring_bufferMIDI *buffer = &midi_rx_buffer;
	uint32_t i = (uint32_t)(buffer->head+1) % MIDI_BUFFER_SIZE;

	// if we should be storing the received character into the location
	// just before the tail (meaning that the head would advance to the
	// current location of the tail), we're about to overflow the buffer
	// and so we don't write the character or advance the head.
	while (i != buffer->tail)
	{
		int c;
		midiEventPacket_t event;
		if (! USB_Available(_epMidiRX))
		{
#if defined(ARDUINO_ARCH_SAM)
			udd_ack_fifocon(_epMidiRX);
#endif
			//break;
		}
		c = USB_Recv(_epMidiRX, &event, sizeof(event));

		//MIDI packet has to be 4 bytes
		if (c < 4)
			return;
		buffer->midiEvent[buffer->head] = event;
		buffer->head = i;

		i = (i + 1) % MIDI_BUFFER_SIZE;
	}
}

uint32_t USBMultiMIDI::available(void)
{
	ring_bufferMIDI *buffer = &midi_rx_buffer;
	return (uint32_t)(MIDI_BUFFER_SIZE + buffer->head - buffer->tail) % MIDI_BUFFER_SIZE;
}

midiEventPacket_t USBMultiMIDI::read(void)
{
	midiEventPacket_t c;
	ring_bufferMIDI *buffer = &midi_rx_buffer;

	if(((uint32_t)(MIDI_BUFFER_SIZE + buffer->head - buffer->tail) % MIDI_BUFFER_SIZE) > 0)
	{
		c = buffer->midiEvent[buffer->tail];
	}
	else
	{
		if (USB_Available(_epMidiRX))
		{
			accept();
			c = buffer->midiEvent[buffer->tail];
		}
		else
		{
			c.header = 0;
			c.data[0] = 0;
			c.data[1] = 0;
			c.data[2] = 0;
		}
	}
	// if the head isn't ahead of the tail, we don't have any characters
	if (buffer->head != buffer->tail)
		buffer->tail = (uint32_t)(buffer->tail + 1) % MIDI_BUFFER_SIZE;
	return c;
}

void USBMultiMIDI::flush(void)
{
	USB_Flush(_epMidiTX);
}

size_t USBMultiMIDI::write(const uint8_t *buffer, size_t size)
{
	if (! is_write_enabled(_epMidiTX))
		return 0;	// just discard packets when no one is listening
	
	int r = USB_Send(_epMidiTX, buffer, size);
	if (r <= 0)
		return 0;
	return r;
}

void USBMultiMIDI::sendMIDI(midiEventPacket_t event)
{
	uint8_t data[4];
	data[0] = event.header;
	data[1] = event.data[0];
	data[2] = event.data[1];
	data[3] = event.data[2];
	write(data, 4);
}
