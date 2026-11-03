#include "PacketBuffer.h"
#include "LockFreeMemoryPoolTLS.h"
#include <cstddef>
#include <cstdlib>

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

	m_iWritePos += iSize;

	return iSize;
}

int Packet::MoveReadPos(int iSize)
{
	if (iSize <= 0)
		return 0;

	int dataSize = m_iWritePos - m_iReadPos;
	if (dataSize < iSize)
		iSize = dataSize;

	m_iReadPos += iSize;

	if (m_iWritePos == m_iReadPos)
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

	int dataSize = m_iWritePos - m_iReadPos;
	if (dataSize < iSize)
		iSize = dataSize;

	memcpy(chpDest, m_chpBuffer + m_iReadPos, iSize);

	m_iReadPos += iSize;

	if (m_iWritePos == m_iReadPos)
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

	m_iWritePos += iSrcSize;

	return iSrcSize;
}

void Packet::SetLanHeader(void)
{
	LanServerHeader header;
	header._len = (unsigned short)(m_iWritePos - eHEADER_RESERVE);

	memcpy(m_chpBuffer + eHEADER_RESERVE - sizeof(LanServerHeader), &header, sizeof(LanServerHeader));
	m_iHeaderSize = sizeof(LanServerHeader);
}

void Packet::SetNetHeader(void)
{
	if (m_bEncoded == true)
		return;

	NetServerHeader header;
	header._byteCode = PACKET_CODE;
	header._len = (unsigned short)(m_iWritePos - eHEADER_RESERVE);
	header._randomKey = (unsigned char)(rand());
	header._checkSum = 0;

	for (int i = eHEADER_RESERVE; i < m_iWritePos; i++)
	{
		header._checkSum += (unsigned char)m_chpBuffer[i];
	}

	memcpy(m_chpBuffer + eHEADER_RESERVE - sizeof(NetServerHeader), &header, sizeof(NetServerHeader));
	m_iHeaderSize = sizeof(NetServerHeader);

	Encode();

	m_bEncoded = true;
}

void Packet::Encode(void)
{
	NetServerHeader* header = (NetServerHeader*)m_chpBuffer;

	unsigned char* data = (unsigned char*)m_chpBuffer + offsetof(NetServerHeader, _checkSum);
	int size = header->_len + 1;

	unsigned char p = 0;
	unsigned char e = 0;
	for (int i = 0; i < size; i++)
	{
		unsigned char n = (unsigned char)(i + 1);
		p = data[i] ^ (unsigned char)(p + header->_randomKey + n);
		e = p ^ (unsigned char)(e + FIXED_KEY + n);
		data[i] = e;
	}
}

bool Packet::Decode(void)
{
	NetServerHeader* header = (NetServerHeader*)m_chpBuffer;

	unsigned char* data = (unsigned char*)m_chpBuffer + offsetof(NetServerHeader, _checkSum);
	int size = header->_len + 1;

	unsigned char prevP = 0;
	unsigned char prevE = 0;
	for (int i = 0; i < size; i++)
	{
		unsigned char n = (unsigned char)(i + 1);
		unsigned char e = data[i];
		unsigned char p = e ^ (unsigned char)(prevE + FIXED_KEY + n);
		data[i] = p ^ (unsigned char)(prevP + header->_randomKey + n);
		prevP = p;
		prevE = e;
	}

	unsigned char checkSum = 0;
	for (int i = 1; i < size; i++)
	{
		checkSum += data[i];
	}
	return checkSum == data[0];
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