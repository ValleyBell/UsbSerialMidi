// COM-Port MIDI Player

#include <iostream>
#include <stdio.h>
#include <fstream>
#include <string>
#include <math.h>

#include <windows.h>
#include <conio.h>

#include "stdtype.h"
#include "MidiLib.hpp"

struct TrackState
{
	UINT16 trkID;
	UINT8 portID;
	midevt_const_it endPos;
	midevt_const_it evtPos;
};


UINT8 OpenCOMPort(const char* port);
void CloseCOMPort(void);
static void printms(double time);
static void RefreshTickTime(void);
static double GetPlaybackPos(void);
void Start(void);
void Stop(void);
static void SendShortEvt(UINT8 portID, const MidiEvent* midiEvt);
static void SendLongEvt(UINT8 portID, const MidiEvent* midiEvt);
void DoEvent(TrackState* trkState, const MidiEvent* midiEvt);
void DoPlaybackStep(void);


static MidiFile CMidi;
static std::vector<TrackState> _trkStates;
static UINT64 _tmrFreq;		// number of virtual timer ticks for 1 second
static UINT64 _tmrStep;		// timestamp: next update of sequence processor
static UINT64 _tmrMinStart;	// timestamp when the song should start playing (for initialization delay)
static UINT32 _midiTempo;
static UINT32 _curEvtTick;
static UINT32 _nextEvtTick;
static UINT64 _curTickTime;	// time for 1 MIDI tick at current tempo
static bool _paused;
static bool _playing;
static bool _breakMidiProc;

#define MAX_PORTS	4
static HANDLE hComPort;
static UINT8 lastPort = (UINT8)-1;
static UINT8 maxUsedPort = 0;

static UINT64 Timer_GetFrequency(void)
{
	LARGE_INTEGER TempLInt;
	QueryPerformanceFrequency(&TempLInt);
	return TempLInt.QuadPart;
}

static UINT64 Timer_GetTime(void)
{
	LARGE_INTEGER lgInt;
	QueryPerformanceCounter(&lgInt);
	return (UINT64)lgInt.QuadPart;
}

static inline UINT32 ReadBE24(const UINT8* data)
{
	return (data[0x00] << 16) | (data[0x01] << 8) | (data[0x02] << 0);
}

int main(int argc, char* argv[])
{
	std::cout << "COM-Port MIDI Player\n";
	std::cout << "--------------------\n";
	if (argc < 3)
	{
		std::cout << "Usage: " << argv[0] << " COMPort input.mid\n";
#ifdef _DEBUG
		getchar();
#endif
		return 0;
	}
	
	UINT8 RetVal;
	
	std::cout << "Opening ...\n";
	RetVal = CMidi.LoadFile(argv[2]);
	if (RetVal)
	{
		std::cout << "Error opening file!\n";
		std::cout << "Errorcode: " << RetVal;
		return 1;
	}
	
	_tmrFreq = Timer_GetFrequency();
	
	RetVal = OpenCOMPort(argv[1]);
	if (RetVal & 0x80)
	{
		std::cout << "Error opening COM Port!\n";
		return 2;
	}
	
	Start();
	
	std::cout << "Playing.\n";
	while(_playing)
	{
		Sleep(1);
		if (_kbhit())
		{
			int key = _getch();
			if (key == 0x1B || key == 'Q' || key == 'q')
			{
				break;
			}
			else if (key == ' ')
			{
				_paused = ! _paused;
				_tmrStep = 0;
			}
		}
		
#if 0
		{
			DWORD comErrs;
			COMSTAT comStat;
			BOOL retB = ClearCommError(hComPort, &comErrs, &comStat);
			if (! retB)
				printf("ClearCommError failed\n");
			printf("ComStat: fCtsHold %u, fDsrHold %u, fRlsdHold %u, fXoffHold %u, fXoffSent %u, fEof %u, fTxim %u  \r",
				comStat.fCtsHold, comStat.fDsrHold, comStat.fRlsdHold, comStat.fXoffHold, comStat.fXoffSent, comStat.fEof, comStat.fTxim);
		}
#endif
		PurgeComm(hComPort, PURGE_RXCLEAR);	// we don't want to receive data, so just always clear the input buffer
		DoPlaybackStep();
		
		printms(GetPlaybackPos());	printf("    \r");
	}
	Stop();
	
	CloseCOMPort();
	
	std::cout << "Cleaning ...\n";
	CMidi.ClearAll();
	std::cout << "Done.\n";
#ifdef _DEBUG
	//getchar();
#endif
	
	return 0;
}

UINT8 OpenCOMPort(const char* port)
{
	BOOL retB;
	
	std::string fullPath = std::string("\\\\.\\") + port;
	hComPort = CreateFileA(fullPath.c_str(), GENERIC_WRITE, 0x00, NULL, OPEN_EXISTING, /*FILE_FLAG_OVERLAPPED*/0, NULL);
	if (hComPort == INVALID_HANDLE_VALUE)
		return 0xFF;
	
	retB = SetupComm(hComPort, 1024, 1024);
	if (! retB)
		printf("SetupComm failed\n");
	retB = PurgeComm(hComPort, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	if (! retB)
		printf("PurgeComm failed\n");
	
	COMMTIMEOUTS cto;
	cto.ReadIntervalTimeout = -1;
	cto.ReadTotalTimeoutMultiplier = -1;
	cto.ReadTotalTimeoutConstant = 6;
	cto.WriteTotalTimeoutMultiplier = 0;
	cto.WriteTotalTimeoutConstant = 0;
	retB = SetCommTimeouts(hComPort, &cto);
	if (! retB)
		printf("SetCommTimeouts failed\n");
	
	DCB dcb;
	GetCommState(hComPort, &dcb);
	memset(&dcb, 0x00, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);
	dcb.BaudRate = 38400;
	dcb.fBinary = 1;
	if (0)	// Yamaha - no CTS flow control used
	{
		dcb.fOutxCtsFlow = 0;
		dcb.fRtsControl = RTS_CONTROL_DISABLE;
	}
	else if (1)	// Roland - requires CTS/RTS flow control
	{
		dcb.fOutxCtsFlow = 1;
		dcb.fRtsControl = RTS_CONTROL_ENABLE;
	}
	dcb.ByteSize = 8;
	dcb.StopBits = ONESTOPBIT;
	dcb.XoffLim = 512;
	retB = SetCommState(hComPort, &dcb);
	if (! retB)
		printf("SetCommState failed\n");
	
	//COMMPROP comProp;
	//retB = GetCommProperties(hComPort, &comProp);
	//if (! retB)
	//	printf("GetCommProperties failed\n");
	
	//DWORD comErrs;
	//COMSTAT comStat;
	//retB = ClearCommError(hComPort, &comErrs, &comStat);
	//if (! retB)
	//	printf("ClearCommError failed\n");
	//printf("Initial CTS: %u\n", ! comStat.fCtsHold);
	
	return 0x00;
}

void CloseCOMPort(void)
{
	CloseHandle(hComPort);
	hComPort = NULL;
	
	return;
}

static void printms(double time)
{
	static const UINT8 secondDigits = 2;
	// print time as mm:ss.c
	unsigned int fracDiv;
	unsigned int sFrac;
	unsigned int sec;
	unsigned int min;
	
	fracDiv = 1;
	for (sFrac = secondDigits; sFrac > 0; sFrac --)
		fracDiv *= 10;
	
	sFrac = (unsigned int)floor(time * fracDiv + 0.5);
	sec = sFrac / fracDiv;
	sFrac %= fracDiv;
	min = sec / 60;
	sec %= 60;
	if (secondDigits == 0)
		printf("%02u:%02u", min, sec);
	else
		printf("%02u:%02u.%0*u", min, sec, secondDigits, sFrac);
	
	return;
}

static void RefreshTickTime(void)
{
	UINT64 tmrMul;
	UINT64 tmrDiv;
	
	tmrMul = _tmrFreq * _midiTempo;
	tmrDiv = (UINT64)1000000 * CMidi.GetMidiResolution();
	if (tmrDiv == 0)
		tmrDiv = 1000000;
	_curTickTime = (tmrMul + tmrDiv / 2) / tmrDiv;
	return;
}

static double GetPlaybackPos(void)
{
	UINT64 curTime = Timer_GetTime();
	INT64 tmrTick = curTime - _tmrMinStart;
	return (double)tmrTick / (double)_tmrFreq;
}

void Start(void)
{
	size_t curTrk;
	
	_trkStates.clear();
	for (curTrk = 0; curTrk < CMidi.GetTrackCount(); curTrk ++)
	{
		MidiTrack* mTrk = CMidi.GetTrack(curTrk);
		TrackState mTS;
		
		mTS.trkID = curTrk;
		mTS.portID = 0;
		mTS.endPos = mTrk->GetEventEnd();
		mTS.evtPos = mTrk->GetEventBegin();
		_trkStates.push_back(mTS);
	}
	_midiTempo = 500000;
	RefreshTickTime();
	
	_curEvtTick = 0;
	_nextEvtTick = 0;
	_tmrStep = 0;
	_tmrMinStart = Timer_GetTime();
	_playing = true;
	return;
}

void Stop(void)
{
	MidiEvent midEvt;
	UINT8 curChn;
	UINT8 curPort;
	
	midEvt.evtType = 0xB0;
	midEvt.evtValA = 0x7B;
	midEvt.evtValB = 0x00;
	for (curPort = 0; curPort <= maxUsedPort; curPort ++)
	{
		for (curChn = 0; curChn < 0x10; curChn ++)
		{
			midEvt.evtType = (midEvt.evtType & 0xF0) | curChn;
			SendShortEvt(curPort, &midEvt);
		}
	}
	
	return;
}

static void SendShortEvt(UINT8 portID, const MidiEvent* midiEvt)
{
	UINT8 evtType = midiEvt->evtType & 0xF0;
	UINT8 evtLen = ((evtType & 0xE0) == 0xC0) ? 2 : 3;
	UINT8 data[5] = {0xF5, 1 + portID, midiEvt->evtType, midiEvt->evtValA, midiEvt->evtValB};
	
	{
		DWORD comErrs;
		COMSTAT comStat;
		BOOL retB = ClearCommError(hComPort, &comErrs, &comStat);
		if (! retB)
			printf("ClearCommError failed\n");
	}
	if (portID == lastPort)
	{
		WriteFile(hComPort, &data[2], evtLen, NULL, NULL);
	}
	else
	{
		lastPort = portID;
		if (portID > maxUsedPort)
			maxUsedPort = portID;
		WriteFile(hComPort, &data[0], 2 + evtLen, NULL, NULL);
	}
	return;
}

static void SendLongEvt(UINT8 portID, const MidiEvent* midiEvt)
{
	std::vector<UINT8> data(3 + midiEvt->evtData.size());
	data[0] = 0xF5;
	data[1] = 1 + portID;
	data[2] = midiEvt->evtType;
	memcpy(&data[3], &midiEvt->evtData[0], midiEvt->evtData.size());
	
	if (portID == lastPort)
	{
		WriteFile(hComPort, &data[2], data.size() - 2, NULL, NULL);
	}
	else
	{
		lastPort = portID;
		if (portID > maxUsedPort)
			maxUsedPort = portID;
		WriteFile(hComPort, &data[0], data.size(), NULL, NULL);
	}
	return;
}

void DoEvent(TrackState* trkState, const MidiEvent* midiEvt)
{
	if (midiEvt->evtType < 0xF0)
	{
		SendShortEvt(trkState->portID, midiEvt);
		return;
	}
	
	switch(midiEvt->evtType)
	{
	case 0xF0:	// SysEx
		if (midiEvt->evtData.size() < 0x03)
			break;	// ignore invalid/empty SysEx messages
		SendLongEvt(trkState->portID, midiEvt);
		break;
	case 0xF7:	// SysEx continuation
		SendLongEvt(trkState->portID, midiEvt);
		break;
	case 0xFF:	// Meta Event
		switch(midiEvt->evtValA)
		{
		//case 0x20:	// Channel Prefix
		case 0x21:	// MIDI Port
			if (midiEvt->evtData.size() >= 1)
			{
				trkState->portID = midiEvt->evtData[0] % MAX_PORTS;
			}
			break;
		case 0x2F:	// Track End
			trkState->evtPos = trkState->endPos;
			break;
		case 0x51:	// Tempo
			_midiTempo = ReadBE24(&midiEvt->evtData[0x00]);
			RefreshTickTime();
			break;
		}
		break;
	}
	
	return;
}

void DoPlaybackStep(void)
{
	if (_paused)
		return;
	
	UINT64 curTime;
	
	curTime = Timer_GetTime();
	if (! _tmrStep && curTime < _tmrMinStart)
		_tmrStep = _tmrMinStart;	// handle "initial delay" after starting the song
	if (curTime < _tmrStep)
		return;
	
	while(_playing)
	{
		UINT32 minNextTick = (UINT32)-1;
		
		size_t curTrk;
		for (curTrk = 0; curTrk < _trkStates.size(); curTrk ++)
		{
			TrackState* mTS = &_trkStates[curTrk];
			if (mTS->evtPos == mTS->endPos)
				continue;
			
			if (mTS->evtPos->tick < minNextTick)
				minNextTick = mTS->evtPos->tick;
		}
		if (minNextTick == (UINT32)-1)	// -1 -> end of sequence
		{
			_playing = false;
			break;
		}
		
		if (minNextTick > _nextEvtTick)
		{
			// next event has higher tick number than "next tick to wait for" (_nextEvtTick)
			// -> set new values for "update time" (system time: _tmrStep, event tick: _nextEvtTick)
			_tmrStep += (minNextTick - _nextEvtTick) * _curTickTime;
			_nextEvtTick = minNextTick;
		}
		
		if (curTime < _tmrStep)
			break;	// exit the loop when going beyond "current time"
		if (_tmrStep + _tmrFreq * 1 < curTime)
			_tmrStep = curTime;	// reset time when lagging behind >= 1 second
		
		_breakMidiProc = false;
		_curEvtTick = _nextEvtTick;
		for (curTrk = 0; curTrk < _trkStates.size(); curTrk ++)
		{
			TrackState* mTS = &_trkStates[curTrk];
			while(mTS->evtPos != mTS->endPos && mTS->evtPos->tick <= _nextEvtTick)
			{
				DoEvent(mTS, &*mTS->evtPos);
				if (_breakMidiProc || mTS->evtPos == mTS->endPos)
					break;
				++mTS->evtPos;
			}
			if (_breakMidiProc)
				break;
		}
	}
	
	return;
}
