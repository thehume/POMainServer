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
#include "Packet.h"
#include "profiler.h"
#include "dumpClass.h"
#include "LockFreeQueue.h"
#include "LockFreeStack.h"
#include "CNetServer.h"
#include "CThreadManager.h"

CMemoryPoolBucket<st_Player> CParentThread::PlayerPool;

DWORD WINAPI CParentThread::ThreadFunction(CParentThread* Instance)
{
	
	CThreadHandler* pThreadHandler = Instance->pThreadManager->pThreadHandler;
	CNetServer* pServer = Instance->pServer;
	int enterQueueSize;
	int PlayerListSize;
	while (!Instance->shutdown)
	{
		Instance->Frame++;
		DWORD startTime = timeGetTime();

		Instance->Update();

		//������ť ��ť -> �����λ��� -> playerList�� �̵�
		//���� �� ���� �� OnclientLeave�� �ִٸ� �ű������ ó���ϰ�
		//workerthread�ʿ� PQCS�� OnclientLeave�� ������ �˸�(stack push�� �ϱ�����)
		PlayerListSize = Instance->PlayerList.size();
		enterQueueSize = Instance->enterQueue.size();
		if (enterQueueSize > 0)
		{
			AcquireSRWLockExclusive(&Instance->enterQueueLock);
			for (int i = 0; i < enterQueueSize; i++)
			{
				Instance->pThreadManager->ThreadMoveTPS++;
				Instance->EnterUserTPS++;

				st_ThreadMoveInfo temp = Instance->enterQueue.front();
				INT64 sessionID = temp.pPlayer->SessionID;
				INT64 AccountNo = temp.pPlayer->AccountNo;
				std::queue<st_JobItem*>* pJobQueue = pServer->getJobQueue(sessionID);
				if (pJobQueue != NULL)
				{
					volatile bool LeaveFlag = false;
					pServer->AcquireJobQueueLock(sessionID);
					st_JobItem* JobItem;
					INT64 JobType;
					int JobQueueSize = pServer->getJobQueueSize(sessionID);
					{
						for (int i = 0; i < JobQueueSize; i++)
						{
							JobItem = pServer->PopJobItem(sessionID);
							JobType = JobItem->JobType;
							CPacket* pPacket = JobItem->pPacket;
							pServer->freeJobItem(JobItem);

							switch (JobType)
							{
							case en_JOB_ON_CLIENT_LEAVE:
							{
								LeaveFlag = true;
								break;
							}

							case en_JOB_ON_RECV:
							{
								if (pPacket->subRef() == 0)
								{
									CPacket::mFree(pPacket);
								}
								break;
							}

							default:
							{
								//�α����
								break;
							}
							}


						}

					}
					pServer->ReleaseJobQueueLock(sessionID);
					if (LeaveFlag == true)
					{
						pServer->PostLeaveCompletion(sessionID); //PQCS�� ��Ŀ���� Leave�Ϸ� �˷��ֱ�.
						Instance->pThreadManager->pThreadHandler->onPlayerLeave(AccountNo, temp.pPlayer->playerInfo);
						PlayerPool.mFree(temp.pPlayer);
					}
					else
					{
						Instance->PlayerList.push_back(temp.pPlayer);
						pThreadHandler->onPlayerMove(AccountNo, temp.srcThread, temp.desThread); //OnPlayerMove ȣ��		
						Instance->onThreadJoin(temp.pPlayer->SessionID, AccountNo, temp.pPlayer->playerInfo);
					}
				}


				Instance->enterQueue.pop();
			}
			ReleaseSRWLockExclusive(&Instance->enterQueueLock);
		}

		//PlayerList ��ȸ�ϸ� ��ó��.
		for (int i = 0; i < PlayerListSize; i++)
		{
			INT64 sessionID;
			for (auto iter = Instance->PlayerList.begin(); iter != Instance->PlayerList.end(); )
			{
				sessionID = (*iter)->SessionID;
				pServer->AcquireJobQueueLock(sessionID);
				std::queue<st_JobItem*>* pJobQueue = pServer->getJobQueue(sessionID);
				int JobQueueSize = pServer->getJobQueueSize(sessionID);
				if (pJobQueue != NULL)
				{
					

					volatile short MoveThreadNum = -1;
					volatile bool MoveFlag = false;
					volatile bool LeaveFlag = false;
					st_JobItem* JobItem;
					INT64 JobType;
					INT64 AccountNo;
					if (JobQueueSize >= 0)
					{

						for (int i = 0; i < JobQueueSize && !(MoveFlag == true && LeaveFlag == false); i++)
						{
							Instance->pThreadManager->GameThread_JobTPS++;
							JobItem = pServer->PopJobItem(sessionID);
							JobType = JobItem->JobType;
							AccountNo = JobItem->AccountNo;
							CPacket* pPacket = JobItem->pPacket;
							pServer->freeJobItem(JobItem);


							switch (JobType)
							{
							case en_JOB_ON_CLIENT_LEAVE: //LeaveFlag�� true�� �ٲ���, ���� ��� �� �� �������̵� ���õ�, PQCS�� �ϷḦ ��������.
							{
								LeaveFlag = true;
								break;
							}

							case en_JOB_ON_RECV:
							{

								if (LeaveFlag == false)
								{
									Instance->OnRecv(&MoveFlag, &MoveThreadNum, (*iter)->playerInfo, (*iter)->SessionID, AccountNo, pPacket);
								}
								if (pPacket->subRef() == 0)
								{
									CPacket::mFree(pPacket);
								}
								break;
							}

							default:
							{
								//�α����
								break;
							}
							}
						}
						if (LeaveFlag == true)
						{
							pServer->PostLeaveCompletion(sessionID); //PQCS�� ��Ŀ���� Leave�Ϸ� �˷��ֱ�.
							//�������� leave �˸�.
							st_Player* pPlayer = *iter;
							iter = Instance->PlayerList.erase(iter);
							pThreadHandler->onPlayerLeave(AccountNo, pPlayer->playerInfo);
							PlayerPool.mFree(pPlayer);
						}
						else if (LeaveFlag == false && MoveFlag == true)
						{
							//�̵��� ��� �������� ���ť�� �Ȱ�, �� ����
							//������ �̵������ �������ʿ� �˸��°��� �̵��� �����忡�� ó��
							Instance->pThreadManager->MovePlayer(Instance->ThreadNumber, MoveThreadNum, *iter);
							iter = Instance->PlayerList.erase(iter);
						}
						else
						{
							iter++;
						}
					}
				}
				else
				{
					//����, �α����.
				}
				pServer->ReleaseJobQueueLock(sessionID);
			}


		}

		DWORD Period = timeGetTime() - startTime;
		if (Period < Instance->TimeInterval)
		{
			Sleep(Instance->TimeInterval - Period);
		}


	}
	return true;
}



DWORD WINAPI CLoginThread::LoginThreadFunction(CLoginThread* Instance)
{


	//���ť���� �̴´�
	CThreadHandler* pThreadHandler = Instance->pThreadManager->pThreadHandler;
	CNetServer* pServer = Instance->pServer;
	int enterQueueSize;
	int PlayerListSize;
	while (!Instance->shutdown)
	{
		Instance->Frame++;
		DWORD startTime = timeGetTime();

		INT64 LoginSessionID;
		while (pServer->popLoginQueue(&LoginSessionID) == true)
		{
			Instance->pThreadManager->LoginTPS++;

			st_Player* pPlayer;
			PlayerPool.mAlloc(&pPlayer);
			pPlayer->SessionID = LoginSessionID;
			pPlayer->AccountNo = -1;
			pPlayer->playerInfo = NULL;
			
			PlayerInfo* pPlayerInfo = pThreadHandler->onPlayerJoin(LoginSessionID);
			if (pPlayerInfo != NULL)
			{
				pPlayer->playerInfo = pPlayerInfo;
				Instance->PlayerList.push_back(pPlayer);
			}
			else
			{
				PlayerPool.mFree(pPlayer);
				st_Session* pSession;
				if (pServer->findSession(LoginSessionID, &pSession) == true)
				{
					pServer->disconnectSession(pSession);
				}
				
			}
			
		}

		Instance->Update();

		PlayerListSize = Instance->PlayerList.size();

		//PlayerList ��ȸ�ϸ� ��ó��.
		for (int i = 0; i < PlayerListSize; i++)
		{
			INT64 sessionID;
			for (auto iter = Instance->PlayerList.begin(); iter != Instance->PlayerList.end(); )
			{
				sessionID = (*iter)->SessionID;
				pServer->AcquireJobQueueLock(sessionID);
				std::queue<st_JobItem*>* pJobQueue = pServer->getJobQueue(sessionID);
				int JobQueueSize = pServer->getJobQueueSize(sessionID);
				if (pJobQueue != NULL)
				{
					volatile short MoveThreadNum = -1;
					volatile bool MoveFlag = false;
					volatile bool LeaveFlag = false;
					st_JobItem* JobItem;
					INT64 JobType;
					INT64 AccountNo;
					for (int i = 0; i < JobQueueSize && !(MoveFlag == true && LeaveFlag == false); i++)
					{
						Instance->pThreadManager->LoginThread_JobTPS++;


						JobItem = pServer->PopJobItem(sessionID);
						JobType = JobItem->JobType;
						AccountNo = JobItem->AccountNo;
						CPacket* pPacket = JobItem->pPacket;
						pServer->freeJobItem(JobItem);


						switch (JobType)
						{
						case en_JOB_ON_CLIENT_LEAVE: //LeaveFlag�� true�� �ٲ���, ���� ��� �� �� �������̵� ���õ�, PQCS�� �ϷḦ ��������.
						{
							LeaveFlag = true;
							break;
						}

						case en_JOB_ON_RECV:
						{

							if (LeaveFlag == false)
							{
								Instance->OnRecv(&MoveFlag, &MoveThreadNum, (*iter)->playerInfo, (*iter)->SessionID, AccountNo, pPacket);
							}
							if (pPacket->subRef() == 0)
							{
								CPacket::mFree(pPacket);
							}
							break;
						}

						default:
						{
							//�α����
							break;
						}
						}
					}
					if (LeaveFlag == true)
					{
						pServer->PostLeaveCompletion(sessionID); //PQCS�� ��Ŀ���� Leave�Ϸ� �˷��ֱ�.
						//�������� leave �˸�.
						st_Player* pPlayer = *iter;
						iter = Instance->PlayerList.erase(iter);
						pThreadHandler->onPlayerLeave(AccountNo, pPlayer->playerInfo);
						PlayerPool.mFree(pPlayer);
					}
					else if (LeaveFlag == false && MoveFlag == true)
					{
						//�̵��� ��� �������� ���ť�� �Ȱ�, �� ����
						//������ �̵������ �������ʿ� �˸��°��� �̵��� �����忡�� ó��
						Instance->pThreadManager->MovePlayer(Instance->ThreadNumber, MoveThreadNum, *iter);
						iter = Instance->PlayerList.erase(iter);
					}
					else
					{
						iter++;
					}
				}
				else
				{
					//����, �α����.
				}
				pServer->ReleaseJobQueueLock(sessionID);
			}


		}

		DWORD Period = timeGetTime() - startTime;
		if (Period < Instance->TimeInterval)
		{
			Sleep(Instance->TimeInterval - Period);
		}


	}
	return true;
}


void CLoginThread::onLoginSucess(INT64 sessionID, INT64 AccountNo)
{
	for (auto iter = PlayerList.begin(); iter != PlayerList.end(); iter++)
	{
		st_Player* pPlayer = *iter;
		if (pPlayer->SessionID == sessionID)
		{
			pPlayer->AccountNo = AccountNo;
			pPlayer->playerInfo->isValid = true;
			break;
		}
	}
}


void CThreadManager::registThread(short threadNo, CParentThread* pThread, bool isLoginThread)
{
	if (isLoginThread)
	{
		LoginThreadList.insert(make_pair(threadNo, (CLoginThread*)pThread));
	}
	else
	{
		GameThreadList.insert(make_pair(threadNo, (CParentThread*)pThread));
	}
	pThread->pThreadManager = this;
	pThread->pThreadHandler = this->pThreadHandler;
	Threadindex++;
}

bool CThreadManager::MovePlayer(short srcThreadNo, short DesThreadNo, st_Player* pPlayer)
{
	auto iter = GameThreadList.find(DesThreadNo);
	if (iter == GameThreadList.end())
	{
		return false;
	}

	CParentThread* pThread = iter->second;
	AcquireSRWLockExclusive(&pThread->enterQueueLock);
	st_ThreadMoveInfo MoveInfo;
	MoveInfo.desThread = DesThreadNo;
	MoveInfo.srcThread = srcThreadNo;
	MoveInfo.pPlayer = pPlayer;
	pThread->enterQueue.push(MoveInfo);
	ReleaseSRWLockExclusive(&pThread->enterQueueLock);

	return true;
}

void CThreadManager::attachServerInstance(CNetServer* pNetServer)
{
	this->pServer = pNetServer;
	for (auto iter = LoginThreadList.begin(); iter != LoginThreadList.end(); iter++)
	{
		CLoginThread* pLoginThread = iter->second;
		pLoginThread->attachServer(pNetServer);
	}

	for (auto iter = GameThreadList.begin(); iter != GameThreadList.end(); iter++)
	{
		CParentThread* pGameThread = iter->second;
		pGameThread->attachServer(pNetServer);
	}
}

void CThreadManager::start()
{
	int index = 0;
	for (auto iter = LoginThreadList.begin(); iter != LoginThreadList.end(); iter++)
	{
		CLoginThread* pLoginThread = iter->second;
		ThreadHANDLE[index] = (HANDLE)_beginthreadex(NULL, 0, (_beginthreadex_proc_type)&pLoginThread->LoginThreadFunction, pLoginThread, 0, 0);
		index++;
	}

	for (auto iter = GameThreadList.begin(); iter != GameThreadList.end(); iter++)
	{
		CParentThread* pGameThread = iter->second;
		ThreadHANDLE[index] = (HANDLE)_beginthreadex(NULL, 0, (_beginthreadex_proc_type)&pGameThread->ThreadFunction, pGameThread, 0, 0);
		index++;
	}
}