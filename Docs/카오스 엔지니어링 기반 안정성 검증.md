[← README로 돌아가기](../README.md)

# 카오스 엔지니어링 기반 안정성 검증

Lock-Free 아키텍처와 Strand 패턴의 무결성을 검증하기 위해, 자체 제작 Chaos Bot System으로 3단계 스트레스 테스트를 진행했습니다. 단순한 처리량(TPS) 측정을 넘어, Lock-Free의 주요 취약점(ABA, Data Race, 좀비 세션)에 대한 방어 로직을 테스트 범위 내에서 검증했습니다.

## 3대 시나리오 검증 요약

| 시나리오 | 테스트 내용 | 결과 |
|----------|-----------|------|
| **A. ABA 오버플로우 방어** | 30분간 570만 개(770MB) 패킷 I/O 폭격 | 메모리 오염 및 Crash 0건 |
| **B. Strand Race 방어** | 공유 자원(Room) 동시 접근 버스트 775회 | Data Race 0건 |
| **C. 좀비 및 단편화 공격** | 1바이트 단편화 83,000건 + RST 강제 종료 150건 | 조립 에러 0건, 좀비 세션 잔류 0건 |

---

## Scenario A: Lock-Free 메모리 무결성 및 ABA 오버플로우 검증

- **Test:** 30분간 950개 이상의 세션이 무차별적으로 접속/해제를 반복하며 570만 개(770MB)의 패킷 I/O 폭격 수행.
- **Result:** `ABA Overflow 0건`, `메모리 누수 0건`, `서버 크래시 0건`
- **Insight:** Lock-Free Object Pool의 ABA(주소 재사용 오염) 문제를 **Generation(세대) 검증과 RefCount(참조 카운트) 기반의 안전한 메모리 반납 로직**으로 방어.

<details>
<summary><b>Scenario A: ABA 오버플로우 검증 테스트 로그</b></summary>

```text
=============================================================
  CHAOS BOT - Statistics (1800.5 sec elapsed)
=============================================================
  [Connection]
    Attempts: 951  Success: 951  Failed: 0
    Disconnects: 951  Hard Close(RST): 0

  [Packet I/O]
    Sent: 5708391 (772.93 MB)  Recv: 5707511 (38.10 MB)
    Send Errors: 0
    Throughput: 3170 pkt/s sent, 3170 pkt/s recv

  *** VULNERABILITY DETECTION ***
    [A] ABA Overflow:     0
    [B] Strand Race:      0
    [C] Zombie Race:      0
    Server Crash:         0
=============================================================
```
</details>

---

## Scenario B: Multi-Thread 논리적 경합 (Strand Race) 검증

- **Test:** 200개의 봇이 120초 동안 의도적으로 패킷 파이프라인 버스트(775회)를 일으키며, 동시다발적으로 방 입장/퇴장 및 채팅 도배 요청(Data Race 유발).
- **Result:** `Strand Race 0건`, `비정상 패킷 차단(Fail) 1,175건`
- **Insight:** 수백 개의 스레드가 동일한 Room 자원에 동시 접근하려 했으나, StrandProcessor를 통한 작업 직렬화(Serialization)가 동작하여 동기화 오류를 방지. 비정상적인 상태 전이 요청은 입구에서 즉시 차단(Disconnect)하여 서버 로직을 보호.

<details>
<summary><b>Scenario B: Strand Race 검증 결과</b></summary>

<img width="530" height="831" alt="KakaoTalk_20260413_150714205" src="https://github.com/user-attachments/assets/7e4b8497-7914-432e-9130-9b8e96b24eeb" />
</details>

---

## Scenario C: 악성 네트워크 공격 및 좀비 세션 검증

- **Test:** 83,903개의 모든 통신 패킷을 1바이트 단위로 조각내어 전송(TCP 단편화)하고, 정상 통신 중 강제 RST(Hard Close) 150회 시도.
- **Result:** `Zombie Race 0건`, `패킷 조립 에러 0건`
- **Insight:** 1바이트 단위 TCP 단편화 테스트에서도 RingBuffer의 패킷 경계 파싱 로직이 정상 동작. RST 강제 종료 시 `CancelIoEx`와 내부 Task Queue를 활용한 좀비 세션 정리(Cleanup) 로직이 정상 작동하여, 리소스 낭비 없이 방어.

<details>
<summary><b>Scenario C: 악성 네트워크 공격 및 좀비 세션 검증 결과</b></summary>

<img width="549" height="853" alt="KakaoTalk_20260413_150714205_01" src="https://github.com/user-attachments/assets/590a5c7b-63a5-45ca-ab5e-e5bf32a5af9e" />
</details>
