#include "ChatServer.h"
#include <process.h>

bool ChatServer::Start(const CHAR* ip, SHORT port, DWORD workerCount, DWORD activeCount, BOOL nagle, DWORD maxSessionCount)
{
	_jobEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	_isRunning = true;
	_contentsThread = (HANDLE)_beginthreadex(NULL, 0, ContentsThread, this, 0, NULL);

	return CNetServer::Start(ip, port, workerCount, activeCount, nagle, maxSessionCount);
}

void ChatServer::Stop()
{
	_isRunning = false;
	SetEvent(_jobEvent);
	WaitForSingleObject(_contentsThread, INFINITE);
	CloseHandle(_contentsThread);

	CNetServer::Stop();
	CloseHandle(_jobEvent);
}

unsigned int __stdcall ChatServer::ContentsThread(LPVOID arg)
{
	ChatServer* server = (ChatServer*)arg;
	server->ContentsThreadMain();
	return 0;
}

void ChatServer::ContentsThreadMain()
{
	ULONGLONG prev = GetTickCount64();

	while (_isRunning)
	{
		::WaitForSingleObject(_jobEvent, 1000);

		Job job;
		while (_jobQueue.Dequeue(job))
			HandleJob(job);

		ULONGLONG now = GetTickCount64();
		if (now - prev >= 1000)
		{
			CheckTimeout();
			prev += 1000;
		}
	}
}

// 가상함수 오버라이딩
bool ChatServer::OnConnectionRequest(char* ip, SHORT port)
{
	return true;
}

void ChatServer::OnAccept(DWORD64 sessionID)
{
	PushJob(Job::ACCEPT, sessionID, nullptr);
}

void ChatServer::OnRelease(DWORD64 sessionID)
{
	PushJob(Job::RELEASE, sessionID, nullptr);
}

void ChatServer::OnRecv(DWORD64 sessionID, Packet* packet)
{
	packet->AddRef();
	PushJob(Job::RECV, sessionID, packet);
}

void ChatServer::OnError(int errorCode, std::wstring errorMsg)
{
}

void ChatServer::ProcessAccept(DWORD64 sessionID)
{
	Player* player = new Player;	// 메모리풀로 변경
	player->Init(sessionID, GetTickCount64());
	_sessionMap[sessionID] = player;
}

void ChatServer::ProcessRelease(DWORD64 sessionID)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr)
		return;

	if (player->sectorX != INVALID_SECTOR)
	{
		auto& sector = _sectors[player->sectorY][player->sectorX];
		Player* last = sector.back();
		sector[player->sectorIndex] = last;
		last->sectorIndex = player->sectorIndex;
		sector.pop_back();
	}

	if (player->isLogin == TRUE)
	{
		auto iter = _accountMap.find(player->accountNo);
		if (iter != _accountMap.end())
			_accountMap.erase(player->accountNo);
	}

	_sessionMap.erase(sessionID);
	delete player;	// 메모리풀로 변경
}

void ChatServer::CheckTimeout()
{
	ULONGLONG now = GetTickCount64();
	for (auto& ref : _sessionMap)
	{
		Player* player = ref.second;
		if (now - player->lastRecvTime >= 40000)
			DisconnectPlayer(player);
	}
}

// 패킷 처리
void ChatServer::PacketProc(DWORD64 sessionID, Packet* packet)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr || player->isDisconnecting)
		return;

	player->lastRecvTime = GetTickCount64();

	WORD type;
	(*packet) >> type;

	switch (type)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		PacketProcLogin(player, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		PacketProcSectorMove(player, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		PacketProcMessage(player, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		PacketProcHeartbeat(player, packet);
		break;
	default:
		DisconnectPlayer(player);
		break;
	}
}

void ChatServer::PacketProcLogin(Player* player, Packet* packet)
{
	INT64 accountNo;
	BYTE status = 1;

	// 같은 세션 중복 로그인
	if (player->isLogin == TRUE)
	{
		DisconnectPlayer(player);
		return;
	}

	// 이상한 패킷
	if (packet->GetDataSize() != sizeof(accountNo) + sizeof(player->id)
		+ sizeof(player->nickname) + sizeof(player->sessionKey))
	{
		DisconnectPlayer(player);
		return;
	}

	(*packet) >> accountNo;

	Packet* resPacket = Packet::Alloc();
	mpResLogin(resPacket, status, accountNo);
	SendPacket(player->sessionId, resPacket);
	Packet::Free(resPacket);

	packet->GetData((char*)player->id, sizeof(player->id));
	packet->GetData((char*)player->nickname, sizeof(player->nickname));
	packet->GetData((char*)player->sessionKey, sizeof(player->sessionKey));

	player->accountNo = accountNo;
	player->isLogin = TRUE;

	// 다른 세션 같은 계정 로그인 -> 기존 계정 끊음
	auto iter = _accountMap.find(accountNo);
	if (iter != _accountMap.end())
		DisconnectPlayer(iter->second);
	_accountMap[accountNo] = player;
}

void ChatServer::PacketProcSectorMove(Player* player, Packet* packet)
{
	INT64 accountNo;
	WORD sectorX, sectorY;

	// 로그인 후 이동 가능
	// 이상한 패킷
	if (player->isLogin == FALSE
		|| packet->GetDataSize() != sizeof(accountNo) + sizeof(sectorX) + sizeof(sectorY))
	{
		DisconnectPlayer(player);
		return;
	}

	(*packet) >> accountNo >> sectorX >> sectorY;

	// 패킷의 accountNo와 서버가 관리중인 accountNo가 다름
	if (accountNo != player->accountNo)
	{
		__debugbreak();
	}

	// 섹터 범위 이상
	if (sectorX >= SECTOR_MAX_X || sectorY >= SECTOR_MAX_Y)
	{
		DisconnectPlayer(player);
		return;
	}

	// 이동 처리
	if (player->sectorX != INVALID_SECTOR)
	{
		auto& sector = _sectors[player->sectorY][player->sectorX];
		Player* last = sector.back();
		sector[player->sectorIndex] = last;
		last->sectorIndex = player->sectorIndex;
		sector.pop_back();
	}

	auto& sector = _sectors[sectorY][sectorX];
	player->sectorIndex = (DWORD)sector.size();
	sector.push_back(player);

	player->sectorX = sectorX;
	player->sectorY = sectorY;

	Packet* resPacket = Packet::Alloc();
	mpResSectorMove(resPacket, player->accountNo, player->sectorX, player->sectorY);
	SendPacket(player->sessionId, resPacket);
	Packet::Free(resPacket);
}

void ChatServer::PacketProcMessage(Player* player, Packet* packet)
{
	INT64 accountNo;
	WORD msgLen;
	WCHAR message[512];

	// 로그인 후 채팅 가능
	// 첫 이동 패킷 이후 채팅 가능
	// 이상한 패킷
	if (player->isLogin == FALSE
		|| player->sectorX == INVALID_SECTOR
		|| packet->GetDataSize() < sizeof(accountNo) + sizeof(msgLen))
	{
		DisconnectPlayer(player);
		return;
	}

	(*packet) >> accountNo >> msgLen;

	// 패킷의 accountNo와 서버가 관리중인 accountNo가 다름
	if (accountNo != player->accountNo)
	{
		__debugbreak();
	}

	// 메시지 길이 부적절
	if (msgLen > sizeof(message) || msgLen != packet->GetDataSize())
	{
		DisconnectPlayer(player);
		return;
	}
	packet->GetData((char*)message, msgLen);

	Packet* resPacket = Packet::Alloc();
	mpResMessage(resPacket, player->accountNo, player->id, player->nickname, msgLen, message);

	int beginX = (player->sectorX == 0) ? 0 : player->sectorX - 1;
	int beginY = (player->sectorY == 0) ? 0 : player->sectorY - 1;
	int endX = (player->sectorX == SECTOR_MAX_X - 1) ? player->sectorX : player->sectorX + 1;
	int endY = (player->sectorY == SECTOR_MAX_Y - 1) ? player->sectorY : player->sectorY + 1;

	for (int y = beginY; y <= endY; y++)
	{
		for (int x = beginX; x <= endX; x++)
		{
			for (Player* target : _sectors[y][x])
			{
				SendPacket(target->sessionId, resPacket);
			}
		}
	}

	Packet::Free(resPacket);
}

void ChatServer::PacketProcHeartbeat(Player* player, Packet* packet)
{
	// PacketProc 에서 갱신 됨.
}

void ChatServer::mpResLogin(Packet* packet, BYTE status, INT64 accountNo)
{
	(*packet) << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << status << accountNo;
}

void ChatServer::mpResSectorMove(Packet* packet, INT64 accountNo, WORD sectorX, WORD sectorY)
{
	(*packet) << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE << accountNo << sectorX << sectorY;
}

void ChatServer::mpResMessage(Packet* packet, INT64 accountNo, const WCHAR* id, const WCHAR* nickname, WORD msgLen, const WCHAR* message)
{
	(*packet) << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE << accountNo;
	packet->PutData((char*)id, sizeof(WCHAR) * ID_MAX_SIZE);
	packet->PutData((char*)nickname, sizeof(WCHAR) * NICK_NAME_MAX_SIZE);
	(*packet) << msgLen;
	packet->PutData((char*)message, msgLen);
}

void ChatServer::PushJob(Job::Type type, DWORD64 sessionID, Packet* packet)
{
	Job job;
	job.type = type;
	job.sessionID = sessionID;
	job.packet = packet;
	_jobQueue.Enqueue(job);
	SetEvent(_jobEvent);
}

void ChatServer::HandleJob(Job& job)
{
	switch (job.type)
	{
	case Job::ACCEPT:
		ProcessAccept(job.sessionID);
		break;
	case Job::RELEASE:
		ProcessRelease(job.sessionID);
		break;
	case Job::RECV:
		PacketProc(job.sessionID, job.packet);
		Packet::Free(job.packet);
		break;
	}
}

Player* ChatServer::FindPlayer(DWORD64 sessionID)
{
	auto iter = _sessionMap.find(sessionID);
	if (iter == _sessionMap.end())
		return nullptr;
	return iter->second;
}

void ChatServer::DisconnectPlayer(Player* player)
{
	if (player->isDisconnecting)
		return;
	player->isDisconnecting = TRUE;
	Disconnect(player->sessionId);
}