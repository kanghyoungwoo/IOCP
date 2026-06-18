[← README로 돌아가기](../README.md)

# Lock-Free 동시성 검증 (Relacy Race Detector)

> **한 줄 요약**
> 서버의 Lock-Free 자료구조 5종을 [Relacy Race Detector](https://github.com/dvyukov/relacy)로 검증했고, 그 과정에서 **`LockFreeStack`의 실제 데이터 레이스(C++ UB)를 발견·수정**했습니다.

| 항목 | 내용 |
|------|------|
| **검증 대상** | LockFreeStack, SPSCQueue, RingBuffer, MPSCQueue, GlobalQueue(MPMC) |
| **검증 도구** | Relacy Race Detector (스레드 인터리빙 탐색) |
| **검증 방식** | 음성 대조군 — 정상 버전 + 메모리 오더를 일부러 약화시킨 결함 버전을 함께 빌드 |
| **발견한 버그** | LockFreeStack `poolNext` 비원자 접근 → 데이터 레이스 (수정 완료) |
| **소스 위치** | [`IOCP/RelacyTests/`](../RelacyTests/) |

---

## 1. 검증 방식 — 음성 대조군 (Negative Control)

"정상 버전이 통과했다"만으로는 약합니다. 테스트 환경 자체가 버그를 잡을 능력이 있는지 알 수 없기 때문입니다.

그래서 각 자료구조마다 **두 가지 버전**을 함께 빌드했습니다.

```
① 정상 버전        → 통과해야 함 (PASS 기대)
② 결함 버전        → release 메모리 오더를 일부러 relaxed로 약화
                     → 반드시 DATA RACE가 잡혀야 함 (FAIL 기대)
```

결함 버전에서 Relacy가 정확히 데이터 레이스를 잡아낸다면, 이는 두 가지를 동시에 증명합니다.

1. **검증 환경이 실제로 버그를 잡을 수 있다** (대조군 역할)
2. **정상 버전의 그 메모리 오더가 꼭 필요하다** (없으면 깨지므로)

---

## 2. 검증 결과 요약

| 자료구조 | 테스트 케이스 | 기대 | 결과 | 판정 |
|:---|:---|:---:|:---:|:---:|
| **LockFreeStack** | 정상 (수정 후) | Pass | 20만 iter 통과 | 🟢 |
| | `RACE_BUG` — poolNext 비원자 | Race | 🔴 DATA RACE (iter 1) | 🟢 |
| **SPSCQueue** | 정상 | Pass | 100만 iter 통과 | 🟢 |
| | `WEAKEN_PUB` — 발행 오더 약화 | Race | 🔴 DATA RACE | 🟢 |
| | `WEAKEN_RECLAIM` — 반납 오더 약화 | Race | 🔴 DATA RACE | 🟢 |
| **RingBuffer** | 정상 | Pass | 100만 iter 통과 | 🟢 |
| | `WEAKEN_PUB` — 발행 오더 약화 | Race | 🔴 DATA RACE | 🟢 |
| | `WEAKEN_RECLAIM` — 반납 오더 약화 | Race | 🔴 DATA RACE | 🟢 |
| **MPSCQueue** | 정상 | Pass | 200만 iter 통과 | 🟢 |
| | `WEAKEN_LINK` — 노드 연결 발행 약화 | Race | 🔴 DATA RACE | 🟢 |
| | `VIOLATE_SC` — 단일 Consumer 불변식 위반 | Race | 🔴 DATA RACE | 🟢 |
| **GlobalQueue (MPMC)** | 정상 | Pass | 300만 iter 통과 | 🟢 |
| | `WEAKEN_PUB` — 발행 오더 약화 | Race | 🔴 DATA RACE | 🟢 |

> [!TIP]
> 정상 버전은 전부 통과했고, 일부러 약화시킨 결함 버전은 전부 데이터 레이스가 잡혔습니다. 즉 **"통과한 정상 버전의 메모리 오더링이 실제로 정확하다"** 는 것이, 결함 버전의 실패를 통해 역으로 증명됩니다.

---

## 3. 핵심 성과 — 실제 버그 발견 및 수정

> [!IMPORTANT]
> 이 검증의 가장 큰 수확은 `LockFreeStack`에서 **실재하던 C++ 미정의 동작(UB)을 찾아내 수정**한 것입니다.

### 문제: `PopBatch`와 `Push` 간 `poolNext` 동시 접근 (`RACE_BUG`)

`LockFreeStack`은 풀(pool) 인덱스를 `poolNext`라는 링크 필드로 연결합니다. 처음 구현에서 `poolNext`는 일반 `uint32_t` 였습니다.

```cpp
uint32_t poolNext = UINT32_MAX;   // 수정 전 — 비원자
```

`PopBatch`는 여러 노드를 한 번에 꺼내기 위해 체인을 따라가며 `poolNext`를 **순서대로 읽습니다.** 그런데 바로 그 순간, 다른 스레드가 같은 노드를 `Push`하며 `poolNext`에 **쓰기**를 할 수 있습니다.

```
스레드 A (PopBatch)         스레드 B (Push)
─────────────────          ──────────────
poolNext 읽기  ←─────────→  poolNext 쓰기     ⚠️ 같은 비원자 변수 동시 접근 = 데이터 레이스
```

세대(generation) 기반 CAS가 뒤에서 올바르게 막아주기 때문에 **논리적 결과는 틀리지 않습니다.** 하지만 C++ 메모리 모델에서 **비원자 변수에 대한 동시 읽기/쓰기 자체가 이미 UB**입니다. 컴파일러 최적화나 특정 하드웨어에서 언제든 실제 버그로 발현될 수 있는, 눈에 잘 안 띄는 결함입니다.

Relacy는 이를 **첫 번째(iter 1)에서 즉시** 잡아냈습니다.

### 수정: `std::atomic`으로 전환

```cpp
std::atomic<uint32_t> poolNext{UINT32_MAX};   // 수정 후 — 원자
```

`poolNext` 접근은 모두 `load/store(memory_order_relaxed)`로 변경했습니다. 순회 순서의 안전성은 이미 `m_head`의 CAS(`acquire`/`release`)가 보장하므로, `poolNext` 자체는 `relaxed`로 충분합니다.

이 수정은 `poolNext`를 링크로 쓰는 **5개 파일**에 일괄 적용했습니다.

| 파일 | 적용 대상 |
|------|-----------|
| `LockFreeStack.h` | Push / Pop / PopBatch의 poolNext 접근 |
| `Define.h` | `SendOverlappedEx::poolNext` |
| `IOCP.h` | `SessionNode::poolNext` (+ vector 재할당용 이동 생성자) |
| `PacketJob.h` | `PacketJob::poolNext` |
| `Room.h` | `Room::poolNext` |

수정 후 `LockFreeStack` 정상 버전은 20만 인터리빙을 데이터 레이스 없이 통과했습니다.

---

## 4. 컴포넌트별 상세

### 4.1 SPSCQueue / RingBuffer (단일 생산자 - 단일 소비자)

생산자가 데이터를 쓴 뒤 인덱스를 갱신하고(`release`), 소비자가 인덱스를 읽은 뒤(`acquire`) 데이터를 읽는 구조입니다. 두 지점의 `release`/`acquire` 짝이 가시성을 보장합니다.

- **`WEAKEN_PUB`**: 발행(쓰기 후 인덱스 갱신)을 `relaxed`로 약화 → 소비자가 **아직 쓰이지 않은 데이터**를 읽음 → 데이터 레이스
- **`WEAKEN_RECLAIM`**: 반납(읽기 후 인덱스 갱신)을 `relaxed`로 약화 → 슬롯 재사용 시점 동기화가 깨짐 → 데이터 레이스

### 4.2 MPSCQueue (다중 생산자 - 단일 소비자)

여러 생산자가 `m_tail`을 원자적으로 교환(`exchange`)해 노드를 잇고, 단일 소비자가 `m_head`를 따라 소비합니다.

- **`WEAKEN_LINK`**: 노드 연결 발행(`prev->next.store`)을 `release` → `relaxed`로 약화 → 소비자가 **연결은 보이지만 payload는 아직 안 보이는** 노드를 읽음 → 데이터 레이스
- **`VIOLATE_SC`**: 이 큐는 **"소비자는 단 하나"** 라는 불변식을 전제로 `m_head`를 비원자로 둡니다. 이 케이스는 일부러 **소비자 2개가 동시에 `Pop()`을 호출**하도록 만들어, 비원자 `m_head`에 동시 접근이 발생 → 데이터 레이스. 코드 주석에 적힌 "다중 소비자는 안전하지 않다"는 불변식이 **빈말이 아님을 실제로 증명**한 케이스입니다.

### 4.3 GlobalQueue (다중 생산자 - 다중 소비자, MPMC)

가장 경합이 심한 환경입니다. 각 슬롯이 시퀀스 번호를 가지는 Vyukov bounded MPMC 링 구조로, 300만 인터리빙을 무손실·무중복으로 통과했습니다.

- **`WEAKEN_PUB`**: 슬롯 시퀀스 발행을 `relaxed`로 약화 → 소비자가 미발행 데이터를 읽음 → 데이터 레이스

---

> [!NOTE]
> 세마포어 같은 OS 대기 메커니즘은 Relacy 모델 범위 밖이라, 큐 로직만 분리해 검증했습니다. 대기 동작은 통합 테스트로 확인합니다.

## 6. 결론

1. **메모리 오더링에 대한 이해**
   `std::atomic`을 쓰는 것에서 그치지 않고, `acquire`/`release` 짝이 *왜* 필요한지를 음성 대조군으로 증명했습니다. 각 메모리 오더가 빠지면 정확히 어떤 데이터 레이스가 생기는지를 도구로 재현했습니다.

2. **간헐적 동시성 버그를 잡는 검증 파이프라인**
   일반 단위 테스트가 놓치는 희귀 인터리빙 버그를, 상태 공간 탐색 도구로 사전에 잡아내는 방법을 직접 구축했습니다. 실제로 운영 중 발현될 수 있던 `LockFreeStack`의 UB를 이 방식으로 제거했습니다.

---

## 부록 — 재현 방법

```bat
:: IOCP/RelacyTests/ 에서
build_and_run.bat
```

- MSVC 2022 + C++20 필요
- 정상/결함 버전 14개 빌드를 자동으로 컴파일·실행
- `relacy_lib/`는 [dvyukov/relacy](https://github.com/dvyukov/relacy) 클론

| 파일 | 검증 대상 |
|------|-----------|
| `lockfreestack_race_test.cpp` | LockFreeStack |
| `spsc_queue_test.cpp` | SPSCQueue |
| `ringbuffer_test.cpp` | RingBuffer |
| `mpsc_queue_test.cpp` | MPSCQueue |
| `globalqueue_mpmc_test.cpp` | GlobalQueue(MPMC) |
| `build_and_run.bat` | 전체 빌드 + 실행 자동화 |
