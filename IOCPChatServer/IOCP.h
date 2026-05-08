#pragma once
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "mswsock.lib")

#include "Define.h"
#include "ClientSession.h"
#include "Packet.h"
#include "ConfigManager.h"
#include "LockFreeStack.h"
#include <thread>
#include <vector>
#include <mswsock.h>

struct SessionNode
{
	uint32_t poolNext = NULL_INDEX;
};

class IOCompletionPort
{
public:
	IOCompletionPort(void) {}

	virtual ~IOCompletionPort(void)
	{
		// 사용 완료 시
		WSACleanup();
	}

	// 소켓을 초기화 하는 함수

	bool Init(const UINT32 Max_IO_Worker_Threads_Count);

	bool BindandListen(int nBindPort);

	// 접속 요청을 수락하고 메시지를 받아서 처리하는 함수
	bool StartServer(const int maxClientCount);

	// 생성되어 있는 스레드를 파괴한다
	void DestroyThread();
	// 클라이언트의 데이터를 받아서
	// 클라이언트에게 메시지를 send하는 함수
	bool SendMsg(const UINT32 ClientSessionIndex_, UINT32 generation_, const UINT32 dataSize_, char* pMsg_);


	void DisconnectClient(const UINT32 clientIndex);

	void UpdateClientActivity(const UINT32 clientIndex);

	virtual void OnConnect(const int clientIndex, const UINT32 generation){}
	virtual void OnClose(const int clientIndex, const UINT32 generation){}
	virtual void OnReceive(const UINT32 clientIndex, const UINT32 generation, const UINT32 size, char* pData) {}

	// 모니터링
	uint64_t GetSendPoolAllocFailCount() const
	{ 
		return mSendBufferPool.GetAllocFailCount();
	}

	// AcceptEx 기아 방지
	void TryPostAcceptEx();


private:
	void CreateClient(const int maxClientCount);

	// WaitingThread Queue에서 대기할 스레드들을 생성
	bool CreateWorkerThread();

	void TimeoutCheckThread();



	// 클라이언트의 index로 해당 client의 info를 반환하는 함수
	ClientSession* GetClientInfo(const UINT32 clientSessionIndex)
	{
		if (clientSessionIndex >= mClientInfos.size())
			return nullptr;
		return mClientInfos[clientSessionIndex].get();
	}

	// Overlapped I/O 작업에 대한 완료 통보를 받아 그에 해당하는 처리를 하는 함수
	void WorkerThread();

	// 소켓의 연결을 종료
	void CloseSocket(ClientSession* pClientSession, bool bIsForce = false);

	// 빈 세션 하나 가져오는 함수
	UINT32 PopFreeSessionIndex();
	// 세션 반납 함수
	void PushFreeSessionIndex(const UINT32 index);

	// 클라이언트 접속 정보 구조체
	//std::vector<ClientSession*> mClientInfos;

	// 클라이언트의 접속을 받기 위한 리슨 소켓
	std::atomic<SOCKET> mListenSocket = INVALID_SOCKET;

	// 접속 되어있는 클라이언트 수
	std::atomic<int>mClientCnt = 0;

	// IO worker 스레드
	std::vector<std::thread> mIOWorkerThreads;

	//// Accept 스레드
	//std::thread mAccepterThread;

	// Send 스레드
	std::thread mSendThread;

	// CompletionPort 객체 핸들
	HANDLE	mIOCPHandle = INVALID_HANDLE_VALUE;

	// 작업 스레드 동작 플래그
	std::atomic<bool> mIsWorkerRun{ true };

	//// 접속 스레드 동작 플래그
	//bool	mIsAccepterRun = true;

	//bool	mIsSenderRun = false;

	UINT32 MaxIOWorkerThreadCount = 0;

	// GetQueuedCompletionStatusEx 관련 상수
	static const ULONG MAX_COMPLETION_ENTRIES = 64;  // 한 번에 처리할 최대 완료 항목 수
	static const DWORD TIMEOUT_WAIT = 100;           // 대기 타임아웃 (ms)

	// 클라이언트 접속 정보 구조체
	//std::vector<ClientSession*> mClientInfos;

	// ClientSession 객체는 서버 수명 동안 유지 (unique_ptr로 소유)
	// raw pointer로 접근하지만 생명주기는 unique_ptr이 관리
	std::vector<std::unique_ptr<ClientSession>> mClientInfos;

	ObjectPool<SendOverlappedEx> mSendBufferPool;

	// SessionNode는 free-list 인덱스 관리용 intrusive node (재활용 추적 전용)
	// ClientSession과 달리 값 타입으로 저장 — poolNext 링크 하나만 필요하기 때문
	std::vector<SessionNode> mSessionNodes;
	LockFreeStack<SessionNode> mFreeSessionStack;

	// 초기 AcceptEx 대기 100개
	//static constexpr UINT32 MAX_PENDING_ACCEPT = 100;

	std::thread mTimeoutThread;
	std::atomic<bool> mIsTimeoutRun{ false };
	std::atomic<int> mPendingAcceptCount{ 0 };
	std::atomic<bool> mIsShuttingDown{ false };
};