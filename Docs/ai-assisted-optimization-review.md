# AI 활용 최적화 회고: SharedSendBuffer 도입과 실패, 그리고 교훈

## 1. 배경

IOCP 기반 채팅 서버의 브로드캐스트 경로를 최적화하는 과정에서 AI(Claude, Gemini)를 적극 활용했다.
목표는 대규모 방(200~500명)에서 채팅 메시지를 브로드캐스트할 때의 성능을 개선하는 것이었다.

### 브로드캐스트 구조

```
유저가 채팅 전송 → Room::NotifyChat() → Room::SendToAllUser()
→ 방 내 모든 유저에게 SendMsg() 호출 (N-1번 반복)
→ 각 호출마다 SendOverlappedEx를 풀에서 Alloc → 데이터 복사 → WSASend
```

200명 방에서 메시지 1개 → **199번의 SendOverlappedEx 할당 + 199번의 CopyMemory**가 발생한다.

---

## 2. AI의 제안: SharedSendBuffer 도입

### 2-1. Claude의 분석

Claude에게 브로드캐스트 경로의 비효율성을 분석하도록 요청했다.
Claude는 다음을 지적했다:

> "200명 방에서 브로드캐스트 시, 동일한 패킷 데이터를 199번 복사하는 것은 비효율적이다.
> 데이터를 한 번만 직렬화하고 참조 카운트로 공유하면 메모리 복사를 제거할 수 있다."

제안된 구조:

```cpp
struct SharedSendBuffer {
    char data[MAX_SOCKBUF];
    uint32_t dataSize;
    std::atomic<int> refCount{0};   // 수신자 수만큼 설정
    uint32_t poolNext = UINT32_MAX;
};
```

- `SendToAllUser`에서 SharedSendBuffer를 1개 할당, 데이터를 1번만 직렬화
- 각 수신자의 SendOverlappedEx는 `pShared` 포인터로 공유 버퍼를 참조
- 전송 완료(SendComplete) 시 `refCount`를 감소시키고, 0이 되면 SharedSendBuffer를 풀에 반납

### 2-2. Gemini 교차 검증

Gemini에게 동일한 코드와 Claude의 제안을 보여주고 교차 검증을 요청했다.
Gemini도 SharedSendBuffer 접근법에 동의하며, 참조 카운트 기반 공유 버퍼가
브로드캐스트 성능을 개선할 것이라고 평가했다.
```text
1. 병목 지점 분석 (Bottleneck Analysis) 평가
평가: 매우 정확함

WSASend 반복 호출 및 커널 전환 비용: 멀티캐스트/브로드캐스트를 지원하는 채팅 서버에서 가장 흔하게 발생하는 전형적인 병목을 정확히 짚어냈습니다. TCP 스택 처리와 User-Kernel 모드 전환이 16 vCPU의 90%를 갉아먹고 있다는 분석은 타당합니다.

Strand 직렬화 및 메모리 복사 중복: Strand 내부에서 I/O 작업을 동기적으로 499번 호출하게 되면, 해당 방의 다른 상태 변경(입장/퇴장/다음 메시지 처리)이 완전히 블로킹됩니다. 이를 병목으로 식별한 것은 아키텍처의 흐름을 잘 이해하고 있다는 증거입니다.

2. 단기 개선 방향 (Short-term) 비평
① Broadcast용 공유 버퍼 (Shared Buffer) - 훌륭한 접근

비평: 할당 및 복사 비용을 O(N)에서 O(1)로 줄이는 가장 확실한 방법입니다. 단일 연속 배열(Contiguous Array)로 관리되는 메모리 풀 환경에서 이 공유 버퍼를 할당받아 사용한다면 단편화 없는 최고의 효율을 낼 수 있습니다.

주의점: 참조 카운트 기반 구조에서는 메모리 해제 시점이 매우 중요합니다. 모든 클라이언트의 Pending I/O가 0이 되고 커널로부터 완료 통지를 모두 받았을 때만 풀로 반환되어야 이중 해제(Double-free)나 잘못된 메모리 참조를 막을 수 있습니다.
```
**두 AI의 의견이 일치**했으므로 구현을 진행함.

---

## 3. 구현 내용

### 변경 사항

| 항목 | 기존 코드 | 개선 코드 |
|------|-----------|-----------|
| 브로드캐스트 데이터 처리 | 수신자마다 CopyMemory | SharedSendBuffer 1회 직렬화 + 포인터 참조 |
| SendMsg 오버로딩 | `SendMsg(UINT32, char*)` 1개 | + `SendMsg(SharedSendBuffer*)` 추가 |
| 메모리 해제 | `mSendPool->Free()` 직접 호출 | `FreeSendOverlapped()` 중앙 반납 함수 (refCount 관리 포함) |
| SendPool 크기 | `maxClientCount × 20` | `maxClientCount × 64` |
| SharedSendBuffer Pool | 없음 | `maxClientCount × 2` 추가 |

### SendMsg(SharedSendBuffer*) 핵심 로직

```cpp
bool ClientSession::SendMsg(SharedSendBuffer* pShared)
{
    auto pSendOvl = mSendPool->Alloc();
    
    // 참조 카운트 증가
    pShared->refCount.fetch_add(1, std::memory_order_relaxed);
    
    // 공유 버퍼를 직접 참조 (복사 없음)
    pSendOvl->base.wsaBuf.buf = pShared->data;
    pSendOvl->pShared = pShared;
    
    // WSASend 큐에 등록...
}
```

### FreeSendOverlapped 중앙 반납 함수

```cpp
void ClientSession::FreeSendOverlapped(SendOverlappedEx* pOvl)
{
    if (pOvl->pShared != nullptr)
    {
        // 마지막 수신자가 완료하면 SharedSendBuffer 반납
        if (pOvl->pShared->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            mSharedPool->Free(pOvl->pShared);
        pOvl->pShared = nullptr;
    }
    mSendPool->Free(pOvl);
}
```

---

## 4. AWS 부하 테스트

### 환경

- 서버: c6i.4xlarge (16 vCPU, 32GB RAM), Windows Server 2022
- 클라이언트: 4 × c5.xlarge Spot, 각 2500봇 (총 10,000 유저)
- 네트워크: Cluster Placement Group, 동일 AZ

### 시나리오별 결과

| 시나리오 | 코드 버전 | SendPool Fail | lat_avg (steady) | 안정성 |
|----------|-----------|---------------|-------------------|--------|
| 100방×100명 | 개선 | 0 | ~25ms | ✅ 안정 |
| 50방×200명 | **기존** | 0 | **15-17ms** | **✅ 안정** |
| 50방×200명 | 개선 | 14,036,441 | 200-10,000ms | ❌ Death Spiral |
| 20방×500명 | 기존 | 0 | 55-137ms | ❌ 100초 후 크래시 |
| 20방×500명 | 개선 | 345,000,000+ | 200-2,000ms | ❌ Death Spiral |

### 핵심 발견

**50방×200명 시나리오에서 기존 코드가 개선 코드보다 압도적으로 안정적이었다.**

- 기존 코드: lat_avg 15-17ms, SendPool Fail 0, 311초 동안 완전 안정
- 개선 코드: lat_avg 수백~수천ms, SendPool Fail 1400만, Death Spiral 진입

---

## 5. 원인 분석

### 5-1. SharedSendBuffer의 atomic refCount 경합

200명 방에서 1회 브로드캐스트 시:

```
fetch_add × 199회 (SendMsg 할당 시)
fetch_sub × 199회 (SendComplete 완료 시)
= 398번의 atomic Read-Modify-Write 연산
```

이 398번의 atomic 연산은 **1개의 캐시 라인**을 6개 IOCP 워커 스레드가 동시에 경쟁한다.

```
기존: 199번 CopyMemory(200바이트)  ≈ ~2μs,     스레드 간 경합 = 0
개선: 398번 atomic 경합             ≈ ~40-120μs, 스레드 간 경합 = 심각
```

**절감한 복사 비용(2μs)보다 추가된 atomic 경합(40μs+)이 20배 이상 비쌌다.**

### 5-2. 커널 레벨 공유 메모리 문제

```
기존: 각 WSASend가 독립 버퍼(pSendOvl->buffer) 참조 → 커널이 병렬 처리 가능
개선: 199개 WSASend가 같은 pShared->data 참조 → 커널 TCP 스택 내부 직렬화 가능성
```

### 5-3. SharedSendBuffer 수명 연장

```
기존: 빠른 세션은 즉시 SendOverlappedEx 반납 → 풀 즉시 회수
개선: 가장 느린 세션이 완료될 때까지 SharedSendBuffer 점유
      → 200명 중 1명만 느려도 전체 공유 버퍼의 수명이 연장
```

### 5-4. 풀 크기 변경의 부작용 (×20 → ×64)

기존 코드의 작은 풀(×20)은 **암묵적 백프레셔** 역할을 했다:

```
기존: pool = 10,000 × 20 = 200,000개
      → 세션당 평균 ~20개 이상 누적 시 Alloc 실패 → 자동 드랍
      → 느린 세션에 대한 자연스러운 유량 제한

개선: pool = 10,000 × 64 = 640,000개
      → 세션당 64개까지 누적 허용 → 큐 깊이 3배 증가 → 레이턴시 악화
```

---

## 6. 교정: Per-Session 백프레셔 도입

### 6-1. 설계

기존 코드의 암묵적 백프레셔를 명시적으로 구현:

```cpp
// ClientSession.h
std::atomic<int> mSendQueueDepth{0};

// ClientSession.cpp - SendMsg 두 버전 모두
if (mSendQueueDepth.load(std::memory_order_relaxed) >= BACKPRESSURE_THRESHOLD)
    return true;  // 드랍

// Alloc 성공 후
mSendQueueDepth.fetch_add(1, std::memory_order_relaxed);

// FreeSendOverlapped에서
mSendQueueDepth.fetch_sub(1, std::memory_order_relaxed);
```

### 6-2. 백프레셔 적용 후 결과 (50방×200명)

| 지표 | 적용 전 | 적용 후 |
|------|---------|---------|
| SendPool Alloc Fail | 14,036,441 | **0** |
| Job Pool Alloc Fail | 613,331 | **0** |
| 서버 비정상 종료 | Death Spiral | **없음** |
| lat_avg (steady) | 200-10,000ms | 900-3,000ms (진동) |

풀 소진은 완전히 해결되었지만, 레이턴시는 기존 코드(15-17ms) 대비 여전히 50-200배 높다.

### 6-3. 잔여 원인

백프레셔 임계값이 `MAX_GATHER_COUNT = 64`로 설정되어, 기존 코드의 유효 한도(~20)보다 3배 높다.
임계값을 16~20으로 낮추면 기존 코드 수준의 레이턴시에 근접할 가능성이 있다.

---

## 7. 교훈

### 7-1. AI 교차 검증의 한계

- Claude와 Gemini 모두 SharedSendBuffer 도입에 동의했다
- 두 AI 모두 **이론적 복잡도 분석**에서는 정확했다 (O(N) 복사 → O(1) 참조)
- 그러나 **실제 하드웨어 수준의 비용** (캐시라인 경합, atomic 연산 지연, 커널 동작)은 고려하지 못했다
- **AI의 의견이 일치해도 실측 검증을 대체할 수 없다**

### 7-2. 이론 vs 실측

```
이론:  CopyMemory 199번 제거 → 성능 향상
실측:  atomic 경합 398번 추가 → 성능 하락

이론:  풀 크기 3배 확대 → 여유 확보
실측:  큐 깊이 3배 증가 → 레이턴시 악화

이론:  공유 버퍼 → 메모리 효율
실측:  수명 연장 → 리소스 점유 증가
```

### 7-3. "단순함"의 가치

기존 코드의 "각 수신자에게 독립 복사" 방식은:
- 공유 상태가 0 → 스레드 간 경합 없음
- 각 SendOverlappedEx가 완전 독립 → 완벽한 병렬성
- 빠른 세션은 즉시 반납 → 풀 회전율 최대화

**멀티스레드 환경에서는 "공유 상태 최소화"가 "복사 최소화"보다 중요하다.**

### 7-4. AI의 올바른 활용 방식

이번 경험에서 AI가 실제로 도움이 된 부분:

1. **문제 진단**: 풀 소진 → Death Spiral 메커니즘 분석
2. **테스트 계획 수립**: AWS 환경 설계, config 계산, 예상 처리량 산출
3. **테스트 결과 분석**: metrics.csv 데이터 해석, 시나리오 간 비교
4. **백프레셔 설계**: 기존 코드의 암묵적 백프레셔를 명시적으로 전환
5. **원인 추적**: 기존 코드와의 구조적 차이에서 성능 차이의 근본 원인 도출

AI가 부족했던 부분:

1. **하드웨어 수준 비용 예측**: 캐시라인 경합, NUMA, 커널 내부 동작
2. **최적화 방향 판단**: "뭘 최적화해야 하는가"의 판단 (복사 vs 경합)
3. **풀 크기 결정**: ×64가 암묵적 백프레셔를 제거한다는 부작용 예측 실패

---

## 8. 현재 상태와 향후 방향

### 현재 상태

- SharedSendBuffer 코드는 유지 중 (제거 비용 대비 백프레셔로 안정화)
- Per-Session 백프레셔가 풀 소진을 완전히 방지
- 레이턴시는 기존 코드 대비 높지만 서버 안정성은 확보

### 향후 개선 옵션

1. **백프레셔 임계값 튜닝**: 64 → 16~20으로 낮추어 기존 코드 수준 레이턴시 달성
2. **SharedSendBuffer 제거**: 기존 방식(독립 복사)으로 복원 + 명시적 백프레셔 유지
3. **풀 크기 복원**: ×64 → ×20으로 되돌림 (명시적 백프레셔가 있으므로 안전)
