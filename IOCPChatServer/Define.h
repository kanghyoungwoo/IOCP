#pragma once
#include <winsock2.h>
#include <Ws2tcpip.h>


#define MAX_SOCKBUF 1024	// ��Ŷ ũ��
#define MAX_WORKERTHREAD 4	// ������ Ǯ�� ���� ������ ��

enum class IOOperation
{
	RECV,
	SEND
};

//WSAOVERLAPPED ����ü�� Ȯ�� ���� �ʿ��� ������ �� ����
typedef struct _stOverlappedEx
{
	WSAOVERLAPPED	m_wsaOverlapped;		// Overlapped I/O ����ü
	SOCKET			m_socketClient;			// Client ����
	WSABUF			m_wsaBuf;				// Overlapped I/O�۾� ����
	IOOperation		m_eOperation;			// �۾� ���� ����
}stOverlappedEx;


