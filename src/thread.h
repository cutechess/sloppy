#ifndef THREAD_H
#define THREAD_H

#ifdef USE_THREADS
#include "sloppy.h"


/* Wrappers for native thread functions.  */

#ifdef WINDOWS
	#include <windows.h>

	typedef HANDLE thread_t;
	typedef CRITICAL_SECTION mutex_t;
	#define tfunc_t DWORD WINAPI
	
	#define mutex_init(x) InitializeCriticalSection(x)
	#define mutex_destroy(x) DeleteCriticalSection (x)
	#define mutex_lock(x) EnterCriticalSection(x)
	#define mutex_unlock(x) LeaveCriticalSection(x)

	extern void t_create(LPTHREAD_START_ROUTINE func, void *arg, thread_t *thrd);
#else /* not WINDOWS */
	#include <pthread.h>

	typedef pthread_t thread_t;
	typedef pthread_mutex_t mutex_t;
	#define tfunc_t void*

	#define mutex_init(x)    pthread_mutex_init((x), NULL)
	#define mutex_destroy(x) pthread_mutex_destroy(x)
	#define mutex_lock(x)    pthread_mutex_lock(x)
	#define mutex_unlock(x)  pthread_mutex_unlock(x)

	#define t_create(func, arg, thrd) pthread_create(thrd, NULL, func, arg)
#endif /* not WINDOWS */

extern void join_threads(thread_t *threads, int num);

#endif /* USE_THREADS */
#endif /* THREAD_H */

