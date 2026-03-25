# 🚀 IOCP 기반 고성능 대규모 채팅 서버

## 📋 프로젝트 개요

Windows IOCP(I/O Completion Port)를 활용한 고성능 멀티스레드 채팅 서버입니다.

단순한 기능 구현을 넘어 **아키텍처 진화(Single-Thread ➡️ Lock-Free)와 극한의 부하 테스트**를 통해 병목 현상을 해결하고, 실제 상용 서비스 수준인 **10,000명 동시 접속 환경에서 Zero-Allocation(메모리 누수 0%)**을 달성한 프로젝트입니다.

### 🎯 Key Achievements

- ⚡ **142,400 TPS** — Lock-Free 아키텍처로 초당 14만 건 패킷 처리, Mutex 대비 p99 지연시간 30% 개선
- 🏋️ **10,000명 동시접속** — AWS EC2 4대에서 실제 유저 시나리오(채팅·입퇴장 반복) 82분 연속 부하 테스트, 연결 유실 0건
- 🧠 **Zero-Allocation 증명** — Lock-Free Object Pool로 740만 회 작업 처리 후 메모리 누수 0%, 371.9MB 고정 메모리 운용

---

## ✨ 주요 특징

- **네트워크 코어:** IOCP 기반 AcceptEx, WSARecv, WSASend 비동기 I/O 처리
- **Lock-Free 아키텍처:** CAS 연산을 활용한 Lock-Free 큐 및 Object Pool을 통해 스레드 경합(Contention) 최소화
- **DB & Cache:** Redis(인메모리 캐시를 통한 초고속 인증), MySQL/AWS RDS(비동기 Task Queue를 통한 활동 로그 영구 기록)
- **안정성:** AWS CloudWatch를 활용한 메모리 모니터링 및 Generation Token 기반의 좀비 세션 방어

## 📦 기술 스택

```markdown
**Language & Network** <br>
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B)
![Windows IOCP](https://img.shields.io/badge/Network-Windows_IOCP-0078D6?style=flat-square&logo=windows)

**Core Architecture** <br>
![Lock-Free](https://img.shields.io/badge/Architecture-Lock--Free_Data_Structure-555555?style=flat-square)
![Strand Pattern](https://img.shields.io/badge/Architecture-Strand_Pattern-555555?style=flat-square)
![Object Pooling](https://img.shields.io/badge/Architecture-Object_Pooling-555555?style=flat-square)

**Database & Cache** <br>
![Redis](https://img.shields.io/badge/Cache-Redis-DC382D?style=flat-square&logo=redis&logoColor=white)
![MySQL](https://img.shields.io/badge/Database-MySQL-4479A1?style=flat-square&logo=mysql&logoColor=white)

**Infrastructure & Profiling** <br>
![AWS EC2](https://img.shields.io/badge/Infrastructure-AWS_EC2-232F3E?style=flat-square&logo=amazonaws)
![CloudWatch](https://img.shields.io/badge/Monitoring-AWS_CloudWatch-FF4F8B?style=flat-square&logo=amazoncloudwatch&logoColor=white)
![VS Diagnostic Tools](https://img.shields.io/badge/Profiling-VS_Diagnostic_Tools-5C2D91?style=flat-square&logo=visualstudio&logoColor=white)
```

- **Language:** C++17
- **Network:** Windows Sockets (Winsock2), IOCP
- **Architecture:** Lock-Free Data Structures, Strand Pattern, Object Pooling
- **Database:** Redis, MySQL (AWS RDS)
- **Monitoring & Profiling:** AWS CloudWatch, Visual Studio Diagnostic Tools

---

## ▶️ 시연 영상

[서로다른채팅방](https://github.com/user-attachments/assets/1d0586ef-4a7d-4af5-ab42-e74c615e3a07)

서로다른채팅방

> 💬 추가 시연 영상
> 

[일반 채팅](https://github.com/user-attachments/assets/bc99abc7-e3d9-41c1-8b06-03baf6d62534)

일반 채팅

[로그인실패 및 중복 로그인 방어](https://github.com/user-attachments/assets/6e91bd00-2db3-4877-8c28-746cf93d7fff)

로그인실패 및 중복 로그인 방어

---

## 🏛️ 서버 아키텍처 진화 및 성능 벤치마크

본 프로젝트는 처리량(TPS)을 극대화하기 위해 총 3단계에 걸쳐 아키텍처를 리팩토링 및 최적화했습니다.

### 🖥️ 테스트 환경

| 구분 | 사양 |
| --- | --- |
| **Server** | AWS EC2 c6i.xlarge (4 vCPU, 8GB RAM) × 1대, Windows Server 2022 |
| **Client** | AWS EC2 c6i.xlarge (4 vCPU, 8GB RAM) × 4대, Windows Server 2022 (각 2,500봇) |
| **Network** | 동일 리전, 동일 클러스터 배치 그룹 (Cluster Placement Group) |

> **아키텍처 진화 요약**
> 

> `v0 → v1` : 단일 큐의 락 경합 → **더블 버퍼링**으로 해결 (TPS 83% 향상)
> 

> `v1 → v2` : 단일 코어 한계 → **멀티스레드** 도입 (Mutex 병목 발견)
> 

> `v2 → v3` : Mutex 병목 → **Lock-Free**로 완전 해결 (142,400 TPS)
> 

### 🔹 v0 → v1: Single-Thread (단일 큐 → 더블 버퍼링)

- **Problem:** IOCP 스레드와 패킷 처리 스레드가 단일 패킷 큐(`mInComingPacketUserIndex`)를 공유하며 `std::mutex`로 보호 → 패킷 삽입/처리마다 락 경합(Lock Contention)이 발생하여 처리 지연 누적, 1,000명 이상에서 처리 스타베이션(Starvation) 발생
- **Solution:** 더블 버퍼링(Double Buffering) 기법 도입. 수신용 큐와 처리용 큐를 분리하고, 로직 스레드가 처리를 시작할 때 두 큐를 O(1) swap하여 락 경합을 원천 차단. IOCP I/O 워커 스레드도 4개 → 8개로 최적화.
- **Result:** 기존 단일 큐 대비 **초당 처리량(TPS) 83% 증가 및 평균 지연시간 76% 단축** 달성. 최대 1,500명 부하까지 안정적 방어.

> 
> 
> 
> ![v1. Single-Thread + Double Buffering Architecture.png](v1._Single-Thread__Double_Buffering_Architecture.png)
> 

#### 📊 단일 큐 vs 더블 버퍼링 성능 비교

| 부하 단계 | 핵심 지표 | 단일 큐 (Before) | 더블 버퍼링 (After) | 개선 |
| --- | --- | --- | --- | --- |
| **500명** | Chat RTT 평균 | 22.0 ms | **13.0 ms** | 📉 **41% 단축** |
|  | Chat RTT p50 | 16.8 ms | **11.0 ms** | 📉 **35% 단축** |
|  | Chat RTT p95 | 59.1 ms | **32.3 ms** | 📉 **45% 단축** |
| **1,000명** | Chat RTT 평균 | 349.7 ms | **83.7 ms** | 📉 **76% 단축** |
|  | Chat RTT p95 | 925.6 ms | **221.5 ms** | 📉 **76% 단축** |
|  | 초당 채팅 처리량 | 551 /sec | **1,010 /sec** | 📈 **83% 증가** |
| **1,500명** | Login 성공 | 1,000명 (한계) | **1,500명** | ✅ **Starvation 해결** |
|  | Room Enter 성공 | 1,000명 (한계) | **1,500명** | ✅ **전원 성공** |
|  | Login RTT 평균 | 처리 불가 | **105.2 ms** | ✅ **정상 처리** |
| **2,000명** | 초당 채팅 처리량 | 350.9 /sec | **405.6 /sec** | 📈 **16% 증가** |
|  | 성공한 총 채팅 수 | 123,599건 | **168,851건** | 📈 **37% 증가** |
|  | Chat RTT 평균 | 222.9 ms | **185.6 ms** | 📉 **17% 단축** |

### 🔹 v1 → v2: Multi-Thread (Mutex + CV)

- **한계 극복:** 1,500명 이상에서 발생하는 단일 코어의 한계(CPU 100% 및 처리 스타베이션)를 극복하기 위해 스레드 풀(Thread Pool) 도입.
- **이슈 발생:** 채팅 서버 특성상 브로드캐스트를 위한 공유 자원(Room, Session) 접근이 잦아 Mutex Lock/Unlock 과정에서 심각한 병목 현상 발생. p99 지연시간이 36.5ms까지 튀는 현상 확인.

> 
> 
> 
> ![v2. Multi-Thread + Mutex CV Architecture.png](v2._Multi-Thread__Mutex_CV_Architecture.png)
> 

### 🔹 v2 → v3: Multi-Thread (Lock-Free) ⭐️ 최종 아키텍처

- **구현:** Mutex를 완전히 제거하고 **CAS 연산 기반의 Lock-Free 큐와 Object Pool, Strand 패턴**을 직접 구현하여 적용. 패킷 전송 시마다 발생하던 잦은 동적 할당(new/delete)을 Lock-Free Object Pool로 대체하여 힙 메모리 단편화와 Lock 경합 병목을 동시에 해소.
- **최종 성과:** 2,000명 극한의 스트레스 테스트(Max TPS)에서 **초당 142,400 건의 패킷 처리를 무응답 에러 없이 달성**하며 동급 아키텍처 대비 압도적인 성능 증명.

> 
> 
> 
> ![v3. Multi-Thread + Lock-Free Architecture.png](v3._Multi-Thread__Lock-Free_Architecture.png)
> 

### 📊 아키텍처별 극한 부하 성능 비교 (2,000명 Stress Test)

초기 단일 스레드(Double Buffering) 아키텍처의 한계를 돌파하기 위해, 멀티 스레드 환경에서 **Mutex 방식과 Lock-Free 방식을 구현하고 극한의 브로드캐스트 부 상황에서 성능을 교차 검증**했습니다.

> 
> 
> 
> ![서버아키텍처성능비교분석.png](%EC%84%9C%EB%B2%84%EC%95%84%ED%82%A4%ED%85%8D%EC%B2%98%EC%84%B1%EB%8A%A5%EB%B9%84%EA%B5%90%EB%B6%84%EC%84%9D.png)
> 

**⚔️ 3가지 아키텍처 모델**

1. **v1. Single-Thread (Double Buffering):** 로직 처리를 단일 스레드가 전담하여 락 오버헤드가 없으나, 코어의 물리적 한계가 존재함.
2. **v2. Multi-Thread (Mutex + CV):** 스레드 풀을 도입했으나, 브로드캐스트 시 공유 자원(방, 세션) 접근으로 인한 Lock 경합(Contention) 발생.
3. **v3. Multi-Thread (Lock-Free) ⭐️ 최종:** CAS 연산 기반의 Lock-Free 글로벌 큐와 Strand 패턴을 결합하여 경합을 원천 차단.

**💡 분석**

- **Single-Thread의 붕괴:** 1,500명까지는 락 오버헤드가 없는 싱글 스레드가 훌륭한 효율을 보였으나, 2,000명 부하를 넘어서는 순간 **단일 코어(CPU 100%)의 병목으로 인해 초당 23만 건의 패킷이 Drop(무응답)**되는 붕괴 현상이 발생했습니다.
- **Mutex의 한계 (Lock Contention):** 멀티 스레드를 도입하여 붕괴는 막았으나, 채팅 서버 특성상 동일한 방(Room)에 접근하기 위해 락을 획득/해제하는 과정에서 병목이 발생하여 **p99 지연시간이 36.5ms까지 튀는 현상**을 확인했습니다.
- **Lock-Free의 압승:** 결과적으로 Lock-Free 아키텍처가 **초당 142,400건의 패킷을 25.4ms라는 매우 안정적인 지연 시간으로 완벽하게 처리**해 내며 대규모 트래픽에서 가장 우수한 아키텍처임을 증명했습니다.

---

## 🚀 10,000명 수용량 테스트 및 Zero-Leak 검증 (Capacity Test)

*서버의 장기 실행 안정성을 철저히 검증하기 위해, AWS EC2 클라이언트 4대(각 2,500봇)에서 10,000명의 더미 클라이언트가 실제 유저와 동일한 시나리오(로그인 → 방 입장 → 채팅 → 방 퇴장 → 재입장)를 3~10초 간격으로 반복하는 82분(1.38시간) 연속 부하 테스트를 진행했습니다.*

> 
> 
> 
> ![10000.gif](10000.gif)
> 

### 📊 1. 클라이언트 부하 테스트 지표 (82분 연속 실행)

| 측정 항목 | 결과 수치 | 상태 요약 |
| --- | --- | --- |
| **최대 동시 접속 (Peak Conn)** | **10,000 명** | 목표 수용량 달성 ✅ |
| **연결 실패 / 끊김 (Fail / Disconn)** | **0 건 / 0 건** | 82분간 단 1건의 유실도 없는 세션 안정성 ✅ |
| **평균 지연 시간 (Avg Latency)** | **15.82 ms** | 지연 없는 실시간 통신 방어 ✅ |
| **p99 지연 시간 (p99 Latency)** | **16.87 ms** (최대 31.49 ms) | 상위 1% 패킷도 30ms 내외로 안정적 처리 ✅ |
| **이상 현상 (Anomalies)** | **없음** | 무중단 서비스 검증 완료 |

### ⚙️ 2. 서버 내부 메모리 풀(Object Pool) 벤치마크 지표

1.38시간 동안 총 **약 740만 번의 작업(Job)이 처리**되었으나, Lock-Free 기반의 메모리 풀이 완벽하게 자원을 재활용하여 동적 할당 병목을 원천 차단했습니다.

| 서버 내부 지표 | 측정 값 | 의미 및 성과 |
| --- | --- | --- |
| **Alloc Total Attempts** | **7,383,090 회** | 1.38시간 동안 약 740만 개의 Job/Packet 처리 완료 |
| **Job Pool Alloc Fail** | **0 회** | Job Pool(10만 개) 고갈 없이 안정적 순환 ✅ |
| **Send Pool Alloc Fail** | **0 회** | Send 버퍼 풀 고갈 없음 ✅ |
| **Pool Free Count** | **100,000** (= Pool Size) | 테스트 종료 후 전량 반환 ➡️ **내부 객체 누수 0건 입증 ✅** |

### 🔍 3. 트러블슈팅: OS 레벨 교차 검증을 통한 Zero-Allocation 증명

위의 서버 내부 지표(`Pool Free Count = 100,000`)로 객체 누수가 없음을 확인했으나, AWS CloudWatch를 통한 OS 레벨 메모리 모니터링 중 흥미로운 현상을 관찰하고 분석했습니다.

```markdown
#### 🚨 Phase 1: 실제 메모리 누수 발견 및 수정
* ** 현상 :** 최초 10,000명 테스트에서 서버 메모리가 99%(6,680MB)를 점유하며 0.4MB/s씩 지속 증가
* **원인 분석:**
  1. **SendBuffer Pool 과잉 할당:** 봇당 400개씩 총 4,000,000개 할당 → 약 4.1GB 낭비
  2. **세션 버퍼 비대화:** `MAX_PACKET_DATA_BUFFER_SIZE`가 65,536으로 과도하게 설정
  3. **리소스 미반환:** 세션 종료 시 `Closed()`에서 `Clear()` 미호출로 Send큐 잔존
* **해결 및 결과:** Pool 크기(400→10) 및 버퍼 크기 최적화. `Clear()` 즉시 호출 적용. **결과적으로 시작 메모리를 2,372MB에서 372MB로 84% 절감하고 누수 완전 해소.**

#### 🏆 Phase 2: OS 레벨 교차 검증을 통한 Zero-Allocation 최종 증명
* **현상:** Phase 1 수정 후 메모리가 371.9MB에서 안정화되었으나, 10,000명 접속 해제 후에도 메모리가 떨어지지 않는 현상 발견
* **가설 및 프로파일링:** VS 프로파일러 힙 스냅샷 분석 결과, 접속 전/후 할당 증가량 `+0 Bytes` 확인
* **결론:** 버그가 아닌 의도된 최적화. Lock-Free Object Pool이 해제된 세션을 OS에 반납하지 않고 캐싱(Pre-allocation)하고 있었음.
* **최종 성과:** 1만 명 유저가 740만 번 통신하는 극한 환경에서도 **동적 할당 오버헤드 없이 371.9MB 고정 메모리로 무중단 Zero-Allocation 서비스 입증.**
```

#### Phase 1: 실제 메모리 누수 발견 및 수정

- **현상**: 최초 10,000명 테스트에서 서버 메모리가 **99%(6,680MB)**를 점유하며 **0.4MB/s**씩 지속 증가
- **원인 분석**:
    1. **SendBuffer Pool 과잉 할당**: 봇당 400개씩 총 4,000,000개 할당 → 약 4.1GB 낭비
    2. **세션 버퍼 비대화**: `MAX_PACKET_DATA_BUFFER_SIZE`가 65,536으로 과도하게 설정
    3. **세션 종료 시 리소스 미반환**: `Closed()`에서 `Clear()` 미호출로 Send큐 잔존
- **해결**:
    1. Pool 크기를 봇당 **400 → 10**으로 최적화
    2. 버퍼 크기를 **65,536 → 8,192**로 조정
    3. `Closed()` 호출 시 `Clear()`로 세션 리소스 즉시 반환
- **결과**: 시작 메모리 **2,372MB → 372MB**로 **84% 절감**, 0.4MB/s 누수 완전 해소

#### Phase 2: OS 레벨 교차 검증을 통한 Zero-Allocation 최종 증명

- **현상**: Phase 1 수정 후 메모리가 371.9MB에서 안정화되었으나, **10,000명 접속 해제 후에도 메모리가 떨어지지 않는 현상** 발견
- **가설 및 프로파일링**: C++ 고질적 문제인 메모리 누수(Memory Leak)를 의심하여 **VS 프로파일러 힙 스냅샷(Heap Snapshot)** 분석 진행. 10,000명 접속 전/후 스냅샷을 비교한 결과, 할당된 메모리의 증가량은 **+0 Bytes**로 확인.
- **결론**: 버그가 아닌 의도된 최적화 — **Lock-Free Object Pool이 해제된 세션/버퍼를 OS에 반납하지 않고 다음 접속을 위해 캐싱(Pre-allocation)**하고 있었음. C++ CRT 힙 할당자의 정상 동작.
- **최종 성과**: 1만 명의 유저가 740만 번의 통신을 주고받는 극한의 환경에서도, 추가적인 동적 할당(`new`/`delete`) 오버헤드 없이 **371.9MB의 고정된 메모리로 무중단 Zero-Allocation 서비스가 가능함**을 입증

> 
> 
> 
> ![cloudwatch3시간.png](cloudwatch3%EC%8B%9C%EA%B0%84.png)
> 

---

## 🛠️ Technical Challenges

### Challenge 1: TCP Stream Packet 경계

- **Problem**: TCP는 스트림 기반 프로토콜로 패킷 경계가 없어서, 여러 패킷이 합쳐지거나 하나의 패킷이 분할되어 수신될 수 있음. 이로 인해 패킷 조립 시 데이터 손실이나 잘못된 파싱이 발생할 위험
- **Approach**: 고정 길이 헤더 구조체를 설계하고, 링버퍼를 도입하여 스트림 데이터를 안전하게 버퍼링
- **Solution**: `User::GetPacket()`에서 헤더 peek → 전체 길이 확인 → 정확한 바이트 수만큼 read

### Challenge 2: 다중 쓰레드에서 Ring Buffer 접근

- **Problem**: 네트워크 쓰레드(IOCP Worker)에서 `User::SetPacketData()`로 데이터 쓰기와 패킷 처리 쓰레드에서 `User::GetPacket()`으로 데이터 읽기가 동시에 발생하여 데이터 race condition 발생
- **Approach**: Mutex 추가로 쓰레드 안전성 확보
- **Solution**: `User` 클래스에 `mPacketRingBuffMutex` 추가, `SetPacketData()`, `GetPacket()`, `Clear()` 메서드를 동일한 뮤텍스로 보호

### Challenge 3: 비동기 이벤트 처리 중 발생하는 상태 불일치 문제

- **Problem**: I/O 쓰레드가 작업을 생성한 후 큐에 넣기 전 사이에 다른 쓰레드가 사용자의 상태를 변경하면 ProcessPacket 쓰레드엔 무효화된 작업이 들어가게 됨
- **Approach**: Client 객체의 생명 주기를 추적할 수 있도록 Generation Token 도입으로 상태 검증
- **Solution**: User 클래스에 Generation Token을 도입하여 패킷 처리 작업을 생성할 때 당시의 token 값을 함께 기록. 큐에서 꺼내어 작업할 때 token 값을 비교하여 값이 다를 경우 해당 패킷을 무효화 처리

### Challenge 4: Graceful Shutdown 구현

- **Problem**: 서버 강제 종료 시 진행 중인 I/O와 DB 작업이 유실되고, 리소스가 정리되지 않아 데이터 손실과 메모리 누수가 발생
- **Approach**: 5단계 순차 종료(Accept 차단 → 클라이언트 킥 + CancelIoEx → I/O Draining → PQCS 워커 종료 → 리소스 정리)하고, DB/Redis 쓰레드는 queue draining 후 종료
- **Solution**: `DestroyThread()`를 5단계로 구성, MySQL/Redis의 `TaskProcessThread()`를 빈 큐 확인 패턴으로 변경하여 잔여 작업을 모두 처리한 뒤 종료하도록 구현. 추가로 `SetConsoleCtrlHandler`로 Ctrl+C 및 콘솔 종료도 Graceful Shutdown으로 구현

### Challenge 5: AcceptEx 빈 세션 탐색 방식의 비효율

- **Problem**: AccepterThread가 빈 세션을 찾기 위해 매번 전체 10,000개를 O(N) 선형 탐색하며, 이미 AcceptEx가 걸린 세션에 중복 호출하여 소켓 누수 발생 가능성 존재
- **Approach**: FreeList를 도입하여 O(1) Pop/Push로 빈 세션을 관리하고, 서버 시작 시 100개만 미리 AcceptEx를 걸어둔 뒤 워커 스레드가 완료 시 1개씩 보충하는 방식으로 변경
- **Solution**: AccepterThread를 제거하고 `PopFreeSessionIndex()`/`PushFreeSessionIndex()`로 세션을 관리하며 ACCEPT 완료 시 워커 스레드가 자동으로 AcceptEx를 보충하도록 구현. 커널에는 항상 ~100개의 대기 소켓만 유지

---

## ⚙️ 빌드 및 모드 전환 방법

본 프로젝트는 `#define` 매크로를 통해 내부 아키텍처를 쉽게 전환하며 테스트할 수 있습니다.

```cpp
// Config.h (또는 해당 헤더)
#define USE_LOCK_FREE_ARCH  // Lock-Free 모드 (기본값, 최고 성능)
// #define USE_MUTEX_ARCH   // Mutex 모드 (성능 비교용)
```

## 🔮 향후 개선 방향

- [x]  ~~std::function 기반 패킷 핸들러로 리팩토링~~
- [x]  ~~MySQL 연동하여 사용자 활동 로그 기록~~
- [x]  ~~더미 클라이언트 테스트~~
- [x]  ~~디버깅을 위한 Dump추가~~
- [x]  ~~좀비 세션 감지 로직 추가~~
- [x]  ~~Lock-Free 구현하고 적용하기~~
- [x]  ~~Process 처리 쓰레드를 단일 -> 멀티 쓰레드로 확장~~
- [x]  ~~10,000명 동시접속 수용량 테스트 및 Zero-Leak 검증~~
- [ ]  멀티서버 구조 확장

## 👨‍💻 개발자

- **이름**: 강형우
- **연락처**: [rkdguddn21@gmail.com](mailto:rkdguddn21@gmail.com)
- **GitHub**: [https://github.com/kanghyoungwoo](https://github.com/kanghyoungwoo)