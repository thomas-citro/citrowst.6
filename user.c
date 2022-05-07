#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>	
#include <semaphore.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <time.h>

#include "config.h"
#include "structs.h"
#include "prototypes.h"

int shmid;
sm* ptr;
sem_t *sem;
int messageQ;
struct message msg;

int main(int argc, char* argv[]) {
	// Seed random number generator
	time_t t;
	time(&t);
	srand((int)time(&t) % getpid());
	
	// Setup resources (shared memory, message queue, semaphore)
	if ((shmid = shmget(SHM_KEY, sizeof(sm), 0600)) < 0) {
		perror("Error: shared memory");
		exit(0);
	}
	if ((messageQ = msgget(MSG_KEY, 0777)) == -1 ) {
		perror("Error: message queue");
		exit(0);
	}
	sem = sem_open("thomas_sem", 0); // Open semaphore
	ptr = shmat(shmid, NULL, 0); // Attach shared memory
	
	// Set time for when proc should either request or release
	int nextMove = getRandomInteger(0, ONE_MILLION); // Between 0ms and 1ms
	struct time moveTime;
	sem_wait(sem);
	moveTime.seconds = ptr->time.seconds;
	moveTime.nanoseconds = ptr->time.nanoseconds;
	sem_post(sem);

	while(1) {
		// Check if it's time for the next action
		if((ptr->time.seconds > moveTime.seconds) || (ptr->time.seconds == moveTime.seconds && ptr->time.nanoseconds >= moveTime.nanoseconds)) {
			sem_wait(sem);
			moveTime.seconds = ptr->time.seconds;
			moveTime.nanoseconds = ptr->time.nanoseconds;
			sem_post(sem);
			incrementClock(&moveTime, 0, nextMove);
			
			// Check if request should be read or write (biased towards read)
			if(getRandomInteger(0, 100) < 75) {
				strcpy(msg.mtext, "READ");
				msg.msgType = 99;
				msgsnd(messageQ, &msg, sizeof(msg), 0);

				// Random value from 0 to limit of pages proc has access to. Multiplied by 1024 with an additional offset in range (0, 1023).
				// 31998 is the limit. Any more and it will cause a segmentation fault.
				int request = getRandomInteger(0, 31998);
				sprintf(msg.mtext, "%d", request);
				msgsnd(messageQ, &msg, sizeof(msg), 0);
			} else {
				strcpy(msg.mtext, "WRITE");
				msg.msgType = 99;
				msgsnd(messageQ, &msg, sizeof(msg),0);
				
				// Random value from 0 to limit of pages proc has access to. Multiplied by 1024 with an additional offset in range (0, 1023).
				// 31998 is the limit. Any more and it will cause a segmentation fault.
				int write = getRandomInteger(0, 31998);
				sprintf(msg.mtext, "%d", write);
				msgsnd(messageQ, &msg, sizeof(msg),0);
			}
		}

		// Check for termination every 1000 memory references
		if((ptr->resourceStruct.count % 1000) == 0 && ptr->resourceStruct.count!=0) {
			if(getRandomInteger(0, 100) < 75) {
				strcpy(msg.mtext, "KILLED");
				msg.msgType = 99;
				msgsnd(messageQ, &msg, sizeof(msg),0);
			}
		}

		exit(0);
	}
	return 0;
}

// Increments the protected clock
void incrementClock(struct time* time, int sec, int ns) {
	sem_wait(sem);
	time->seconds += sec;
	time->nanoseconds += ns;
	while(time->nanoseconds >= ONE_BILLION) {
		time->nanoseconds -= ONE_BILLION;
		time->seconds++;
	}
	sem_post(sem);
}

// Return random integer between lower and upper bounds (inclusive)
int getRandomInteger(int lower, int upper) {
	int num = (rand() % (upper - lower + 1)) + lower;
	return num;
}
