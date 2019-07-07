#include "stdafx.h"
#include "OverlappedData.h"

OverlappedData::OverlappedData(EType type) : m_type(type)
{
	::memset((WSAOVERLAPPED*)this, 0, sizeof(WSAOVERLAPPED));
}

OverlappedData::~OverlappedData()
{
}

OverlappedDataAccept::OverlappedDataAccept(SOCKET* socket/*, PacketBuffer* packet*/)
	: OverlappedData(EType::T_Accept/*, packet*/), m_Socket(socket)
{
}


OverlappedDataAccept::~OverlappedDataAccept()
{
}