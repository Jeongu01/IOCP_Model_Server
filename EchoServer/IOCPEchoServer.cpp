#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma comment(lib, "ws2_32")
#include <WinSock2.h>
#include <process.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <cstring>
#include <RingBuffer.h>
#include <conio.h>
#include <unordered_map>
#include <PacketBuffer.h>

#define SERVERPORT	6000
#define BUFSIZE		498
#define THREADNUM	21

struct Header
{
	USHORT len;
};

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

	Session(SOCKET s, DWORD64 id, const char* ipAddr, SHORT port) :
		sock(s),
		sessionID(id),
		//recvQ(BUFSIZE + 1),
		//sendQ(BUFSIZE + 1),
		recvQ(999),
		sendQ(499),
		recvOverlapped(IOType::RECV),
		sendOverlapped(IOType::SEND),
		port(port),
		ioCount(0),
		isSending(FALSE)
	{
		strncpy_s(this->ip, ipAddr, _TRUNCATE);
		InitializeCriticalSection(&cs);
	}
};

HANDLE hcp;
SOCKET listen_sock;

std::unordered_map<ULONGLONG, Session*> sessionMap;
SRWLOCK sessionMapLock;

void ReleaseSession(ULONGLONG sessionID);
Session* FindSession(ULONGLONG sessionID);

void RecvProc(Session* session, DWORD cbTransferred);
void SendProc(Session* session, DWORD cbTransferred);
void OnRecv(ULONGLONG sessionID, Packet& packet);
void SendPacket(ULONGLONG sessionID, Packet& packet);

unsigned int WINAPI AcceptThread(LPVOID arg);
unsigned int WINAPI WorkerThread(LPVOID arg);

int main()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;

	hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 10);
	if (hcp == NULL) return 1;

	InitializeSRWLock(&sessionMapLock);

	HANDLE hThread[THREADNUM];
	for (int i = 0; i < THREADNUM - 1; i++)
	{
		hThread[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, NULL, 0, NULL);
		if (hThread == NULL) return 1;
	}
	hThread[THREADNUM - 1] = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, NULL, 0, NULL);

	printf("서버를 종료하려면 'q'를 누르세요\n");
	while (1)
	{
		char ch = _getch();
		if (ch == 'q' || ch == 'Q')
		{
			printf("서버를 종료합니다.\n");

			closesocket(listen_sock);

			for (int i = 0; i < THREADNUM; i++)
			{
				PostQueuedCompletionStatus(hcp, 0, 0, 0);
			}
			break;
		}
	}

	WaitForMultipleObjects(THREADNUM, hThread, TRUE, INFINITE);
	for (int i = 0; i < THREADNUM; i++)
	{
		CloseHandle(hThread[i]);
	}
	CloseHandle(hcp);
	WSACleanup();
	return 0;
}

unsigned int WINAPI AcceptThread(LPVOID arg)
{
	int retval;

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET)
		return 1;

	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	serveraddr.sin_port = htons(SERVERPORT);
	retval = bind(listen_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
		return 1;

	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
		return 1;

	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;
	WSABUF wsabuf;
	DWORD recvbytes, flags;
	DWORD64 sessionIDCount = 0;

	while (1)
	{
		addrlen = sizeof(clientaddr);
		client_sock = accept(listen_sock, (SOCKADDR*)&clientaddr, &addrlen);
		if (client_sock == INVALID_SOCKET)
		{
			int errCode = WSAGetLastError();
			if (errCode == WSAEINTR)
			{
				printf("[END] AcceptThread 종료\n");
			}
			else
			{
				printf("[ERROR] accept: %d\n", errCode);
				__debugbreak();
			}
			break;
		}

		// 링거 설정
		LINGER linger;
		linger.l_onoff = 1;
		linger.l_linger = 0;
		if (setsockopt(client_sock, SOL_SOCKET, SO_LINGER, (const char*)&linger, sizeof(linger)) == SOCKET_ERROR)
		{
			printf("[ERROR] setsockopt(LINGER) 실패: %d\n", WSAGetLastError());
			__debugbreak();
		}

		// 비동기 send를 위한 송신 버퍼 크기 0으로 변경
		int sendBufSize = 0;
		if (setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sendBufSize, sizeof(sendBufSize)) == SOCKET_ERROR)
		{
			printf("[ERROR] setsockopt(SNDBUF) 실패: %d\n", WSAGetLastError());
			__debugbreak();
		}

		Session* ptr = new Session(client_sock, sessionIDCount++, inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
		if (ptr == NULL) break;

		AcquireSRWLockShared(&sessionMapLock);
		sessionMap[ptr->sessionID] = ptr;
		ReleaseSRWLockShared(&sessionMapLock);

		CreateIoCompletionPort((HANDLE)client_sock, hcp, ptr->sessionID, 0);

		printf("[TCP 서버] 클라이언트 접속: IP 주소=%s, 포트 번호=%d\n", ptr->ip, ptr->port);

		InterlockedIncrement(&ptr->ioCount);
		flags = 0;
		wsabuf.buf = ptr->recvQ.GetRearBufferPtr();
		wsabuf.len = ptr->recvQ.DirectEnqueueSize();
		retval = WSARecv(client_sock, &wsabuf, 1, &recvbytes, &flags, (OVERLAPPED*)&ptr->recvOverlapped, NULL);
		if (retval == SOCKET_ERROR)
		{
			if (WSAGetLastError() != ERROR_IO_PENDING)
			{
				printf("[ERROR] WSARecv: %d\n", WSAGetLastError());
				if (InterlockedDecrement(&ptr->ioCount) == 0)
					ReleaseSession(ptr->sessionID);
				__debugbreak();
			}
			continue;
		}
	}
}

unsigned int WINAPI WorkerThread(LPVOID arg)
{
	int retval;

	while (1)
	{
		DWORD cbTransferred = -1;
		DWORD64 sessionID = -1;
		MyOverlapped* overlapped;
		retval = GetQueuedCompletionStatus(hcp, &cbTransferred, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlapped, INFINITE);

		// 종료 처리
		if (cbTransferred == 0 && sessionID == 0 && overlapped == 0)
		{
			printf("[END] WorkerThread %d 종료\n", GetCurrentThreadId());
			break;
		}

		// 시간 초과
		if (overlapped == NULL)
		{
			printf("[ERROR] GQCS Failed/Time Out: %d\n", WSAGetLastError());
			__debugbreak();
			continue;
		}

		Session* session;
		if (overlapped->type == IOType::RECV)
			session = CONTAINING_RECORD(overlapped, Session, recvOverlapped);
		else if (overlapped->type == IOType::SEND)
			session = CONTAINING_RECORD(overlapped, Session, sendOverlapped);
		else
		{
			printf("[ERROR] Overlapped Type Error\n");
			__debugbreak();
			break;
		}

		EnterCriticalSection(&session->cs);

		// 에러 및 끊김 처리
		if (retval == FALSE || cbTransferred == 0)
		{
			// IO 실패
			if (retval == FALSE)
			{
				int errCode = WSAGetLastError();
				if (errCode != ERROR_NETNAME_DELETED)
				{
					printf("[ERROR] GQCS IO Failed : %d\n", errCode);
				}
			}
			printf("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\n", session->ip, session->port);
			LeaveCriticalSection(&session->cs);
			if (InterlockedDecrement(&session->ioCount) == 0)
				ReleaseSession(session->sessionID);
			continue;
		}


		if (overlapped->type == IOType::RECV)
		{
			RecvProc(session, cbTransferred);
		}
		else if (overlapped->type == IOType::SEND)
		{
			SendProc(session, cbTransferred);
		}

		LeaveCriticalSection(&session->cs);
	}
	return 0;
}

Session* FindSession(ULONGLONG sessionID)
{
	Session* session = nullptr;

	AcquireSRWLockShared(&sessionMapLock);

	auto iter = sessionMap.find(sessionID);
	if (iter != sessionMap.end())
	{
		session = iter->second;
	}

	ReleaseSRWLockShared(&sessionMapLock);
	return session;
}

void ReleaseSession(ULONGLONG sessionID)
{
	Session* session = FindSession(sessionID);
	if (session == nullptr)
		return;

	AcquireSRWLockExclusive(&sessionMapLock);
	sessionMap.erase(sessionID);
	ReleaseSRWLockExclusive(&sessionMapLock);

	closesocket(session->sock);
	DeleteCriticalSection(&session->cs);
	delete session;
	session = nullptr;
	return;
}

void RecvProc(Session* session, DWORD cbTransferred)
{
	printf("[DEBUG] 수신 바이트 수: %d\n", cbTransferred);
	int retval;

	EnterCriticalSection(&session->cs);
	session->recvQ.MoveRear(cbTransferred);

	while (true)
	{
		int useSize = session->recvQ.GetUseSize();
		if (useSize < sizeof(Header))
			break;

		Header header;
		session->recvQ.Peek((char*)&header, sizeof(Header));
		if (useSize < sizeof(Header) + header.len)
			break;
		session->recvQ.MoveFront(sizeof(Header));

		Packet recvPacket;
		recvPacket.PutData(session->recvQ.GetFrontBufferPtr(), header.len);
		session->recvQ.MoveFront(header.len);

		OnRecv(session->sessionID, recvPacket);
	}

	// WSARecv 재등록
	WSABUF wsabuf[2];
	DWORD recvbytes;
	DWORD flags = 0;
	int recvQFreeSize = session->recvQ.GetFreeSize();
	int recvQDirectSize = session->recvQ.DirectEnqueueSize();

	wsabuf[0].buf = session->recvQ.GetRearBufferPtr();
	wsabuf[0].len = recvQDirectSize;
	if (recvQFreeSize == recvQDirectSize)
	{
		retval = WSARecv(session->sock, wsabuf, 1, &recvbytes, &flags, (OVERLAPPED*)&session->recvOverlapped, NULL);
	}
	else if (recvQFreeSize > recvQDirectSize)
	{
		wsabuf[1].buf = session->recvQ.GetBufferPtr();
		wsabuf[1].len = recvQFreeSize - recvQDirectSize;
		retval = WSARecv(session->sock, wsabuf, 2, &recvbytes, &flags, (OVERLAPPED*)&session->recvOverlapped, NULL);
	}
	if (retval == SOCKET_ERROR)
	{
		int errCode = WSAGetLastError();
		if (errCode != WSA_IO_PENDING)
		{
			if (errCode != WSAECONNRESET)
			{
				printf("[ERROR] WSARecv: %d\n", errCode);
				__debugbreak();
			}
			if (InterlockedDecrement(&session->ioCount) == 0)
			{
				LeaveCriticalSection(&session->cs);
				ReleaseSession(session->sessionID);
				return;
			}
		}
	}

	LeaveCriticalSection(&session->cs);
}

void SendProc(Session* session, DWORD cbTransferred)
{
	printf("[DEBUG] 송신 바이트 수: %d\n", cbTransferred);

	if (InterlockedDecrement(&session->ioCount) == 0)
	{
		ReleaseSession(session->sessionID);
		return;
	}

	EnterCriticalSection(&session->cs);

	// SendQ 후처리
	session->sendQ.MoveFront(cbTransferred);

	if (session->sendQ.GetUseSize() > 0)
	{
		int retval;
		DWORD sendbytes;
		WSABUF wsabuf[2];
		int sendQUseSize = session->sendQ.GetUseSize();
		int sendQDirectSize = session->sendQ.DirectDequeueSize();

		wsabuf[0].buf = session->sendQ.GetFrontBufferPtr();
		wsabuf[0].len = sendQDirectSize;
		InterlockedIncrement(&session->ioCount);
		if (sendQUseSize == sendQDirectSize)
		{
			retval = WSASend(session->sock, wsabuf, 1, &sendbytes, 0, (OVERLAPPED*)&session->sendOverlapped, NULL);
		}
		else if (sendQUseSize > sendQDirectSize)
		{
			wsabuf[1].buf = session->sendQ.GetBufferPtr();
			wsabuf[1].len = sendQUseSize - sendQDirectSize;
			retval = WSASend(session->sock, wsabuf, 2, &sendbytes, 0, (OVERLAPPED*)&session->sendOverlapped, NULL);
		}
		if (retval == SOCKET_ERROR)
		{
			int errCode = WSAGetLastError();
			if (errCode != WSA_IO_PENDING)
			{
				if (errCode != WSAECONNRESET)
				{
					printf("[ERROR] WSASend: %d\n", errCode);
					__debugbreak();
				}
				if (InterlockedDecrement(&session->ioCount) == 0)
				{
					LeaveCriticalSection(&session->cs);
					ReleaseSession(session->sessionID);
					return;
				}
			}
		}
	}
	else
	{
		session->isSending = FALSE;
	}

	LeaveCriticalSection(&session->cs);
}

void OnRecv(ULONGLONG sessionID, Packet& packet)
{
	INT64 echo;
	packet >> echo;
	Session* session = FindSession(sessionID);
	printf("[TCP/%s:%d] %lld\n", session->ip, session->port, echo);
	Packet sendPacket = Packet(BUFSIZE + 1);
	sendPacket << echo;
	SendPacket(sessionID, sendPacket);
}

void SendPacket(ULONGLONG sessionID, Packet& packet)
{
	int retval;
	Session* session = FindSession(sessionID);
	if (session == nullptr)
		return;

	EnterCriticalSection(&session->cs);

	Header header;
	header.len = packet.GetDataSize();
	session->sendQ.Enqueue((char*)&header, sizeof(header));
	session->sendQ.Enqueue(packet.GetReadPtr(), header.len);
	packet.MoveReadPos(header.len);

	if (InterlockedExchange(&session->isSending, TRUE) == FALSE)
	{
		DWORD sendbytes;
		WSABUF wsabuf[2];
		int sendQUseSize = session->sendQ.GetUseSize();
		int sendQDirectSize = session->sendQ.DirectDequeueSize();

		wsabuf[0].buf = session->sendQ.GetFrontBufferPtr();
		wsabuf[0].len = sendQDirectSize;
		InterlockedIncrement(&session->ioCount);
		if (sendQUseSize == sendQDirectSize)
		{
			retval = WSASend(session->sock, wsabuf, 1, &sendbytes, 0, (OVERLAPPED*)&session->sendOverlapped, NULL);
		}
		else if (sendQUseSize > sendQDirectSize)
		{
			wsabuf[1].buf = session->sendQ.GetBufferPtr();
			wsabuf[1].len = sendQUseSize - sendQDirectSize;
			retval = WSASend(session->sock, wsabuf, 2, &sendbytes, 0, (OVERLAPPED*)&session->sendOverlapped, NULL);
		}
		if (retval == SOCKET_ERROR)
		{
			int errCode = WSAGetLastError();
			if (errCode != WSA_IO_PENDING)
			{
				if (errCode != WSAECONNRESET)
				{
					printf("[ERROR] WSASend: %d\n", errCode);
					__debugbreak();
				}
				if (InterlockedDecrement(&session->ioCount) == 0)
				{
					LeaveCriticalSection(&session->cs);
					ReleaseSession(session->sessionID);
					return;
				}
			}
		}
	}

	LeaveCriticalSection(&session->cs);
}