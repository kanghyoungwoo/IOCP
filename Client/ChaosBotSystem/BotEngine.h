#pragma once
// ================================================================
// BotEngine.h - IOCP 기반 네트워크 I/O 처리
// ================================================================

#include "Protocol.h"
#include "ChaosConfig.h"
#include "BotClient.h"
#include "ChaosProxy.h"
#include <mswsock.h>
#include <vector>
#include <thread>
#include <mutex>
#include <map>

// IOCP Overlapped 구조체 확장
enum class IO_TYPE
{
    CONNECT,
    RECV,
    SEND,
    DISCONNECT
};

struct OverlappedEx
{
    WSAOVERLAPPED overlapped;
    IO_TYPE       ioType;
    WSABUF        wsaBuf;
    uint32_t      botIndex;
    uint32_t      sessionId;

    OverlappedEx() {
        memset(this, 0, sizeof(OverlappedEx));
    }
};

class BotEngine
{
public:
    BotEngine() = default;
    ~BotEngine();

    bool Init(const ChaosConfig& config);
    void Run();
    void Stop();

    // 봇 전용 API
    bool ConnectBot(uint32_t botIndex);
    bool SendPacket(uint32_t botIndex, const char* data, uint32_t size);
    
    // 프록시에서 실제 전송을 위해 호출
    bool PostSend(uint32_t botIndex, const char* data, uint32_t size);
    
    void DisconnectBot(uint32_t botIndex, bool hardClose = false);

    const ChaosConfig& GetConfig() const { return mConfig; }
    BotClient* GetBot(uint32_t index) { return mBots[index]; }

private:
    void WorkerThread();
    bool RegisterToIOCP(SOCKET s, uint32_t botIndex);
    bool PostRecv(uint32_t botIndex);

    ChaosConfig mConfig;
    HANDLE      mIOCP = INVALID_HANDLE_VALUE;
    std::atomic<bool> mIsRunning{false};

    std::vector<BotClient*> mBots;
    std::vector<std::thread> mWorkerThreads;
    std::thread mUpdateThread;

    ChaosProxy mProxy; // 카오스 프록시 분리

    LPFN_CONNECTEX mConnectEx = nullptr;
};
