[← README로 돌아가기](../README.md)

# 단위 테스트 (Google Test)

서버의 핵심 자료구조와 도메인 로직의 정확성을 검증하기 위해, Google Test 기반의 단위 테스트 90개를 구성했습니다. 단위 테스트는 네트워크나 DB 없이 순수 C++ 로직만을 격리하여 검증하며, 카오스 엔지니어링(블랙박스 통합 테스트)과 상호 보완적인 역할을 합니다.

## 3계층 테스트 전략

```
Tier 1: 자료구조 단위 테스트     ← 격리된 순수 C++ 로직 (외부 의존성 없음)
Tier 2: 도메인 로직 단위 테스트   ← 패킷/User/Room 상태 머신 + Mock 기반 브로드캐스팅 검증
Tier 3: 시스템 통합 카오스 테스트  ← ChaosBotSystem (실제 TCP 소켓, 네트워크 고장 주입)
```

| 계층 | 도구 | 성격 | 검증 대상 |
|------|------|------|----------|
| **Tier 1** | Google Test | 화이트박스, 결정적 | RingBuffer, LockFreeStack, MPSCQueue, ObjectPool |
| **Tier 2** | Google Test | 화이트박스, Mock 주입 | Packet 구조체, User 상태 머신, Room 입퇴장/브로드캐스팅 |
| **Tier 3** | [ChaosBotSystem](chaos-engineering.md) | 블랙박스, 확률적 | ABA 방어, Strand Race, 좀비 세션, 패킷 단편화 |

---

## 테스트 환경

| 구성 요소 | 버전 |
|-----------|------|
| Google Test | 1.17.0 (vcpkg) |
| Visual Studio | 2022 (v143 toolset) |
| C++ 표준 | C++17 |
| 플랫폼 | x64 Debug / Release |

---

## 테스트 실행 방법

### PowerShell에서 직접 실행

```powershell
# 빌드
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  IOCPChatServer.sln /p:Configuration=Debug /p:Platform=x64 /t:IOCPChatServer_Tests /v:minimal

# 전체 테스트 실행
& ".\Tests\x64\Debug\IOCPChatServer_Tests.exe"

# 특정 테스트 스위트만 실행
& ".\Tests\x64\Debug\IOCPChatServer_Tests.exe" --gtest_filter="RoomTest.*"

# 멀티스레드 안정성 확인 (5회 반복)
& ".\Tests\x64\Debug\IOCPChatServer_Tests.exe" --gtest_repeat=5 --gtest_shuffle
```

### Visual Studio Test Explorer

1. 솔루션 열기 → 빌드 (`Ctrl+Shift+B`)
2. **테스트** → **테스트 탐색기** (`Ctrl+E, T`)
3. **모두 실행** 클릭

---

## Tier 1: 자료구조 단위 테스트 (38개)

외부 의존성 없이 순수 C++ 헤더만으로 테스트합니다. 멀티스레드 동시성 테스트를 포함합니다.

### RingBuffer (15개)

| 테스트 | 검증 내용 |
|--------|----------|
| `EmptyDefaults` | 초기 상태 Size==0, IsEmpty==true |
| `WriteSingleByte` / `ReadSingleByte` | 단일 바이트 쓰기/읽기 |
| `WriteReadBlock` | 블록 단위 데이터 무결성 |
| `ReadFromEmptyFails` | 빈 버퍼 읽기 시 0 반환 |
| `PartialWriteWhenNearFull` | 잔여 공간만큼만 부분 쓰기 |
| `WrapAround` | 순환 경계를 넘는 데이터 무결성 |
| `FullBufferRejectsWrite` | 가득 찬 버퍼에 쓰기 거부 |
| `PeekDoesNotConsume` | Peek 후 Size 불변 |
| `PeekOutOfRange` | 범위 초과 Peek 안전 처리 |
| `PeekBlockCrossWrap` | wrap 경계 걸치는 PeekBlock |
| `PeekBlockTooLarge` | 과대 PeekBlock 요청 실패 |
| `ClearResetsState` | Clear 후 초기화 확인 |
| `NullptrHandling` | nullptr 입력 시 0 반환 |
| `RepeatedWriteReadCycles` | 100회 반복 쓰기/읽기 사이클 |

### LockFreeStack (6개)

| 테스트 | 검증 내용 |
|--------|----------|
| `PopFromEmptyReturnsNull` | 빈 스택 Pop → nullptr |
| `PushPopSingle` | 단일 Push/Pop |
| `PushPopMultipleLIFO` | LIFO 순서 검증 |
| `PushAllPopAll` | 100개 전체 Push/Pop, 유일성 검증 |
| `IsLockFreeCheck` | 하드웨어 CAS lock-free 확인 |
| `ConcurrentPushPop` | **4스레드 동시 Push/Pop**, 데이터 손실 0건 |

### MPSCQueue (6개)

| 테스트 | 검증 내용 |
|--------|----------|
| `PopFromEmptyReturnsNull` | 빈 큐 Pop → nullptr |
| `SinglePushPop` | 단일 노드 Push/Pop |
| `FIFOOrdering` | FIFO 순서 검증 |
| `PushAfterDrain` | 비운 뒤 재사용 |
| `RapidPushPopCycles` | 50회 × 10개 반복 사이클 |
| `MultiProducerSingleConsumer` | **4 Producer × 1,000개**, 총 4,000개 수신 + 유일성 검증 |

### ObjectPool (11개)

| 테스트 | 검증 내용 |
|--------|----------|
| `AllocBeforeInitReturnsNull` | 미초기화 상태 Alloc → nullptr |
| `InitSetsSize` | Init(100) 후 풀 크기/잔여 수 확인 |
| `AllocReturnsNonNull` | 정상 할당 |
| `AllocAllUnique` | 100회 할당 모두 고유 주소 |
| `AllocExhaustedReturnsNull` | 풀 소진 시 nullptr |
| `FreeAndRealloc` | Free 후 재할당 성공 |
| `FreeNullptrSafe` | nullptr Free 안전 처리 |
| `FreeCountTracking` | GetFreeCount() 정확성 추적 |
| `AllocFailCount` | 실패 카운터 증가 검증 |
| `IsLockFree` | lock-free 속성 확인 |
| `ConcurrentAllocFree` | **4스레드 × 500회 Alloc/Free**, 풀 무결성 검증 |

---

## Tier 2: 도메인 로직 단위 테스트 (52개)

서버의 패킷 프로토콜, 유저 상태 머신, 방 관리 로직을 검증합니다. Room 브로드캐스팅은 `SendPacketFunc`에 Mock 람다를 주입하여 네트워크 없이 검증합니다.

### Packet 구조체 (17개)

| 테스트 | 검증 내용 |
|--------|----------|
| `HeaderIs5Bytes` | `PACKET_HEADER` = 5바이트 (`#pragma pack` 정합성) |
| `LoginRequestPacket` | `LOGIN_REQUEST_PACKET` = 71바이트 (Header + UserID + UserPW) |
| `LoginResponsePacket` | 7바이트 |
| `RoomEnterRequest` / `RoomEnterResponse` | 9바이트 / 7바이트 |
| `RoomLeaveRequest` | 9바이트 |
| `RoomChatRequest` / `RoomChatNotify` | 가변 길이 검증 |
| `ByteArrayToHeader` | raw 바이트 → 헤더 reinterpret_cast 파싱 |
| `LoginRequestFields` | 필드별 직렬화/역직렬화 |
| `RingBufferSize` / `MaxSinglePacketSize` | 상수 값 검증 (4096 / 512) |
| `MaxUserIDLength` / `MaxChatMsg` | 문자열 길이 상수 (32 / 256) |
| `NoCollisions` | 15개 PACKET_ID 유일성 (중복 없음) |
| `NoneIsZero` / `EnterRoomFullUser` | ERROR_CODE 값 검증 |

### User 상태 머신 (17개)

| 테스트 | 검증 내용 |
|--------|----------|
| `InitialStateIsNone` | 초기 상태 = `DOMAIN_STATE::NONE` |
| `InitialRoomIndexIsInvalid` | 초기 roomIndex = -1 |
| `InitialNetConnIndex` / `InitialUserIDIsEmpty` | 초기값 검증 |
| `SetLoginChangesState` | SetLogin → `LOGIN` 상태 전이 + UserID 저장 |
| `EnterRoomChangesState` | EnterRoom → `ROOM` 상태 전이 + roomIndex 저장 |
| `ResetRoomClearsRoomIndex` | ResetRoom → roomIndex만 초기화 (상태 유지) |
| `ClearResetsEverything` | Clear → 전체 초기화 (NONE, roomIndex=-1, UserID="") |
| `SessionGenerationDefault` / `SetSessionGeneration` | 세대 카운터 기본값/설정 |
| `IsDisconnectingDefault` / `SetDisconnecting` | 접속 해제 플래그 |
| `ClearResetsDisconnecting` | Clear 시 플래그 초기화 |
| `SetPacketDataAndGetPacket` | RingBuffer를 통한 패킷 쓰기/읽기 라운드트립 |
| `GetPacketFromEmptyBuffer` | 빈 버퍼에서 읽기 시 PacketId=0 |
| `SetPacketDataNullptr` | nullptr 입력 안전 처리 |
| `MultiplePacketsInBuffer` | 2개 패킷 순차 쓰기 후 순차 읽기 (FIFO) |

### Room 입퇴장 + 브로드캐스팅 Mock (18개)

Room 테스트는 `SendPacketFunc`에 람다를 주입하여 실제 네트워크 호출 없이 브로드캐스팅 동작을 검증합니다.

```cpp
// Mock 주입 예시
room.SendPacketFunc = [this](UINT32 connIdx, UINT32 gen, UINT32 size, char* pData)
{
    sendCalls.push_back({connIdx, gen, size, {pData, pData + size}});
};
```

| 테스트 | 검증 내용 |
|--------|----------|
| **입퇴장** | |
| `EnterUserSuccess` | 정상 입장 → `ERROR_CODE::NONE` |
| `EnterMultipleUsersUpToMax` | 정원까지 순차 입장 성공 |
| `EnterUserFullRoomReturnsError` | **정원 초과 → `ENTER_ROOM_FULL_USER`** |
| `EnterAfterLeaveSucceeds` | 퇴장 후 빈 자리에 재입장 성공 |
| `LeaveUserNotInRoomSafe` | 미등록 유저 퇴장 시 크래시 없음 |
| `LeaveUserDecrementsCount` | 퇴장 후 인원 수 정확히 감소 |
| **유저 검색** | |
| `FindUserByClientIndexFound` | connIndex로 유저 검색 성공 |
| `FindUserByClientIndexNotFound` | 존재하지 않는 index → nullptr |
| `FindUserAfterLeaveReturnsNull` | 퇴장한 유저 검색 → nullptr |
| **브로드캐스팅 (Mock)** | |
| `NotifyChatBroadcastsToAllUsers` | **3명 방에서 채팅 → SendPacketFunc 3회 호출** |
| `NotifyChatContainsCorrectPacketData` | 전송 데이터가 `ROOM_CHAT_NOTIFY_PACKET` 형식, UserID/Message 정확 |
| `NotifyChatSendsToCorrectConnIndices` | 각 유저의 connIndex로 정확히 전송 |
| `NotifyChatSessionGenerationCorrect` | 각 유저의 sessionGeneration이 정확히 전달 |
| `NotifyChatEmptyRoomNoCalls` | 빈 방에서 채팅 → 호출 0회 |
| `NotifyChatAfterLeaveSendsToRemaining` | **퇴장 후 남은 유저에게만 브로드캐스트** |
| **리셋** | |
| `ResetClearsAllUsers` | Reset 후 전체 재입장 가능 |
| `ResetWithParamsChangesCapacity` | Reset(roomNumber, maxUser) 후 설정 변경 반영 |
| `GetRoomNumber` | 방 번호 조회 |

---

## 테스트 결과

```
[==========] Running 90 tests from 15 test suites.
...
[==========] 90 tests from 15 test suites ran. (87 ms total)
[  PASSED  ] 90 tests.
```

| 구분 | 테스트 수 | 결과 |
|------|----------|------|
| Tier 1 (자료구조) | 38개 | PASSED |
| Tier 2 (도메인 로직) | 52개 | PASSED |
| **합계** | **90개** | **ALL PASSED** |

---

## 프로젝트 구조

```
IOCPChatServer/
├── Tests/
│   ├── IOCPChatServer_Tests.vcxproj   # 테스트 프로젝트
│   ├── test_main.cpp                  # gtest 진입점
│   ├── test_RingBuffer.cpp            # Tier 1
│   ├── test_LockFreeStack.cpp         # Tier 1
│   ├── test_MPSCQueue.cpp             # Tier 1
│   ├── test_ObjectPool.cpp            # Tier 1
│   ├── test_Packet.cpp                # Tier 2
│   ├── test_User.cpp                  # Tier 2
│   └── test_Room.cpp                  # Tier 2 (Broadcasting Mock 포함)
├── RingBuffer.h          ← 테스트 대상
├── LockFreeStack.h       ← 테스트 대상
├── MPSCQueue.h           ← 테스트 대상
├── ObjectPool.h          ← 테스트 대상
├── Packet.h              ← 테스트 대상
├── User.h / User.cpp     ← 테스트 대상
└── Room.h / Room.cpp     ← 테스트 대상
```
