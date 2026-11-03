#pragma once
#include "CNetServer.h"
#include "Player.h"
#include "CommonProtocol.h"
#include <unordered_map>
#include <list>
#include "LockFreeQueue.h"

#define SECTOR_MAX_X		50
#define SECTOR_MAX_Y		50

class ChatServer : public CNetServer
{
public:
	bool Start(const CHAR* ip, SHORT port, DWORD workerCount, DWORD activeCount, BOOL nagle, DWORD maxSessionCount);
	void Stop();

private:
	struct Job
	{
		enum Type : WORD
		{
			ACCEPT,
			RELEASE,
			RECV,
		};

		Type type;
		DWORD64 sessionID;
		Packet* packet;
	};

	static unsigned int WINAPI ContentsThread(LPVOID arg);
	void ContentsThreadMain();

	// 가상함수 오버라이딩
	virtual bool OnConnectionRequest(char* ip, SHORT port) override;
	virtual void OnAccept(DWORD64 sessionID) override;
	virtual void OnRelease(DWORD64 sessionID) override;
	virtual void OnRecv(DWORD64 sessionID, Packet* packet) override;
	virtual void OnError(int errorCode, std::wstring errorMsg) override;

	void ProcessAccept(DWORD64 sessionID);
	void ProcessRelease(DWORD64 sessionID);
	void CheckTimeout();

	// 패킷 처리
	void PacketProc(DWORD64 sessionID, Packet* packet);

	void PacketProcLogin(Player* player, Packet* packet);
	void PacketProcSectorMove(Player* player, Packet* packet);
	void PacketProcMessage(Player* player, Packet* packet);
	void PacketProcHeartbeat(Player* player, Packet* packet);

	// 응답 패킷
	void mpResLogin(Packet* packet, BYTE status, INT64 accountNo);
	void mpResSectorMove(Packet* packet, INT64 accountNo, WORD sectorX, WORD sectorY);
	void mpResMessage(Packet* packet, INT64 accountNo, const WCHAR* id, const WCHAR* nickname, WORD msgLen, const WCHAR* message);

	void PushJob(Job::Type type, DWORD64 sessionID, Packet* packet);
	void HandleJob(Job& job);

	Player* FindPlayer(DWORD64 sessionID)
	{
		auto iter = _sessionMap.find(sessionID);
		if (iter == _sessionMap.end())
			return nullptr;
		return iter->second;
	}

	void DisconnectPlayer(Player* player)
	{
		if (player->isDisconnecting)
			return;
		player->isDisconnecting = TRUE;
		Disconnect(player->sessionId);
	}

private:
	std::unordered_map<DWORD64, Player*> _sessionMap;
	std::unordered_map<INT64, Player*> _accountMap;
	std::list<Player*> _sectors[SECTOR_MAX_Y][SECTOR_MAX_X];

	LockFreeQueue<Job> _jobQueue;
	HANDLE _jobEvent = NULL;
	HANDLE _contentsThread = NULL;
	bool _isRunning = false;
};

