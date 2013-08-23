#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdbool.h>

#if 0
#define debug_printf(format, ...) printf(format, ##__VA_ARGS__)
#else
#define debug_printf(format, ...) do {} while(0)
#endif
#define err_printf(format, ...)				\
		do {					\
			printf(format, ##__VA_ARGS__);	\
			exit(EXIT_FAILURE);		\
		} while(0)

#define NR 60
#define ILL_INSTR 0xFF
const int exec_size = 0x1000;
static sigjmp_buf senv[NR];
static volatile int jump_ok[NR];
static volatile int total = 0;
static void *exec_mem[NR];
static pthread_t create_thread[NR];
static pthread_t destroy_thread[NR];
static pthread_mutex_t mutex[NR];
static int main_tid = 0;

static bool destroy_page = false;

static void sigact_handler(int signo, siginfo_t *si, void *data)
{
	char *str;
	int id;

	debug_printf("id %d\n", ((int) gettid() - main_tid) / 2);
	debug_printf("signal is %s; ", strsignal(signo));
	id = ((int) gettid() - main_tid) / 2;
	if (signo == SIGILL) {
		switch(si->si_code) {
		case ILL_ILLOPC:
			str = "illegal opcode"; break;
		case ILL_ILLOPN:
			str = "illegal operand"; break;
		case ILL_ILLADR:
			str = "illegal address mode"; break;
		case ILL_ILLTRP:
			str = "illegal trap"; break;
		default:
			str = "more error code?";
		}
	}
	if (signo == SIGSEGV) {
		switch(si->si_code) {
		case SEGV_MAPERR:
			str = "mapping error"; break;
		case SEGV_ACCERR:
			str = "invalid permissios for mapped object"; break;
		default:
			str = "wrong sigsegv si code";
		}
	}
	debug_printf("thread is %d; illegal reason: %s at %p\n", id, str, si->si_addr);

	if (1 != jump_ok[id])
		printf("jump to thread %d is not ready\n", id);
	else
		/* jump back to the bofore of illegal instruction */
		siglongjmp(senv[id], id + 1); /* cannot pass id 0 here */
}

static void *create_func(void *t)
{
	int tid;
	int ret;

	tid = (int) t;

	debug_printf("%d == %d\n", tid, ((int) gettid() - main_tid) / 2);

	/* we restart from here */
	ret = sigsetjmp(senv[tid], 1);
	if (ret == 0) {
		jump_ok[tid] = 1;
		debug_printf("sigsetjmp registered\n");
	} else if (ret > 0) {
		debug_printf("sigsetjmp jump back to thread %d\n", tid);
		if ((ret - 1) != tid)
			err_printf("jump back to the wrong thread %d -> %d\n",
					ret - 1, tid);
	}

	/* the creation of (fault) page itself is atomic */
	pthread_mutex_lock(&mutex[tid]);
	if (exec_mem[tid] == NULL) {
		exec_mem[tid] = mmap(NULL, exec_size, PROT_WRITE | PROT_EXEC,
						MAP_ANON | MAP_PRIVATE, -1, 0);
		if (exec_mem[tid] == MAP_FAILED)
			err_printf("cannot do mmap on thread %d\n", tid);
		/* put invalid data for the executable */
		memset(exec_mem[tid], ILL_INSTR, exec_size);
		debug_printf("executable address is %p\n", exec_mem[tid]);
	}
	total++;
	debug_printf("%dth error on thread %d\n", total, tid);
	if ((total % 10000) == 0) {
		printf("."); fflush(stdout);
		total = 0;
	}
	pthread_mutex_unlock(&mutex[tid]);

	/*
	 * Two cases here after the mutex:
	 * *) if mem get munmapped by another thread, we will see page fault
	 * *) if mem is still valid, we will see invalid instruction
	 * Either way, we will handle it with signal handler and jump back
	 * again
	 */
	((void(*)(void)) exec_mem[tid]) ();

	pthread_exit(NULL);
	return NULL;
}

static void *destroy_func(void *t)
{
	int tid;

	tid = (int) t;
	while (1) {
		/* the destroying of page itself is atomic */
		pthread_mutex_lock(&mutex[tid]);
		if (destroy_page == true && exec_mem[tid] != NULL) {
			munmap(exec_mem[tid], exec_size);
			exec_mem[tid] = NULL;
		}
		pthread_mutex_unlock(&mutex[tid]);
	}
	pthread_exit(NULL);
	return NULL;
}

int main(int argc, char *argv[])
{
	int t;
	struct sigaction sa, osa;

	if (argv[1] != NULL && 0 == (strncmp("--destroy", argv[1], 9))) {
		destroy_page = true;
		printf("destroy exec page too\n");
	}

	/* set up signal handler for the page fault and invalid instruction */
	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = sigact_handler;
	sigaction(SIGILL, &sa, &osa);
	if (destroy_page == true)
		sigaction(SIGSEGV, &sa, &osa);

	/* each pthread will inherent above signal handler */

	main_tid = (int) gettid();
	for (t = 0; t < NR; t++) {
		pthread_create(&create_thread[t], NULL, create_func, (void *) t);
		pthread_create(&destroy_thread[t], NULL, destroy_func, (void *) t);
	}

	pthread_exit(NULL);

	return 0;
}
