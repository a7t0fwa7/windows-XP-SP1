#ifndef _WIN32THREAD_H
#define _WIN32THREAD_H
typedef struct win32_cond { LONG waiters; HANDLE sem; } perl_cond;
typedef DWORD perl_key;
typedef HANDLE perl_os_thread;

#ifndef DONT_USE_CRITICAL_SECTION

/* Critical Sections used instead of mutexes: lightweight,
 * but can't be communicated to child processes, and can't get
 * HANDLE to it for use elsewhere.
 */
typedef CRITICAL_SECTION perl_mutex;
#define MUTEX_INIT(m) InitializeCriticalSection(m)
#define MUTEX_LOCK(m) EnterCriticalSection(m)
#define MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define MUTEX_DESTROY(m) DeleteCriticalSection(m)

#else

typedef HANDLE perl_mutex;
#define MUTEX_INIT(m) \
    STMT_START {						\
	if ((*(m) = CreateMutex(NULL,FALSE,NULL)) == NULL)	\
	    croak("panic: MUTEX_INIT");				\
    } STMT_END
#define MUTEX_LOCK(m) \
    STMT_START {						\
	if (WaitForSingleObject(*(m),INFINITE) == WAIT_FAILED)	\
	    croak("panic: MUTEX_LOCK");				\
    } STMT_END
#define MUTEX_UNLOCK(m) \
    STMT_START {						\
	if (ReleaseMutex(*(m)) == 0)				\
	    croak("panic: MUTEX_UNLOCK");			\
    } STMT_END
#define MUTEX_DESTROY(m) \
    STMT_START {						\
	if (CloseHandle(*(m)) == 0)				\
	    croak("panic: MUTEX_DESTROY");			\
    } STMT_END

#endif

/* These macros assume that the mutex associated with the condition
 * will always be held before COND_{SIGNAL,BROADCAST,WAIT,DESTROY},
 * so there's no separate mutex protecting access to (c)->waiters
 */
#define COND_INIT(c) \
    STMT_START {						\
	(c)->waiters = 0;					\
	(c)->sem = CreateSemaphore(NULL,0,LONG_MAX,NULL);	\
	if ((c)->sem == NULL)					\
	    croak("panic: COND_INIT (%ld)",GetLastError());	\
    } STMT_END

#define COND_SIGNAL(c) \
    STMT_START {						\
	if ((c)->waiters > 0 &&					\
	    ReleaseSemaphore((c)->sem,1,NULL) == 0)		\
	    croak("panic: COND_SIGNAL (%ld)",GetLastError());	\
    } STMT_END

#define COND_BROADCAST(c) \
    STMT_START {						\
	if ((c)->waiters > 0 &&					\
	    ReleaseSemaphore((c)->sem,(c)->waiters,NULL) == 0)	\
	    croak("panic: COND_BROADCAST (%ld)",GetLastError());\
    } STMT_END

#define COND_WAIT(c, m) \
    STMT_START {						\
	(c)->waiters++;						\
	MUTEX_UNLOCK(m);					\
	/* Note that there's no race here, since a		\
	 * COND_BROADCAST() on another thread will have seen the\
	 * right number of waiters (i.e. including this one) */	\
	if (WaitForSingleObject((c)->sem,INFINITE)==WAIT_FAILED)\
	    croak("panic: COND_WAIT (%ld)",GetLastError());	\
	/* XXX there may be an inconsequential race here */	\
	MUTEX_LOCK(m);						\
	(c)->waiters--;						\
    } STMT_END

#define COND_DESTROY(c) \
    STMT_START {						\
	(c)->waiters = 0;					\
	if (CloseHandle((c)->sem) == 0)				\
	    croak("panic: COND_DESTROY (%ld)",GetLastError());	\
    } STMT_END

#define DETACH(t) \
    STMT_START {						\
	if (CloseHandle((t)->self) == 0) {			\
	    MUTEX_UNLOCK(&(t)->mutex);				\
	    croak("panic: DETACH");				\
	}							\
    } STMT_END


#define THREAD_CREATE(t, f)	Perl_thread_create(t, f)
#define THREAD_POST_CREATE(t)	NOOP

/* XXX Docs mention that the RTL versions of thread creation routines
 * should be used, but that advice only seems applicable when the RTL
 * is not in a DLL.  RTL DLLs in both Borland and VC seem to do all of
 * the init/deinit required upon DLL_THREAD_ATTACH/DETACH.  So we seem
 * to be completely safe using straight Win32 API calls, rather than
 * the RTL calls.
 *
 * _beginthread() in the RTLs call CloseHandle() just after the thread
 * function returns, which means: 1) we have a race on our hands
 * 2) it is impossible to implement join() semantics.
 *
 * IOW, do *NOT* turn on USE_RTL_THREAD_API!  It is here
 * for experimental purposes only. GSAR 98-01-02
 */
#ifdef USE_RTL_THREAD_API
#  include <process.h>
#  if defined(__BORLANDC__)
     /* Borland RTL doesn't allow a return value from thread function! */
#    define THREAD_RET_TYPE	void _USERENTRY
#    define THREAD_RET_CAST(p)	((void)(thr->i.retv = (void *)(p)))
#  elif defined (_MSC_VER)
#    define THREAD_RET_TYPE	unsigned __stdcall
#    define THREAD_RET_CAST(p)	((unsigned)(p))
#  else
     /* CRTDLL.DLL doesn't allow a return value from thread function! */
#    define THREAD_RET_TYPE	void __cdecl
#    define THREAD_RET_CAST(p)	((void)(thr->i.retv = (void *)(p)))
#  endif
#else	/* !USE_RTL_THREAD_API */
#  define THREAD_RET_TYPE	DWORD WINAPI
#  define THREAD_RET_CAST(p)	((DWORD)(p))
#endif	/* !USE_RTL_THREAD_API */

typedef THREAD_RET_TYPE thread_func_t(void *);


START_EXTERN_C

#if defined(PERLDLL) && defined(USE_DECLSPEC_THREAD) && (!defined(__BORLANDC__) || defined(_DLL))
extern __declspec(thread) struct perl_thread *Perl_current_thread;
#define SET_THR(t)   		(Perl_current_thread = t)
#define THR			Perl_current_thread
#else
#define THR			Perl_getTHR()
#define SET_THR(t)		Perl_setTHR(t)
#endif
struct perl_thread;

void Perl_alloc_thread_key _((void));
int Perl_thread_create _((struct perl_thread *thr, thread_func_t *fn));
void Perl_set_thread_self _((struct perl_thread *thr));
struct perl_thread *Perl_getTHR _((void));
void Perl_setTHR _((struct perl_thread *t));
void Perl_init_thread_intern _((struct perl_thread *t));

END_EXTERN_C

#define INIT_THREADS NOOP
#define ALLOC_THREAD_KEY Perl_alloc_thread_key()
#define SET_THREAD_SELF(thr) Perl_set_thread_self(thr)

#if defined(USE_RTL_THREAD_API) && !defined(_MSC_VER)
#define JOIN(t, avp)							\
    STMT_START {							\
	if ((WaitForSingleObject((t)->self,INFINITE) == WAIT_FAILED)	\
	     || (GetExitCodeThread((t)->self,(LPDWORD)(avp)) == 0)	\
	     || (CloseHandle((t)->self) == 0))				\
	    croak("panic: JOIN");					\
	*avp = (AV *)((t)->i.retv);					\
    } STMT_END
#else	/* !USE_RTL_THREAD_API || _MSC_VER */
#define JOIN(t, avp)							\
    STMT_START {							\
	if ((WaitForSingleObject((t)->self,INFINITE) == WAIT_FAILED)	\
	     || (GetExitCodeThread((t)->self,(LPDWORD)(avp)) == 0)	\
	     || (CloseHandle((t)->self) == 0))				\
	    croak("panic: JOIN");					\
    } STMT_END
#endif	/* !USE_RTL_THREAD_API || _MSC_VER */

#define YIELD			Sleep(0)

#endif /* _WIN32THREAD_H */

