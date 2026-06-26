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
	void* nodeAddr;
	void* nextAddr;
};

static Log logArr[10000];
static long logIdx = 0;
static void write(Action type, void* ptr1, void* ptr2, int data = 0)
{
	unsigned int idx = InterlockedIncrement(&logIdx) % 10000;
	Log log;
	log.type = type;
	log.threadID = GetCurrentThreadId();
	log.nodeAddr = ptr1;
	log.nextAddr = ptr2;
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

public:
	LockFreeStack() : _Top(nullptr) {}
	~LockFreeStack() 
	{
		while (_Top)
		{
			Node* next = _Top->_Next;
			delete _Top;
			_Top = next;
		}
	}

	void Push(T data)
	{
		Node* newNode = new Node();
		newNode->_Data = data;
		Node* currentTop = nullptr;
		do
		{
			currentTop = _Top;
			newNode->_Next = currentTop;
			write(PUSH_TRY, newNode, newNode->_Next);
		} while (InterlockedCompareExchangePointer((volatile PVOID*)&_Top, newNode, currentTop) != currentTop);
		write(PUSH_SUC, newNode, newNode->_Next);
	}

	bool Pop(T* ret)
	{
		Node* popNode = nullptr;
		Node* newTop = nullptr;

		write(POP_STR, nullptr, 0);
		do
		{
			popNode = _Top;
			if (popNode == nullptr)
				return false;
			newTop = popNode->_Next;
			write(POP_TRY, popNode, newTop);
		} while (InterlockedCompareExchangePointer((volatile PVOID*)&_Top, newTop, popNode) != popNode);
		write(POP_SUC, popNode, newTop);
		(*ret) = popNode->_Data;
		delete popNode;
		return true;
	}

private:
	Node* _Top;
};