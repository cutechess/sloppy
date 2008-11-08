#include "thread.h"

#ifdef USE_THREADS

#include "util.h"

#ifdef WINDOWS

void
t_create(LPTHREAD_START_ROUTINE func, void *arg, thread_t *thrd)
{
	DWORD iID;
	*thrd = CreateThread(NULL, 0, func, arg, 0, &iID);
}

void
join_threads(thread_t *threads, int num)
{
	int i;
	DWORD ret = WaitForMultipleObjects(num, threads, TRUE, INFINITE);
	if (ret == WAIT_FAILED)
		fatal_error("Wait failed with error: %d", GetLastError());
	
	for (i = 0; i < num; i++)
		CloseHandle(threads[i]);
}

#else /* not WINDOWS */

#include <string.h>

void
join_threads(thread_t *threads, int num)
{
	int i;
	for (i = 0; i < num; i++) {
		int rc = pthread_join(threads[i], NULL);
		if (rc != 0)
			fatal_error("Can't join thread: %s", strerror(rc));
	}
}

#endif /* not WINDOWS */

#endif /* USE_THREADS */
