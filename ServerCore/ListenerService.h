#pragma once

#include "WinSock2.h"
#include <vector>

class ListenerService
{
private:
	SOCKET listenSocket;
	SOCKADDR_STORAGE address;
	unsigned short listenPort;
	bool isStart = false;
	std::vector<SOCKET> acceptSockets;

public:
	ListenerService();
	bool CreateListenSocket(string listenAddress, USHORT listenPort);
	SOCKET CreateAcceptSocket(bool isIPv6);
	bool Accept(int backLogCnt, bool isIPv6);
	bool DoAccept(SOCKET acceptSocket);
	bool Start();
	bool Stop();
};
