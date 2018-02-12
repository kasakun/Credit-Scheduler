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
#include <time.h>

#include "gt_include.h"
#define NUM_THREADS 128

extern int credits_init(int thread_id)
{
	if (thread_id < 0 || thread_id > 127)
	{
		printf(stderr, "Thread exceeds!\n");
		return -1;
	}
	if(thread_id < 8 || (thread_id > 31 && thread_id < 40) || (thread_id > 63 && thread_id < 72) || (thread_id > 95 && thread_id < 104))
		return 25;
	else if ((thread_id >7 && thread_id < 16)|| (thread_id >39 && thread_id < 48)|| (thread_id >71 && thread_id < 80)|| (thread_id >103 && thread_id < 112))
		return 50;
	else if ((thread_id > 15 && thread_id < 24) ||(thread_id > 47 && thread_id < 56)|| (thread_id > 79 && thread_id < 88) || (thread_id > 111 && thread_id < 120))
		return 75;
	else 
		return 100;
}

extern credits_burn(uthread_struct_t *u_obj)
{
	if (u_obj->credits > 0)
		u_obj->credits -= 25;
}