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
	enum 
	{ 
		MAX_SEND_PACKET = 100,			// WSASend 1회에 묶는 최대 Packet 수
		RECV_BUFFER_SIZE = 2000,		// RecvQ 크기
		INDEX_BITS = 16,				// sessionID 하위 16bit = 세션 배열 index
		INDEX_MASK = 0xFFFF
	};

	static const LONG RELEASE_FLAG = (LONG)0x8000'0000;		// ioCount 최상위 비트 : Release 플래그

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
	LONG ioCount;
	LONG isSending;

	Session() :
		sock(INVALID_SOCKET),
		sessionID(0),
		recvQ(RECV_BUFFER_SIZE + 1),
		sendingPackets(),
		sendingCount(0),
		recvOverlapped(IOType::RECV),
		sendOverlapped(IOType::SEND),
		port(0),
		ioCount(RELEASE_FLAG),
		isSending(FALSE)
	{
		ip[0] = '\0';
	}

	/**
	 * @brief 새 접속용 초기화 (배열 재사용이라 이전 접속의 값을 반드시 지움)
	 * @param (s)소켓 (id)sessionID (ipAddr)IP (portNum)포트
	 * @return 없음
	 */
	void Init(SOCKET s, DWORD64 id, const char* ipAddr, USHORT portNum)
	{
		sock = s;
		strncpy_s(ip, ipAddr, _TRUNCATE);
		port = portNum;
		recvQ.ClearBuffer();
		sendingCount = 0;
		isSending = FALSE;
		sessionID = id;
	}
};