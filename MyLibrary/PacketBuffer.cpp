#include "PacketBuffer.h"

Packet::Packet()
{
	m_iBufferSize = eBUFFER_DEFAULT;
	m_chpBuffer = new char[m_iBufferSize];
	Clear();
}

Packet::Packet(int iBufferSize)
{
	if (iBufferSize <= 0)
		iBufferSize = eBUFFER_DEFAULT;

	m_iBufferSize = iBufferSize;
	m_chpBuffer = new char[m_iBufferSize];
	Clear();
}

Packet::~Packet()
{
	delete[] m_chpBuffer;
}

void Packet::Clear(void)
{
	m_iDataSize = 0;
	m_iReadPos = 0;
	m_iWritePos = 0;
}

int Packet::MoveWritePos(int iSize)
{
	if (iSize <= 0)
		return 0;

	int freeSize = m_iBufferSize - m_iDataSize;
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
		m_iWritePos = 0;
		m_iReadPos = 0;
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
		m_iReadPos = 0;
		m_iWritePos = 0;
	}

	return iSize;
}

int Packet::PutData(char* chpSrc, int iSrcSize)
{
	if (iSrcSize <= 0)
		return 0;

	int freeSize = m_iBufferSize - m_iDataSize;
	if (freeSize < iSrcSize)
		iSrcSize = freeSize;

	memcpy(m_chpBuffer + m_iWritePos, chpSrc, iSrcSize);

	m_iDataSize += iSrcSize;
	m_iWritePos += iSrcSize;

	return iSrcSize;
}