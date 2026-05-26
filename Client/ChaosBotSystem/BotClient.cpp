// ================================================================
// BotClient.cpp - 가상 클라이언트 상태 기계 구현
// ================================================================

#include "BotClient.h"
#include "BotEngine.h"
#include "HighResTimer.h"
#include <cstdio>
#include <cstring>

const char* BotStateToString(BotState state)
{
    switch (state)
    {
    case BotState::IDLE:            return "IDLE";
    case BotState::CONNECTING:      return "CONNECTING";
    case BotState::CONNECTED:       return "CONNECTED";
    case BotState::LOGIN_SENT:      return "LOGIN_SENT";
    case BotState::LOGGED_IN:       return "LOGGED_IN";
    case BotState::ROOM_ENTER_SENT: return "ROOM_ENTER_SENT";
    case BotState::IN_ROOM:         return "IN_ROOM";
    case BotState::ROOM_LEAVE_SENT: return "ROOM_LEAVE_SENT";
    case BotState::DISCONNECTING:   return "DISCONNECTING";
    case BotState::DISCONNECTED:    return "DISCONNECTED";
    case BotState::DEAD:            return "DEAD";
    default:                        return "UNKNOWN";
    }
}

void BotClient::Init(uint32_t botIndex, BotEngine* pEngine)
{
    mBotIndex = botIndex;
    mEngine = pEngine;
    mState = BotState::IDLE;
    mRecvPos = 0;
    mCycleCount = 0;

    // 유저 ID 생성: "test_user_XXXX" 형태
    char id[MAX_USER_ID_LENGTH + 1];
    snprintf(id, sizeof(id), "test_user_%05u", botIndex);
    mUserId = id;
}

// ----------------------------------------------------------------
// 네트워크 이벤트 핸들러
// ----------------------------------------------------------------

void BotClient::OnConnected()
{
    mState = BotState::CONNECTED;
    mRecvPos = 0;
    mConnectTime = HiResTimer::NowMilliseconds();
    
    int opt = 1;
    setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt));

    GetStats().connectSuccess++;
}

void BotClient::OnDisconnected()
{
    BotState prevState = mState;
    SOCKET s = mSocket.exchange(INVALID_SOCKET);
    if (s != INVALID_SOCKET) {
        closesocket(s);
    }
    mRecvPos = 0;
    mRoomNumber = -1;

    if (prevState != BotState::DISCONNECTING && prevState != BotState::DISCONNECTED)
    {
        GetStats().unexpectedDisconnects++;

        // Scenario D: LeaveRoom 직후 Chat을 보낸 경계 구간에서 끊겼으면 routingBoundaryDrop으로 집계
        if (mBehavior == BotBehavior::RAPID_LEAVE_REENTER &&
            prevState == BotState::ROOM_LEAVE_SENT)
        {
            GetStats().routingBoundaryDrops++;
        }
    }

    mState = BotState::DISCONNECTED;
    GetStats().disconnects++;
}

void BotClient::OnRecvData(const char* data, uint32_t size)
{
    // 수신 버퍼에 누적
    if (mRecvPos + size > RECV_BUF_SIZE)
    {
        // 버퍼 오버플로우 방지 - 리셋
        mRecvPos = 0;
        return;
    }
    memcpy(mRecvBuffer + mRecvPos, data, size);
    mRecvPos += size;
    GetStats().bytesReceived += size;

    // 패킷 조립 및 처리
    while (mRecvPos >= PACKET_HEADER_LENGTH)
    {
        auto* header = reinterpret_cast<const PACKET_HEADER*>(mRecvBuffer);
        uint16_t packetLen = header->PacketLength;

        if (packetLen < PACKET_HEADER_LENGTH || packetLen > RECV_BUF_SIZE)
        {
            // 잘못된 패킷 길이 - 버퍼 리셋
            mRecvPos = 0;
            return;
        }

        if (mRecvPos < packetLen)
        {
            break;  // 아직 패킷이 완성되지 않음
        }

        // 패킷 처리
        GetStats().packetsReceived++;
        ProcessPacket(header->PacketId, mRecvBuffer, packetLen);

        // 처리된 패킷 제거
        uint32_t remaining = mRecvPos - packetLen;
        if (remaining > 0)
        {
            memmove(mRecvBuffer, mRecvBuffer + packetLen, remaining);
        }
        mRecvPos = remaining;
    }
}

void BotClient::OnSendComplete(uint32_t bytesSent)
{
    GetStats().bytesSent += bytesSent;
}

// ----------------------------------------------------------------
// 패킷 처리
// ----------------------------------------------------------------

void BotClient::ProcessPacket(uint16_t packetId, const char* data, uint16_t dataSize)
{
    switch (static_cast<PACKET_ID>(packetId))
    {
    case PACKET_ID::LOGIN_RESPONSE:
        HandleLoginResponse(data, dataSize);
        break;
    case PACKET_ID::ROOM_ENTER_RESPONSE:
        HandleRoomEnterResponse(data, dataSize);
        break;
    case PACKET_ID::ROOM_LEAVE_RESPONSE:
        HandleRoomLeaveResponse(data, dataSize);
        break;
    case PACKET_ID::ROOM_CHAT_RESPONSE:
        HandleChatResponse(data, dataSize);
        break;
    case PACKET_ID::ROOM_CHAT_NOTIFY:
        HandleChatNotify(data, dataSize);
        break;
    case PACKET_ID::SYS_PING:
        HandlePing(data, dataSize);
        break;
    default:
        break;
    }
}

void BotClient::HandleLoginResponse(const char* data, uint16_t size)
{
    if (size < sizeof(LOGIN_RESPONSE_PACKET)) return;
    auto* pkt = reinterpret_cast<const LOGIN_RESPONSE_PACKET*>(data);

    if (pkt->Result == 0)
    {
        mState = BotState::LOGGED_IN;
        GetStats().loginSuccess++;
    }
    else
    {
        GetStats().loginFailed++;
        mState = BotState::CONNECTED;
    }
}

void BotClient::HandleRoomEnterResponse(const char* data, uint16_t size)
{
    if (size < sizeof(ROOM_ENTER_RESPONSE_PACKET)) return;
    auto* pkt = reinterpret_cast<const ROOM_ENTER_RESPONSE_PACKET*>(data);

    if (pkt->Result == 0)
    {
        mRoomNumber = mTargetRoom;
        mState = BotState::IN_ROOM;
        GetStats().roomEnterSuccess++;
    }
    else
    {
        GetStats().roomEnterFailed++;
        mState = BotState::LOGGED_IN;
    }
}

void BotClient::HandleRoomLeaveResponse(const char* data, uint16_t size)
{
    if (size < sizeof(ROOM_LEAVE_RESPONSE_PACKET)) return;
    auto* pkt = reinterpret_cast<const ROOM_LEAVE_RESPONSE_PACKET*>(data);

    mRoomNumber = -1;
    mState = BotState::LOGGED_IN;
}

void BotClient::HandleChatResponse(const char* data, uint16_t size)
{
    // 채팅 응답 - 특별한 처리 없음
}

void BotClient::HandleChatNotify(const char* data, uint16_t size)
{
    GetStats().chatNotifiesReceived++;
}

void BotClient::HandlePing(const char* data, uint16_t size)
{
    // 서버 PING에 즉시 PONG 응답
    SendPong();
}

// ----------------------------------------------------------------
// 패킷 전송
// ----------------------------------------------------------------

bool BotClient::SendRawPacket(const char* data, uint32_t size)
{
    if (mSocket == INVALID_SOCKET || mState == BotState::DISCONNECTED || mState == BotState::DISCONNECTING) return false;

    if (!mEngine->SendPacket(mBotIndex, data, size))
    {
        GetStats().sendErrors++;
        return false;
    }
    GetStats().packetsSent++;
    GetStats().bytesSent += size;
    return true;
}

bool BotClient::SendLogin()
{
    LOGIN_REQUEST_PACKET pkt;
    PacketBuilder::BuildLoginRequest(pkt, mUserId.c_str(), mUserPw.c_str());

    GetStats().loginRequests++;
    mState = BotState::LOGIN_SENT;
    return SendRawPacket(reinterpret_cast<const char*>(&pkt), sizeof(pkt));
}

bool BotClient::SendRoomEnter(int32_t roomNumber)
{
    ROOM_ENTER_REQUEST_PACKET pkt;
    PacketBuilder::BuildRoomEnterRequest(pkt, roomNumber);

    mTargetRoom = roomNumber;
    GetStats().roomEnterRequests++;
    mState = BotState::ROOM_ENTER_SENT;
    return SendRawPacket(reinterpret_cast<const char*>(&pkt), sizeof(pkt));
}

bool BotClient::SendRoomLeave(int32_t roomNumber)
{
    ROOM_LEAVE_REQUEST_PACKET pkt;
    PacketBuilder::BuildRoomLeaveRequest(pkt, roomNumber);

    GetStats().roomLeaveRequests++;
    mState = BotState::ROOM_LEAVE_SENT;
    return SendRawPacket(reinterpret_cast<const char*>(&pkt), sizeof(pkt));
}

bool BotClient::SendChat(const char* message)
{
    ROOM_CHAT_REQUEST_PACKET pkt;
    PacketBuilder::BuildChatRequest(pkt, message);

    GetStats().chatMessagesSent++;
    return SendRawPacket(reinterpret_cast<const char*>(&pkt), sizeof(pkt));
}

bool BotClient::SendPong()
{
    PONG_PACKET pkt;
    PacketBuilder::BuildPongPacket(pkt);

    GetStats().pongsSent++;
    return SendRawPacket(reinterpret_cast<const char*>(&pkt), sizeof(pkt));
}

// ----------------------------------------------------------------
// 시나리오 B: 파이프라인 버스트
// [입장] -> [퇴장] -> [입장] -> [채팅] 을 하나의 send 버퍼에 합쳐 전송
// TCP_NODELAY 설정 시 하나의 TCP 세그먼트로 전송됨
// ----------------------------------------------------------------
bool BotClient::SendPipelineBurst(int32_t roomNumber)
{
    // 4개 패킷을 하나의 버퍼에 합침
    char buffer[2048];
    uint32_t offset = 0;

    // 1. 방 입장
    ROOM_ENTER_REQUEST_PACKET enterPkt;
    PacketBuilder::BuildRoomEnterRequest(enterPkt, roomNumber);
    memcpy(buffer + offset, &enterPkt, sizeof(enterPkt));
    offset += sizeof(enterPkt);

    // 2. 방 퇴장
    ROOM_LEAVE_REQUEST_PACKET leavePkt;
    PacketBuilder::BuildRoomLeaveRequest(leavePkt, roomNumber);
    memcpy(buffer + offset, &leavePkt, sizeof(leavePkt));
    offset += sizeof(leavePkt);

    // 3. 다시 방 입장
    memcpy(buffer + offset, &enterPkt, sizeof(enterPkt));
    offset += sizeof(enterPkt);

    // 4. 채팅 (방에 있는 상태를 가정)
    ROOM_CHAT_REQUEST_PACKET chatPkt;
    PacketBuilder::BuildChatRequest(chatPkt, "CHAOS_BURST_TEST");
    memcpy(buffer + offset, &chatPkt, sizeof(chatPkt));
    offset += sizeof(chatPkt);

    GetStats().pipelineBursts++;
    GetStats().roomEnterRequests += 2;
    GetStats().roomLeaveRequests++;
    GetStats().chatMessagesSent++;

    // 하나의 send로 전송
    return SendRawPacket(buffer, offset);
}

// ----------------------------------------------------------------
// 시나리오 C: Hard Close (RST 패킷)
// SO_LINGER를 0으로 설정하고 closesocket하면 RST가 전송됨
// ----------------------------------------------------------------
void BotClient::HardClose()
{
    SOCKET s = mSocket.exchange(INVALID_SOCKET);
    if (s == INVALID_SOCKET) return;

    // SO_LINGER: l_onoff=1, l_linger=0 -> closesocket 시 RST 전송
    struct linger lin;
    lin.l_onoff = 1;
    lin.l_linger = 0;
    setsockopt(s, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lin), sizeof(lin));

    closesocket(s);
    mRecvPos = 0;
    mRoomNumber = -1;
    mState.store(BotState::DISCONNECTED);
    GetStats().hardCloses++;
}

// ----------------------------------------------------------------
// 상태 머신 Tick
// ----------------------------------------------------------------

void BotClient::Tick(int64_t nowMs)
{
    if (mState == BotState::DEAD || mState == BotState::IDLE) return;

    // 행동 간격 제어
    if (nowMs - mLastActionTime < mActionIntervalMs) return;
    mLastActionTime = nowMs;

    switch (mBehavior)
    {
    case BotBehavior::NORMAL:
        TickNormal(nowMs);
        break;
    case BotBehavior::GENERATION_PUMP:
        TickGenerationPump(nowMs);
        break;
    case BotBehavior::PIPELINE_BURST:
        TickPipelineBurst(nowMs);
        break;
    case BotBehavior::ZOMBIE_ATTACK:
        TickZombieAttack(nowMs);
        break;
    case BotBehavior::FLOOD_CHAT:
        TickFloodChat(nowMs);
        break;
    case BotBehavior::HOLD_VICTIM:
        TickNormal(nowMs);  // 일반 행동 + 프록시가 패킷 보류
        break;
    case BotBehavior::RAPID_LEAVE_REENTER:
        TickRapidLeaveReenter(nowMs);
        break;
    }
}

// ----------------------------------------------------------------
// Normal Behavior: 로그인 -> 입장 -> 채팅 -> 퇴장 반복
// ----------------------------------------------------------------
void BotClient::TickNormal(int64_t nowMs)
{
    switch (mState)
    {
    case BotState::CONNECTED:
        SendLogin();
        break;

    case BotState::LOGGED_IN:
        SendRoomEnter(mTargetRoom);
        break;

    case BotState::IN_ROOM:
    {
        // 일정 횟수 채팅 후 퇴장
        mCycleCount++;
        if (mCycleCount % 10 == 0)
        {
            SendRoomLeave(mRoomNumber);
            mCycleCount = 0;
        }
        else
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "chaos_%u_msg_%u", mBotIndex, mCycleCount);
            SendChat(msg);
        }
        break;
    }

    case BotState::DISCONNECTED:
        // 재접속 요청 (엔진에서 처리)
        mState = BotState::IDLE;
        break;

    default:
        break;
    }
}

// ----------------------------------------------------------------
// 시나리오 A: Generation Pumping
// 빠른 접속 -> 로그인 -> 즉시 연결 종료 반복
// 세션 객체가 FreeList에 반납되고 재할당되며 Generation이 증가함
// ----------------------------------------------------------------
void BotClient::TickGenerationPump(int64_t nowMs)
{
    switch (mState)
    {
    case BotState::CONNECTED:
        SendLogin();
        mActionIntervalMs = 10;  // 최소 간격
        break;

    case BotState::LOGGED_IN:
        // 로그인 성공 즉시 연결 종료 -> 세션 카운트 1 증가 유도
        mCycleCount++;
        GetStats().generationPumpCycles++;
        HardClose();  // RST로 빠르게 종료
        break;

    case BotState::DISCONNECTED:
        if (mMaxCycles > 0 && mCycleCount >= mMaxCycles) // mMaxCycles=0 → 항상 false
        {
            mState = BotState::DEAD;
            return;
        }
        mState = BotState::IDLE;  // 재접속 요청
        break;

    default:
        break;
    }
}

// ----------------------------------------------------------------
// 시나리오 B: Pipeline Burst
// 로그인 후 방 입장 -> [입장-퇴장-입장-채팅] 파이프라인 전송
// ----------------------------------------------------------------
void BotClient::TickPipelineBurst(int64_t nowMs)
{
    switch (mState)
    {
    case BotState::CONNECTED:
        SendLogin();
        break;

    case BotState::LOGGED_IN:
        // 파이프라인 버스트 전송 (방 번호 지정)
        SendPipelineBurst(mTargetRoom);
        mCycleCount++;

        if (mMaxCycles > 0 && mCycleCount >= mMaxCycles)
        {
            mState = BotState::DEAD;
        }
        else
        {
            // 응답을 기다리지 않고 연속 전송
            mState = BotState::LOGGED_IN;
        }
        break;

    case BotState::DISCONNECTED:
        mState = BotState::IDLE;  // 서버가 끊으면 재접속
        break;

    default:
        break;
    }
}

// ----------------------------------------------------------------
// 시나리오 C: Zombie Attack
// 방 입장 -> 브로드캐스트 폭주 -> RST -> 즉시 재접속
// ----------------------------------------------------------------
void BotClient::TickZombieAttack(int64_t nowMs)
{
    switch (mState)
    {
    case BotState::CONNECTED:
        SendLogin();
        break;

    case BotState::LOGGED_IN:
        SendRoomEnter(mTargetRoom);
        break;

    case BotState::IN_ROOM:
    {
        // 대량 채팅으로 브로드캐스트 폭주 유발
        for (uint32_t i = 0; i < 20; ++i)
        {
            char msg[MAX_CHAT_MSG + 1];
            snprintf(msg, sizeof(msg), "ZOMBIE_FLOOD_%u_%u", mBotIndex, i);
            SendChat(msg);
        }

        // RST로 비정상 종료
        HardClose();
        GetStats().zombieReconnects++;
        mCycleCount++;

        if (mMaxCycles > 0 && mCycleCount >= mMaxCycles)
        {
            mState = BotState::DEAD;
        }
        // DISCONNECTED 상태에서 즉시 재접속 시도 (TickZombieAttack의 DISCONNECTED 케이스)
        break;
    }

    case BotState::DISCONNECTED:
        // 즉시 재접속 (마이크로초 단위 지연)
        mState = BotState::IDLE;  // 엔진에 재접속
        mActionIntervalMs = 1;   // 최소 간격
        break;

    default:
        break;
    }
}

// ----------------------------------------------------------------
// Flood Chat: 방에 들어가서 채팅 폭주 (시나리오 C 보조)
// ----------------------------------------------------------------
void BotClient::TickFloodChat(int64_t nowMs)
{
    switch (mState)
    {
    case BotState::CONNECTED:
        SendLogin();
        break;

    case BotState::LOGGED_IN:
        SendRoomEnter(mTargetRoom);
        break;

    case BotState::IN_ROOM:
    {
        char msg[MAX_CHAT_MSG + 1];
        snprintf(msg, sizeof(msg), "FLOOD_%u_%lld", mBotIndex, nowMs);
        SendChat(msg);
        mActionIntervalMs = 1;  // 가능한 빠르게
        break;
    }

    case BotState::DISCONNECTED:
        mState = BotState::IDLE;
        break;

    default:
        break;
    }
}

// ----------------------------------------------------------------
// 시나리오 D: Fast Path / Slow Path 경계 스트레스
// 방 안에서 N회 채팅 후 LeaveRoom을 보내고 즉시 Chat을 전송.
// 서버가 ROOM→LOGIN 상태 전환을 처리하는 순간 Chat 패킷이 도착하여
// TryAcquireRouting() 경계 조건(Fast Path vs Slow Path 선택)을 스트레스 테스트.
// mMaxCycles = chatBeforeLeave (ScenarioRunner에서 설정)
// ----------------------------------------------------------------
void BotClient::TickRapidLeaveReenter(int64_t nowMs)
{
    switch (mState)
    {
    case BotState::CONNECTED:
        SendLogin();
        mActionIntervalMs = 1;  // 최소 간격으로 경계 스트레스 극대화
        break;

    case BotState::LOGGED_IN:
        SendRoomEnter(mTargetRoom);
        break;

    case BotState::IN_ROOM:
    {
        uint32_t chatLimit = (mMaxCycles > 0) ? mMaxCycles : 3;
        mCycleCount++;
        if (mCycleCount >= chatLimit)
        {
            // 핵심: LeaveRoom 직후 즉시 Chat 전송 → 경계 패킷 생성
            SendRoomLeave(mRoomNumber);
            SendChat("D_BOUNDARY");
            GetStats().routingBoundaryAttempts++;
            mCycleCount = 0;
        }
        else
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "D_%u_%u", mBotIndex, mCycleCount);
            SendChat(msg);
        }
        break;
    }

    case BotState::ROOM_LEAVE_SENT:
        // HandleRoomLeaveResponse() 가 LOGGED_IN으로 전환할 때까지 대기
        break;

    case BotState::DISCONNECTED:
        mState = BotState::IDLE;
        break;

    default:
        break;
    }
}