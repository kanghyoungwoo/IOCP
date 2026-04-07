#include "IOCPClient.h"
#include "DebugLog.h"
#include <cstring>

IOCPClient::~IOCPClient()
{
	Shutdown();
}

bool IOCPClient::Init(uint32_t botCount, uint32_t workerThreadCount)
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		DEBUG_LOG("[IOCPClient] WSAStartup failed: %d\n", WSAGetLastError());
		return false;
	}

	mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, workerThreadCount);
	if (mIOCPHandle == NULL)
	{
		DEBUG_LOG("[IOCPClient] CreateIoCompletionPort failed: %d\n", GetLastError());
		return false;
	}

	if (!LoadConnectEx())
	{
		DEBUG_LOG("[IOCPClient] ConnectEx load failed\n");
		return false;
	}

	mBotCount = botCount;
	mBotSockets = std::make_unique<BotSocket[]>(botCount);

	// #3 Send Overlapped Pool 초기화 (봇당 2개씩 여유)
	mSendPool.Init(botCount * 2);

	for (uint32_t i = 0; i < botCount; ++i)
	{
		auto& bot = mBotSockets[i];
		memset(&bot.recvOverlapped, 0, sizeof(ClientOverlapped));
		memset(&bot.connectOverlapped, 0, sizeof(ClientOverlapped));
		bot.recvOverlapped.botIndex = i;
		bot.recvOverlapped.ioType = ClientIOType::RECV;
		bot.connectOverlapped.botIndex = i;
		bot.connectOverlapped.ioType = ClientIOType::CONNECT;
	}

	mIsRunning = true;
	for (uint32_t i = 0; i < workerThreadCount; ++i)
	{
		mWorkerThreads.emplace_back([this]() { WorkerThread(); });
	}

	DEBUG_LOG("[IOCPClient] Init OK (bots: %u, workers: %u)\n", botCount, workerThreadCount);
	return true;
}

void IOCPClient::Shutdown()
{
	mIsRunning = false;

	for (uint32_t i = 0; i < mBotCount; ++i)
		CloseSocket(i);

	for (size_t i = 0; i < mWorkerThreads.size(); ++i)
		PostQueuedCompletionStatus(mIOCPHandle, 0, 0, NULL);

	for (auto& th : mWorkerThreads)
	{
		if (th.joinable())
			th.join();
	}
	mWorkerThreads.clear();

	if (mIOCPHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(mIOCPHandle);
		mIOCPHandle = INVALID_HANDLE_VALUE;
	}

	WSACleanup();
}

bool IOCPClient::LoadConnectEx()
{
	SOCKET tmpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (tmpSocket == INVALID_SOCKET)
		return false;

	GUID guidConnectEx = WSAID_CONNECTEX;
	DWORD bytes = 0;
	int result = WSAIoctl(
		tmpSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidConnectEx, sizeof(guidConnectEx),
		&mConnectExFunc, sizeof(mConnectExFunc),
		&bytes, NULL, NULL);

	closesocket(tmpSocket);
	return (result == 0);
}

bool IOCPClient::CreateBotSocket(uint32_t botIndex)
{
	auto& bot = mBotSockets[botIndex];

	if (bot.socket != INVALID_SOCKET)
	{
		closesocket(bot.socket);
		bot.socket = INVALID_SOCKET;
	}

	// #7 closing 플래그 리셋
	bot.closing = false;
	bot.connected = false;

	bot.socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (bot.socket == INVALID_SOCKET)
	{
		DEBUG_LOG("[IOCPClient] Bot[%u] socket create failed: %d\n", botIndex, WSAGetLastError());
		return false;
	}

	SOCKADDR_IN localAddr = {};
	localAddr.sin_family = AF_INET;
	localAddr.sin_addr.s_addr = INADDR_ANY;
	localAddr.sin_port = 0;
	if (bind(bot.socket, (SOCKADDR*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR)
	{
		DEBUG_LOG("[IOCPClient] Bot[%u] bind failed: %d\n", botIndex, WSAGetLastError());
		closesocket(bot.socket);
		bot.socket = INVALID_SOCKET;
		return false;
	}

	if (CreateIoCompletionPort((HANDLE)bot.socket, mIOCPHandle, (ULONG_PTR)botIndex, 0) == NULL)
	{
		DEBUG_LOG("[IOCPClient] Bot[%u] IOCP bind failed: %d\n", botIndex, GetLastError());
		closesocket(bot.socket);
		bot.socket = INVALID_SOCKET;
		return false;
	}

	return true;
}

bool IOCPClient::AsyncConnect(uint32_t botIndex, const char* serverIP, uint16_t port)
{
	if (!CreateBotSocket(botIndex))
		return false;

	auto& bot = mBotSockets[botIndex];

	SOCKADDR_IN serverAddr = {};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	inet_pton(AF_INET, serverIP, &serverAddr.sin_addr);

	memset(&bot.connectOverlapped.overlapped, 0, sizeof(WSAOVERLAPPED));
	bot.connectOverlapped.ioType = ClientIOType::CONNECT;
	bot.connectOverlapped.botIndex = botIndex;

	BOOL result = mConnectExFunc(
		bot.socket,
		(SOCKADDR*)&serverAddr, sizeof(serverAddr),
		NULL, 0, NULL,
		&bot.connectOverlapped.overlapped);

	if (!result && WSAGetLastError() != WSA_IO_PENDING)
	{
		DEBUG_LOG("[IOCPClient] Bot[%u] ConnectEx failed: %d\n", botIndex, WSAGetLastError());
		closesocket(bot.socket);
		bot.socket = INVALID_SOCKET;
		return false;
	}

	return true;
}

bool IOCPClient::PostRecv(uint32_t botIndex)
{
	auto& bot = mBotSockets[botIndex];
	if (bot.socket == INVALID_SOCKET || bot.closing)
		return false;

	memset(&bot.recvOverlapped.overlapped, 0, sizeof(WSAOVERLAPPED));
	bot.recvOverlapped.ioType = ClientIOType::RECV;
	bot.recvOverlapped.botIndex = botIndex;
	bot.recvOverlapped.wsaBuf.buf = bot.recvBuffer;
	bot.recvOverlapped.wsaBuf.len = sizeof(bot.recvBuffer);

	DWORD flags = 0;
	int result = WSARecv(
		bot.socket,
		&bot.recvOverlapped.wsaBuf, 1,
		NULL, &flags,
		&bot.recvOverlapped.overlapped,
		NULL);

	if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
	{
		return false;
	}

	return true;
}

bool IOCPClient::AsyncSend(uint32_t botIndex, const char* data, uint32_t size)
{
	auto& bot = mBotSockets[botIndex];

	// #7 closing 중이면 전송하지 않음
	if (bot.socket == INVALID_SOCKET || !bot.connected || bot.closing)
		return false;

	// #3 Pool에서 할당
	auto* sendOvl = mSendPool.Alloc();
	memset(&sendOvl->overlapped, 0, sizeof(WSAOVERLAPPED));
	sendOvl->ioType = ClientIOType::SEND;
	sendOvl->botIndex = botIndex;
	memcpy(sendOvl->buffer, data, size);
	sendOvl->wsaBuf.buf = sendOvl->buffer;
	sendOvl->wsaBuf.len = size;

	DWORD bytesSent = 0;
	int result = WSASend(
		bot.socket,
		&sendOvl->wsaBuf, 1,
		&bytesSent, 0,
		&sendOvl->overlapped,
		NULL);

	if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
	{
		DEBUG_LOG("[IOCPClient] Bot[%u] WSASend failed: %d\n", botIndex, WSAGetLastError());
		mSendPool.Free(sendOvl);  // Pool에 반납
		return false;
	}

	return true;
}

void IOCPClient::CloseSocket(uint32_t botIndex)
{
	if (botIndex >= mBotCount)
		return;

	auto& bot = mBotSockets[botIndex];

	// #7 이미 closing 중이면 중복 호출 방지
	bool expected = false;
	if (!bot.closing.compare_exchange_strong(expected, true))
		return;

	bot.connected = false;

	if (bot.socket != INVALID_SOCKET)
	{
		closesocket(bot.socket);
		bot.socket = INVALID_SOCKET;
	}
}

void IOCPClient::WorkerThread()
{
	OVERLAPPED_ENTRY entries[64];
	ULONG numEntries = 0;

	while (mIsRunning)
	{
		BOOL success = GetQueuedCompletionStatusEx(
			mIOCPHandle,
			entries,
			64,
			&numEntries,
			100,
			FALSE);

		if (!success)
		{
			DWORD err = GetLastError();
			if (err == WAIT_TIMEOUT)
				continue;
			if (err == ERROR_ABANDONED_WAIT_0)
				break;
			continue;
		}

		for (ULONG i = 0; i < numEntries; ++i)
		{
			auto& entry = entries[i];
			DWORD bytesTransferred = entry.dwNumberOfBytesTransferred;
			LPOVERLAPPED lpOvl = entry.lpOverlapped;

			if (bytesTransferred == 0 && lpOvl == NULL)
			{
				return;
			}

			if (lpOvl == NULL)
				continue;

			auto* pOvl = reinterpret_cast<ClientOverlapped*>(lpOvl);
			uint32_t botIndex = pOvl->botIndex;

			switch (pOvl->ioType)
			{
			case ClientIOType::CONNECT:
			{
				auto& bot = mBotSockets[botIndex];

				// ConnectEx 결과 검증
				DWORD flags = 0;
				DWORD transferred = 0;
				BOOL connectResult = WSAGetOverlappedResult(
					bot.socket, &bot.connectOverlapped.overlapped,
					&transferred, FALSE, &flags);

				if (!connectResult)
				{
					DEBUG_LOG("[IOCPClient] Bot[%u] ConnectEx result failed: %d\n", botIndex, WSAGetLastError());
					CloseSocket(botIndex);

					if (mOnConnectComplete)
						mOnConnectComplete(botIndex, false);
					break;
				}

				setsockopt(bot.socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
				bot.connected = true;

				if (mOnConnectComplete)
					mOnConnectComplete(botIndex, true);

				// PostRecv 실패 시 좀비 봇 방지
				if (!PostRecv(botIndex))
				{
					CloseSocket(botIndex);
					if (mOnDisconnect)
						mOnDisconnect(botIndex);
				}
				break;
			}

			case ClientIOType::RECV:
			{
				auto& bot = mBotSockets[botIndex];

				// I/O 실패 상태 검증 (Internal == 0이면 성공)
				if (lpOvl->Internal != 0)
				{
					if (!bot.closing)
					{
						CloseSocket(botIndex);
						if (mOnDisconnect)
							mOnDisconnect(botIndex);
					}
					break;
				}

				if (bytesTransferred == 0)
				{
					// 서버 측 연결 종료 (FIN)
					CloseSocket(botIndex);
					if (mOnDisconnect)
						mOnDisconnect(botIndex);
					break;
				}

				if (mOnRecvComplete)
					mOnRecvComplete(botIndex, bytesTransferred, bot.recvBuffer);

				// PostRecv 실패 시 좀비 봇 방지
				if (!PostRecv(botIndex))
				{
					CloseSocket(botIndex);
					if (mOnDisconnect)
						mOnDisconnect(botIndex);
				}
				break;
			}

			case ClientIOType::SEND:
			{
				// #3 Pool에 반납
				mSendPool.Free(pOvl);
				break;
			}
			}
		}
	}
}
