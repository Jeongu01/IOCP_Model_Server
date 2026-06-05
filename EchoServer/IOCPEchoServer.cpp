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

#define SERVERPORT	6000
#define BUFSIZE		512
#define THREADNUM	21

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

	Session(SOCKET s, DWORD64 id, const char* ipAddr, SHORT port) :
		sock(s),
		sessionID(id),
		recvQ(BUFSIZE + 1),
		sendQ(BUFSIZE + 1),
		recvOverlapped(IOType::RECV),
		sendOverlapped(IOType::SEND),
		port(port),
		ioCount(0)
	{
		strncpy_s(this->ip, ipAddr, _TRUNCATE);
	}
};

HANDLE hcp;
SOCKET listen_sock;

std::unordered_map<ULONGLONG, Session*> sessionMap;
SRWLOCK sessionMapLock;

void ReleaseSession(ULONGLONG sessionID);
Session* FindSession(ULONGLONG sessionID);

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
			ReleaseSession(session->sessionID);
			continue;
		}


		if (overlapped->type == IOType::RECV)
		{
			// RecvQ 후처리
			char buf[BUFSIZE + 1];
			strncpy_s(buf, session->recvQ.GetFrontBufferPtr(), cbTransferred);
			session->recvQ.MoveFront(cbTransferred);

			buf[cbTransferred] = '\0';
			printf("[TCP/%s:%d] %s\n", session->ip, session->port, buf);

			// Send
			session->sendQ.Enqueue(buf, cbTransferred);

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
				if (WSAGetLastError() != WSA_IO_PENDING)
				{
					printf("[ERROR] WSASend: %d\n", WSAGetLastError());
					ReleaseSession(session->sessionID);
				}
			}

			// WSARecv 재등록
			DWORD recvbytes;
			DWORD flags = 0;
			int recvQFreeSize = session->recvQ.GetFreeSize();
			int recvQDirectSize = session->recvQ.DirectEnqueueSize();

			wsabuf[0].buf = session->recvQ.GetRearBufferPtr();
			wsabuf[0].len = recvQDirectSize;
			InterlockedIncrement(&session->ioCount);
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
				if (WSAGetLastError() != WSA_IO_PENDING)
				{
					printf("[ERROR] WSARecv: %d\n", WSAGetLastError());
					ReleaseSession(session->sessionID);
				}
			}
		}
		else if (overlapped->type == IOType::SEND)
		{
			// SendQ 후처리
			session->sendQ.MoveFront(cbTransferred);
		}
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
	if (InterlockedDecrement(&session->ioCount) == 0)
	{
		AcquireSRWLockExclusive(&sessionMapLock);
		sessionMap.erase(sessionID);
		ReleaseSRWLockExclusive(&sessionMapLock);

		closesocket(session->sock);
		delete session;
	}
}