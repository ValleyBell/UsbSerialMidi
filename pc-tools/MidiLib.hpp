#ifndef __MIDILIB_HPP__
#define __MIDILIB_HPP__

#include "stdtype.h"

#include <list>
#include <vector>
#include <stdio.h>	// for FILE

struct MidiEvent
{
	UINT32 tick;
	bool rsUse;	// use Running Status to shorten event
	UINT8 evtType;
	UINT8 evtValA;	// Note Height, Controller Type, ...
	UINT8 evtValB;
	std::vector<UINT8> evtData;
};

typedef std::list<MidiEvent> MidiEvtList;
typedef MidiEvtList::iterator midevt_iterator;
typedef MidiEvtList::const_iterator midevt_const_it;

class MidiTrack
{
public:
	
	MidiTrack(void);
	~MidiTrack();
	
	UINT32 GetEventCount(void) const;
	UINT32 GetTickCount(void) const;
	const MidiEvtList& GetEvents(void) const;
	midevt_iterator GetEventBegin(void);
	midevt_iterator GetEventEnd(void);
	midevt_iterator GetEventFromTick(UINT32 tick);
	
	static INT16 GetPitchBendValue(UINT8 valLSB, UINT8 valMSB);
	static INT16 GetPitchBendValue(const MidiEvent& evt);
	static void SetPitchBendValue(MidiEvent* evt, INT16 pbValue);
	
	// create MIDI events
	static MidiEvent CreateEvent_Std(UINT8 Event, UINT8 Val1, UINT8 Val2);
	static MidiEvent CreateEvent_SysEx(UINT32 DataLen, const void* Data);
	static MidiEvent CreateEvent_Meta(UINT8 Type, UINT32 DataLen, const void* Data);
	
	// append with delay to last event
	void AppendEvent(const MidiEvent& Event);
	void AppendEvent(UINT32 Delay, MidiEvent Event);
	void AppendEvent(UINT32 Delay, UINT8 Event, UINT8 Val1, UINT8 Val2);
	void AppendSysEx(UINT32 Delay, UINT32 DataLen, const void* Data);
	void AppendMetaEvent(UINT32 Delay, UINT8 Type, UINT32 DataLen, const void* Data);
	
	// insert with absolute tick
	void InsertEventT(const MidiEvent& Event);
	void InsertEventT(UINT32 Tick, MidiEvent Event);
	void InsertEventT(UINT32 Tick, UINT8 Event, UINT8 Val1, UINT8 Val2);
	void InsertSysExT(UINT32 Tick, UINT32 DataLen, const void* Data);
	void InsertMetaEventT(UINT32 Tick, UINT8 Type, UINT32 DataLen, const void* Data);
	// insert with previous event and delay
	void InsertEventD(midevt_iterator prevEvt, const MidiEvent& Event);
	void InsertEventD(midevt_iterator prevEvt, UINT32 Delay, MidiEvent Event);
	void InsertEventD(midevt_iterator prevEvt, UINT32 Delay, UINT8 Event, UINT8 Val1, UINT8 Val2);
	void InsertSysExD(midevt_iterator prevEvt, UINT32 Delay, UINT32 DataLen, const void* Data);
	void InsertMetaEventD(midevt_iterator prevEvt, UINT32 Delay, UINT8 Type, UINT32 DataLen, const void* Data);
	
	void RemoveEvent(midevt_iterator evtIt);
	
	UINT8 ReadFromFile(FILE* infile);
	UINT8 WriteToFile(FILE* outfile) const;
	
private:
	MidiEvtList _events;
	
	midevt_iterator GetFirstEventAtTick(UINT32 Tick);
};

class MidiFile
{
private:
	UINT16 _format;
	//UINT16 _trackCount;
	UINT16 _resolution;
	std::vector<MidiTrack*> _tracks;
	
public:
	MidiFile(void);
	~MidiFile();
	void ClearAll(void);
	
	UINT8 LoadFile(const char* fileName);
	UINT8 LoadFile(FILE* infile);
	//UINT8 LoadFile(UINT32 FileLen, UINT8* FileData);
	
	UINT8 SaveFile(const char* fileName);
	UINT8 SaveFile(FILE* outfile);
	//UINT8 SaveFile(UINT32* RetFileSize, UINT8** RetFileData);
	
	UINT16 GetMidiFormat(void) const;
	UINT16 GetMidiResolution(void) const;
	UINT16 GetTrackCount(void) const;
	MidiTrack* GetTrack(UINT16 trackID);
	
	UINT8 SetMidiFormat(UINT16 newFormat);
	UINT8 SetMidiResolution(UINT16 newResolution);
	//UINT8 ConvertMidiFormat(UINT16 newFormat);	// maybe later
	//UINT8 ConvertMidiResolution(UINT16 newResolution);	// maybe later
	
	MidiTrack* NewTrack_Append(void);
	MidiTrack* NewTrack_Insert(UINT16 newTrackID);
	// Note: The functions return a pointer to the inserted track.
	//       The track parameter is empty after the data was inserted.
	MidiTrack* Track_Append(MidiTrack* trkData);
	MidiTrack* Track_Insert(UINT16 newTrackID, MidiTrack* trkData);
	
	UINT8 DeleteTrack(UINT16 trackID);
};

#endif	// __MIDILIB_HPP__
