#pragma once

class ServerCore
{
public:
	ServerCore();
	~ServerCore();

	void Start();
	bool InitWinSock();
	bool CreateIocpWorkerThreads(int nThreadCnt);

	static unsigned int __stdcall WorkerThreadProc(void* pv);
};
