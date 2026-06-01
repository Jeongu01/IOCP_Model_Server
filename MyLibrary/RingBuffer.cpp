#include <Windows.h>
#include <memory>
#include "RingBuffer.h"
#define BUFSIZE	10000

RingBuffer::RingBuffer(void)
{
	m_capacity = BUFSIZE + 1;
	m_pBuffer = new char[m_capacity];
	InitializeCriticalSection(&m_cs);
}

RingBuffer::RingBuffer(int iBufferSize)
{
	m_capacity = iBufferSize + 1;
	m_pBuffer = new char[m_capacity];
	InitializeCriticalSection(&m_cs);
}

RingBuffer::~RingBuffer()
{
	delete[] m_pBuffer;
	DeleteCriticalSection(&m_cs);
}

int RingBuffer::GetBufferSize(void) const
{
	return m_capacity - 1;
}

int RingBuffer::GetUseSize(void) const
{
	int f = m_front;
	int r = m_rear;
	if (r >= f)
		return r - f;

	return m_capacity - (f - r);
}

int RingBuffer::GetFreeSize(void)
{
	return (m_capacity - 1) - GetUseSize();
}

int RingBuffer::Enqueue(const char* chpData, int iSize)
{
	if (chpData == nullptr || iSize <= 0)
		return 0;

	int freeSize = GetFreeSize();
	if (freeSize <= 0)
		return 0;

	if (freeSize < iSize)
		iSize = freeSize;

	int firstWriteSize = m_capacity - m_rear;
	if (firstWriteSize > iSize)
		firstWriteSize = iSize;

	memcpy(m_pBuffer + m_rear, chpData, firstWriteSize);

	int remain = iSize - firstWriteSize;
	if (remain > 0)
	{
		memcpy(m_pBuffer, chpData + firstWriteSize, remain);
	}

	m_rear = (m_rear + iSize) % m_capacity;

	return iSize;
}

int RingBuffer::Dequeue(char* chpDest, int iSize)
{
	if (chpDest == nullptr || iSize <= 0)
		return 0;

	if (GetUseSize() < iSize)
		return 0;

	int firstReadSize = m_capacity - m_front;
	if (firstReadSize > iSize)
		firstReadSize = iSize;

	memcpy(chpDest, m_pBuffer + m_front, firstReadSize);

	int remain = iSize - firstReadSize;
	if (remain > 0)
	{
		memcpy(chpDest + firstReadSize, m_pBuffer, remain);
	}

	m_front = (m_front + iSize) % m_capacity;

	return iSize;
}

int RingBuffer::Peek(char* chpDest, int iSize) const
{

	if (chpDest == nullptr || iSize <= 0)
		return 0;

	if (GetUseSize() < iSize)
		return 0;

	int firstReadSize = m_capacity - m_front;
	if (firstReadSize > iSize)
		firstReadSize = iSize;

	memcpy(chpDest, m_pBuffer + m_front, firstReadSize);

	int remain = iSize - firstReadSize;
	if (remain > 0)
	{
		memcpy(chpDest + firstReadSize, m_pBuffer, remain);
	}

	return iSize;
}

void RingBuffer::ClearBuffer(void)
{
	m_front = 0;
	m_rear = 0;
}

int RingBuffer::DirectEnqueueSize(void)
{
	int freeSize = GetFreeSize();
	if (freeSize <= 0)
		return 0;

	if (m_front <= m_rear)
		return min(m_capacity - m_rear, freeSize);
	return m_front - m_rear - 1;
}

int RingBuffer::DirectDequeueSize(void)
{
	int useSize = GetUseSize();
	if (useSize <= 0)
		return 0;

	if (m_front < m_rear)
		return useSize;
	return m_capacity - m_front;
}

int RingBuffer::MoveFront(int iSize)
{
	int useSize = GetUseSize();
	if (useSize < iSize)
		iSize = useSize;

	m_front = (m_front + iSize) % m_capacity;

	return iSize;
}

int RingBuffer::MoveRear(int iSize)
{
	int freeSize = GetFreeSize();
	if (freeSize < iSize)
		iSize = freeSize;

	m_rear = (m_rear + iSize) % m_capacity;

	return iSize;
}

char* RingBuffer::GetFrontBufferPtr(void)
{
	return m_pBuffer + m_front;
}

char* RingBuffer::GetRearBufferPtr(void)
{
	return m_pBuffer + m_rear;
}

char* RingBuffer::GetBufferPtr(void)
{
	return m_pBuffer;
}
