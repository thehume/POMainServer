#pragma comment(lib, "winmm.lib" )
#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <dbghelp.h>
#include <random>
#include <locale.h>
#include <process.h>
#include <stdlib.h>
#include <iostream>
#include <list>
#include <queue>
#include <unordered_map>
#include "log.h"
#include "ringbuffer.h"
#include "MemoryPoolBucket.h"
#include "CommonProtocol.h"
#include "Packet.h"
#include "profiler.h"
#include "dumpClass.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "CNetServer.h"
#include "CThreadManager.h"
#include "GameServer.h"

void GameThread::OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum, PlayerInfo* pPlayerInfo, INT64 sessionID, INT64 accountNo, CPacket* pPacket)
{
	WORD packetType;
	*pPacket >> packetType;
	MyPlayerInfo* pMyPlayerInfo = (MyPlayerInfo*)pPlayerInfo;

	switch (packetType)
	{
	case en_PACKET_CS_GAME_REQ_ECHO:
	{
		INT64		AccountNo;
		LONGLONG	SendTick;

		*pPacket >> AccountNo >> SendTick;

		CS_GAME_RES_ECHO(sessionID, AccountNo, SendTick);
		break;
	}
	}
}

void GameThread::onThreadJoin(INT64 sessionID, INT64 accountNo, PlayerInfo* pPlayerInfo)
{
	//�α��ν����� -> ������ ������� ����
	MyPlayerInfo* pMyPlayerInfo = (MyPlayerInfo*)pPlayerInfo;
	if (pMyPlayerInfo->Loginflag == true && pMyPlayerInfo->Certificationflag == df_CERTIFICATION_SUCESS)
	{
		//������Ŷ ������
		CS_GAME_RES_LOGIN(sessionID, true, accountNo);
		pMyPlayerInfo->Certificationflag = df_CERTIFICATION_COMPLETE;
	}
}

void GameThread::CS_GAME_RES_LOGIN(INT64 sessionID, BYTE Status, INT64 AccountNo)
{
	WORD Type = en_PACKET_CS_GAME_RES_LOGIN;

	CPacket* pPacket = CPacket::mAlloc();
	pPacket->Clear();
	pPacket->addRef(1);

	*pPacket << Type;
	*pPacket << Status;
	*pPacket << AccountNo;

	sendPacket(sessionID, pPacket);
	if (pPacket->subRef() == 0)
	{
		CPacket::mFree(pPacket);
	}
}

void GameThread::CS_GAME_RES_ECHO(INT64 sessionID, INT64 AccountNo, LONGLONG SendTick)
{
	WORD Type = en_PACKET_CS_GAME_RES_ECHO;

	CPacket* pPacket = CPacket::mAlloc();
	pPacket->Clear();
	pPacket->addRef(1);

	*pPacket << Type;
	*pPacket << AccountNo;
	*pPacket << SendTick;

	sendPacket(sessionID, pPacket);
	if (pPacket->subRef() == 0)
	{
		CPacket::mFree(pPacket);
	}
}

void GameLoginThread::CS_GAME_RES_LOGIN(INT64 sessionID, BYTE Status, INT64 AccountNo)
{
	WORD Type = en_PACKET_CS_GAME_RES_LOGIN;

	CPacket* pPacket = CPacket::mAlloc();
	pPacket->Clear();
	pPacket->addRef(1);

	*pPacket << Type;
	*pPacket << Status;
	*pPacket << AccountNo;

	sendPacket(sessionID, pPacket);
	if (pPacket->subRef() == 0)
	{
		CPacket::mFree(pPacket);
	}
}

void GameLoginThread::CS_GAME_RES_ECHO(INT64 sessionID, INT64 AccountNo, LONGLONG SendTick)
{
	WORD Type = en_PACKET_CS_GAME_RES_ECHO;

	CPacket* pPacket = CPacket::mAlloc();
	pPacket->Clear();
	pPacket->addRef(1);

	*pPacket << Type;
	*pPacket << AccountNo;
	*pPacket << SendTick;

	sendPacket(sessionID, pPacket);
	if (pPacket->subRef() == 0)
	{
		CPacket::mFree(pPacket);
	}
}


void GameLoginThread::OnRecv(volatile bool* MoveFlag, volatile short* MoveThreadNum, PlayerInfo* pPlayerInfo, INT64 sessionID, INT64 accountNo, CPacket* pPacket)
{
	WORD packetType;
	*pPacket >> packetType;
	MyPlayerInfo* pMyPlayerInfo = (MyPlayerInfo*)pPlayerInfo;

	switch (packetType)
	{
		case en_PACKET_CS_GAME_REQ_LOGIN:
		{
			INT64	AccountNo;
			st_SessionKey SessionKey;

			*pPacket >> AccountNo >> SessionKey;
			//���� sessionKey�� redis���� �����ϴ� �κ� ��..
			//AccountNo�� �����ؾ��ϴµ�.. ���?.. �켱 PlayerInfo ����ü�� �������ش�.

			//����������
			onLoginSucess(sessionID, AccountNo); // ���� �������� �÷��̾��Ʈ�� ȣ��Ǵ� �Լ�
			pMyPlayerInfo->Loginflag = true;
			pMyPlayerInfo->AccountNo = AccountNo;
			pMyPlayerInfo->Certificationflag = df_CERTIFICATION_SUCESS;

			*MoveFlag = true;
			*MoveThreadNum = df_THREAD_CONTENTS;

			//�������н�
			//pMyPlayerInfo->Certificationflag = false;
			//CS_GAME_RES_LOGIN(sessionID, false, AccountNo);

			break;
		}

		case en_PACKET_CS_GAME_REQ_ECHO:
		{
			INT64		AccountNo;
			LONGLONG	SendTick;

			*pPacket >> AccountNo >> SendTick;

			CS_GAME_RES_ECHO(sessionID, AccountNo, SendTick);
			break;
		}
	}

}

PlayerInfo* MyThreadHandler::onPlayerJoin(INT64 sessionID)
{
	//�α��� �������� ����, �������ʿ� Join�� �˷��ִ� �Լ�
	MyPlayerInfo* pMyPlayerInfo = new MyPlayerInfo;
	pMyPlayerInfo->AccountNo = -1;
	pMyPlayerInfo->Certificationflag = df_CERTIFICATION_BEFORE;
	pMyPlayerInfo->isValid = false;
	pMyPlayerInfo->Loginflag = false;

	return (PlayerInfo*)pMyPlayerInfo;
}

void MyThreadHandler::onPlayerLeave(INT64 accountNo, PlayerInfo* pPlayerInfo)
{
	//�������ʿ� �÷��̾� Leave �˷��ִ� �Լ�
	MyPlayerInfo* pMyPlayerInfo = (MyPlayerInfo*)pPlayerInfo;
	delete pMyPlayerInfo;
}