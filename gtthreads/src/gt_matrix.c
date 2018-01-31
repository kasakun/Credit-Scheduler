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
#include "gt_credit_sched.h"


#define ROWS 256		  // ** Changed to 256(max size)
#define COLS ROWS
#define SIZE COLS

#define NUM_THREADS 128    // ** Changed to 128 threads
#define PER_THREAD_ROWS (SIZE/NUM_THREADS)

#define NUM_CPUS 2         // ** Assign different work to the core
#define NUM_GROUPS NUM_CPUS
#define PER_GROUP (SIZE/NUM_GROUPS)


/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct matrix // * Matrix Structure
{
	int m[SIZE][SIZE];

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;


typedef struct __uthread_arg // * Assign work to each thread
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;

	unsigned int tid;		// * thread id
	unsigned int gid;		// * thread id in each cpu
	int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
	int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
	int end_row;	// ** Limit the size of the matrix
	int end_col;	// ** Limit the size of the matrix

	int credits;  	// ** Credits added
	int sched_mode; // ** Scheduler mode added
	
}uthread_arg_t;
	
struct timeval tv1;

static void generate_matrix(matrix_t *mat, int val)
{

	int i,j;
	mat->rows = SIZE;
	mat->cols = SIZE;
	for(i = 0; i < mat->rows;i++)
		for( j = 0; j < mat->cols; j++ )
		{
			mat->m[i][j] = val;
		}

	return;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	for(i=0;i<SIZE;i++)
	{
		for(j=0;j<SIZE;j++)
			printf(" %d ",mat->m[i][j]);
		printf("\n");
	}

	return;
}

static void * uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	unsigned int cpuid;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

	start_row = 0;
	end_row = ptr->end_row;
	start_col = 0;
	end_col = ptr->end_col;

#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	//fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
#else
	//fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
#endif
	//printf("Thread(id:%d, group/cpu:%d) start...\n", ptr->tid, ptr->gid);
	//printf("Row:%d, Col:%d, Start!\n", end_row, end_col);
	for(i = start_row; i < end_row; i++)
		for(j = start_col; j < end_col; j++)
			for(k = 0; k < end_col; k++) {// ** Change dimension here.
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];
				//printf("k:%d\n", ptr->_C->m[i][j]);
			}
	//printf("Thread(id:%d, group/cpu:%d) Finish!\n", ptr->tid, ptr->gid);		
	//printf("Thread(id:%d, group:%d, cpu:%d) complete! Time used:%lu\n", ptr->tid, ptr->gid, cpuid, (end_time.tv_usec - start_time.tv_usec));

#ifdef GT_THREADS
	// fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
	//		ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#else
	//fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
	//		ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif

#undef ptr
	return 0;
}



// static void init_matrices()
// {
// 	generate_matrix(&A, 1);
// 	generate_matrix(&B, 1);
// 	generate_matrix(&C, 0);

// 	return;
// }

void resize(int thread_id, int* size) // ** Size Modification
{
	if (thread_id < 32)
	{
		*size = 256;
	}
	else if (thread_id < 64)
	{
		*size = 128;
	}
	else if (thread_id < 96)
	{
		*size = 64;
	}
	else if (thread_id < 128)
	{
		*size = 32;
	}
	else
	{
		printf(stderr, "Threads exceeds!\n");
	}
}

uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main()
{
	uthread_arg_t *uarg;// * Point to lists of u_arg
	unsigned int inx;            // * Thread_id
	matrix_t A, B;
	generate_matrix(&A, 1);
	generate_matrix(&B, 1);

	gtthread_app_init();
	// printf("Initialize complete\n");

	for(inx=0; inx<NUM_THREADS; inx++)  // 
	{
		matrix_t C;
		int size;			// ** Matrix Size

		generate_matrix(&C, 0);
		
		uarg = &uargs[inx];
		uarg->_A = &A;
		uarg->_B = &B;
		uarg->_C = &C;

		uarg->tid = inx;

		uarg->gid = (inx % NUM_GROUPS); 
		resize(inx, &size);

		uarg->start_row = 0;
		uarg->end_row = size;
		uarg->start_col = 0;
		uarg->end_col = size;

		uarg->sched_mode = CREDIT_SCHED;
		//uarg->sched_mode = DEFAULT_SCHED;
		uarg->credits = credits_init(inx);

		uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, uarg->sched_mode, uarg->credits, size);
	}

	gtthread_app_exit();
#if 0
	printf("==============TEST=================\n");
	printf("Thread 0 cpu_time: %lu\n", time_helper[0].cpu_time);
	printf("Thread 1 cpu_time: %lu\n", time_helper[1].cpu_time);
	printf("MEAN cpu_time: %f\n", cpu_time_mean(&time_helper, 2));
	printf("MEAN cpu_time: %f\n", cpu_time_standard(&time_helper, 2));
#endif
	print_result(&time_helper);
	// fprintf(stderr, "********************************");
	return(0);

}
