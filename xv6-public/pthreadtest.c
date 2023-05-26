#include "types.h"
#include "user.h"
#include "x86.h"

#include "pthread.h"

#define NULL ((void *)0)

static volatile int counter = 0;

void *myThread(void *arg)
{

	//sleep(100);
	printf(1, "%s: begin counter: %d\n", (char *)arg, counter);
	int i;
	int a;
	if(*((char*)arg) == 'A') {
		a = 20000;
	} else if(*((char*)arg) == 'B'){
		a = 5000;
	}
	else {
		a = 1;
		
	}
	//printf(1, "%s: i created\n", (char *)arg);
	for (i = 0; i < a; i++)
	{
		counter = counter + 1;
		/*
		if(i % 1000==0){
			printf(1, "%s: i: %d\n", (char *)arg, i);
			//sleep(100);
		}*/
	}/*
	while(1) {

	}*/
	//printf(1, "%s: done\n", (char *)arg);
	thread_exit(((void*)&i));
	while(1) {
		//suspend quiting thread
	}
	return NULL;
} 

int main()
{
	// Start
	printf(1, "main: begin (counter = %d)\n", counter);
	// Create threads
	/*
    if(fork() > 0) {
		if(fork() > 0) {
			wait();
		}
		else {
			exit();
		}
		wait();
	}else {
		exit();
	}
    */
	thread_t p1;
	thread_t p2; // thread identifiers.
	printf(1, "main: create");
	thread_create(&p1, myThread, "A");
	printf(1, " (p1 = %d)\n", p1);

	printf(1, "main: create");
	thread_create(&p2, myThread, "B");
	printf(1, " (p2 = %d)\n", p2);

/*
	// Wait for threads
	while(1) {

	}*/
	void *retval1;
	void *retval2;
	printf(1, "p1 %d joined: %d retval: %d\n",p1,thread_join(p1, &retval1), *((int*)retval1));
	printf(1, "p2 %d joined: %d retval: %d\n",p2,thread_join(p2, &retval2), *((int*)retval2));
    // Done
	/*while(1) {
		
	}*/


	sleep(1000);
	printf(1, "main: done with both (counter = %d)\n", counter);
	while(1) {

	} 
	exit();
}