#include <ogcsys.h>


#ifndef _PTHREADHACK_
#define _PTHREADHACK_

#define PTHREAD_MUTEX_INITIALIZER 0

extern void pthread_mutex_lock(mutex_t* i)
{
	LWP_MutexLock(*i);
}

extern void pthread_mutex_unlock(mutex_t* i)
{
	LWP_MutexUnlock(*i);
}

#endif