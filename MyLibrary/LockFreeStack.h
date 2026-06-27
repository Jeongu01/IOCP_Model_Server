#pragma once
#include <Windows.h>

enum Action
{
	PUSH_STR,
	PUSH_TRY,
	PUSH_SUC,
	POP_STR,
	POP_TRY,
	POP_SUC,
};

struct Log
{
	Action type;
	int threadID;
	DWORD64 nodeAddr;
	DWORD64 nextAddr;
	DWORD64 data;
};

static Log logArr[10000];
static DWORD64 logIdx = 0;
static void write(Action type, DWORD64 ptr1, DWORD64 ptr2, DWORD data = 0)
{
	unsigned int idx = InterlockedIncrement(&logIdx) % 10000;
	Log log;
	log.type = type;
	log.threadID = GetCurrentThreadId();
	log.nodeAddr = ptr1;
	log.nextAddr = ptr2;
	log.data = data;
	logArr[idx] = log;
}

template<typename T>
class LockFreeStack
{
private:
	struct Node
	{
		T _Data = 0;
		Node* _Next = nullptr;
	};

	static const DWORD64 PTR_MASK = 0x00007fffffffffff;
	static const int TAG_SHIFT = 47;
	static const DWORD64 TAG_MASK = 0x1ffff;

	inline Node* GetPointer(DWORD64 taggedPtr)
	{
		return (Node*)(taggedPtr & PTR_MASK);
	}

	inline DWORD64 GetTag(DWORD64 taggedPtr)
	{
		return (taggedPtr >> TAG_SHIFT) & TAG_MASK;
	}

	inline DWORD64 MakeTagged(DWORD64 tag, Node* ptr)
	{
		return ((tag & TAG_MASK) << TAG_SHIFT) | ((DWORD64)(ptr) & PTR_MASK);
	}

public:
	LockFreeStack() : _Top(0) {}
	~LockFreeStack() 
	{
		Node* current = GetPointer(_Top);
		while (current)
		{
			Node* next = current->_Next;
			delete current;
			current = next;
		}
	}

	void Push(T data)
	{
		Node* newNode = new Node();	// 추후 메모리풀로 변경
		newNode->_Data = data;

		Node* currentTopPtr = nullptr;
		DWORD64 currentTopTagged = 0;
		DWORD64 newTopTagged = 0;
		DWORD64 nextTag = 0;

		do
		{
			currentTopTagged = _Top;
			currentTopPtr = GetPointer(currentTopTagged);
			newNode->_Next = currentTopPtr;
			
			nextTag = GetTag(currentTopTagged) + 1;
			newTopTagged = MakeTagged(nextTag, newNode);
			write(PUSH_TRY, newTopTagged, currentTopTagged);
		} while (InterlockedCompareExchange(&_Top, newTopTagged, currentTopTagged) != currentTopTagged);
		write(PUSH_SUC, newTopTagged, currentTopTagged);
	}

	bool Pop(T* ret)
	{
		DWORD64 oldTopTagged = 0;
		DWORD64 nextTopTagged = 0;
		Node* oldTopPtr = nullptr;
		Node* nextTopPtr = nullptr;
		DWORD64 nextTopTag = 0;

		write(POP_STR, 0, 0);
		do
		{
			oldTopTagged = _Top;
			oldTopPtr = GetPointer(oldTopTagged);
			if (oldTopPtr == nullptr)
				return false;

			nextTopPtr = oldTopPtr->_Next;
			nextTopTag = GetTag(oldTopTagged) + 1;
			nextTopTagged = MakeTagged(nextTopTag, nextTopPtr);
			write(POP_TRY, oldTopTagged, nextTopTagged);
		} while (InterlockedCompareExchange(&_Top, nextTopTagged, oldTopTagged) != oldTopTagged);
		write(POP_SUC, oldTopTagged, nextTopTagged, oldTopPtr->_Data);
		(*ret) = oldTopPtr->_Data;
		delete oldTopPtr;
		return true;
	}

private:
	volatile DWORD64 _Top;
};