[← README로 돌아가기](../README.md)

# 빌드 상세 가이드

## 필수 환경

| 구성 요소 | 버전 | 비고 |
|-----------|------|------|
| **Visual Studio** | 2022 (v143 toolset) | C++17, x64 Release 권장 |
| **Windows SDK** | 10.0+ | IOCP, AcceptEx, CancelIoEx |

> DB(MySQL, Redis)는 `TestMode=true`(기본값)에서는 불필요합니다.

## 번들 라이브러리 (별도 설치 불필요)

| 라이브러리 | 용도 | 포함 형태 |
|-----------|------|----------|
| hiredis | Redis C 클라이언트 | DLL (lib/, include/) |
| nlohmann/json | JSON 파서 | Header-only |
| plog | 로깅 | Header-only |
| RedisCpp-hiredis | Redis C++ 래퍼 | 소스 포함 |

## Quick Start (DB 없이 즉시 실행)

```bash
git clone https://github.com/kanghyoungwoo/IOCPChatServer.git
```

1. Visual Studio 2022에서 `IOCP/IOCPChatServer/IOCPChatServer.sln` 열기
2. 솔루션 구성: `Release | x64` 선택
3. `F5`(빌드 및 실행) — `config.json`의 `TestMode=true`(기본값)로 Redis/MySQL 없이 순수 엔진 모드로 동작

서버가 시작되면 포트 `11021`에서 TCP 연결을 수신합니다.

## 아키텍처 전환 (매크로)

`Define.h`에서 전처리기 매크로를 변경하여 내부 아키텍처를 전환할 수 있습니다:

```cpp
#define USE_LOCK_FREE_ARCH  // Lock-Free 모드 (기본값, 최고 성능)
// #define USE_MUTEX_ARCH   // Mutex 모드 (성능 비교용)
```

## config.json 주요 설정

```json
{
    "TestMode": true,          // true: DB 우회 (순수 엔진 테스트), false: MySQL/Redis 연동
    "Network": {
        "ServerPort": 11021,
        "MaxClient": 10000,
        "MaxIOWorkerThread": 8
    },
    "Thread": {
        "MaxWorkerThread": 2,  // Worker 스레드 수
        "MaxLogicThread": 4    // Logic 스레드 수
    },
    "Room": {
        "MaxRoomUserCount": 1000,
        "MaxRoomCount": 10
    }
}
```

---

## DB 연동 빌드 (선택)

실제 인증/로그 기능을 사용하려면 MySQL과 Redis가 필요합니다.

### 1. MySQL 설정

1. MySQL 8.0+ 설치
2. 데이터베이스 생성 및 테이블 구성
3. `config.json`의 `Database` 섹션에 접속 정보 입력

### 2. Redis 설정

1. Redis 7.0+ 설치 (Windows: WSL 또는 Memurai 사용)
2. `config.json`의 `RedisHost`/`RedisPort` 설정

### 3. AWS 모드

`Define.h`에서 AWS 환경용 매크로를 활성화합니다:

```cpp
#define USE_AMAZON_AWS_DB    // AWS RDS + ElastiCache 연결 모드
```

### DLL 의존성

`hiredis.dll`이 실행 파일과 같은 디렉토리에 있어야 합니다. 프로젝트의 `lib/` 폴더에 포함되어 있으며, 빌드 시 자동으로 복사됩니다.
