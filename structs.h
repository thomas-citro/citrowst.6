struct message {
    long msgType;
    char mtext[512];
};

struct time{
	int nanoseconds;
	int seconds;
};

typedef struct {
	int memType;
	int count;
	int write;
	float weights[32];
} resourceInfo;

struct memory {
	int referenceBit[256];
	int dirtyBit[256]; 
	int bitVector[256]; 
	int frame[256]; 
	int referenceStat; 
	int pagetable[MAX_CONCURRENT_PROCS][32]; 
	int pagelocation[576]; 
};

typedef struct shmStruct{
	resourceInfo resourceStruct;
	struct time time;
} sm;

struct waitQueue {
    int queue[18]; 
};