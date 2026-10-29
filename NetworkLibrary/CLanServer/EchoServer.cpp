#include "EchoServer.h"
#include <stdio.h>

bool EchoServer::OnConnectionRequest(char* ip, SHORT port)
{
	return true;
}

void EchoServer::OnAccept(DWORD64 sessionID)
{
	printf("[Accept] SessionID: %lld Connected\n", sessionID);
}

void EchoServer::OnRelease(DWORD64 sessionID)
{
	printf("[Release] SessionID: %lld Disconnected\n", sessionID);
}

void EchoServer::OnRecv(DWORD64 sessionID, Packet* packet)
{
	INT64 data;
	(*packet) >> data;

	printf("[SessionID:%lld] %lld\n", sessionID, data);

	Packet *sendPacket = Packet::Alloc();
	(*sendPacket) << data;
	SendPacket(sessionID, sendPacket);
	Packet::Free(sendPacket);
}

void EchoServer::OnError(int errorCode, std::wstring errorMsg)
{
}
