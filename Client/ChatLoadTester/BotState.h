#pragma once
#include <cstdint>

enum class BotState : uint8_t
{
	IDLE,              // 초기 상태
	CONNECTING,        // ConnectEx 호출, 완료 대기
	CONNECTED,         // TCP 연결 완료
	LOGIN_SENT,        // 로그인 요청 전송
	LOGGED_IN,         // 로그인 응답 성공
	ENTER_ROOM_SENT,   // 방 입장 요청 전송
	IN_ROOM,           // 방 입장 완료, 채팅 대기
	CHATTING,          // 채팅 메시지 전송
	LEAVE_ROOM_SENT,   // 방 퇴장 요청 전송
	DISCONNECTED,      // 연결 해제
	ERROR_STATE        // 에러 발생
};

enum class TimerEventType : uint8_t
{
	CONNECT,           // 접속 시도
	SEND_LOGIN,        // 로그인 전송
	ENTER_ROOM,        // 방 입장
	SEND_CHAT,         // 채팅 전송
	LEAVE_ROOM,        // 방 퇴장
	RECONNECT          // 재접속
};
