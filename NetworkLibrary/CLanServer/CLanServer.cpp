#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "CLanServer.h"
#include "process.h"

struct Header
{
	USHORT len;
};

CLanServer::CLanServer() :
	m_hcp(NULL), m_listenSock(INVALID_SOCKET), m_isRunning(false), m_sessionIDGenerator(0), m_maxSessionCount(0), m_nagle(TRUE)
{
	InitializeSRWLock(&m_sessionMapLock);
}

CLanServer::~CLanServer()
{
	if (m_isRunning)
		Stop();
}

bool CLanServer::Start(const CHAR* ip, SHORT port, DWORD workerCount, DWORD activeCount, BOOL nagle, DWORD maxSessionCount)
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;

	m_maxSessionCount = maxSessionCount;
	m_nagle = nagle;

	// IOCP 생성
	m_hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, activeCount);
	if (m_hcp == NULL) return false;

	// 리슨 소켓 생성
	m_listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (m_listenSock == INVALID_SOCKET) return false;

	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = inet_addr(ip);
	serveraddr.sin_port = htons(port);

	// bind
	if (bind(m_listenSock, (SOCKADDR*)&serveraddr, sizeof(serveraddr)) == SOCKET_ERROR)
		return false;

	// listen
	if (listen(m_listenSock, SOMAXCONN) == SOCKET_ERROR)
		return false;

	m_isRunning = true;

	// Accept 스레드 생성
	HANDLE hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, NULL);
	if (hAcceptThread == NULL) return false;
	m_threads.push_back(hAcceptThread);
	
	// 워커 스레드 생성
	for (unsigned int i = 0; i < workerCount; i++)
	{
		HANDLE hWorkerThread = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);
		if (hWorkerThread == NULL) return false;
		m_threads.push_back(hWorkerThread);
	}

	return true;
}

void CLanServer::Stop()
{
	m_isRunning = false;

	// 리슨 소켓 닫기
	if (m_listenSock != INVALID_SOCKET)
	{
		closesocket(m_listenSock);
		m_listenSock = INVALID_SOCKET;
	}

	// 워커 스레드 종료
	for (int i = 0; i < m_threads.size(); i++)
	{
		PostQueuedCompletionStatus(m_hcp, 0, 0, 0);
	}

	WaitForMultipleObjects((DWORD)m_threads.size(), m_threads.data(), TRUE, INFINITE);
	for (HANDLE hThread : m_threads)
	{
		CloseHandle(hThread);
	}
	m_threads.clear();

	// 남은 세션 정리
	AcquireSRWLockExclusive(&m_sessionMapLock);
	for (auto& pair : m_sessionMap)
	{
		Session* session = pair.second;
		closesocket(session->sock);
		delete session;
	}
	m_sessionMap.clear();
	ReleaseSRWLockExclusive(&m_sessionMapLock);

	if (m_hcp != NULL)
	{
		CloseHandle(m_hcp);
		m_hcp = NULL;
	}

	WSACleanup();
}

int CLanServer::GetSessionCount()
{
	AcquireSRWLockShared(&m_sessionMapLock);
	int ret = (int)m_sessionMap.size();
	ReleaseSRWLockShared(&m_sessionMapLock);
	return ret;
}

bool CLanServer::Disconnect(DWORD64 sessionID)
{
	AcquireSRWLockShared(&m_sessionMapLock);
	Session* session = FindSession(sessionID);
	ReleaseSRWLockShared(&m_sessionMapLock);

	if (session == nullptr)
		return false;

	closesocket(session->sock);
	return true;
}

bool CLanServer::SendPacket(DWORD64 sessionID, Packet* packet)
{
	int retval = 0;
	AcquireSRWLockExclusive(&m_sessionMapLock);
	Session* session = FindSession(sessionID);
	ReleaseSRWLockExclusive(&m_sessionMapLock);

	if (session == nullptr) return false;

	EnterCriticalSection(&session->cs);

	Header header;
	header.len = packet->GetDataSize();
	session->sendQ.Enqueue((char*)&header, sizeof(header));
	session->sendQ.Enqueue(packet->GetReadPtr(), header.len);
	packet->MoveReadPos(header.len);

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
					__debugbreak();
				}
				if (InterlockedDecrement(&session->ioCount) == 0)
				{
					LeaveCriticalSection(&session->cs);
					ReleaseSession(session->sessionID);
					return false;
				}
			}
		}
	}

	LeaveCriticalSection(&session->cs);

	return true;
}

int CLanServer::getAcceptTPS()
{
	return 0;
}

int CLanServer::getRecvMessageTPS()
{
	return 0;
}

int CLanServer::getSendMessageTPS()
{
	return 0;
}

unsigned int __stdcall CLanServer::AcceptThread(LPVOID arg)
{
	CLanServer* server = (CLanServer*)arg;
	server->AcceptThreadMain();
	return 0;
}

void CLanServer::AcceptThreadMain()
{
	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;
	WSABUF wsabuf;
	DWORD recvbytes, flags;

	while (m_isRunning)
	{
		addrlen = sizeof(clientaddr);
		client_sock = accept(m_listenSock, (SOCKADDR*)&clientaddr, &addrlen);
		if (client_sock == INVALID_SOCKET)
		{
			break;
		}

		// IP 및 포트 획득
		char* ipStr = inet_ntoa(clientaddr.sin_addr);
		SHORT port = ntohs(clientaddr.sin_port);

		// IP 필터링
		if (OnConnectionRequest(ipStr, port) == false)
		{
			closesocket(client_sock);
			continue;
		}

		// 최대 접속자 수 확인
		if (GetSessionCount() >= (int)m_maxSessionCount)
		{
			closesocket(client_sock);
			continue;
		}

		// 링거 설정
		LINGER linger;
		linger.l_onoff = 1;
		linger.l_linger = 0;
		setsockopt(client_sock, SOL_SOCKET, SO_LINGER, (const char*)&linger, sizeof(linger));

		// 비동기 send를 위한 송신 버퍼 크기 0으로 변경
		int sendBufSize = 0;
		setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sendBufSize, sizeof(sendBufSize));
		
		// Nagle 설정
		if (m_nagle == FALSE)
		{
			int optval = 1;
			setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&optval, sizeof(optval));
		}

		Session* ptr = new Session(client_sock, m_sessionIDGenerator++, ipStr, port);

		AcquireSRWLockShared(&m_sessionMapLock);
		m_sessionMap[ptr->sessionID] = ptr;
		ReleaseSRWLockShared(&m_sessionMapLock);

		CreateIoCompletionPort((HANDLE)client_sock, m_hcp, ptr->sessionID, 0);

		OnAccept(ptr->sessionID);

		// 첫 WSARecv
		InterlockedIncrement(&ptr->ioCount);
		flags = 0;
		wsabuf.buf = ptr->recvQ.GetRearBufferPtr();
		wsabuf.len = ptr->recvQ.DirectEnqueueSize();

		int retval = WSARecv(client_sock, &wsabuf, 1, &recvbytes, &flags, (OVERLAPPED*)&ptr->recvOverlapped, NULL);
		if (retval == SOCKET_ERROR)
		{
			if (WSAGetLastError() != ERROR_IO_PENDING)
			{
				if (InterlockedDecrement(&ptr->ioCount) == 0)
					ReleaseSession(ptr->sessionID);
				__debugbreak();
			}
		}
	}
}

unsigned int __stdcall CLanServer::WorkerThread(LPVOID arg)
{
	CLanServer* server = (CLanServer*)arg;
	server->WorkerThreadMain();
	return 0;
}

void CLanServer::WorkerThreadMain()
{

	int retval;

	while (1)
	{
		DWORD cbTransferred = -1;
		DWORD64 sessionID = -1;
		MyOverlapped* overlapped;
		retval = GetQueuedCompletionStatus(m_hcp, &cbTransferred, (PULONG_PTR)&sessionID, (LPOVERLAPPED*)&overlapped, INFINITE);

		// 종료 처리
		if (cbTransferred == 0 && sessionID == 0 && overlapped == 0)
		{
			break;
		}

		// 시간 초과
		if (overlapped == NULL)
		{
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
			__debugbreak();
			break;
		}

		// 에러 및 끊김 처리
		if (retval == FALSE || cbTransferred == 0)
		{
			if (InterlockedDecrement(&session->ioCount) == 0)
				ReleaseSession(session->sessionID);
			continue;
		}

		EnterCriticalSection(&session->cs);

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
}

Session* CLanServer::FindSession(DWORD64 sessionID)
{
	auto iter = m_sessionMap.find(sessionID);
	if (iter == m_sessionMap.end())
		return nullptr;
	return iter->second;
}

void CLanServer::ReleaseSession(DWORD64 sessionID)
{
	AcquireSRWLockExclusive(&m_sessionMapLock);
	Session* session = FindSession(sessionID);
	if (session == nullptr)
	{
		ReleaseSRWLockExclusive(&m_sessionMapLock);
		return;
	}

	m_sessionMap.erase(sessionID);
	ReleaseSRWLockExclusive(&m_sessionMapLock);

	OnRelease(session->sessionID);

	closesocket(session->sock);
	delete session;
	session = nullptr;
}

void CLanServer::RecvProc(Session* session, DWORD cbTransferred)
{
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
		session->recvQ.Dequeue(recvPacket.GetWritePtr(), header.len);
		recvPacket.MoveWritePos(header.len);

		OnRecv(session->sessionID, &recvPacket);
	}

	// WSARecv 재등록
	WSABUF wsabuf[2];
	DWORD recvbytes;
	DWORD flags = 0;
	int recvQFreeSize = session->recvQ.GetFreeSize();
	int recvQDirectSize = session->recvQ.DirectEnqueueSize();

	wsabuf[0].buf = session->recvQ.GetRearBufferPtr();
	wsabuf[0].len = recvQDirectSize;

	int retval = 0;
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
				__debugbreak();
			}
			if (InterlockedDecrement(&session->ioCount) == 0)
			{
				ReleaseSession(session->sessionID);
				return;
			}
		}
	}
}

void CLanServer::SendProc(Session* session, DWORD cbTransferred)
{
	session->sendQ.MoveFront(cbTransferred);
	session->isSending = FALSE;

	if (session->sendQ.GetUseSize() > 0)
	{
		if (InterlockedExchange(&session->isSending, TRUE) == FALSE)
		{
			DWORD sendbytes;
			WSABUF wsabuf[2];
			int sendQUseSize = session->sendQ.GetUseSize();
			int sendQDirectSize = session->sendQ.DirectDequeueSize();

			wsabuf[0].buf = session->sendQ.GetFrontBufferPtr();
			wsabuf[0].len = sendQDirectSize;
			InterlockedIncrement(&session->ioCount);

			int retval = 0;
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
						__debugbreak();
					}
					if (InterlockedDecrement(&session->ioCount) == 0)
					{
						ReleaseSession(session->sessionID);
						return;
					}
				}
			}
		}
	}

	if (InterlockedDecrement(&session->ioCount) == 0)
	{
		ReleaseSession(session->sessionID);
		return;
	}
}
