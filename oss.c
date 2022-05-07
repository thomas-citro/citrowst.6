#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <time.h>

#include "config.h"
#include "structs.h"
#include "prototypes.h"

// Structures
struct memory *mem; 
struct memory memstruct;
struct message msg;
struct waitQueue wPtr;

// Shared memory, message queue, & semaphore
int shmid; 
int shmid_mem;
sm* ptr;
sem_t *sem;
int messageQ;

// Statistics
int totalFaults;
int totalMemoryRequests;

// Other globals
int activeChildren[20];
int pageNumber[MAX_CONCURRENT_PROCS][32];
int pidNum = 0;
int terminated = 0;	
int nextMemoryMap = 1;
int FIFO[256];
int headFIFO = 0;
int tailFIFO = 0;
FILE* fp;

int main(int argc, char* argv[]) {
	// Initialize variables
	int frameNumberResult = 0;
	int foundFrame = 0;
	int frameToSwap = -1;
	int sendNumber = -1;
	int unblockNumber = -1;
	int procCounter = 0;
	int totalProcsCounter = 0;
	int i = 0;
	pid_t cpid;
	int pid = 0;
	
	parseArguments(argc, argv); // Parse command-line arguments	
	fp = fopen("oss.log", "w"); // Open the file for logging
	
	// Seed random number generator
	srand(time(NULL));
	time_t t;
	srand((unsigned) time(&t));
	
	for(i = 0; i < MAX_CONCURRENT_PROCS; i++) activeChildren[i] = i; // Procs start as active
	for (i = 0; i < 256; i++) FIFO[i] = -1; // Initialize FIFO queue
	
	// Catch ctrl+c and 2 real-life second alarm interupts
	if (signal(SIGINT, sigErrors) == SIG_ERR) exit(0);
	if (signal(SIGALRM, sigErrors) == SIG_ERR) exit(0);
	
	setupResources(); // Setup resources (shared memory, message queues, semaphore)
	create(); // Queue for blocked procs
	initializePages(); // Setup page numbers
	
	struct time randFork;
	alarm(TWO_SECOND_ALARM); // Start 2 real-time second alarm

	// Main loop
	while(totalProcsCounter < MAX_TOTAL_PROCS || procCounter > 0) { 							
		
		if (waitpid(cpid, NULL, WNOHANG) > 0) {
			procCounter--;
		}
		int nextFork = getRandomInteger(ONE_MILLION, FIVE_HUNDRED_MILLION); // Next fork between 1ms and 500ms
		incrementClock(&randFork, 0, nextFork);
		
		// Run while less than 18 procs are currently running
		if(procCounter < MAX_CONCURRENT_PROCS && ptr->time.nanoseconds < nextFork) {
			sem_wait(sem);
			randFork.seconds = ptr->time.seconds;
			randFork.nanoseconds = ptr->time.nanoseconds;
			sem_post(sem);
			nextFork = getRandomInteger(ONE_MILLION, FIVE_HUNDRED_MILLION); // Next fork between 1ms and 500ms
			incrementClock(&randFork, 0, nextFork);
	
			// Check if all procs terminated
			for (i = 0; i < MAX_CONCURRENT_PROCS; i++) {
				if(activeChildren[i] == -1) terminated++;
			}

			if(terminated == MAX_CONCURRENT_PROCS) {	
				// Exit and cleanup since all procs terminated
				displayStatistics();
				cleanup();
				return 0;
			} else {
				terminated = 0;
			}

			if (activeChildren[pidNum] != -1) {
				pid = activeChildren[pidNum];
			} else {
				int s = pidNum;
				for(s=pidNum; s<MAX_CONCURRENT_PROCS;s++) {
					if(activeChildren[s] == -1) pidNum++;
					else break;
				}
				pid = activeChildren[pidNum];
			}

			// Fork/exec child to user.c
			cpid=fork();
			totalProcsCounter++;
			procCounter++;

			if(cpid == 0) {
				char passPid[10];
				sprintf(passPid, "%d", pid);		
				execl("./user","user", NULL);
				perror("Error: exec");
				exit(0);
			}

			unblockNumber = getProcess(); 
			// Grant request and remove from queue
			if (unblockNumber != -1) removeProcess(unblockNumber);
	
			if (msgrcv(messageQ, &msg,sizeof(msg)+1,99,0) == -1) {
				perror("msgrcv");
			}

			// Child wants to write
			if(strcmp(msg.mtext, "WRITE") == 0) {
				msgrcv(messageQ,&msg,sizeof(msg),99,0);

				int write = atoi(msg.mtext);
				ptr->resourceStruct.count+=1;
				// Check if the address is available
				frameNumberResult = findPageLocation(pid, write/1024);
				incrementClock(&ptr->time, 0, FOURTEEN_MILLION); // Each request for disk read/write takes 14ms
				fprintf(fp,"Master: P%d requests write of address %d at time %d:%d\n", pid, write, ptr->time.seconds, ptr->time.nanoseconds);
				totalMemoryRequests++;

				// Grant request if available
				if (frameNumberResult != -1) {
					incrementClock(&ptr->time, 0, 10); // No page fault, so increment clock by 10ns
					mem->referenceBit[frameNumberResult] = 1;
					mem->dirtyBit[frameNumberResult] = 1;
					fprintf(fp,"	Found address %d in frame %d. Writing data to frame at time %d:%d\n", write, frameNumberResult, ptr->time.seconds, ptr->time.nanoseconds);
				} else {
					// PAGE FAULT: an empty frame is filled or frame to replace is chosen
					totalFaults++;
					foundFrame = findFrame();
					fprintf(fp,"	Address %d is not in a frame - Pagefault\n", write);
					// Select a page to replace if memory is full from the FIFO queue
					if (foundFrame == -1) {
						frameToSwap = findPageReplacement();
						pageSend(pageNumber[pid][write/1024],frameToSwap, 0);
						mem->pagetable[pid][write/1024] = frameToSwap;
						fprintf(fp,"		Clearing frame %d and swapping in P%d\n", frameToSwap, pid);
						fprintf(fp,"		Dirty bit for frame %d is set - Adding additional time to clock\n", frameToSwap);
					} else {
						// Place the page in the next open frame
						sendNumber = pageNumber[pid][write/1024];
						pageSend(sendNumber, foundFrame, 0);
						mem->pagetable[pid][write/1024] = foundFrame;
						fprintf(fp,"		Clearing frame %d and swapping in P%d page %d\n", foundFrame, pid, sendNumber);
						fprintf(fp,"		Dirty bit of frame %d is set - Adding additional time to clock\n", foundFrame);
					}
					// Add proc to the queue
					addProcess(pid);	
				}
			}
			
			// Child wants to read
			if(strcmp(msg.mtext, "READ") == 0) {
				msgrcv(messageQ, &msg, sizeof(msg), 99, 0); // Get address
				int request = atoi(msg.mtext); 
				ptr->resourceStruct.count += 1;
				
				// Check if request can be granted
				frameNumberResult = findPageLocation(pid, request/1024);
				incrementClock(&ptr->time, 0, FOURTEEN_MILLION); // Each request for disk read/write takes 14ms
				fprintf(fp,"Master: P%d requests read of address %d at time %d:%d\n", pid, request, ptr->time.seconds, ptr->time.nanoseconds);
				totalMemoryRequests++;
				
				if (frameNumberResult != -1) {
					// Read request can be granted
					incrementClock(&ptr->time, 0, 10); // No page fault, so increment clock by 10ns
					mem->referenceBit[frameNumberResult] = 1;
					fprintf(fp,"	Found address %d in frame %d. Giving data to P%d at time %d:%d\n", request, frameNumberResult, pid,ptr->time.seconds, ptr->time.nanoseconds);
				} else {
					// PAGE FAULT: an empty frame is filled or frame to replace is chosen
					totalFaults++;
					foundFrame = findFrame();
					fprintf(fp,"	Address %d is not in a frame - Pagefault\n", request);
					// Select a page to replace if memory is full from the FIFO queue
					if (foundFrame == -1) {
						frameToSwap = findPageReplacement();
						pageSend(pageNumber[pid][request/1024],frameToSwap, 0);
						mem->pagetable[pid][request/1024] = frameToSwap;
						fprintf(fp,"		Clearing frame %d and swapping in P%d\n", frameToSwap, pid);
					} else {
						// Put the page into the next empty frame
						sendNumber = pageNumber[pid][request/1024];
						pageSend(sendNumber, foundFrame, 0);
						mem->pagetable[pid][request/1024] = foundFrame;
						fprintf(fp,"		Clearing frame %d and swapping in P%d page %d\n", foundFrame, pid, sendNumber);
					}
					addProcess(pid); // Add proc to the queue
				}

			}

			// Release resources if child kills
			if(strcmp(msg.mtext, "KILLED") == 0) {
				fprintf(fp,"Master: Terminating P%d at %d:%d\n", pid, ptr->time.seconds, ptr->time.nanoseconds);
				activeChildren[pid] = -1;
				setDefaultMemory(pid);
			}
			// Display the memory map every second
			if(ptr->time.seconds == nextMemoryMap) {
				nextMemoryMap += 1;
				displayMemoryMap();
			}
				
			if(pidNum < 17) pidNum++;	
			else pidNum = 0;
		}
	}
	
	// Cleanup resources and print stats before exit
	displayStatistics();
	cleanup();
	return 0;
}

// Return random integer between lower and upper bounds (inclusive)
int getRandomInteger(int lower, int upper) {
	int num = (rand() % (upper - lower + 1)) + lower;
	return num;
}

void displayMemoryMap() {
	fprintf(fp,"\nCurrent memory layout at time %d:%d is:\n", ptr->time.seconds, ptr->time.nanoseconds);
	fprintf(fp,"\t     Occupied\tDirtyBit\n");

	int i;
	for(i = 0; i < 256; i++) {
		fprintf(fp,"Frame %d:\t",i + 1);
		
		if (mem->bitVector[i] == 0) fprintf(fp,".\t");
		else fprintf(fp,"+\t");
		
		if (mem->dirtyBit[i] == 0) fprintf(fp,"0\t");
		else fprintf(fp,"1\t");
		
		fprintf(fp,"\n");
	}
}

void displayStatistics() {
	printf("\nNumber of memory accesses per second: %f\n",(((float)(totalMemoryRequests))/((float)(ptr->time.seconds))));	
	printf("Number of page faults per memory access: %f\n",((float)(totalFaults)/(float)totalMemoryRequests));
	printf("Average memory access speed: %f\n\n", (((float)(ptr->time.seconds)+((float)ptr->time.nanoseconds/(float)(ONE_BILLION)))/((float)totalMemoryRequests)));

	fprintf(fp,"\nNumber of memory accesses per second: %f\n",(((float)(totalMemoryRequests))/((float)(ptr->time.seconds))));
	fprintf(fp,"Number of page faults per memory access: %f\n",((float)(totalFaults)/(float)totalMemoryRequests));
	fprintf(fp,"Average memory access speed: %f\n\n", (((float)(ptr->time.seconds)+((float)ptr->time.nanoseconds/(float)(ONE_BILLION)))/((float)totalMemoryRequests)));
}

// Set to default memory values
void setDefaultMemory(int index) {
	int i; 
	int frame;
	int page;
	for (i = 0; i < 32; i++) {
		if(mem->pagetable[index][i] != -1) {
			frame = mem->pagetable[index][i];
			page = pageNumber[index][i];
			mem->referenceBit[frame] = 0;
			mem->dirtyBit[frame] = 0;
			mem->bitVector[frame] = 0;
			mem->frame[frame] = -1;
			mem->pagetable[index][i] = -1;
			mem->pagelocation[page] = -1;
		} 
	}
}

// Returns next empty frame
int findFrame() {
	int i;
	for (i=0; i<256; i++) {
		if (mem->bitVector[i] == 0) return i;
	}
	return -1;
}

void pageSend(int pageNum, int frameNum, int dirtyBitValue) {
	// Insert into tail of FIFO queue
	FIFO[tailFIFO] = frameNum;
	printf("Added %d to tail of FIFO queue. FIFO[%d] = %d\n", frameNum, tailFIFO, frameNum);
	if (tailFIFO == 255) tailFIFO = 0;
	else tailFIFO++;
	printf("Now tailFIFO == %d\n", tailFIFO);
	
	// Set information about page
	mem->dirtyBit[frameNum] = dirtyBitValue;
	mem->bitVector[frameNum] = 1;
	mem->frame[frameNum] = pageNum;
	mem->pagelocation[pageNum] = frameNum;
}

// Find page to replace
int findPageReplacement() {
	printf("	Need to find page replacement. headFIFO == %d, tailFIFO == %d\n", headFIFO, tailFIFO);
	int frameNum = FIFO[headFIFO];
	FIFO[headFIFO] = -1;
	if (headFIFO == 255) headFIFO = 0;
	else headFIFO++;
	return frameNum;
}

// Returns frame number if it's available, otherwise returns -1
int findPageLocation(int u_pid, int u_num) {
	int pageNum;
	int frameNum;
	
	pageNum = pageNumber[u_pid][u_num];
	frameNum = mem->pagelocation[pageNum];
	if (frameNum == -1) {
		return -1;
	}
	
	if (mem->frame[frameNum] == pageNum) {
		return frameNum;
	}
	
	return -1;
}

// Sets initial page numbers
void initializePages() {
	int process;
	int num;
	int i;
	for (process=0; process<MAX_CONCURRENT_PROCS; process++) {
		for (num=0; num<32; num++) {
			mem->pagetable[process][num] = -1;
			pageNumber[process][num] = process*32 + num;
		}
	}
	
	for (num = 0; num < 576; num++) mem->pagelocation[num] = -1;
	for (i=0; i<256; i++) mem->frame[i] = -1;
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

// Sets up resources (shared memory, message queues, semaphore, etc.)
void setupResources() {
	// Shared memory
	shmid_mem = shmget(SHM_KEY, sizeof(memstruct), 0777 | IPC_CREAT);
	if (shmid_mem == -1) exit(0);
	mem = (struct memory *) shmat(shmid_mem, NULL, 0);
	if (mem == (struct memory *)(-1) ) exit(0);
	if ((shmid = shmget(SHM_KEY, sizeof(sm), IPC_CREAT | 0600)) < 0) exit(0);
	
	// Message queue
	if ( (messageQ = msgget(MSG_KEY, 0777 | IPC_CREAT)) == -1 ) {
		perror("Error: message queue");
		exit(0);
	}	
	
	sem = sem_open("thomas_sem", O_CREAT, 0777, 1); // For clock protection

	ptr = shmat(shmid, NULL, 0);
}

// Cleanup resources (shared memory, etc...)
void cleanup() {
	shmctl(shmid_mem, IPC_RMID, NULL);
	shmctl(shmid, IPC_RMID, NULL);	
	msgctl(messageQ, IPC_RMID, NULL); 
	sem_unlink("thomas_sem");
	sem_close(sem);
}

// Control interrupts
void sigErrors(int signum) {
	if (signum == SIGINT) {
		printf("\nInterrupted by ctrl-c\n");
		cleanup();
		displayStatistics();
	} else {
		printf("\nInterrupted by %d second alarm\n", TWO_SECOND_ALARM);
		cleanup();
		displayStatistics();
	}
	exit(0);
}

// Initialize queue
void create() {
	int i;
	for(i = 0; i < MAX_CONCURRENT_PROCS; i++) wPtr.queue[i] = -1;
}

// Get next process from queue
int getProcess() {
	if (wPtr.queue[0] == -1) return -1;
	else return wPtr.queue[0];
}

// Insert proc into the queue if blocked
int addProcess(int waitPid) {
	int i;
	for (i=0; i<MAX_CONCURRENT_PROCS; i++) {
		if (wPtr.queue[i] == -1) {
			wPtr.queue[i] = waitPid;
			return 1;
		}
	}
	return -1;
}

// Remove proc from the queue if request is granted
int removeProcess(int pNum) {
	int i;
	for (i=0; i<MAX_CONCURRENT_PROCS; i++) {
		if (wPtr.queue[i] == pNum) {
			while(i+1 < MAX_CONCURRENT_PROCS) {
				wPtr.queue[i] = wPtr.queue[i+1];
				i++;
			}
			wPtr.queue[17] = -1;
			return 1;
		}
	}
	return -1;
}

// Parse command-line arguments
void parseArguments(int argc, char* argv[]) {
	int c;
	while((c = getopt(argc, argv, "h")) != EOF) {
		switch(c) {
			case 'h':
				printUsageStatement();
				break;
			default:
				return;
		}
	}
}

void printUsageStatement() {
	fprintf(stderr, "Usage: ./oss\n");
	exit(0);
} 