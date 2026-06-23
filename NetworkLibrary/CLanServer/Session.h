#pragma once

#include <Winsock2.h>
#include <RingBuffer.h>

enum class IOType
{
	RECV,
	SEND,
};

struct MyOverlapped
{
	OVERLAPPED overlapped;
	IOType type;

	MyOverlapped(IOType type) : type(type)
	{
		ZeroMemory(&overlapped, sizeof(overlapped));
	}
};

struct Session
{
	SOCKET sock;
	DWORD64 sessionID;
	RingBuffer recvQ;
	RingBuffer sendQ;
	MyOverlapped recvOverlapped;
	MyOverlapped sendOverlapped;
	char ip[16];
	USHORT port;
	ULONG ioCount;
	LONG isSending;
	CRITICAL_SECTION cs;

	Session(SOCKET s, DWORD64 id, const char* ipAddr, SHORT port, DWORD bufSize = 2000) :
		sock(s),
		sessionID(id),
		recvQ(bufSize + 1),
		sendQ(bufSize + 1),
		recvOverlapped(IOType::RECV),
		sendOverlapped(IOType::SEND),
		port(port),
		ioCount(0),
		isSending(FALSE)
	{
		strncpy_s(this->ip, ipAddr, _TRUNCATE);
		InitializeCriticalSection(&cs);
	}

	~Session()
	{
		DeleteCriticalSection(&cs);
	}
};