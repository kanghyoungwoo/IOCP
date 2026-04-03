#include "BotEngine.h"
#include "HighResTimer.h"
#include "Statistics.h"
#include "Logger.h"
#include <mswsock.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

BotEngine::~BotEngine()
{
    Stop();
    for (auto* bot : mBots) delete bot;
}

bool BotEngine::Init(const ChaosConfig& config)
{
    mConfig = config;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    mIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (mIOCP == NULL) return false;

    SOCKET dummy = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    GUID guid = WSAID_CONNECTEX;
    DWORD bytes;
    WSAIoctl(dummy, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &mConnectEx, sizeof(mConnectEx), &bytes, NULL, NULL);
    closesocket(dummy);

    if (!mProxy.Init(config, this)) return false;

    mBots.resize(mConfig.engine.totalBots);
    for (uint32_t i = 0; i < mConfig.engine.totalBots; ++i)
    {
        mBots[i] = new BotClient();
        mBots[i]->Init(i, this);
    }

    return true;
}

void BotEngine::Run()
{
    mIsRunning = true;
    GetStats().Reset();

    mProxy.Start();

    for (uint32_t i = 0; i < mConfig.engine.ioWorkerThreads; ++i)
    {
        mWorkerThreads.emplace_back(&BotEngine::WorkerThread, this);
    }

    LOG_DEBUG("[Engine] Running with %u bots and %u threads.", mConfig.engine.totalBots, mConfig.engine.ioWorkerThreads);

    mUpdateThread = std::thread([this]() {
        while (mIsRunning) {
            int64_t now = HiResTimer::NowMilliseconds();
            for (auto* bot : mBots) {
                bot->Tick(now);
                if (bot->GetState() == BotState::IDLE) {
                    ConnectBot(bot->GetIndex());
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

void BotEngine::Stop()
{
    if (!mIsRunning) return;
    mIsRunning = false;

    mProxy.Stop();

    if (mUpdateThread.joinable()) mUpdateThread.join();

    for (auto* bot : mBots) {
        bot->OnDisconnected();  // exchange 패턴으로 안전하게 닫힘
    }



    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for (uint32_t i = 0; i < mConfig.engine.ioWorkerThreads; ++i)
    {
        PostQueuedCompletionStatus(mIOCP, 0, 0, NULL);
    }

    for (auto& t : mWorkerThreads) if (t.joinable()) t.join();

    CloseHandle(mIOCP);
    WSACleanup();
}

void BotEngine::WorkerThread()
{
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    LPOVERLAPPED overlapped;

    while (mIsRunning)
    {
        BOOL success = GetQueuedCompletionStatus(mIOCP, &bytesTransferred, &completionKey, &overlapped, INFINITE);

        if (overlapped == NULL) break;

        auto* ov = reinterpret_cast<OverlappedEx*>(overlapped);
        uint32_t botIndex = ov->botIndex;
        BotClient* bot = mBots[botIndex];

        // *** 세션 유효성 검증 - 이전 연결의 잔여 완료 무시 ***
        if (ov->sessionId != 0 && ov->sessionId != bot->GetSessionId())
        {
            // 버퍼 정리만 하고 스킵
            if (ov->wsaBuf.buf) delete[] ov->wsaBuf.buf;
            delete ov;
            continue;
        }

        if (!success)
        {
            bot->OnDisconnected();
            if (ov->ioType == IO_TYPE::SEND && ov->wsaBuf.buf) {
                delete[] reinterpret_cast<char*>(ov->wsaBuf.buf);
            } else if (ov->ioType == IO_TYPE::RECV && ov->wsaBuf.buf) {
                delete[] reinterpret_cast<char*>(ov->wsaBuf.buf);
            }
            delete ov;
            continue;
        }

        switch (ov->ioType)
        {
        case IO_TYPE::CONNECT:
            setsockopt(bot->GetSocket(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
            bot->OnConnected();
            PostRecv(botIndex);
            break;
        case IO_TYPE::RECV:
            if (bytesTransferred == 0) bot->OnDisconnected();
            else
            {
                bot->OnRecvData(ov->wsaBuf.buf, bytesTransferred);
                PostRecv(botIndex);
            }
            delete[] reinterpret_cast<char*>(ov->wsaBuf.buf);
            break;
        case IO_TYPE::SEND:
            bot->OnSendComplete(bytesTransferred);
            delete[] reinterpret_cast<char*>(ov->wsaBuf.buf);
            break;
        }

        delete ov;
    }
}

bool BotEngine::ConnectBot(uint32_t botIndex)
{
    BotClient* bot = mBots[botIndex];
    if (!bot->TryChangeState(BotState::IDLE, BotState::CONNECTING)) return false;
    bot->IncrementSessionId();

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        bot->SetState(BotState::IDLE);
        return false;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;
    bind(s, (struct sockaddr*)&addr, sizeof(addr));

    if (!RegisterToIOCP(s, botIndex)) {
        closesocket(s);
        bot->SetState(BotState::IDLE);
        return false;
    }

    bot->SetSocket(s);
    GetStats().connectAttempts++;

    struct sockaddr_in targetAddr;
    inet_pton(AF_INET, mConfig.engine.serverHost.c_str(), &targetAddr.sin_addr);
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(mConfig.engine.serverPort);

    auto* ov = new OverlappedEx();
    ov->ioType = IO_TYPE::CONNECT;
    ov->botIndex = botIndex;
    ov->sessionId = bot->GetSessionId();

    if (!mConnectEx(s, (struct sockaddr*)&targetAddr, sizeof(targetAddr), NULL, 0, NULL, (LPOVERLAPPED)ov))
    {
        if (WSAGetLastError() != ERROR_IO_PENDING) {
            closesocket(s);
            bot->SetSocket(INVALID_SOCKET);
            bot->SetState(BotState::IDLE);
            delete ov;
            return false;
        }
    }

    return true;
}

bool BotEngine::RegisterToIOCP(SOCKET s, uint32_t botIndex)
{
    return CreateIoCompletionPort((HANDLE)s, mIOCP, botIndex, 0) != NULL;
}

bool BotEngine::PostRecv(uint32_t botIndex)
{
    BotClient* bot = mBots[botIndex];
    auto* ov = new OverlappedEx();
    ov->ioType = IO_TYPE::RECV;
    ov->botIndex = botIndex;
    ov->sessionId = bot->GetSessionId();
    
    char* recvBuf = new char[8192];
    ov->wsaBuf.buf = recvBuf;
    ov->wsaBuf.len = 8192;

    DWORD flags = 0;
    if (WSARecv(bot->GetSocket(), &ov->wsaBuf, 1, NULL, &flags, (LPOVERLAPPED)ov, NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != ERROR_IO_PENDING) {
            delete[] recvBuf;
            delete ov;
            return false;
        }
    }
    return true;
}

bool BotEngine::SendPacket(uint32_t botIndex, const char* data, uint32_t size)
{
    return mProxy.ProcessSend(botIndex, data, size);
}

bool BotEngine::PostSend(uint32_t botIndex, const char* data, uint32_t size)
{
    BotClient* bot = mBots[botIndex];
    if (bot->GetSocket() == INVALID_SOCKET) return false;

    auto* ov = new OverlappedEx();
    ov->ioType = IO_TYPE::SEND;
    ov->botIndex = botIndex;
    ov->sessionId = bot->GetSessionId();
    
    char* sendBuf = new char[size];
    memcpy(sendBuf, data, size);
    ov->wsaBuf.buf = sendBuf;
    ov->wsaBuf.len = size;

    if (WSASend(bot->GetSocket(), &ov->wsaBuf, 1, NULL, 0, (LPOVERLAPPED)ov, NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != ERROR_IO_PENDING)
        {
            delete[] sendBuf;
            delete ov;
            return false;
        }
    }
    return true;
}

void BotEngine::DisconnectBot(uint32_t botIndex, bool hardClose)
{
    if (hardClose) mBots[botIndex]->HardClose();
    else mBots[botIndex]->OnDisconnected();
}

