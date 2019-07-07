#pragma once

#include <winsock2.h>

struct OverlappedData : public WSAOVERLAPPED
{
	enum class EType {
		T_Recv = 0,
		T_Send,
		T_Accept,
		T_Connect,
		T_Disconnect,
		T_Kick,
		T_Post,
		T_Task,
		T_SendBulk,
	};

protected:
	explicit		OverlappedData(EType type);
	virtual			~OverlappedData();

	EType			m_type;

public:
	EType			GetType() const { return m_type; }
};

struct OverlappedDataAccept : public OverlappedData
{
	enum { SIZE_LOCAL_ADDR = sizeof(struct sockaddr_storage) + 16 };
	enum { SIZE_REMOTE_ADDR = sizeof(struct sockaddr_storage) + 16 };

	SOCKET* m_Socket;
	SOCKET* GetSocket() { return m_Socket; }

	OverlappedDataAccept(SOCKET* socket/*, PacketBuffer* buffer*/);
	virtual ~OverlappedDataAccept();
};
