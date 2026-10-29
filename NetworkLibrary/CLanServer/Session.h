#pragma once

#include <Winsock2.h>
#include "RingBuffer.h"
#include "PacketBuffer.h"
#include "LockFreeQueue.h"

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
	enum { MAX_SEND_PACKET = 100 };		// WSASend 1회에 묶는 최대 Packet 수

	SOCKET sock;
	DWORD64 sessionID;
	RingBuffer recvQ;
	LockFreeQueue<Packet*> sendQ;
	Packet* sendingPackets[MAX_SEND_PACKET];	// 송신 중인 패킷들. 정상 완료 시 전부 Free
	int sendingCount;

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
		sendingCount(0),
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