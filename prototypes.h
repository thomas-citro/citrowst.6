void displayStatistics();
void displayMemoryMap();
void setupResources();
void cleanup();
void sigErrors();
void incrementClock(struct time*, int, int);
int findPageLocation(int, int);
void initializePages();
int findPageReplacement();
int findFrame();
void pageSend(int, int, int);
void setDefaultMemory(int);
void create();
int getProcess();
int addProcess(int);
int removeProcess(int);
void parseArguments(int, char**);
void printUsageStatement();
int getRandomInteger(int, int);