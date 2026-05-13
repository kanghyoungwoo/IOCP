[← README로 돌아가기](../README.md)

# 패킷 프로토콜 스펙

## 헤더 구조

```cpp
#pragma pack(push, 1)
struct PACKET_HEADER
{
    UINT16 PacketLength;  // 헤더 포함 전체 패킷 크기
    UINT16 PacketId;      // 패킷 종류 식별자
    UINT8  PacketType;    // 압축/인코딩 플래그 (현재 미사용)
};
#pragma pack(pop)

const UINT32 PACKET_HEADER_LENGTH = sizeof(PACKET_HEADER);  // 5 bytes
```

```
┌────────────────────┬──────────────────┬──────────────┐
│ PacketLength (2B)  │ PacketId (2B)    │ Type (1B)    │
└────────────────────┴──────────────────┴──────────────┘
```

---

## 패킷 ID 목록

### System (내부 이벤트)

| Packet ID | 값 | 방향 | 설명 |
|-----------|-----|------|------|
| `SYS_USER_CONNECT` | 11 | Internal | TCP 접속 완료 시 서버 내부 이벤트 |
| `SYS_USER_DISCONNECT` | 12 | Internal | 연결 해제 시 서버 내부 이벤트 |
| `SYS_PING` | 21 | S→C | 서버가 클라이언트에 보내는 생존 확인 |
| `SYS_PONG` | 22 | C→S | 클라이언트 생존 응답 |

### Auth (인증)

| Packet ID | 값 | 방향 | 설명 |
|-----------|-----|------|------|
| `LOGIN_REQUEST` | 201 | C→S | 로그인 요청 |
| `LOGIN_RESPONSE` | 202 | S→C | 로그인 결과 응답 |

### Room (방 관리)

| Packet ID | 값 | 방향 | 설명 |
|-----------|-----|------|------|
| `ROOM_ENTER_REQUEST` | 206 | C→S | 방 입장 요청 |
| `ROOM_ENTER_RESPONSE` | 207 | S→C | 방 입장 결과 응답 |
| `ROOM_LEAVE_REQUEST` | 215 | C→S | 방 퇴장 요청 |
| `ROOM_LEAVE_RESPONSE` | 216 | S→C | 방 퇴장 결과 응답 |

### Chat (채팅)

| Packet ID | 값 | 방향 | 설명 |
|-----------|-----|------|------|
| `ROOM_CHAT_REQUEST` | 221 | C→S | 채팅 메시지 전송 |
| `ROOM_CHAT_RESPONSE` | 222 | S→C | 전송 결과 응답 (발신자에게) |
| `ROOM_CHAT_NOTIFY` | 223 | S→C | 채팅 브로드캐스트 (방 전체) |

---

## 패킷 바디 상세

### LOGIN_REQUEST (201)

```cpp
struct LOGIN_REQUEST_PACKET : public PACKET_HEADER
{
    char UserID[33];  // MAX_USER_ID_LENGTH(32) + null
    char UserPW[33];  // MAX_USER_PW_LENGTH(32) + null
};
// 총 크기: 5 + 33 + 33 = 71 bytes
```

| 필드 | 타입 | 크기 | 설명 |
|------|------|------|------|
| UserID | char[] | 33 | 유저 ID (null 종료) |
| UserPW | char[] | 33 | 비밀번호 (null 종료) |

### LOGIN_RESPONSE (202)

```cpp
struct LOGIN_RESPONSE_PACKET : public PACKET_HEADER
{
    UINT16 Result;  // 0 = 성공, 그 외 ErrorCode
};
// 총 크기: 5 + 2 = 7 bytes
```

### ROOM_ENTER_REQUEST (206)

```cpp
struct ROOM_ENTER_REQUEST_PACKET : public PACKET_HEADER
{
    INT32 RoomNumber;  // 입장할 방 번호
};
// 총 크기: 5 + 4 = 9 bytes
```

### ROOM_ENTER_RESPONSE (207)

```cpp
struct ROOM_ENTER_RESPONSE_PACKET : public PACKET_HEADER
{
    INT16 Result;  // 0 = 성공, 그 외 ErrorCode
};
// 총 크기: 5 + 2 = 7 bytes
```

### ROOM_LEAVE_REQUEST (215)

```cpp
struct ROOM_LEAVE_REQUEST_PACKET : public PACKET_HEADER
{
    INT32 RoomNumber;  // 퇴장할 방 번호
};
// 총 크기: 5 + 4 = 9 bytes
```

### ROOM_LEAVE_RESPONSE (216)

```cpp
struct ROOM_LEAVE_RESPONSE_PACKET : public PACKET_HEADER
{
    INT16 Result;  // 0 = 성공, 그 외 ErrorCode
};
// 총 크기: 5 + 2 = 7 bytes
```

### ROOM_CHAT_REQUEST (221)

```cpp
struct ROOM_CHAT_REQUEST_PACKET : public PACKET_HEADER
{
    char Message[257];  // MAX_CHAT_MSG(256) + null
};
// 총 크기: 5 + 257 = 262 bytes
```

### ROOM_CHAT_RESPONSE (222)

```cpp
struct ROOM_CHAT_RESPONSE_PACKET : public PACKET_HEADER
{
    INT16 Result;  // 0 = 성공
};
// 총 크기: 5 + 2 = 7 bytes
```

### ROOM_CHAT_NOTIFY (223)

```cpp
struct ROOM_CHAT_NOTIFY_PACKET : public PACKET_HEADER
{
    char UserID[33];    // 발신자 ID
    char Message[257];  // 채팅 메시지
};
// 총 크기: 5 + 33 + 257 = 295 bytes
```

| 필드 | 타입 | 크기 | 설명 |
|------|------|------|------|
| UserID | char[] | 33 | 채팅을 보낸 유저 ID |
| Message | char[] | 257 | 채팅 메시지 (null 종료) |

---

## 주요 상수

| 상수 | 값 | 설명 |
|------|----|------|
| `PACKET_HEADER_LENGTH` | 5 | 헤더 크기 (bytes) |
| `MAX_SINGLE_PACKET_SIZE` | 512 | 단일 패킷 최대 크기 |
| `RING_BUFFER_SIZE` | 4096 | 수신 RingBuffer 크기 |
| `MAX_USER_ID_LENGTH` | 32 | 유저 ID 최대 길이 |
| `MAX_USER_PW_LENGTH` | 32 | 비밀번호 최대 길이 |
| `MAX_CHAT_MSG` | 256 | 채팅 메시지 최대 길이 |

---

## Result 코드

| 값 | 의미 |
|----|------|
| 0 | `ERROR_CODE::NONE` — 성공 |
| 그 외 | `ErrorCode.h` 참조 |

> 악성 패킷 방어: `PacketLength`가 `MAX_SINGLE_PACKET_SIZE(512)`를 초과하면 즉시 연결 해제
