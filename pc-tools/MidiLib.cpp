// C++ Midi Library

#include <iostream>
#include <fstream>
#include <list>
#include <vector>
#include <algorithm>
#include <string.h>

#include "stdtype.h"
#include "MidiLib.hpp"


#define FCC_MTHD	0x6468544D	// 'MThd'
#define FCC_MTRK	0x6B72544D	// 'MTrk'


static UINT16 ReadBE16(FILE* infile);
static UINT32 ReadBE32(FILE* infile);
static UINT32 ReadMidiValue(FILE* infile);
static void WriteBE16(FILE* outfile, UINT16 Value);
static void WriteBE32(FILE* outfile, UINT32 Value);
static void WriteMidiValue(FILE* outfile, UINT32 Value);


// --- MidiTrack Class ---
MidiTrack::MidiTrack(void)
{
	return;
}

MidiTrack::~MidiTrack()
{
	return;
}

UINT8 MidiTrack::ReadFromFile(FILE* infile)
{
	UINT32 TempLng;
	UINT32 TrkPos;
	UINT32 TrkEnd;
	UINT8 LastEvt;
	UINT8 CurEvt;
	UINT8 EvtVal;
	UINT32 CurTick;
	
	fread(&TempLng, 0x04, 1, infile);
	if (TempLng != FCC_MTRK)
		return 0x10;
	
	TempLng = ReadBE32(infile);	// Read Track Length
	TrkPos = (UINT32)ftell(infile);
	TrkEnd = TrkPos + TempLng;
	
	_events.clear();
	//_events.reserve(TempLng / 4);
	
	LastEvt = 0x00;
	CurTick = 0;
	// read events
	while(! feof(infile) && (UINT32)ftell(infile) < TrkEnd)
	{
		MidiEvent* newEvt;
		bool rsUse;
		
		CurTick += ReadMidiValue(infile);
		if (feof(infile))
			break;
		
		fread(&CurEvt, 0x01, 1, infile);
		if (CurEvt < 0x80)
		{
			if (LastEvt < 0x80 || LastEvt >= 0xF0)
				return 0x01;
			EvtVal = CurEvt;
			CurEvt = LastEvt;
			rsUse = true;
		}
		else
		{
			if (CurEvt < 0xF0)
			{
				LastEvt = CurEvt;
				fread(&EvtVal, 0x01, 1, infile);
			}
			rsUse = false;
		}
		
		_events.push_back(MidiEvent());
		newEvt = &_events.back();
		
		newEvt->tick = CurTick;
		newEvt->rsUse = rsUse;
		newEvt->evtType = CurEvt;
		switch(CurEvt & 0xF0)
		{
		case 0x80:
		case 0x90:
		case 0xA0:
		case 0xB0:
		case 0xE0:
			newEvt->evtValA = EvtVal;
			fread(&newEvt->evtValB, 0x01, 1, infile);
			break;
		case 0xC0:
		case 0xD0:
			newEvt->evtValA = EvtVal;
			newEvt->evtValB = 0x00;
			break;
		case 0xF0:
			switch(CurEvt)
			{
			case 0xFF:
				fread(&newEvt->evtValA, 0x01, 1, infile);
				// fall through
			case 0xF0:
			case 0xF7:
				newEvt->evtData.resize(ReadMidiValue(infile));
				if (newEvt->evtData.size())
					fread(&newEvt->evtData[0x00], 0x01, newEvt->evtData.size(), infile);
				break;
			}
		}
	}
	fseek(infile, TrkEnd, SEEK_SET);
	
	return 0x00;
}

UINT8 MidiTrack::WriteToFile(FILE* outfile) const
{
	UINT32 TempLng;
	UINT32 TrkPos;
	UINT32 TrkEnd;
	UINT8 LastEvt;
	midevt_const_it evtIt;
	UINT32 CurTick;
	
	TempLng = FCC_MTRK;
	fwrite(&TempLng, 0x04, 1, outfile);
	TempLng = 0x00000000;
	fwrite(&TempLng, 0x04, 1, outfile);
	
	TrkPos = (UINT32)ftell(outfile);
	
	LastEvt = 0x00;
	CurTick = 0;
	// write events
	for (evtIt = _events.begin(); evtIt != _events.end(); ++evtIt)
	{
		WriteMidiValue(outfile, evtIt->tick - CurTick);
		CurTick = evtIt->tick;
		
		if (evtIt->evtType < 0xF0)
		{
			if (! evtIt->rsUse || LastEvt != evtIt->evtType)
				fwrite(&evtIt->evtType, 0x01, 1, outfile);
		}
		switch(evtIt->evtType & 0xF0)
		{
		case 0x80:
		case 0x90:
		case 0xA0:
		case 0xB0:
		case 0xE0:
			fwrite(&evtIt->evtValA, 0x01, 1, outfile);
			fwrite(&evtIt->evtValB, 0x01, 1, outfile);
			break;
		case 0xC0:
		case 0xD0:
			fwrite(&evtIt->evtValA, 0x01, 1, outfile);
			break;
		case 0xF0:
			fwrite(&evtIt->evtType, 0x01, 1, outfile);
			switch(evtIt->evtType)
			{
			case 0xFF:
				fwrite(&evtIt->evtValA, 0x01, 1, outfile);
				// fall through
			case 0xF0:
			case 0xF7:
				WriteMidiValue(outfile, evtIt->evtData.size());
				if (evtIt->evtData.size() > 0)
					fwrite(&evtIt->evtData[0x00], 0x01, evtIt->evtData.size(), outfile);
				break;
			}
		}
		LastEvt = evtIt->evtType;
	}
	TrkEnd = (UINT32)ftell(outfile);
	
	fseek(outfile, TrkPos - 0x04, SEEK_SET);
	WriteBE32(outfile, TrkEnd - TrkPos);
	
	fseek(outfile, TrkEnd, SEEK_SET);
	
	return 0x00;
}

/*static*/ INT16 MidiTrack::GetPitchBendValue(UINT8 valLSB, UINT8 valMSB)
{
	return	(((valMSB & 0x7F) << 7) |
			((valLSB & 0x7F) << 0)) - 0x2000;
}

/*static*/ INT16 MidiTrack::GetPitchBendValue(const MidiEvent& evt)
{
	if ((evt.evtType & 0xF0) != 0xE0)
		return (INT16)0x8000;
	else
		return GetPitchBendValue(evt.evtValA, evt.evtValB);
}

/*static*/ void MidiTrack::SetPitchBendValue(MidiEvent* evt, INT16 pbValue)
{
	if ((evt->evtType & 0xF0) != 0xE0)
		return;
	
	if (pbValue < -0x2000)
		pbValue = -0x2000;
	else if (pbValue > 0x1FFF)
		pbValue = 0x1FFF;
	pbValue += 0x2000;
	evt->evtValA = (pbValue >> 0) & 0x7F;
	evt->evtValB = (pbValue >> 7) & 0x7F;
	
	return;
}

UINT32 MidiTrack::GetEventCount(void) const
{
	return _events.size();
}

UINT32 MidiTrack::GetTickCount(void) const
{
	return _events.empty() ? 0 : _events.back().tick;
}

const MidiEvtList& MidiTrack::GetEvents(void) const
{
	return _events;
}

midevt_iterator MidiTrack::GetEventBegin(void)
{
	return _events.begin();
}

midevt_iterator MidiTrack::GetEventEnd(void)
{
	return _events.end();
}

midevt_iterator MidiTrack::GetEventFromTick(UINT32 tick)
{
	midevt_iterator evtIt;
	
	if (tick >= GetTickCount())
		return _events.end();
	
	for (evtIt = _events.begin(); evtIt != _events.end(); ++evtIt)
	{
		if (evtIt->tick >= tick)
			return evtIt;
	}
	
	return _events.end();
}

/*static*/ MidiEvent MidiTrack::CreateEvent_Std(UINT8 Event, UINT8 Val1, UINT8 Val2)
{
	MidiEvent newEvt;
	
	newEvt.tick = 0;
	newEvt.rsUse = false;
	newEvt.evtType = Event;
	newEvt.evtValA = Val1;
	newEvt.evtValB = Val2;
	newEvt.evtData.clear();
	
	return newEvt;
}

/*static*/ MidiEvent MidiTrack::CreateEvent_SysEx(UINT32 DataLen, const void* Data)
{
	MidiEvent newEvt;
	
	newEvt.tick = 0;
	newEvt.rsUse = false;
	newEvt.evtType = 0xF0;
	newEvt.evtValA = 0x00;
	newEvt.evtValB = 0x00;
	newEvt.evtData.resize(DataLen);
	if (DataLen)
		memcpy(&newEvt.evtData[0x00], Data, DataLen);
	
	return newEvt;
}

/*static*/ MidiEvent MidiTrack::CreateEvent_Meta(UINT8 Type, UINT32 DataLen, const void* Data)
{
	MidiEvent newEvt;
	
	newEvt.tick = 0;
	newEvt.rsUse = false;
	newEvt.evtType = 0xFF;
	newEvt.evtValA = Type;
	newEvt.evtValB = 0x00;
	newEvt.evtData.resize(DataLen);
	if (DataLen)
		memcpy(&newEvt.evtData[0x00], Data, DataLen);
	
	return newEvt;
}

void MidiTrack::AppendEvent(const MidiEvent& Event)
{
	if (Event.tick < GetTickCount())
		return;
	
	_events.push_back(Event);
	
	return;
}

void MidiTrack::AppendEvent(UINT32 Delay, MidiEvent Event)
{
	Event.tick = GetTickCount() + Delay;
	AppendEvent(Event);
	
	return;
}

void MidiTrack::AppendEvent(UINT32 Delay, UINT8 Event, UINT8 Val1, UINT8 Val2)
{
	AppendEvent(Delay, CreateEvent_Std(Event, Val1, Val2));
	
	return;
}

void MidiTrack::AppendSysEx(UINT32 Delay, UINT32 DataLen, const void* Data)
{
	AppendEvent(Delay, CreateEvent_SysEx(DataLen, Data));
	
	return;
}

void MidiTrack::AppendMetaEvent(UINT32 Delay, UINT8 Type, UINT32 DataLen, const void* Data)
{
	AppendEvent(Delay, CreateEvent_Meta(Type, DataLen, Data));
	
	return;
}


void MidiTrack::InsertEventT(const MidiEvent& Event)
{
	midevt_iterator evtIt;
	
	evtIt = GetFirstEventAtTick(Event.tick);
	_events.insert(evtIt, Event);
	
	return;
}

void MidiTrack::InsertEventT(UINT32 tick, MidiEvent Event)
{
	Event.tick = tick;
	InsertEventT(Event);
	
	return;
}

void MidiTrack::InsertEventT(UINT32 tick, UINT8 Event, UINT8 Val1, UINT8 Val2)
{
	InsertEventT(tick, CreateEvent_Std(Event, Val1, Val2));
	
	return;
}

void MidiTrack::InsertSysExT(UINT32 tick, UINT32 DataLen, const void* Data)
{
	InsertEventT(tick, CreateEvent_SysEx(DataLen, Data));
	
	return;
}

void MidiTrack::InsertMetaEventT(UINT32 tick, UINT8 Type, UINT32 DataLen, const void* Data)
{
	InsertEventT(tick, CreateEvent_Meta(Type, DataLen, Data));
	
	return;
}



// insert with previous event and delay
void MidiTrack::InsertEventD(midevt_iterator prevEvt, const MidiEvent& Event)
{
	if (prevEvt == _events.end())
	{
		if (Event.tick >= GetTickCount())
			AppendEvent(Event);
		else if (! Event.tick)
			_events.insert(_events.begin(), Event);
		return;
	}
	if (Event.tick < prevEvt->tick)
		return;
	
	midevt_iterator nextEvt(prevEvt);
	++nextEvt;
	if (nextEvt != _events.end() && Event.tick > nextEvt->tick)
		return;
	
	_events.insert(nextEvt, Event);
	
	return;
}

void MidiTrack::InsertEventD(midevt_iterator prevEvt, UINT32 Delay, MidiEvent Event)
{
	if (prevEvt == _events.end() && Delay)
	{
		InsertEventT(Delay, Event);
		return;
	}
	
	if (prevEvt == _events.end())
		Event.tick = 0;
	else
		Event.tick = prevEvt->tick + Delay;
	InsertEventD(prevEvt, Event);
	
	return;
}

void MidiTrack::InsertEventD(midevt_iterator prevEvt, UINT32 Delay, UINT8 Event, UINT8 Val1, UINT8 Val2)
{
	InsertEventD(prevEvt, Delay, CreateEvent_Std(Event, Val1, Val2));
	
	return;
}

void MidiTrack::InsertSysExD(midevt_iterator prevEvt, UINT32 Delay, UINT32 DataLen, const void* Data)
{
	InsertEventD(prevEvt, Delay, CreateEvent_SysEx(DataLen, Data));
	
	return;
}

void MidiTrack::InsertMetaEventD(midevt_iterator prevEvt, UINT32 Delay, UINT8 Type, UINT32 DataLen, const void* Data)
{
	InsertEventD(prevEvt, Delay, CreateEvent_Meta(Type, DataLen, Data));
	
	return;
}

void MidiTrack::RemoveEvent(midevt_iterator evtIt)
{
	_events.erase(evtIt);
	
	return;
}

midevt_iterator MidiTrack::GetFirstEventAtTick(UINT32 tick)
{
	if (tick > GetTickCount())
		return _events.end();
	
	midevt_iterator evtIt;
	
	for (evtIt = _events.begin(); evtIt != _events.end(); ++evtIt)
	{
		if (evtIt->tick >= tick)
			return evtIt;
	}
	
	return _events.end();
}


// --- MidiFile Class ---
MidiFile::MidiFile(void)
{
	_format = 1;
	//_trackCount = 0;
	_resolution = 96;
	//this->FirstTrack = NULL;
	
	return;
}

MidiFile::~MidiFile()
{
	ClearAll();
	
	return;
}

void MidiFile::ClearAll()
{
	std::vector<MidiTrack*>::iterator trkIt;
	
	for (trkIt = _tracks.begin(); trkIt != _tracks.end(); ++trkIt)
		delete *trkIt;
	_tracks.clear();
	
	return;
}

UINT16 MidiFile::GetMidiFormat(void) const
{
	return _format;
}

UINT16 MidiFile::GetMidiResolution(void) const
{
	return _resolution;
}

UINT16 MidiFile::GetTrackCount(void) const
{
	return (UINT16)_tracks.size();
}

MidiTrack* MidiFile::GetTrack(UINT16 trackID)
{
	return (trackID < _tracks.size()) ? _tracks[trackID] : NULL;
}

UINT8 MidiFile::SetMidiFormat(UINT16 newFormat)
{
	if (newFormat > 2)
		return 0xFF;
	
	if (newFormat == 0 && GetTrackCount() > 1)
		return 0x01;
	
	_format = newFormat;
	
	return 0x00;
}

UINT8 MidiFile::SetMidiResolution(UINT16 newResolution)
{
	if (! newResolution || newResolution > 0x7FFF)
		return 0xFF;
	
	_resolution = newResolution;
	
	return 0x00;
}

MidiTrack* MidiFile::NewTrack_Append(void)
{
	MidiTrack* newTrk = new MidiTrack();
	MidiTrack* retVal;
	
	retVal = Track_Append(newTrk);
	if (retVal == NULL)
		delete newTrk;
	return retVal;
}

MidiTrack* MidiFile::Track_Append(MidiTrack* trkData)
{
	if (GetTrackCount() >= 0x8000)
		return NULL;
	
	_tracks.push_back(trkData);
	
	return _tracks.back();
}

MidiTrack* MidiFile::NewTrack_Insert(UINT16 newTrackID)
{
	MidiTrack* newTrk = new MidiTrack();
	MidiTrack* retVal;
	
	retVal = Track_Insert(newTrackID, newTrk);
	if (retVal == NULL)
		delete newTrk;
	return retVal;
}

MidiTrack* MidiFile::Track_Insert(UINT16 newTrackID, MidiTrack* trkData)
{
	if (newTrackID > GetTrackCount())
		return NULL;
	else if (newTrackID == GetTrackCount())
		return Track_Append(trkData);
	
	_tracks.insert(_tracks.begin() + newTrackID, trkData);
	
	return _tracks[newTrackID];
}

UINT8 MidiFile::DeleteTrack(UINT16 trackID)
{
	if (trackID >= GetTrackCount())
		return 0xFF;
	
	delete _tracks[trackID];
	_tracks.erase(_tracks.begin() + trackID);
	
	return 0x00;
}


UINT8 MidiFile::LoadFile(const char* fileName)
{
	FILE* infile;
	UINT8 retVal;
	
	infile = fopen(fileName, "rb");
	if (infile == NULL)
		return 0xFF;
	
	retVal = LoadFile(infile);
	fclose(infile);
	
	return retVal;
}

UINT8 MidiFile::LoadFile(FILE* infile)
{
	UINT32 TempLng;
	UINT32 HdrPos;
	UINT32 HdrEnd;
	UINT16 trkCnt;
	UINT16 CurTrk;
	UINT8 RetVal;
	
//	fseek(infile, 0, SEEK_END);
//	file_size = ftell(infile);
//	fseek(infile, 0, SEEK_SET);
	
	fread(&TempLng, 0x04, 1, infile);
	if (TempLng != FCC_MTHD)
		return 0x10;
	
	ClearAll();
	
	TempLng = ReadBE32(infile);	// Read Header Length
	HdrPos = (UINT32)ftell(infile);
	HdrEnd = HdrPos + TempLng;
	
	_format = ReadBE16(infile);
	trkCnt = ReadBE16(infile);
	_resolution = ReadBE16(infile);
	
	fseek(infile, HdrEnd, SEEK_SET);
	
	RetVal = 0x00;
	_tracks.reserve(trkCnt);
	for (CurTrk = 0; CurTrk < trkCnt; CurTrk ++)
	{
		MidiTrack* newTrk = new MidiTrack;
		RetVal = newTrk->ReadFromFile(infile);
		if (RetVal)
			break;
		
		Track_Append(newTrk);
	}
	
	return RetVal;
}

UINT8 MidiFile::SaveFile(const char* fileName)
{
	FILE* outfile;
	UINT8 retVal;
	
	outfile = fopen(fileName, "wb");
	if (outfile == NULL)
		return 0xFF;
	
	retVal = SaveFile(outfile);
	fclose(outfile);
	
	return retVal;
}

UINT8 MidiFile::SaveFile(FILE* outfile)
{
	UINT32 TempLng;
	UINT32 HdrPos;
	UINT32 HdrEnd;
	UINT8 RetVal;
	std::vector<MidiTrack*>::const_iterator trkIt;
	
	TempLng = FCC_MTHD;
	fwrite(&TempLng, 0x04, 1, outfile);
	TempLng = 0x00000000;
	fwrite(&TempLng, 0x04, 1, outfile);
	
	HdrPos = (UINT32)ftell(outfile);
	WriteBE16(outfile, _format);
	WriteBE16(outfile, GetTrackCount());
	WriteBE16(outfile, _resolution);
	HdrEnd = (UINT32)ftell(outfile);
	
	fseek(outfile, HdrPos - 0x04, SEEK_SET);
	WriteBE32(outfile, HdrEnd - HdrPos);
	
	fseek(outfile, HdrEnd, SEEK_SET);
	
	RetVal = 0x00;
	for (trkIt = _tracks.begin(); trkIt != _tracks.end(); ++trkIt)
	{
		RetVal = (*trkIt)->WriteToFile(outfile);
		if (RetVal)
			break;
	}
	
	return RetVal;
}

static UINT16 ReadBE16(FILE* infile)
{
	UINT8 InData[0x02];
	
	fread(InData, 0x02, 1, infile);
	return (InData[0x00] << 8) | (InData[0x01] << 0);
}

static UINT32 ReadBE32(FILE* infile)
{
	UINT8 InData[0x04];
	
	fread(InData, 0x04, 1, infile);
	return	(InData[0x00] << 24) | (InData[0x01] << 16) |
			(InData[0x02] <<  8) | (InData[0x03] <<  0);
}

static UINT32 ReadMidiValue(FILE* infile)
{
	UINT8 TempByt;
	UINT32 ResVal;
	size_t readBytes;
	
	ResVal = 0x00;
	do
	{
		readBytes = fread(&TempByt, 0x01, 1, infile);
		if (! readBytes)
			break;
		ResVal <<= 7;
		ResVal |= (TempByt & 0x7F);
	} while(TempByt & 0x80);
	
	return ResVal;
}

static void WriteBE16(FILE* outfile, UINT16 Value)
{
	UINT8 OutData[0x02];
	
	OutData[0x00] = (Value & 0xFF00) >> 8;
	OutData[0x01] = (Value & 0x00FF) >> 0;
	fwrite(OutData, 0x02, 1, outfile);
	
	return;
}

static void WriteBE32(FILE* outfile, UINT32 Value)
{
	UINT8 OutData[0x04];
	
	OutData[0x00] = (Value & 0xFF000000) >> 24;
	OutData[0x01] = (Value & 0x00FF0000) >> 16;
	OutData[0x02] = (Value & 0x0000FF00) >>  8;
	OutData[0x03] = (Value & 0x000000FF) >>  0;
	fwrite(OutData, 0x04, 1, outfile);
	
	return;
}

static void WriteMidiValue(FILE* outfile, UINT32 Value)
{
	UINT8 ValSize;
	UINT8 ValData[0x05];	// 32-bit -> 5 7-bit groups (1 * 4-bit + 4 * 7-bit)
	UINT32 TempLng;
	UINT32 CurPos;
	
	ValSize = 0x00;
	TempLng = Value;
	do
	{
		TempLng >>= 7;
		ValSize ++;
	} while(TempLng);
	
	CurPos = ValSize;
	TempLng = Value;
	do
	{
		CurPos --;
		ValData[CurPos] = 0x80 | (TempLng & 0x7F);
		TempLng >>= 7;
	} while(TempLng);
	ValData[ValSize - 1] &= 0x7F;
	fwrite(ValData, 1, ValSize, outfile);
	
	return;
}
