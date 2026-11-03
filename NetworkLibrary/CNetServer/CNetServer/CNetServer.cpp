#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "CNetServer.h"
#include "process.h"

CNetServer::CNetServer() :
	m_hcp(NULL), m_listenSock(INVALID_SOCKET), m_isRunning(false), m_sessions(nullptr),
	m_sessionCount(0), m_sessionIDGenerator(0), m_maxSessionCount(0), m_nagle(TRUE)
{
}

CNetServer::~CNetServer()
{
	if (m_isRunning)
		Stop();
}

bool CNetServer::Start(const CHAR* ip, SHORT port, DWORD workerCount, DWORD activeCount, BOOL nagle, DWORD maxSessionCount)
{
	if (maxSessionCount == 0 || maxSessionCount > Session::INDEX_MASK + 1)
		return false;

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

	// Session 배열 세팅
	m_sessions = new Session[m_maxSessionCount];
	for (int i = (int)m_maxSessionCount - 1; i >= 0; i--)
		m_indexStack.Push((DWORD)i);

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

void CNetServer::Stop()
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
	if (m_sessions != nullptr)
	{
		for (DWORD i = 0; i < m_maxSessionCount; i++)
		{
			Session* session = &m_sessions[i];
			if (session->ioCount & Session::RELEASE_FLAG)
				continue;

			closesocket(session->sock);

			for (int j = 0; j < session->sendingCount; j++)
				Packet::Free(session->sendingPackets[j]);

			Packet* packet;
			while (session->sendQ.Dequeue(packet))
				Packet::Free(packet);
		}

		delete[] m_sessions;
		m_sessions = nullptr;
	}

	if (m_hcp != NULL)
	{
		CloseHandle(m_hcp);
		m_hcp = NULL;
	}

	WSACleanup();
}

int CNetServer::GetSessionCount()
{
	return m_sessionCount;
}

bool CNetServer::Disconnect(DWORD64 sessionID)
{
	Session* session = AcquireSession(sessionID);
	if (session == nullptr)
		return false;

	closesocket(session->sock);

	ReturnSession(session);
	return true;
}

bool CNetServer::SendPacket(DWORD64 sessionID, Packet* packet)
{
	Session* session = AcquireSession(sessionID);
	if (session == nullptr)
		return false;

	packet->SetNetHeader();
	packet->AddRef();
	session->sendQ.Enqueue(packet);

	SendPost(session);

	ReturnSession(session);
	return true;
}

int CNetServer::getAcceptTPS()
{
	return 0;
}

int CNetServer::getRecvMessageTPS()
{
	return 0;
}

int CNetServer::getSendMessageTPS()
{
	return 0;
}

unsigned int __stdcall CNetServer::AcceptThread(LPVOID arg)
{
	CNetServer* server = (CNetServer*)arg;
	server->AcceptThreadMain();
	return 0;
}

void CNetServer::AcceptThreadMain()
{
	SOCKET client_sock;
	SOCKADDR_IN clientaddr;
	int addrlen;
	WSABUF wsabuf;
	DWORD flags;

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

		// 빈 세션 index 획득. 없으면 최대 접속자 수 초과
		DWORD index;
		if (m_indexStack.Pop(&index) == false)
		{
			closesocket(client_sock);
			continue;
		}

		// IP 필터링
		if (OnConnectionRequest(ipStr, port) == false)
		{
			m_indexStack.Push(index);
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

		Session* session = &m_sessions[index];
		DWORD64 sessionID = (++m_sessionIDGenerator << Session::INDEX_BITS) | index;
		session->Init(client_sock, sessionID, ipStr, port);

		InterlockedIncrement(&session->ioCount);
		InterlockedAnd(&session->ioCount, ~Session::RELEASE_FLAG);
		InterlockedIncrement(&m_sessionCount);

		CreateIoCompletionPort((HANDLE)client_sock, m_hcp, sessionID, 0);

		OnAccept(sessionID);

		// 첫 WSARecv
		InterlockedIncrement(&session->ioCount);
		flags = 0;
		wsabuf.buf = session->recvQ.GetRearBufferPtr();
		wsabuf.len = session->recvQ.DirectEnqueueSize();

		ZeroMemory(&session->recvOverlapped.overlapped, sizeof(OVERLAPPED));
		int retval = WSARecv(client_sock, &wsabuf, 1, NULL, &flags, (OVERLAPPED*)&session->recvOverlapped, NULL);
		if (retval == SOCKET_ERROR)
		{
			int errCode = WSAGetLastError();
			if (errCode != ERROR_IO_PENDING)
			{
				if (errCode != WSAECONNRESET)
				{
					__debugbreak();
				}
				InterlockedDecrement(&session->ioCount);
			}
		}

		ReturnSession(session);
	}
}

unsigned int __stdcall CNetServer::WorkerThread(LPVOID arg)
{
	CNetServer* server = (CNetServer*)arg;
	server->WorkerThreadMain();
	return 0;
}

void CNetServer::WorkerThreadMain()
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
				ReleaseSession(session);
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
	}
}

Session* CNetServer::AcquireSession(DWORD64 sessionID)
{
	DWORD index = (DWORD)(sessionID & Session::INDEX_MASK);
	if (index >= m_maxSessionCount)
		return nullptr;

	Session* session = &m_sessions[index];

	LONG ioCount = InterlockedIncrement(&session->ioCount);

	if ((ioCount & Session::RELEASE_FLAG) != 0 || session->sessionID != sessionID)
	{
		ReturnSession(session);
		return nullptr;
	}

	return session;
}

void CNetServer::ReturnSession(Session* session)
{
	if (InterlockedDecrement(&session->ioCount) == 0)
		ReleaseSession(session);
}

void CNetServer::ReleaseSession(Session* session)
{
	if (InterlockedCompareExchange(&session->ioCount, Session::RELEASE_FLAG, 0) != 0)
		return;

	closesocket(session->sock);

	for (int i = 0; i < session->sendingCount; i++)
		Packet::Free(session->sendingPackets[i]);
	session->sendingCount = 0;

	Packet* packet;
	while (session->sendQ.Dequeue(packet))
		Packet::Free(packet);

	InterlockedDecrement(&m_sessionCount);

	OnRelease(session->sessionID);

	m_indexStack.Push((DWORD)(session - m_sessions));
}

void CNetServer::RecvProc(Session* session, DWORD cbTransferred)
{
	session->recvQ.MoveRear(cbTransferred);

	while (true)
	{
		int useSize = session->recvQ.GetUseSize();
		if (useSize < sizeof(NetServerHeader))
			break;

		NetServerHeader header;
		session->recvQ.Peek((char*)&header, sizeof(header));
		if (useSize < sizeof(header) + header._len)
			break;

		// 패킷 코드 불일치
		if (header._byteCode != Packet::PACKET_CODE)
		{
			ReturnSession(session);
			return;
		}

		Packet* recvPacket = Packet::Alloc();
		session->recvQ.Dequeue(recvPacket->GetBufferPtr(), sizeof(header) + header._len);
		recvPacket->MoveWritePos(header._len);

		// checksum 불일치
		if (recvPacket->Decode() == false)
		{
			ReturnSession(session);
			Packet::Free(recvPacket);
			return;
		}

		OnRecv(session->sessionID, recvPacket);
		Packet::Free(recvPacket);
	}

	// WSARecv 재등록
	WSABUF wsabuf[2];
	DWORD flags = 0;
	int recvQFreeSize = session->recvQ.GetFreeSize();
	int recvQDirectSize = session->recvQ.DirectEnqueueSize();

	wsabuf[0].buf = session->recvQ.GetRearBufferPtr();
	wsabuf[0].len = recvQDirectSize;

	int retval = 0;
	ZeroMemory(&session->recvOverlapped.overlapped, sizeof(OVERLAPPED));
	if (recvQFreeSize == recvQDirectSize)
	{
		retval = WSARecv(session->sock, wsabuf, 1, NULL, &flags, (OVERLAPPED*)&session->recvOverlapped, NULL);
	}
	else if (recvQFreeSize > recvQDirectSize)
	{
		wsabuf[1].buf = session->recvQ.GetBufferPtr();
		wsabuf[1].len = recvQFreeSize - recvQDirectSize;
		retval = WSARecv(session->sock, wsabuf, 2, NULL, &flags, (OVERLAPPED*)&session->recvOverlapped, NULL);
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
				ReleaseSession(session);
				return;
			}
		}
	}
}

void CNetServer::SendProc(Session* session, DWORD cbTransferred)
{
	for (int i = 0; i < session->sendingCount; i++)
		Packet::Free(session->sendingPackets[i]);
	session->sendingCount = 0;

	InterlockedExchange(&session->isSending, FALSE);

	SendPost(session);

	if (InterlockedDecrement(&session->ioCount) == 0)
		ReleaseSession(session);
}

void CNetServer::SendPost(Session* session)
{
	while (true)
	{
		if (InterlockedExchange(&session->isSending, TRUE) == TRUE)
			return;

		if (session->sendQ.GetSize() > 0)
			break;

		InterlockedExchange(&session->isSending, FALSE);
		if (session->sendQ.GetSize() == 0)
			return;
	}

	WSABUF wsabuf[Session::MAX_SEND_PACKET];
	int count = 0;
	Packet* packet;
	while (count < Session::MAX_SEND_PACKET
		&& session->sendQ.Dequeue(packet))
	{
		session->sendingPackets[count] = packet;
		wsabuf[count].buf = packet->GetSendPtr();
		wsabuf[count].len = (ULONG)packet->GetSendSize();
		count++;
	}
	session->sendingCount = count;

	InterlockedIncrement(&session->ioCount);
	ZeroMemory(&session->sendOverlapped.overlapped, sizeof(OVERLAPPED));
	int retval = WSASend(session->sock, wsabuf, count, NULL, 0, (OVERLAPPED*)&session->sendOverlapped, NULL);
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
				ReleaseSession(session);
		}
	}
}
