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

void *page_fault_func(void *t);
void *tlb_thrashing_func(void *t);

#define NR 4
#define ILL_INSTR 0xFF
static int exec_size;
static sigjmp_buf senv[NR];
static volatile int jump_ok[NR];
static volatile int total = 0;
static void *exec_mem[NR];
static pthread_t create_thread[NR];
static pthread_t destroy_thread[NR];
static pthread_mutex_t mutex[NR];
static bool destroy_page = false;

static int get_thread_index(pthread_t threads[])
{
	pthread_t tid;
	int id;
	int i;

	tid = pthread_self();
	id = -1;
	for (i = 0; i < NR; i++) {
		if (tid == threads[i]) {
			id = i;
			break;
		}
	}
	if (id == -1) {
		printf("cannot find thread index for thread %x\n",
				(unsigned int) tid);
		exit(EXIT_FAILURE);
	}
	return id;
}

static void sigact_handler(int signo, siginfo_t *si, void *data)
{
	char *str;
	int id;

	/* find the ID number, so we can jump back correct thread */
	id = get_thread_index(create_thread);
	debug_printf("signal is %s; ", strsignal(signo));

	str = "";
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

	debug_printf("thread is %d; illegal reason: %s at %p\n", id, str,
			si->si_addr);

	if (1 != jump_ok[id])
		printf("jump to thread %d is not ready\n", id);
	else
		/* jump back to the bofore of illegal instruction */
		siglongjmp(senv[id], id + 1); /* cannot pass id 0 here */
}

static void *create_func(void *t)
{
	int id;
	int ret;

	id = get_thread_index(create_thread);

	/* we restart from here */
	ret = sigsetjmp(senv[id], 1);
	if (ret == 0) {
		jump_ok[id] = 1;
		debug_printf("sigsetjmp registered\n");
	} else if (ret > 0) {
		debug_printf("sigsetjmp jump back to thread %d\n", id);
		if ((ret - 1) != id)
			err_printf("jump back to the wrong thread %d -> %d\n",
					ret - 1, id);
	}

	/* the creation of (fault) page itself is atomic */
	pthread_mutex_lock(&mutex[id]);
	if (exec_mem[id] == NULL) {
		exec_mem[id] = mmap(NULL, exec_size, PROT_READ | PROT_WRITE |
				PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
		if (exec_mem[id] == MAP_FAILED)
			err_printf("cannot do mmap on thread %d\n", id);
		/* put invalid data for the executable */
		memset(exec_mem[id], ILL_INSTR, exec_size);
		debug_printf("executable address for thread %d is %p\n",
				id, exec_mem[id]);
	}
	total++;
	debug_printf("%dth error on thread %d\n", total, id);
	if ((total % 1000) == 0) {
		printf("."); fflush(stdout);
		total = 0;
	}
	pthread_mutex_unlock(&mutex[id]);

	/*
	 * Two cases here after the mutex:
	 * *) if mem get munmapped by another thread, we will see page fault
	 * *) if mem is still valid, we will see invalid instruction
	 * Either way, we will handle it with signal handler and jump back
	 * again
	 */
	((void(*)(void)) exec_mem[id]) ();

	pthread_exit(NULL);
	return NULL;
}

static void *destroy_func(void *t)
{
	int id;

	id = get_thread_index(destroy_thread);
	while (1) {
		/* the destroying of page itself is atomic */
		pthread_mutex_lock(&mutex[id]);
		if (destroy_page == true && exec_mem[id] != NULL) {
			munmap(exec_mem[id], exec_size);
			exec_mem[id] = NULL;
		}
		pthread_mutex_unlock(&mutex[id]);
	}
	pthread_exit(NULL);
	return NULL;
}

void set_signal_handler(void)
{
	struct sigaction sa, osa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = sigact_handler;
	sigaction(SIGILL, &sa, &osa);
	if (destroy_page == true)
		sigaction(SIGSEGV, &sa, &osa);
	/*
	 * each pthread will inherent above signal handler SIGILL and
	 * SIGSEGV, becasue it is HW error signal.
	 */
}

int handle_parameters(int argc, char *argv[])
{
	if (argv[1] != NULL && 0 == (strncmp("--destroy", argv[1], 9))) {
		destroy_page = true;
		printf("destroy exec page too\n");
	}

	if (argv[1] != NULL && 0 == (strncmp("-h", argv[1], 2))) {
		printf("%s --destroy\n", argv[0]);
		return 0;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int t;
	pthread_t tlb_thrashing_thread;
	pthread_t page_fault_thread;

	handle_parameters(argc, argv);
	/* set up signal handler for the page fault and invalid instruction */
	set_signal_handler();

	exec_size = sysconf(_SC_PAGE_SIZE);
	for (t = 0; t < NR; t++) {
		pthread_mutex_init(&mutex[t], NULL);
		pthread_create(&create_thread[t], NULL, create_func, &t);
		pthread_create(&destroy_thread[t], NULL, destroy_func, &t);
	}
	pthread_create(&tlb_thrashing_thread, NULL, tlb_thrashing_func, NULL);
	pthread_create(&page_fault_thread, NULL, page_fault_func, NULL);


	pthread_join(tlb_thrashing_thread, NULL);
	pthread_join(page_fault_thread, NULL);
	for (t = 0; t < NR; t++) {
		pthread_mutex_destroy(&mutex[t]);
		pthread_join(create_thread[t], NULL);
		pthread_join(destroy_thread[t], NULL);
	}

	pthread_exit(NULL);

	return 0;
}
