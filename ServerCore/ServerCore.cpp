#include "stdafx.h"
#include "ServerCore.h"
#include "OverlappedData.h"

ServerCore::ServerCore()
{

}

ServerCore::~ServerCore()
{

}

bool ServerCore::InitWinSock()
{
	WSADATA wsaData;

	if (0 != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		//LOG_FATAL(LogType::SYSTEM, L"WSAStartup failed; err=%d", WSAGetLastError());
		return false;
	}

	// confirm winsock dll supports 2.2
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		WSACleanup();
		//LOG_FATAL(LogType::SYSTEM, L"Winsock version: %d.%d (< 2.2)",
			LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
		return false;
	}

	if (!IocpSocket::InitializeFunctions())
		return false;

	return true;
}

void ServerCore::Start()
{
	// process exit시 실행할 func 등록 
	////atexit(exit_process);

	if (this->InitWinSock() == false)
	{
		return;
	}

	// logger create
	// load cofig
}

bool ServerCore::CreateIocpWorkerThreads(int nThreadCnt)
{
	if (nThreadCnt <= 0)
	{
		return false;
	}

	ZeroMemory(m_aWorkThreadHandles, sizeof(m_aWorkThreadHandles));

	for (int i = 0; i < nThreadCnt; i++)
	{
		m_aWorkThreadHandles[i] = (HANDLE)_beginthreadex(NULL, 0, ServerCore::WorkerThreadProc, m_completionPort, CREATE_SUSPENDED, &m_aWorkThreadIDs[i]);

		if (m_aWorkThreadHandles[i] == INVALID_HANDLE_VALUE)
		{
			return false;
		}
	}

	return true;
}


// ughi todo: 다른 class로 옮기자
unsigned int __stdcall ServerCore::WorkerThreadProc(void* arg)
{
	CompletionPort* port = static_cast<CompletionPort*>(arg);
	if (port == nullptr)
		return 0xffffffff;

	DWORD dwNumberOfBytes;
	ULONG_PTR nCompletionKey;
	WSAOVERLAPPED * ov;
	DWORD dwLastError;

	SetMyThreadIndex();

	srand(((unsigned int)& dwNumberOfBytes) ^ GetTickCount() ^ GetCurrentThreadId());

	const LONG threadID = ThreadManager::Instance().RegisterThread(ThreadManager::IO_THREAD_TYPE);


	// Initialize thread-local-buffer pool
	BufferPool::Instance().ReserveLocalBufferPool(128);

	ServerCore::Instance().GetSettings().GetLogicInstance()->PostThreadCreate();

	LOG_INFO(LogType::SYSTEM, L"Entered IOCP worker thread.");

	DWORD dwTimeout = INFINITE;

	for (;;)
	{
		dwTimeout = theTimer::Instance().Schedule(threadID);

		const bool ok = port->GetCompletionStatus(&dwNumberOfBytes, &nCompletionKey, &ov, dwTimeout);

		if (!ok)
		{
			dwLastError = GetLastError();

			if (NULL == ov)
			{
				if (dwLastError != WSA_WAIT_TIMEOUT)
					//LOG_FATAL(LogType::SYSTEM, L"Bad call to GetQueuedCompletionStatus, reason: %d", dwLastError);
				continue;	// AttachedWorker 실행해야
			}

			OverlappedData* packet = static_cast<OverlappedData*>(ov);
			if (packet == nullptr)
			{
				//LOG_FATAL(LogType::SYSTEM, L"GetQueuedCompletionStatus dequeued failed I/O: %d, Packet is null", dwLastError);
				break;
			}

			if (dwNumberOfBytes != 0)
			{
				//LOG_FATAL(LogType::SYSTEM, L"GetQueuedCompletionStatus dequeued failed I/O: %d, Packet: %d", dwLastError, packet->GetType());
				break;
			}

			// disconnected

			SOCKET* socket = reinterpret_cast<SOCKET*>(nCompletionKey);
			char* address = "(unknown)";
			unsigned short peerPort = 0;
			if (socket != nullptr)
			{
				if (socket->m_szIpAddress)
				{
					address = socket->m_szIpAddress;
				}

				peerPort = socket->m_peerPort;
			}

			// ERROR_CONNECTION_REFUSED 는 Disconnect에서 로그를 남기므로 별도 처리를 하지 않는다.
			if (ERROR_CONNECTION_REFUSED != dwLastError)
			{
				//LOG_INFO(LogType::SYSTEM, L"GetQueuedCompletionStatus dequeued with error(%d) packet(type:%d) %S:%d", dwLastError, packet->GetType(), address, peerPort);
			}
				
			switch (packet->GetType())
			{
			case OverlappedData::EType::T_Recv:
				socket->OnRead(packet, 0);
				break;

			case OverlappedData::EType::T_Send:
				socket->Kick(L"GetQueuedCompletionStatus Failed, on send");
				socket->RefDec(); // IocpSocket::Send() 혹은 _CheckedFlush() 대응
				break;

			case OverlappedData::EType::T_Connect:
				socket->OnDisconnect(packet);
				break;

			case OverlappedData::EType::T_Kick:
				LOG_FATAL(LogType::SYSTEM, L"Kick.Disconnect failed");
				socket->OnKick(packet);
				break;

			case OverlappedData::EType::T_Accept:
				(static_cast<OverlappedDataAccept*>(packet))->GetSocket()->AcceptFail();
				break;

			case OverlappedData::EType::T_Post:
				socket->Kick(L"GetQueuedCompletionStatus Failed, on post");
				socket->RefDec(); // IocpSocket::Post()대응
				break;

			case OverlappedData::EType::T_Task:
				ASSERT(socket == NULL);
				ASSERT(0);	// should not be here
				break;

			case OverlappedData::EType::T_SendBulk:
				socket->Kick(L"GetQueuedCompletionStatus Failed, on sendbulk");
				socket->RefDec(); // IocpSocket::Send() 혹은 _CheckedFlush() 대응
				break;

			default: // 나머진 그냥 킥
				socket->Kick(L"GetQueuedCompletionStatus Failed");
				break;
			}
			// reference counter에서 처리하게 해야함
			packet->RefDec();
		}	// end of FALSE == bOK 
		else
		{
			if (ov != NULL)
			{	// 일반적으로 실행되는 루프
				OverlappedData* packet = static_cast<OverlappedData*>(ov);
				IocpSocket* socket = reinterpret_cast<IocpSocket*>(nCompletionKey);
				//LOG_INFO(LogType::SYSTEM, L"GQCS: ret %u, key %p bytes %u ov %p packetType %d", ok, nCompletionKey, dwNumberOfBytes, ov, packet->GetType());
				ASSERT(packet->GetType() != OverlappedData::EType::T_Accept || (static_cast<OverlappedDataAccept*>(packet))->GetSocket() != socket);
				switch (packet->GetType())
				{
				case OverlappedData::EType::T_Send: socket->OnWrite(packet, dwNumberOfBytes); break;
				case OverlappedData::EType::T_Recv: socket->OnRead(packet, dwNumberOfBytes); break;
				case OverlappedData::EType::T_Accept:	(static_cast<OverlappedDataAccept*>(packet))->GetSocket()->OnAccept(packet); break;
				case OverlappedData::EType::T_Connect: socket->OnConnect(); break;
				case OverlappedData::EType::T_Kick: socket->OnKick(packet); break;
				case OverlappedData::EType::T_Post: socket->OnPost(packet); break;
				case OverlappedData::EType::T_Task: {
					IGgsTask* task = static_cast<IocpPacketTask*>(packet)->m_task;
					task->Run();
					task->OnFinish();
					break;
				}
				case OverlappedData::EType::T_SendBulk: socket->_OnBulkWrite(packet, dwNumberOfBytes); break;
				default:
					LOG_FATAL(LogType::SYSTEM, L"unknown ov type: %d", packet->GetType());
					MAKE_CRASH(L"unknown ov type: %d", packet->GetType());
				}
				// reference counter에서 처리하게 해야함
				packet->RefDec();
			}
			else
			{
				switch (nCompletionKey)
				{
				case IOCP_OTHER_JOBS::TERMINATE_THREAD:
					goto _finishIocpThread;
					break;

				case IOCP_OTHER_JOBS::HELLO:
					LOG_INFO(LogType::SYSTEM, L"Thread alive(%d)", ::GetCurrentThreadId());
					break;

				default:
					LOG_FATAL(LogType::SYSTEM, L"Unknown key: %d", nCompletionKey);
					goto _finishIocpThread;
					break;
				}
			}
		}
	}

_finishIocpThread:
	ServerCore::Instance().GetSettings().GetLogicInstance()->PreThreadDestroy();

	LOG_INFO(LogType::SYSTEM, L"Quit from IOCP worker thread.");
	BufferPool::Instance().DestroyLocalBufferPool();

	_endthreadex(0);
	return 0;
}