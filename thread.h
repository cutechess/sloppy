#ifndef THREAD_H
#define THREAD_H

#ifdef USE_THREADS

#include "sloppy.h"

#ifdef WINDOWS
	#include <windows.h>

	#define thread_t HANDLE
	#define THREAD_FUNC DWORD WINAPI
	#define THREAD_ARG LPVOID

	#define mutex_t CRITICAL_SECTION
	#define mutex_init(x) InitializeCriticalSection(x)
	#define mutex_destroy(x) DeleteCriticalSection (x)
	#define mutex_lock(x) EnterCriticalSection(x)
	#define mutex_unlock(x) LeaveCriticalSection(x)

	extern void t_create(LPTHREAD_START_ROUTINE func, void *arg, thread_t *thrd);
#else /* not WINDOWS */
	#include <pthread.h>

	#define thread_t pthread_t
	#define THREAD_FUNC void*
	#define THREAD_ARG void*
	#define t_create(func, arg, thrd) pthread_create(thrd, NULL, func, arg)

	#define mutex_t pthread_mutex_t
	#define mutex_init(x)    pthread_mutex_init((x), NULL)
	#define mutex_destroy(x) pthread_mutex_destroy(x)
	#define mutex_lock(x)    pthread_mutex_lock(x)
	#define mutex_unlock(x)  pthread_mutex_unlock(x)
#endif /* not WINDOWS */

extern void join_threads(thread_t *threads, int num);

#endif /* USE_THREADS */
#endif /* THREAD_H */
