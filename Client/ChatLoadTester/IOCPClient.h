#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

// I/O 작업 타입
enum class ClientIOType : uint8_t
{
	CONNECT,
	RECV,
	SEND
};

// 클라이언트 전용 Overlapped 구조체
struct ClientOverlapped
{
	WSAOVERLAPPED	overlapped;
	WSABUF			wsaBuf;
	ClientIOType	ioType;
	uint32_t		botIndex;
	char			buffer[1024];
};

// IOCP 완료 이벤트 콜백
using OnConnectCompleteFunc = std::function<void(uint32_t botIndex, bool success)>;
using OnRecvCompleteFunc = std::function<void(uint32_t botIndex, uint32_t size, char* data)>;
using OnDisconnectFunc = std::function<void(uint32_t botIndex)>;

// Send Overlapped Object Pool (lock-based, simple)
class SendOverlappedPool
{
public:
	void Init(uint32_t count)
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPool.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
			mPool.push_back(new ClientOverlapped());
	}

	~SendOverlappedPool()
	{
		for (auto* p : mPool)
			delete p;
		mPool.clear();
	}

	ClientOverlapped* Alloc()
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mPool.empty())
		{
			// 풀 고갈 시 동적 할당 (fallback)
			return new ClientOverlapped();
		}
		auto* p = mPool.back();
		mPool.pop_back();
		return p;
	}

	void Free(ClientOverlapped* p)
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPool.push_back(p);
	}

private:
	std::vector<ClientOverlapped*> mPool;
	std::mutex mMutex;
};

class IOCPClient
{
public:
	IOCPClient() = default;
	~IOCPClient();

	bool Init(uint32_t botCount, uint32_t workerThreadCount);
	void Shutdown();

	bool AsyncConnect(uint32_t botIndex, const char* serverIP, uint16_t port);
	bool AsyncSend(uint32_t botIndex, const char* data, uint32_t size);
	void CloseSocket(uint32_t botIndex);

	void SetOnConnectComplete(OnConnectCompleteFunc func) { mOnConnectComplete = func; }
	void SetOnRecvComplete(OnRecvCompleteFunc func) { mOnRecvComplete = func; }
	void SetOnDisconnect(OnDisconnectFunc func) { mOnDisconnect = func; }

private:
	void WorkerThread();
	bool PostRecv(uint32_t botIndex);
	bool CreateBotSocket(uint32_t botIndex);
	bool LoadConnectEx();

	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;
	LPFN_CONNECTEX mConnectExFunc = nullptr;

	struct BotSocket
	{
		SOCKET socket = INVALID_SOCKET;
		ClientOverlapped recvOverlapped;
		ClientOverlapped connectOverlapped;
		char recvBuffer[1024];
		std::atomic<bool> connected{ false };
		std::atomic<bool> closing{ false };   // #7 동시성 보호
	};

	std::unique_ptr<BotSocket[]> mBotSockets;
	std::vector<std::thread> mWorkerThreads;
	std::atomic<bool> mIsRunning{ false };
	uint32_t mBotCount = 0;

	SendOverlappedPool mSendPool;  // #3 Object Pool

	OnConnectCompleteFunc mOnConnectComplete;
	OnRecvCompleteFunc mOnRecvComplete;
	OnDisconnectFunc mOnDisconnect;
};
