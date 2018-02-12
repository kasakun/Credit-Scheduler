#ifndef __GT_STATS_H
#define __GT_STATS_H

#define TH_NUM 128  
#define GROUP 16

typedef struct uthread_time {
	int id;
	int credits;
	int size;

	long thread_life;
	long cpu_time;
	
} uthread_time_t;

extern void uthread_time_init(uthread_time_t* time_new, int tid, int credits_set, int SIZE, long Thread_life, long Cpu_time);
extern double cpu_time_mean(uthread_time_t* head, int length);
extern double thread_life_mean(uthread_time_t* head, int length);
extern double cpu_time_standard(uthread_time_t* head, int length);
extern double thread_life_standard(uthread_time_t* head, int length);
extern void print_result(uthread_time_t*head);

uthread_time_t time_helper[TH_NUM];

#endif 