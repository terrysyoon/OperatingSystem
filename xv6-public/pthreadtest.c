#include "types.h"
#include "user.h"
#include "x86.h"

#include "pthread.h"

#define NULL ((void *)0)

static volatile int counter = 0;

void *myThread(void *arg)
{
	printf(1, "%s: begin counter: %d\n", (char *)arg, counter);
	int i;

	//printf(1, "%s: i created\n", (char *)arg);
	for (i = 0; i < 10000000; i++)
	{
		counter = counter + 1;
	}
	//printf(1, "%s: done\n", (char *)arg);
	thread_exit(NULL);
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
	thread_t p1, p2; // thread identifiers.
	printf(1, "main: create");
	thread_create(&p1, myThread, "A");
	printf(1, " (p1 = %d)\n", p1);

	printf(1, "main: create");
	thread_create(&p2, myThread, "B");
	printf(1, " (p2 = %d)\n", p2);

	// Wait for threads
	printf(1, "p1 %d joined: %d\n",p1,thread_join(p1, NULL));
	printf(1, "p2 %d joined: %d\n",p2,thread_join(p2, NULL));
    // Done
	printf(1, "main: done with both (counter = %d)\n", counter);

	exit();
}