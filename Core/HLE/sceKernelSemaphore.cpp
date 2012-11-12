// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelSemaphore.h"

/** Current state of a semaphore.
* @see sceKernelReferSemaStatus.
*/

struct NativeSemaphore
{
	/** Size of the ::SceKernelSemaInfo structure. */
	SceSize 	size;
	/** NUL-terminated name of the semaphore. */
	char 		name[32];
	/** Attributes. */
	SceUInt 	attr;
	/** The initial count the semaphore was created with. */
	int 		initCount;
	/** The current count. */
	int 		currentCount;
	/** The maximum count. */
	int 		maxCount;
	/** The number of threads waiting on the semaphore. */
	int 		numWaitThreads;
};


struct Semaphore : public KernelObject 
{
	const char *GetName() {return ns.name;}
	const char *GetTypeName() {return "Semaphore";}

	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_SEMID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Semaphore; }

	NativeSemaphore ns;
	std::vector<SceUID> waitingThreads;
};

// Resume all waiting threads (for delete / cancel.)
// Returns true if it woke any threads.
bool __KernelClearSemaThreads(Semaphore *s, int reason)
{
	bool wokeThreads = false;

	std::vector<SceUID>::iterator iter;
	for (iter = s->waitingThreads.begin(); iter!=s->waitingThreads.end(); iter++)
	{
		SceUID threadID = *iter;

		// TODO: Set returnValue = reason?
		__KernelResumeThreadFromWait(threadID);
		wokeThreads = true;
	}
	s->waitingThreads.empty();

	return wokeThreads;
}

// int sceKernelCancelSema(SceUID id, int newCount, int *numWaitThreads);
// void because it changes threads.
void sceKernelCancelSema(SceUID id, int newCount, u32 numWaitThreadsPtr)
{
	DEBUG_LOG(HLE,"sceKernelCancelSema(%i)", id);

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (numWaitThreadsPtr)
		{
			u32* numWaitThreads = (u32*)Memory::GetPointer(numWaitThreadsPtr);
			*numWaitThreads = s->ns.numWaitThreads;
		}

		if (newCount == -1)
			s->ns.currentCount = s->ns.initCount;
		else
			s->ns.currentCount = newCount;
		s->ns.numWaitThreads = 0;

		// We need to set the return value BEFORE rescheduling threads.
		RETURN(0);

		// TODO: Should this reschedule?
		if (__KernelClearSemaThreads(s, SCE_KERNEL_ERROR_WAIT_CANCEL))
			__KernelReSchedule("semaphore cancelled");
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelCancelSema : Trying to cancel invalid semaphore %i", id);
		RETURN(error);
	}
}

//SceUID sceKernelCreateSema(const char *name, SceUInt attr, int initVal, int maxVal, SceKernelSemaOptParam *option);
void sceKernelCreateSema()
{
	const char *name = Memory::GetCharPointer(PARAM(0));

	Semaphore *s = new Semaphore;
	SceUID id = kernelObjects.Create(s);

	s->ns.size = sizeof(NativeSemaphore);
	strncpy(s->ns.name, name, 32);
	s->ns.attr = PARAM(1);
	s->ns.initCount = PARAM(2);
	s->ns.currentCount = s->ns.initCount;
	s->ns.maxCount = PARAM(3);
	s->ns.numWaitThreads = 0;

	DEBUG_LOG(HLE,"%i=sceKernelCreateSema(%s, %08x, %i, %i, %08x)", id, s->ns.name, s->ns.attr, s->ns.initCount, s->ns.maxCount, PARAM(4));

	RETURN(id);
}

//int sceKernelDeleteSema(SceUID semaid);
// void because it changes threads.
void sceKernelDeleteSema(SceUID id)
{
	DEBUG_LOG(HLE,"sceKernelDeleteSema(%i)", id);

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (__KernelClearSemaThreads(s, SCE_KERNEL_ERROR_WAIT_DELETE))
		{
			RETURN(kernelObjects.Destroy<Semaphore>(id));
			__KernelReSchedule("semaphore deleted");
		}
		else
			RETURN(kernelObjects.Destroy<Semaphore>(id));
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelDeleteSema : Trying to delete invalid semaphore %i", id);
		RETURN(error);
	}
}

int sceKernelReferSemaStatus(SceUID id, u32 infoPtr)
{
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		DEBUG_LOG(HLE,"sceKernelReferSemaStatus(%i, %08x)", id, infoPtr);
		NativeSemaphore *outptr = (NativeSemaphore*)Memory::GetPointer(infoPtr);
		memcpy((char*)outptr, (char*)&s->ns, s->ns.size);
		return 0;
	}
	else
	{
		ERROR_LOG(HLE,"Error %08x", error);
		return error;
	}
}
	
//int sceKernelSignalSema(SceUID semaid, int signal);
// void because it changes threads.
void sceKernelSignalSema(SceUID id, int signal)
{
	//TODO: check that this thing really works :)
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		int oldval = s->ns.currentCount;

		if (s->ns.currentCount + signal > s->ns.maxCount)
			s->ns.currentCount += s->ns.maxCount;
		else
			s->ns.currentCount += signal;
		DEBUG_LOG(HLE,"sceKernelSignalSema(%i, %i) (old: %i, new: %i)", id, signal, oldval, s->ns.currentCount);

		// We need to set the return value BEFORE processing other threads.
		RETURN(0);

		bool wokeThreads = false;
retry:
		//TODO: check for threads to wake up - wake them
		std::vector<SceUID>::iterator iter;
		for (iter = s->waitingThreads.begin(); iter!=s->waitingThreads.end(); s++)
		{
			SceUID threadID = *iter;
			int wVal = (int)__KernelGetWaitValue(threadID, error);
			if (wVal <= s->ns.currentCount)
			{
				s->ns.currentCount -= wVal;
				s->ns.numWaitThreads--;

				__KernelResumeThreadFromWait(threadID);
				wokeThreads = true;
				s->waitingThreads.erase(iter);
				goto retry;
			}
			else
			{
				break;
			}
		}
		//pop the thread that were released from waiting

		// I don't think we should reschedule here
		//if (wokeThreads)
		//	__KernelReSchedule("semaphore signalled");
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelSignalSema : Trying to signal invalid semaphore %i", id);
		RETURN(error;)
	}
}

void __KernelWaitSema(SceUID id, int wantedCount, u32 timeoutPtr, const char *badSemaMessage, bool processCallbacks)
{
	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		// We need to set the return value BEFORE processing callbacks / etc.
		RETURN(0);

		if (s->ns.currentCount >= wantedCount) //TODO fix
			s->ns.currentCount -= wantedCount;
		else
		{
			s->ns.numWaitThreads++;
			s->waitingThreads.push_back(__KernelGetCurThread());
			// TODO: timeoutPtr?
			__KernelWaitCurThread(WAITTYPE_SEMA, id, wantedCount, 0, processCallbacks);
			if (processCallbacks)
				__KernelCheckCallbacks();
		}
	}
	else
	{
		ERROR_LOG(HLE, badSemaMessage, id);
		RETURN(error);
	}
}

//int sceKernelWaitSema(SceUID semaid, int signal, SceUInt *timeout);
// void because it changes threads.
void sceKernelWaitSema(SceUID id, int wantedCount, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelWaitSema(%i, %i, %i)", id, wantedCount, timeoutPtr);

	__KernelWaitSema(id, wantedCount, timeoutPtr, "sceKernelWaitSema: Trying to wait for invalid semaphore %i", false);
} 

//int sceKernelWaitSemaCB(SceUID semaid, int signal, SceUInt *timeout);
// void because it changes threads.
void sceKernelWaitSemaCB(SceUID id, int wantedCount, u32 timeoutPtr)
{
	DEBUG_LOG(HLE,"sceKernelWaitSemaCB(%i, %i, %i)", id, wantedCount, timeoutPtr);

	__KernelWaitSema(id, wantedCount, timeoutPtr, "sceKernelWaitSemaCB: Trying to wait for invalid semaphore %i", true);
}

// Should be same as WaitSema but without the wait, instead returning SCE_KERNEL_ERROR_SEMA_ZERO
int sceKernelPollSema(SceUID id, int wantedCount)
{
	DEBUG_LOG(HLE,"sceKernelPollSema(%i, %i)", id, wantedCount);

	u32 error;
	Semaphore *s = kernelObjects.Get<Semaphore>(id, error);
	if (s)
	{
		if (s->ns.currentCount >= wantedCount) //TODO fix
			s->ns.currentCount -= wantedCount;
		else
		{
			return SCE_KERNEL_ERROR_SEMA_ZERO;
		}

		return 0;
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelPollSema: Trying to poll invalid semaphore %i", id);
		return error;
	}
}

