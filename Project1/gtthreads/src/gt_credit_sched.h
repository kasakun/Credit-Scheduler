#ifndef __GT_CREDITS_SCHEDULER_H
#define __GT_CREDITS_SCHEDULER_H 

#define UTHREAD_CRED_UNDER 0x01  // ** Under queue
#define UTHREAD_CRED_OVER 0x02  // ** Over queue


extern int credits_init(int thread_id);
extern void credits_burn(uthread_struct_t *u_obj);
extern int uthread_priority(uthread_struct_t *u_obj);


#endif