#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sched.h>
#include <errno.h>
#include <math.h>

#include "gt_include.h"
extern void uthread_time_init(uthread_time_t* time_new, int tid, int credits_set, int SIZE, long Thread_life, long Cpu_time) {
	time_new->id = tid;
	time_new->credits = credits_set;
	time_new->size = SIZE;
	time_new->cpu_time = Cpu_time;
	time_new->thread_life = Thread_life;
}
extern double cpu_time_mean(uthread_time_t* head, int length) {
	if (length < 0) {
		printf("Invalid");
	}
	double mean = 0.0;
	int l = length;
	uthread_time_t* ptr = head;
	while (l) {
		mean += (double)(ptr->cpu_time)/length;
		--l;
		ptr++ ;
	}

	return mean;
}
extern double thread_life_mean(uthread_time_t* head, int length) {
	if (length < 0) {
		printf("Invalid");
	}
	double mean = 0.0;
	int l = length;
	uthread_time_t* ptr = head;
	while (l) {
		mean += (double)(ptr->thread_life)/length;
		--l;
		++ptr;
	}

	return mean;
}
extern double cpu_time_standard(uthread_time_t* head, int length) {
	if (length < 0 && length == 1) {
		printf("Invalid");
		return 0.0;
	}
	double mean = 0.0;
	double standard = 0.0;

	int l = length;
	uthread_time_t* ptr = head;

	mean = cpu_time_mean(head, length);
	while (l) {
		standard += (double)((ptr->cpu_time - mean)*(ptr->cpu_time - mean))/(length - 1);
		--l;
		++ptr;
	}

	standard = sqrt(standard);
	return standard;
}
extern double thread_life_standard(uthread_time_t* head, int length){
	if (length < 0 && length == 1) {
		printf("Invalid");
		return 0.0;
	}
	double mean = 0.0;
	double standard = 0.0;

	int l = length;
	uthread_time_t* ptr = head;

	mean = thread_life_mean(head, length);
	while (l) {
		standard += (double)((ptr->thread_life - mean)*(ptr->thread_life - mean))/(length - 1);
		--l;
		++ptr;
	}

	standard = sqrt(standard);
	return standard;
}
extern void print_result(uthread_time_t*head){
	// The 16-combninations are: group: (SIZE, CREDITS)
	// 0-7: (256,25)
	// 8-15: (256,50)
	// 16-23: (256,75)
	// 24-31: (256,100)
	// 32-39: (128,25)
	// 40-47: (128,50)
	// 48-55: (128,75)
	// 56-63: (128,100)
	// 64-71: (64,25)
	// 72-79: (64,50)
	// 80-87: (64,75)
	// 88-95: (64,100)
	// 96-103: (32,25)
	// 104-111: (32,50)
	// 112-119: (32,75)
	// 120-127: (32,100)
	uthread_time_t* ptr = head;
	double cpu_mean = 0.0;
	double cpu_standard = 0.0;
	double life_mean = 0.0;
	double life_standard = 0.0;

	printf("====================================================================\n");
	printf("====================================================================\n");

	for (int i = 0, j = 0; i < TH_NUM; i += TH_NUM/GROUP) {
		cpu_mean = cpu_time_mean(ptr, TH_NUM/GROUP);
		cpu_standard = cpu_time_standard(ptr, TH_NUM/GROUP);
		
		life_mean = thread_life_mean(ptr, TH_NUM/GROUP);
		life_standard = thread_life_standard(ptr, TH_NUM/GROUP);
		printf("Group:%d, credits:%d, size:%d\n", ++j, (ptr)->credits, (ptr)->size);
		printf("Mean Execution Time: %fms, Standard:%fms\n", cpu_mean/1000, cpu_standard/1000);
		printf("Mean Thread Life Time: %fms, Standard:%fms\n", life_mean/1000, life_standard/1000);
		printf("--------------------------------------------------------------------\n");
		ptr += TH_NUM/GROUP;
	}
	printf("====================================================================\n");
	printf("====================================================================\n");
}