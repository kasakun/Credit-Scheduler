#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>

#include "gt_include.h"

// ** DEBUG
#define DEBUG 1
// ** CREDIT BURN
#define BURN_CREDIT 25
/**********************************************************************/
/** DECLARATIONS **/
/**********************************************************************/


/**********************************************************************/
/* kthread runqueue and env */

/* XXX: should be the apic-id */
#define KTHREAD_CUR_ID	0

/**********************************************************************/
/* uthread scheduling */
static void uthread_context_func(int);
static int uthread_init(uthread_struct_t *u_new);

/**********************************************************************/
/* uthread creation */
#define UTHREAD_DEFAULT_SSIZE (16 * 1024)

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int sched_mode, int credits, int SIZE);

/**********************************************************************/
/** DEFNITIONS **/
/**********************************************************************/

/**********************************************************************/
/* uthread scheduling */

/* Assumes that the caller has disabled vtalrm and sigusr1 signals */
/* uthread_init will be using */
static int uthread_init(uthread_struct_t *u_new)
{
	stack_t oldstack;
	sigset_t set, oldset;
	struct sigaction act, oldact;

	gt_spin_lock(&(ksched_shared_info.uthread_init_lock));

	/* Register a signal(SIGUSR2) for alternate stack */
	act.sa_handler = uthread_context_func;
	act.sa_flags = (SA_ONSTACK | SA_RESTART);
	if(sigaction(SIGUSR2,&act,&oldact))
	{
		fprintf(stderr, "uthread sigusr2 install failed !!");
		return -1;
	}

	/* Install alternate signal stack (for SIGUSR2) */
	if(sigaltstack(&(u_new->uthread_stack), &oldstack))
	{
		fprintf(stderr, "uthread sigaltstack install failed.");
		return -1;
	}

	/* Unblock the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_UNBLOCK, &set, &oldset);


	/* SIGUSR2 handler expects kthread_runq->cur_uthread
	 * to point to the newly created thread. We will temporarily
	 * change cur_uthread, before entering the synchronous call
	 * to SIGUSR2. */

	/* kthread_runq is made to point to this new thread
	 * in the caller. Raise the signal(SIGUSR2) synchronously */
#if 0
	raise(SIGUSR2);
#endif
	syscall(__NR_tkill, kthread_cpu_map[kthread_apic_id()]->tid, SIGUSR2);
	/* Block the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, &oldset);
	if(sigaction(SIGUSR2,&oldact,NULL))
	{
		fprintf(stderr, "uthread sigusr2 revert failed !!");
		return -1;
	}

	/* Disable the stack for signal(SIGUSR2) handling */
	u_new->uthread_stack.ss_flags = SS_DISABLE;

	/* Restore the old stack/signal handling */
	if(sigaltstack(&oldstack, NULL))
	{
		fprintf(stderr, "uthread sigaltstack revert failed.");
		return -1;
	}

	gt_spin_unlock(&(ksched_shared_info.uthread_init_lock));
	return 0;
}

extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(kthread_runqueue_t *))
{
	kthread_context_t *k_ctx;
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_obj;

	/* Signals used for cpu_thread scheduling */
	//kthread_block_signal(SIGVTALRM);
	//kthread_block_signal(SIGUSR1);

#if 0
	fprintf(stderr, "uthread_schedule invoked !!\n");
#endif

	k_ctx = kthread_cpu_map[kthread_apic_id()];
	kthread_runq = &(k_ctx->krunqueue);

	if((u_obj = kthread_runq->cur_uthread))
	{
		/*Go through the runq and schedule the next thread to run */
		kthread_runq->cur_uthread = NULL;
		if(u_obj->uthread_state & (UTHREAD_DONE | UTHREAD_CANCELLED)) // * Zombie Thread
		{
			/* XXX: Inserting uthread into zombie queue is causing improper
			 * cleanup/exit of uthread (core dump) */
			uthread_head_t * kthread_zhead = &(kthread_runq->zombie_uthreads);
			gt_spin_lock(&(kthread_runq->kthread_runqlock));
			kthread_runq->kthread_runqlock.holder = 0x01;
			TAILQ_INSERT_TAIL(kthread_zhead, u_obj, uthread_runq); // * insert (head*, elm*, field)
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));
		
			{
				ksched_shared_info_t *ksched_info = &ksched_shared_info;	
				gt_spin_lock(&ksched_info->ksched_lock);
				ksched_info->kthread_cur_uthreads--;
				gt_spin_unlock(&ksched_info->ksched_lock);
			}
			if (u_obj->uthread_state == UTHREAD_DONE) {
				gettimeofday(&u_obj->cpu_time_2, NULL);
				gettimeofday(&u_obj->vanish_time, NULL);
				// CPU Time Update
				// Thread Time Calculation
				u_obj->cpu_time = (u_obj->cpu_time_2.tv_sec - u_obj->cpu_time_1.tv_sec)*1000000 + u_obj->cpu_time_2.tv_usec - u_obj->cpu_time_1.tv_usec;
				u_obj->thread_life = (u_obj->vanish_time.tv_sec - u_obj->create_time.tv_sec)*1000000 + u_obj->vanish_time.tv_usec - u_obj->create_time.tv_usec;
#if DEBUG
				printf("Thread(id:%u) go to zombie, Lifetime: %luus, CPU TIME: %luus\n", u_obj->uthread_tid, u_obj->thread_life,  u_obj->cpu_time);	
#endif
				uthread_time_t temp;
				uthread_time_init(&temp, u_obj->uthread_tid, u_obj->credits_set, u_obj->size, u_obj->thread_life,  u_obj->cpu_time);
				time_helper[u_obj->uthread_tid] = temp;			
			}
		}
		else // * Normal Thread
		{
			// ** Large change here, Original Code:
			/* XXX: Apply uthread_group_penalty before insertion */
			// u_obj->uthread_state = UTHREAD_RUNNABLE;
			// add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
			// /* XXX: Save the context (signal mask not saved) */
			// if(sigsetjmp(u_obj->uthread_env, 0))
			// 	return;
			u_obj->uthread_state = UTHREAD_RUNNABLE;
			gettimeofday(&u_obj->cpu_time_2, NULL);
			u_obj->cpu_time = (u_obj->cpu_time_2.tv_sec - u_obj->cpu_time_1.tv_sec)*1000000 + u_obj->cpu_time_2.tv_usec - u_obj->cpu_time_1.tv_usec;
			if (u_obj->sched_mode == CREDIT_SCHED) {			
				u_obj->credits -= BURN_CREDIT;
				if (u_obj->credits > 0) {
					add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_obj);
					#if DEBUG
					printf("Thread(id:%u) GID: %d CREDITS: %d go to active\n", u_obj->uthread_tid, u_obj->uthread_gid, u_obj->credits);
					#endif
				}
				else {
					u_obj->credits = u_obj->credits_set;
					add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
					#if DEBUG
					printf("Thread(id:%u) GID: %d CREDITS: %d go to expired\n", u_obj->uthread_tid, u_obj->uthread_gid, u_obj->credits);
					#endif
				}
				/* XXX: Save the context (signal mask not saved) */
				if(sigsetjmp(u_obj->uthread_env, 0)) {
					return;
				}
			}
			else if (u_obj->sched_mode == DEFAULT_SCHED) {
				add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
				/* XXX: Save the context (signal mask not saved) */
				if(sigsetjmp(u_obj->uthread_env, 0))
					return;
			}
			else{
				printf("Fatal error: Unknown scheduling mode. Quitting...");
				exit(0);
			}
		}
	}

	/* kthread_best_sched_uthread acquires kthread_runqlock. Dont lock it up when calling the function. */
	if(!(u_obj = kthread_best_sched_uthread(kthread_runq)))
	{
		/* Done executing all uthreads. Return to main */
		/* XXX: We can actually get rid of KTHREAD_DONE flag */
		if(ksched_shared_info.kthread_tot_uthreads && !ksched_shared_info.kthread_cur_uthreads)
		{
			fprintf(stderr, "Quitting kthread (%d)\n", k_ctx->cpuid);
			k_ctx->kthread_flags |= KTHREAD_DONE;
		}

		siglongjmp(k_ctx->kthread_env, 1);
		return;
	}

	kthread_runq->cur_uthread = u_obj;
	if((u_obj->uthread_state == UTHREAD_INIT) && (uthread_init(u_obj)))
	{
		fprintf(stderr, "uthread_init failed on kthread(%d)\n", k_ctx->cpuid);
		exit(0);
	}

	u_obj->uthread_state = UTHREAD_RUNNING;
	
	/* Re-install the scheduling signal handlers */
	kthread_install_sighandler(SIGVTALRM, k_ctx->kthread_sched_timer);
	kthread_install_sighandler(SIGUSR1, k_ctx->kthread_sched_relay);
	/* Jump to the selected uthread context */
	// * DEBUG 
	// printf("Thread:%d jump back, state:%d\n", u_obj->uthread_tid, u_obj->uthread_state);
	gettimeofday(&u_obj->cpu_time_1, NULL);
	siglongjmp(u_obj->uthread_env, 1);

	return;
}


/* For uthreads, we obtain a seperate stack by registering an alternate
 * stack for SIGUSR2 signal. Once the context is saved, we turn this 
 * into a regular stack for uthread (by using SS_DISABLE). */
static void uthread_context_func(int signo)
{
	uthread_struct_t *cur_uthread;
	kthread_runqueue_t *kthread_runq;

	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);

	//printf("..... uthread_context_func .....\n");
	/* kthread->cur_uthread points to newly created uthread */
	if(!sigsetjmp(kthread_runq->cur_uthread->uthread_env,0))
	{
		/* In UTHREAD_INIT : saves the context and returns.
		 * Otherwise, continues execution. */
		/* DONT USE any locks here !! */
		assert(kthread_runq->cur_uthread->uthread_state == UTHREAD_INIT);
		kthread_runq->cur_uthread->uthread_state = UTHREAD_RUNNABLE;
		return;
	}

	/* UTHREAD_RUNNING : siglongjmp was executed. */
	cur_uthread = kthread_runq->cur_uthread;
	assert(cur_uthread->uthread_state == UTHREAD_RUNNING);
	/* Execute the uthread task */
	
	//struct timeval start_time, end_time;

	//gettimeofday(&start_time, NULL);
#if DEBUG
	printf("Thread(id:%d) occupies cpu(%d)...\n", cur_uthread->uthread_tid, cur_uthread->uthread_gid);
#endif
	cur_uthread->uthread_func(cur_uthread->uthread_arg);
#if DEBUG
	printf("Thread(id:%d) leaves cpu(%d)...\n", cur_uthread->uthread_tid, cur_uthread->uthread_gid);
#endif
	//gettimeofday(&end_time, NULL);

	//printf("Thread(id:%u) thread_func_life:%lu\n", cur_uthread->uthread_tid, (end_time.tv_usec - start_time.tv_usec));
	cur_uthread->uthread_state = UTHREAD_DONE;
	uthread_schedule(&sched_find_best_uthread);
	return;
}

/**********************************************************************/
/* uthread creation */

extern kthread_runqueue_t *ksched_find_target(uthread_struct_t *);

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int sched_mode, int credits, int SIZE)
{
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_new;
	unsigned int pri;

	/* Signals used for cpu_thread scheduling */
	// kthread_block_signal(SIGVTALRM);
	// kthread_block_signal(SIGUSR1);

	/* create a new uthread structure and fill it */
	if(!(u_new = (uthread_struct_t *)MALLOCZ_SAFE(sizeof(uthread_struct_t))))
	{
		fprintf(stderr, "uthread mem alloc failure !!");
		exit(0);
	}
	gettimeofday(&u_new->create_time, NULL);
	u_new->uthread_state = UTHREAD_INIT;
	
	pri = (credits == 100) ? 0 : ((credits == 75) ? 4 : ((credits == 50) ? 8 : ((credits == 25) ? 16: -1)));
	
	u_new->uthread_priority = sched_mode == CREDIT_SCHED ? pri : DEFAULT_UTHREAD_PRIORITY;
	u_new->uthread_gid = u_gid;
	u_new->uthread_func = u_func;
	u_new->uthread_arg = u_arg;

	// ** Initialize
	u_new->sched_mode = sched_mode; // *cannot directly use u_arg here
	u_new->credits_set = credits;
	u_new->credits = u_new->credits_set;
	u_new->size = SIZE;

	// time
	u_new->thread_life = 0;
	u_new->cpu_time = 0;


	/* Allocate new stack for uthread */
	u_new->uthread_stack.ss_flags = 0; /* Stack enabled for signal handling */
	if(!(u_new->uthread_stack.ss_sp = (void *)MALLOC_SAFE(UTHREAD_DEFAULT_SSIZE)))
	{
		fprintf(stderr, "uthread stack mem alloc failure !!");
		return -1;
	}
	u_new->uthread_stack.ss_size = UTHREAD_DEFAULT_SSIZE;


	{
		ksched_shared_info_t *ksched_info = &ksched_shared_info;

		gt_spin_lock(&ksched_info->ksched_lock);
		u_new->uthread_tid = ksched_info->kthread_tot_uthreads++;
		ksched_info->kthread_cur_uthreads++;
		gt_spin_unlock(&ksched_info->ksched_lock);
	}

	/* XXX: ksched_find_target should be a function pointer */
	kthread_runq = ksched_find_target(u_new);

	*u_tid = u_new->uthread_tid;
	/* Queue the uthread for target-cpu. Let target-cpu take care of initialization. */
	//printf("Thread:%d, go to active queue.\n", *u_tid);
	add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_new);
	//printf("Thread(id:%u) born:%luus\n", u_new->uthread_tid, u_new->create_time.tv_usec);

	/* WARNING : DONOT USE u_new WITHOUT A LOCK, ONCE IT IS ENQUEUED. */

	/* Resume with the old thread (with all signals enabled) */
	// kthread_unblock_signal(SIGVTALRM);
	// kthread_unblock_signal(SIGUSR1);

	return 0;
}

// ** Yield()
extern void gt_yield() {
	uthread_schedule(&sched_find_best_uthread);
	return;
}

#if 0
/**********************************************************************/
kthread_runqueue_t kthread_runqueue;
kthread_runqueue_t *kthread_runq = &kthread_runqueue;
sigjmp_buf kthread_env;

/* Main Test */
typedef struct uthread_arg
{
	int num1;
	int num2;
	int num3;
	int num4;	
} uthread_arg_t;

#define NUM_THREADS 10
static int func(void *arg);

int main()
{
	uthread_struct_t *uthread;
	uthread_t u_tid;
	uthread_arg_t *uarg;

	int inx;

	/* XXX: Put this lock in kthread_shared_info_t */
	gt_spinlock_init(&uthread_group_penalty_lock);

	/* spin locks are initialized internally */
	kthread_init_runqueue(kthread_runq);

	for(inx=0; inx<NUM_THREADS; inx++)
	{
		uarg = (uthread_arg_t *)MALLOC_SAFE(sizeof(uthread_arg_t));
		uarg->num1 = inx;
		uarg->num2 = 0x33;
		uarg->num3 = 0x55;
		uarg->num4 = 0x77;
		uthread_create(&u_tid, func, uarg, (inx % MAX_UTHREAD_GROUPS));
	}

	kthread_init_vtalrm_timeslice();
	kthread_install_sighandler(SIGVTALRM, kthread_sched_vtalrm_handler);
	if(sigsetjmp(kthread_env, 0) > 0)
	{
		/* XXX: (TODO) : uthread cleanup */
		exit(0);
	}
	
	uthread_schedule(&ksched_priority);
	return(0);
}

static int func(void *arg)
{
	unsigned int count;
#define u_info ((uthread_arg_t *)arg)
	printf("Thread %d created\n", u_info->num1);
	count = 0;
	while(count <= 0xffffff)
	{
		if(!(count % 5000000))
			printf("uthread(%d) => count : %d\n", u_info->num1, count);
		count++;
	}
#undef u_info
	return 0;
}
#endif
