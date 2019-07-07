#include "stdafx.h"
#include <string>
#include <ws2ipdef.h>
#include <WS2tcpip.h>
#include <mstcpip.h>
#include <memory>

#include "ListenerService.h"
#include "OverlappedData.h"

ListenerService::ListenerService()
{
	this->acceptSockets.reserve(500);
}

bool ListenerService::CreateListenSocket(string listenAddress, USHORT listenPort)
{
	if (listenAddress.empty())
	{
		return false;
	}

	const int family = ParseAddress(listenAddress.c_str(), listenPort, true, this->address);
	this->address.ss_family = (USHORT)family;

	if (AF_UNSPEC == family)
	{
		return false;
	}

	SOCKET hSocket = ::WSASocket(family, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (hSocket == INVALID_SOCKET)
	{
		return false;
	}

	//NAGLE OFF
	int tcpNodelay = 1;
	if (setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (char*)& tcpNodelay, sizeof(int)) < 0)
	{
		return false;
	}

	// set per-connection keep-alive setting
	if (SetKeepAlive(hSocket, 10000, 1000) == false)
	{
		int nLastError = WSAGetLastError();
		//LOG_FATAL(LogType::SYSTEM, L"WSAIoctl SIO_KEEPALIVE_VALS failed: %d %s", nLastError, GetWsaErrorString(nLastError));
	}

	// SO_REUSEADDR 해야 하나?
	// https://m.blog.naver.com/PostView.nhn?blogId=bringmelove1&logNo=119146643&proxyReferer=https%3A%2F%2Fwww.google.com%2F
	BOOL bReuseAddr = TRUE;
	setsockopt(hSocket, SOL_SOCKET, SO_REUSEADDR, (char*)& bReuseAddr, sizeof(BOOL));

	this->listenSocket = hSocket;

	return true;
}

SOCKET ListenerService::CreateAcceptSocket(bool isIPv6)
{
	auto socketDeleter = [](SOCKET* socket)
	{
		closesocket(*socket);
	};

	auto socket = ::WSASocket(isIPv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (socket == INVALID_SOCKET)
	{
		int nLastError = WSAGetLastError();
		//LOG_ERROR(LogType::SYSTEM, L"IocpSocket::Create - WSASocket failed: %d %s",	nLastError, GetWsaErrorString(nLastError));

		return INVALID_SOCKET;
	}

	// flush unsent data before socket close
	LINGER lngr;
	lngr.l_onoff = 1;	// turn on
	lngr.l_linger = 0;	// timeout but not recommended for nonblocking socket, so zero.
	if (0 != setsockopt(socket, SOL_SOCKET, SO_LINGER, (char*)& lngr, sizeof(lngr)))
	{
		return INVALID_SOCKET;
	}

	if (isIPv6)
	{
		DWORD ipv6only = FALSE;
		if (0 != ::setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)& ipv6only, sizeof(ipv6only)))
		{
			return INVALID_SOCKET;
		}
	}

	//m_state = SocketState::sDisconnected;

	return socket;
}

bool ListenerService::Accept(int backLogCnt, bool isIPv6 = false)
{
	for (int i = 0; i < backLogCnt; ++i)
	{
		//acceptex는 bind 되거나 connect되지 않은 socket 이어야 한다
		auto acceptSocket = this->CreateAcceptSocket(isIPv6);

		// todo: iocp attach
		if (!ServerCore::Instance().AttachHandle(*socket, this))
		{
			// LOG_ERROR(LogType::SYSTEM, L"IocpSocket::Accept - AssociateDeviceWithCompletionPort failed");
			return false;
		}

		if (this->DoAccept(acceptSocket) == false)
		{
			return false;
		}

		this->acceptSockets.push_back(acceptSocket);
	}

	return true;
}

bool ListenerService::DoAccept(SOCKET acceptSocket)
{
	DWORD dwRecvd = 0;
	auto overlappedDataAccept = std::make_shared<OverlappedDataAccept>();
	//m_AcceptPacket = new OverlappedDataAccept(this/*, BufferPool::Instance().GetBuffer()*/);

	//ASSERT(*&m_state == SocketState::sDisconnected);
	//::InterlockedExchange(&m_state, SocketState::sWaitForConnection);

	// 모든 provider 들이 확장 API를 지원하지 않기 때문에 Load해서 사용하는 것이 좋다
	AcceptEx(this->listenSocket, acceptSocket, m_AcceptPacket->GetBuffer(), 0,
		OverlappedDataAccept::SIZE_LOCAL_ADDR, OverlappedDataAccept::SIZE_REMOTE_ADDR,
		&dwRecvd, static_cast<WSAOVERLAPPED*>(overlappedDataAccept.get()));

	// Load 해서 하용하는 부분
	//if (!s_lpfnAcceptEx(hSocket, m_hSocket, m_AcceptPacket->GetBuffer(), 0,
	//	IocpPacketAccept::SIZE_LOCAL_ADDR, IocpPacketAccept::SIZE_REMOTE_ADDR,
	//	&dwRecvd, static_cast<WSAOVERLAPPED*>(m_AcceptPacket)))
	//{
	//	DWORD err = ::WSAGetLastError();
	//	if (WSA_IO_PENDING != err)
	//	{
	//		LOG_ERROR(LogType::SYSTEM, L"AcceptEx failed: %d, %s", err, GetWsaErrorString(err));
	//		ASSERT(InterlockedDecrement(&m_syncd.ConnPostCount) == 0);

	//		m_AcceptPacket->RefDec();
	//		m_AcceptPacket = NULL;
	//		m_state = SocketState::sDisconnected;
	//		this->RefDec(); // 바로 윗쪽에서 올린거 상쇄
	//		return false;
	//	}
	//}

	return true;
}

bool ListenerService::Start()
{
	// todo: iocp 등록

	// bind socket
	if (SOCKET_ERROR == ::bind(this->listenSocket, (SOCKADDR*)& this->address, sizeof(this->address)))
	{
		return false;
	}

	// Start listening on the listening socket
	if (SOCKET_ERROR == listen(this->listenSocket, 200))
	{
		int nLastError = WSAGetLastError();
		//LOG_FATAL(LogType::SYSTEM, L"listen() failed: %d %s", nLastError, GetWsaErrorString(nLastError));

		return false;
	}

	return true;
}

bool ListenerService::Stop()
{
	// todo:
	return true;
}

static bool SetKeepAlive(SOCKET hSocket, ULONG timeout, ULONG interval)
{
	tcp_keepalive keepAlive = { 0 };
	keepAlive.onoff = 1;
	keepAlive.keepalivetime = timeout;
	keepAlive.keepaliveinterval = interval;
	DWORD dwReturned = 0;

	return SOCKET_ERROR != ::WSAIoctl(hSocket, SIO_KEEPALIVE_VALS, &keepAlive, sizeof(keepAlive), NULL, 0, &dwReturned, NULL, NULL);
}

int ParseAddress(const char* host, unsigned short _port, bool forListen, OUT SOCKADDR_STORAGE& address)
{
	::memset(&address, 0, sizeof(address));

	char port[16];
	::sprintf_s(port, 16, "%d", _port);
	struct addrinfo* infos = NULL;
	struct addrinfo hint = { 0 };
	hint.ai_family = PF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	hint.ai_protocol = IPPROTO_TCP;
	if (forListen)
		hint.ai_flags = AI_PASSIVE;

	int result = ::getaddrinfo(host, port, &hint, &infos);
	if (result != 0 || infos == NULL)
		return AF_UNSPEC;

	sockaddr_in * addressV4 = NULL;
	sockaddr_in6 * addressV6 = NULL;

	struct addrinfo* info = infos;
	do
	{
		if (AF_INET == info->ai_family && !addressV4)
			addressV4 = (sockaddr_in*)info->ai_addr;
		if (AF_INET6 == info->ai_family && !addressV6)
			addressV6 = (sockaddr_in6*)info->ai_addr;
		info = info->ai_next;
	} while (info);

	int family = AF_UNSPEC;
	if (addressV4)
	{
		family = AF_INET;
		::memcpy((sockaddr_in*)& address, addressV4, sizeof(sockaddr_in));
	}
	else if (addressV6)
	{
		family = AF_INET6;
		::memcpy((sockaddr_in6*)& address, addressV6, sizeof(sockaddr_in6));
	}

	::freeaddrinfo(infos);
	return family;
}
