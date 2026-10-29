#include "PacketBuffer.h"
#include "LockFreeMemoryPoolTLS.h"

namespace
{
	jeongu::LockFreeMemoryPoolTLS<Packet> s_packetPool(false, 0, 0);
}

Packet::Packet()
{
	m_iBufferSize = eBUFFER_DEFAULT + eHEADER_RESERVE;
	m_chpBuffer = new char[m_iBufferSize];
	m_refCount = 0;
	Clear();
}

Packet::Packet(int iBufferSize)
{
	if (iBufferSize <= 0)
		iBufferSize = eBUFFER_DEFAULT;

	m_iBufferSize = iBufferSize + eHEADER_RESERVE;
	m_chpBuffer = new char[m_iBufferSize];
	m_refCount = 0;
	Clear();
}

Packet::~Packet()
{
	delete[] m_chpBuffer;
}

void Packet::Clear(void)
{
	m_iDataSize = 0;
	m_iReadPos = eHEADER_RESERVE;
	m_iWritePos = eHEADER_RESERVE;
	m_iHeaderSize = 0;
	m_bEncoded = false;
}

int Packet::MoveWritePos(int iSize)
{
	if (iSize <= 0)
		return 0;

	int freeSize = m_iBufferSize - m_iWritePos;
	if (freeSize < iSize)
		iSize = freeSize;

	m_iDataSize += iSize;
	m_iWritePos += iSize;

	return iSize;
}

int Packet::MoveReadPos(int iSize)
{
	if (iSize <= 0)
		return 0;

	if (m_iDataSize < iSize)
		iSize = m_iDataSize;

	m_iDataSize -= iSize;
	m_iReadPos += iSize;

	if (m_iDataSize == 0)
	{
		m_iWritePos = eHEADER_RESERVE;
		m_iReadPos = eHEADER_RESERVE;
	}

	return iSize;
}

Packet& Packet::operator=(Packet& clSrcPacket)
{
	if (&clSrcPacket == this)
		return *this;

	if (m_iBufferSize < clSrcPacket.m_iBufferSize)
	{
		delete[] m_chpBuffer;
		m_iBufferSize = clSrcPacket.m_iBufferSize;
		m_chpBuffer = new char[m_iBufferSize];
	}

	memcpy(m_chpBuffer, clSrcPacket.m_chpBuffer, clSrcPacket.m_iBufferSize);

	m_iDataSize = clSrcPacket.m_iDataSize;
	m_iReadPos = clSrcPacket.m_iReadPos;
	m_iWritePos = clSrcPacket.m_iWritePos;
	m_iHeaderSize = clSrcPacket.m_iHeaderSize;
	m_bEncoded = clSrcPacket.m_bEncoded;

	return *this;
}

int Packet::GetData(char* chpDest, int iSize)
{
	if (iSize <= 0)
		return 0;

	if (m_iDataSize < iSize)
		iSize = m_iDataSize;

	memcpy(chpDest, m_chpBuffer + m_iReadPos, iSize);

	m_iDataSize -= iSize;
	m_iReadPos += iSize;

	if (m_iDataSize == 0)
	{
		m_iReadPos = eHEADER_RESERVE;
		m_iWritePos = eHEADER_RESERVE;
	}

	return iSize;
}

int Packet::PutData(char* chpSrc, int iSrcSize)
{
	if (iSrcSize <= 0)
		return 0;

	int freeSize = m_iBufferSize - m_iWritePos;
	if (freeSize < iSrcSize)
		iSrcSize = freeSize;

	memcpy(m_chpBuffer + m_iWritePos, chpSrc, iSrcSize);

	m_iDataSize += iSrcSize;
	m_iWritePos += iSrcSize;

	return iSrcSize;
}

void Packet::SetLanHeader(void)
{
	LanServerHeader header;
	header._len = (unsigned short)m_iDataSize;

	memcpy(m_chpBuffer + eHEADER_RESERVE - sizeof(LanServerHeader), &header, sizeof(LanServerHeader));
	m_iHeaderSize = sizeof(LanServerHeader);
}

void Packet::SetNetHeader(void)
{
	if (m_bEncoded)
		return;
	m_bEncoded = true;

	NetServerHeader header;
	header._byteCode = 0;		// TODO
	header._len = (unsigned short)m_iDataSize;
	header._randomKey = 0;		// TODO
	header._checkSum = 0;		// TODO

	memcpy(m_chpBuffer + eHEADER_RESERVE - sizeof(NetServerHeader), &header, sizeof(NetServerHeader));
	m_iHeaderSize = sizeof(NetServerHeader);

	Encode();
}

void Packet::Encode(void)
{
	// TODO
}

void Packet::Decode(void)
{
	// TODO
}

Packet* Packet::Alloc(void)
{
	Packet* packet = s_packetPool.Alloc();
	packet->Clear();
	packet->m_refCount = 1;
	return packet;
}

void Packet::Free(Packet* packet)
{
	LONG ref = InterlockedDecrement(&packet->m_refCount);
	if (ref > 0)
		return;

	if (ref < 0)
		__debugbreak();

	s_packetPool.Free(packet);
}

void Packet::AddRef(void)
{
	InterlockedIncrement(&m_refCount);
}

LONG64 Packet::GetUseCount(void)
{
#if LFMP_STATS
	return s_packetPool.GetUseCount();
#else
	return -1;
#endif
}

DWORD Packet::GetCapacityCount(void)
{
	return s_packetPool.GetCapacityCount();
}