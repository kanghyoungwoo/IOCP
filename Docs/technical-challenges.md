[← README로 돌아가기](../README.md)

# Technical Challenges & Edge Case 방어

## Technical Challenges

### Challenge 1: TCP Stream Packet 경계

- **Problem**: TCP는 스트림 기반 프로토콜로 패킷 경계가 없어서, 여러 패킷이 합쳐지거나 하나의 패킷이 분할되어 수신될 수 있음. 패킷 조립 시 데이터 손실이나 잘못된 파싱 위험.
- **Approach**: 고정 길이 헤더 구조체를 설계하고, 링버퍼를 도입하여 스트림 데이터를 안전하게 버퍼링.
- **Solution**: `User::GetPacket()`에서 헤더 peek → 전체 길이 확인 → 정확한 바이트 수만큼 read.

### Challenge 2: 다중 스레드에서 Ring Buffer 접근

- **Problem**: 네트워크 스레드(IOCP Worker)에서 `User::SetPacketData()`로 데이터 쓰기와 패킷 처리 스레드에서 `User::GetPacket()`으로 데이터 읽기가 동시에 발생하여 race condition 발생.
- **Approach**: Mutex 추가로 스레드 안전성 확보.
- **Solution**: `User` 클래스에 `mPacketRingBuffMutex` 추가, `SetPacketData()`, `GetPacket()`, `Clear()` 메서드를 동일한 뮤텍스로 보호.

### Challenge 3: 비동기 이벤트 처리 중 발생하는 상태 불일치 문제

- **Problem**: I/O 스레드가 작업을 생성한 후 큐에 넣기 전 사이에 다른 스레드가 사용자의 상태를 변경하면 ProcessPacket 스레드엔 무효화된 작업이 들어가게 됨.
- **Approach**: Client 객체의 생명 주기를 추적할 수 있도록 Generation Token 도입으로 상태 검증.
- **Solution**: User 클래스에 Generation Token을 도입하여 패킷 처리 작업을 생성할 때 당시의 token 값을 함께 기록. 큐에서 꺼내어 작업할 때 token 값을 비교하여 값이 다를 경우 해당 패킷을 무효화 처리.

### Challenge 4: Graceful Shutdown 구현

- **Problem**: 서버 강제 종료 시 진행 중인 I/O와 DB 작업이 유실되고, 리소스가 정리되지 않아 데이터 손실과 메모리 누수가 발생.
- **Approach**: 5단계 순차 종료(Accept 차단 → 클라이언트 킥 + CancelIoEx → I/O Draining → PQCS 워커 종료 → 리소스 정리)하고, DB/Redis 스레드는 queue draining 후 종료.
- **Solution**: `DestroyThread()`를 5단계로 구성, MySQL/Redis의 `TaskProcessThread()`를 빈 큐 확인 패턴으로 변경하여 잔여 작업을 모두 처리한 뒤 종료. `SetConsoleCtrlHandler`로 Ctrl+C 및 콘솔 종료도 Graceful Shutdown으로 구현.

### Challenge 5: AcceptEx 빈 세션 탐색 방식의 비효율

- **Problem**: AccepterThread가 빈 세션을 찾기 위해 매번 전체 10,000개를 O(N) 선형 탐색하며, 이미 AcceptEx가 걸린 세션에 중복 호출하여 소켓 누수 발생 가능.
- **Approach**: FreeList를 도입하여 O(1) Pop/Push로 빈 세션을 관리하고, 서버 시작 시 100개만 미리 AcceptEx를 걸어둔 뒤 워커 스레드가 완료 시 1개씩 보충.
- **Solution**: AccepterThread를 제거하고 `PopFreeSessionIndex()`/`PushFreeSessionIndex()`로 세션을 관리하며 ACCEPT 완료 시 워커 스레드가 자동으로 AcceptEx를 보충. 커널에는 항상 ~100개의 대기 소켓만 유지.

### Challenge 6: 비동기 I/O 완료 전 세션 재사용 방지

- **Problem**: IOCP 환경에서 클라이언트가 연결을 끊으면 세션을 즉시 풀에 반납하고 싶지만,
  `CancelIoEx()` 이후에도 이미 커널에 등록된 WSARecv / WSASend 완료 통보가
  워커 스레드로 뒤늦게 도착할 수 있음.
  세션을 너무 일찍 반납하면 새로운 클라이언트에게 재할당된 슬롯에
  이전 클라이언트의 완료 통보가 덮어써 데이터 오염 및 크래시 발생.

- **Approach**: 세션 슬롯의 생명주기를 "소켓 연결 시간"이 아닌 
  "진행 중인 I/O 연산 수"로 추적.
  I/O를 등록할 때마다 `AddRef()`, 완료 통보를 처리할 때마다 `ReleaseRef()`하여
  참조 카운트가 0이 될 때만 세션을 풀에 반납.

- **Solution**: `ClientSession`에 `std::atomic<int> mRefCount` 도입.
  `BindRecv()` / `SendIO()` / `PostImmediateAccept()` 진입 시 `AddRef()`,
  WorkerThread의 각 완료 처리 끝단에 `ReleaseRef()`.
  `ReleaseRef()`가 0을 반환하는 시점에만 `Clear()` → `PushFreeSessionIndex()`를 호출하여
  모든 커널 I/O가 소진된 이후에만 세션 슬롯을 재사용 가능 상태로 전환.
  Generation Token(Challenge 3)이 잘못된 연결의 패킷을 걸러낸다면,
  RefCount는 아직 I/O가 남은 슬롯의 조기 반납을 막는 보완적 안전장치.

---

## Edge Case 방어 로직

부하 테스트 이후, 악의적인 클라이언트가 서버를 공격할 수 있는 시나리오를 분석하고 사전 방어 로직을 설계했습니다.

### Case 1: 비정상 패킷 크기 검증 (Oversized / Malformed Packet)

- **Attack**: 공격자가 패킷 헤더의 PacketLength를 65,535(UINT16 최대값)로 조작하여 전송. 링버퍼(8KB)는 해당 크기를 절대 모을 수 없어 세션이 영구 좀비 상태에 빠짐 — 패킷 처리 불가, 그러나 연결은 유지되어 세션 자원 점유.
- **Defense**: `GetPacket()`에서 헤더를 peek한 직후, PacketLength의 상한(`MAX_PACKET_DATA_BUFFER_SIZE`)과 하한(`PACKET_HEADER_LENGTH`) 범위를 검증. 범위 밖이면 오염된 링버퍼를 즉시 `Clear()`하고 해당 패킷을 폐기.

### Case 2: 링버퍼 오버플로우 시 연결 해제 (Buffer Overflow Protection)

- **Attack**: 공격자가 서버의 처리 속도를 초과하는 대량의 데이터를 연속 전송하여 링버퍼(8KB)를 고의로 가득 채움. 오버플로우 이후의 데이터는 유실되어 패킷 경계가 영구적으로 깨지며, 해당 세션의 모든 후속 패킷 파싱이 불가능.
- **Defense**: `SetPacketData()`의 반환값을 bool로 변경하여 오버플로우를 호출자에게 전파. 오버플로우 감지 시 기존 `DisconnectAsync()` 경로(`shutdown(SD_BOTH)` → WorkerThread가 0바이트 감지 → `CloseSocket`)를 재활용하여 안전하게 세션 정리.

### Case 3: Slowloris 변형 공격 방어 (Incomplete Packet Timeout)

> 1바이트씩 천천히 보내 타임아웃을 피해가는 악의적 세션 공격 방어

- **Attack**: 공격자가 1바이트씩 59초 간격으로 전송. 기존에는 WSARecv 완료 시마다 `UpdateActivity()`가 갱신되어, 1바이트만 보내도 60초 타임아웃이 매번 리셋됨 — 세션 하나를 영구 점유 가능.
- **Defense**: `UpdateActivity()`의 호출 시점을 WSARecv 완료(바이트 수신) → 완전한 패킷 조립 성공 시로 이동. 콜백 패턴으로 PacketManager에서 유효한 패킷 처리 완료 시에만 활동 시간을 갱신. 불완전한 바이트 스트림으로는 타임아웃을 리셋할 수 없어 60초 후 자동 연결 해제.
