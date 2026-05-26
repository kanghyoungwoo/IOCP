#pragma once
// ================================================================
// BotClient.h - Bot State Machine
// ================================================================

#include "Protocol.h"
#include "Statistics.h"
#include <cstdint>
#include <string>
#include <atomic>
#include <functional>

enum class BotState
{
    IDLE,
    CONNECTING,
    CONNECTED,
    LOGIN_SENT,
    LOGGED_IN,
    ROOM_ENTER_SENT,
    IN_ROOM,
    ROOM_LEAVE_SENT,
    DISCONNECTING,
    DISCONNECTED,
    DEAD
};

const char* BotStateToString(BotState state);

enum class BotBehavior
{
    NORMAL,
    GENERATION_PUMP,
    PIPELINE_BURST,
    ZOMBIE_ATTACK,
    HOLD_VICTIM,
    FLOOD_CHAT,
    RAPID_LEAVE_REENTER     // Scenario D: LeaveRoom 직후 Chat 전송으로 Fast/Slow Path 경계 스트레스
};

class BotEngine;

class BotClient
{
public:
    BotClient() = default;
    ~BotClient() = default;

    void Init(uint32_t botIndex, BotEngine* pEngine);

    void OnConnected();
    void OnDisconnected();
    void OnRecvData(const char* data, uint32_t size);
    void OnSendComplete(uint32_t bytesSent);

    void Tick(int64_t nowMs);

    uint32_t    GetIndex() const { return mBotIndex; }
    uint32_t    GetSessionId() const { return mSessionId; }
    void        IncrementSessionId() { mSessionId++; }
    SOCKET      GetSocket() const { return mSocket; }
    BotState    GetState() const { return mState; }
    BotBehavior GetBehavior() const { return mBehavior; }
    const std::string& GetUserId() const { return mUserId; }
    int32_t     GetRoomNumber() const { return mRoomNumber; }
    bool        IsActive() const { return mState != BotState::DEAD && mState != BotState::IDLE; }

    void SetSocket(SOCKET s) { mSocket = s; }
    void SetBehavior(BotBehavior b) { mBehavior = b; }
    void SetTargetRoom(int32_t room) { mTargetRoom = room; }
    void SetState(BotState s) { mState = s; }
    bool TryChangeState(BotState expected, BotState desired) { return mState.compare_exchange_strong(expected, desired); }

    bool SendRawPacket(const char* data, uint32_t size);
    bool SendLogin();
    bool SendRoomEnter(int32_t roomNumber);
    bool SendRoomLeave(int32_t roomNumber);
    bool SendChat(const char* message);
    bool SendPong();

    bool SendPipelineBurst(int32_t roomNumber);

    void HardClose();
    void SetMaxCycles(uint32_t cycles) { mMaxCycles = cycles; }
private:
    void ProcessPacket(uint16_t packetId, const char* data, uint16_t dataSize);
    void HandleLoginResponse(const char* data, uint16_t size);
    void HandleRoomEnterResponse(const char* data, uint16_t size);
    void HandleRoomLeaveResponse(const char* data, uint16_t size);
    void HandleChatResponse(const char* data, uint16_t size);
    void HandleChatNotify(const char* data, uint16_t size);
    void HandlePing(const char* data, uint16_t size);

    void TickNormal(int64_t nowMs);
    void TickGenerationPump(int64_t nowMs);
    void TickPipelineBurst(int64_t nowMs);
    void TickZombieAttack(int64_t nowMs);
    void TickFloodChat(int64_t nowMs);
    void TickRapidLeaveReenter(int64_t nowMs);

    uint32_t    mBotIndex = 0;
    BotEngine*  mEngine = nullptr;
    std::atomic<SOCKET> mSocket{INVALID_SOCKET};
    std::atomic<BotState> mState{BotState::IDLE};
    BotBehavior mBehavior = BotBehavior::NORMAL;

    std::string mUserId;
    std::string mUserPw = "chaos1234";
    int32_t     mRoomNumber = -1;
    int32_t     mTargetRoom = 0;

    static constexpr uint32_t RECV_BUF_SIZE = 8192;
    char        mRecvBuffer[RECV_BUF_SIZE] = {};
    uint32_t    mRecvPos = 0;

    int64_t     mLastActionTime = 0;
    int64_t     mConnectTime = 0;
    uint32_t    mActionIntervalMs = 200;

    uint32_t    mCycleCount = 0;
    uint32_t    mMaxCycles = 0;
    std::atomic<uint32_t> mSessionId{ 0 };
};
