/* 
 * File:   oss.c
 * Author: Michael Beckering
 * Project 6
 * Spring 2018 CS-4760-E01
 * Created on April 19, 2018, 11:32 AM
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <time.h>
#include <sys/wait.h>
#include <signal.h>

#define SHMKEY_sim_s 4020012
#define SHMKEY_sim_ns 4020013
#define SHMKEY_memstruct 4020014
#define SHMKEY_msgq 4020015
#define BILLION 1000000000
#define BUFF_SZ sizeof(unsigned int)

/************************** FUNCTION PROTOTYPES *******************************/

static int setperiodic(double); //periodic interrupt setup
static int setinterrupt(); //periodic interrupt setup
static void interrupt(int signo, siginfo_t *info, void *context); //handler
static void siginthandler(int sig_num); //sigint handler
void initIPC(); //initialize IPC stuff: system clock, shared struct, msg queue
void clearIPC(); //free all that stuff
void incrementClock(unsigned int, unsigned int); //increments simclock by (s,ns)
void helpmessage(); //displays help message
void setTimeToNextProc(); //sets time to next user process fork
int isTimeToSpawnProc(); //returns 1 if it's time to fork a new user
void setPageNumbers(); //assign syspage number ranges for all 18 possible users
void printMap(); //prints memory map to log file

/************************* GLOBAL VARIABLES ***********************************/

int shmid_sim_secs, shmid_sim_ns; //shared memory ID holders for sim clock
int shmid_mem; //shared memory ID holder for the liveState struct
int shmid_qid; //shared memory ID holder for message queue
int seed; //seed for rand_r rolls
static unsigned int *SC_secs; //pointer to shm sim clock (seconds)
static unsigned int *SC_ns; //pointer to shm sim clock (nanoseconds)
unsigned int timeToNextProcNS, timeToNextProcSecs; //length of time to spawnext
unsigned int maxTimeBetweenProcsSecs, maxTimeBetweenProcsNS; //max time
unsigned int spawnNextProcSecs, spawnNextProcNS; //exact time to spawn next user
int pagenumber[18][32]; //returns actual page # depending on process # and
                            //that process's logical page #: process 0 gets
                            //pages 0-31, p1 gets 32-63, p2 gets 64-95, etc
pid_t childpids[18]; //stores actual system children pids to kill upon interrupt

//shared memory data structure for our system info needs
struct memory {
    int refbit[256]; //reference bit array: 0=lastchance 1=referenced
    int dirtystatus[256]; //dirty bit array: 0=clean, 1=dirty
    int bitvector[256]; //0=open frame 1=occupied frame
    int refptr; //for FIFO enforcement, runs from [0-255] and resets to 0
    int pagetable[18][32]; //returns frame # / maps pages to frames:
                           //[process#][page#] = frame#, -1=unused page
    int pagelocation[576]; //returns frame number location of that page
};

struct memory *mem; //struct pointer for our memory information
struct memory memstruct; //actual struct

//struct used in message queues
struct message {
    pid_t user_sys_pis; //actual user system pid (for wait/terminate)
    int userpid; //simulate user pid [0-17]
    char rw; //read/write request r=read w=write
    int userpagenum; //user's point-of-view page number request valid range [0-31]
    int terminating; //1=user is reporting termination
};
//actual struct variable for msg queue
struct message msg;

/*********************************** MAIN *************************************/

int main(int argc, char** argv) {
    seed = getpid(); //seed initialization
    maxTimeBetweenProcsNS = 500000000; //500ms max time between user forks
    maxTimeBetweenProcsSecs = 0; //0 seconds max time between user forks
    double runtime = 2; //seconds before interrupt & termination
    int maxusers = 18; //maximum concurrent users processes in the system
    int currentusers = 0; //current living user processes in the system
    int option; //for getopt
    
    //set up interrupt handling for SIGINT and timed interrupt
    signal (SIGINT, siginthandler);
    if (setperiodic(runtime) == -1) {
        perror("Failed to setup periodic interrupt");
        return 1;
    }
    if (setinterrupt() == -1) {
        perror("Failed to set up SIGALRM handler");
        return 1;
    }
    
    //getopt loop to parse command line options
    while ((option = getopt(argc, argv, "hp:")) != -1) {
        switch(option) {
            case 'h':
                helpmessage();
                exit(0);
            case 'p':
                maxusers = atoi(optarg);
                if ( (maxusers < 1) || (maxusers > 18) ) {
                    printf("OSS: Error: invalid -p range [1-18], "
                            "assigning default.\n");
                    maxusers = 18;
                }
                break;
            default:
                break;
        }
    }
    printf("OSS: maximum concurrent user processes: %i\n", maxusers);
    
    maxusers = 1; //************************************************************ TEMP FOR TESTING
    
    initIPC();
    
    //set time for first user to spawn
    setTimeToNextProc();
    //go ahead and increment sim clock to that time to prevent useless looping
    incrementClock(spawnNextProcSecs, spawnNextProcNS);
    
    setPageNumbers();
    printf("user 0 page 0 = page # %i\n", pagenumber[0][0]);
    printf("user 0 page 1 = page # %i\n", pagenumber[0][1]);
    printf("user 0 page 2 = page # %i\n", pagenumber[0][2]);
    printf("user 1 page 0 = page # %i\n", pagenumber[1][0]);
    printf("user 1 page 1 = page # %i\n", pagenumber[1][1]);
    printf("user 17 page 31 = page # %i\n", pagenumber[17][31]);
    
    printMap();
    
    /*********************** BEGIN MEMORY MANAGEMENT **************************/
    while (1) {
        //if it's time to fork a new user AND we're user the user limit
        if (isTimeToSpawnProc() && (currentusers < maxusers) ) {
            currentusers++;
            //fork a user
            //TODO: fork a user
        }
        //if it's time to fork a new user but we're at the process limit
        else if (isTimeToSpawnProc() && (currentusers == maxusers) ) {
            //set a new time to spawn a user process
            setTimeToNextProc();
        }
    }
    /************************ END MEMORY MANAGEMENT ***************************/
    
    clearIPC();

    return (EXIT_SUCCESS);
}

/*************************** FUNCTION DEFINITIONS *****************************/

void printMap(){
    int i;
    printf("Memory map:\n");
    printf("[U=used frame, D=dirty frame, 0/1=refbit, .=unused]\n");
    printf("Current head pointer position: frame %i\n", mem->refptr);
    //status of first 64 frames
    for (i=0; i<64; i++) {
        if (mem->bitvector[i] == 0) printf(".");
        else if (mem->dirtystatus == 0) printf("U");
        else printf("D");
    }
    printf ("\n");
    //status of first 64 reference bits
    for (i=0; i<64; i++) {
        if (mem->bitvector[i] == 1) {
            if (mem->refbit == 0) printf("0");
            else printf("1");
        }
        else printf(".");
    }
    printf ("\n");
    //status of frames 64-127
    for (i=64; i<128; i++) {
        if (mem->bitvector[i] == 0) printf(".");
        else if (mem->dirtystatus == 0) printf("U");
        else printf("D");
    }
    printf ("\n");
    //status of reference bits on frames 64-127
    for (i=64; i<128; i++) {
        if (mem->bitvector[i] == 1) {
            if (mem->refbit == 0) printf("0");
            else printf("1");
        }
        else printf(".");
    }
    printf ("\n");
    //status of frames 128-191
    for (i=128; i<192; i++) {
        if (mem->bitvector[i] == 0) printf(".");
        else if (mem->dirtystatus == 0) printf("U");
        else printf("D");
    }
    printf ("\n");
    //status of reference bits on frames 128-191
    for (i=128; i<192; i++) {
        if (mem->bitvector[i] == 1) {
            if (mem->refbit == 0) printf("0");
            else printf("1");
        }
        else printf(".");
    }
    printf ("\n");
    //status of frames 192-255
    for (i=192; i<256; i++) {
        if (mem->bitvector[i] == 0) printf(".");
        else if (mem->dirtystatus == 0) printf("U");
        else printf("D");
    }
    printf ("\n");
    //status of reference bits on frames 192-255
    for (i=192; i<256; i++) {
        if (mem->bitvector[i] == 1) {
            if (mem->refbit == 0) printf("0");
            else printf("1");
        }
        else printf(".");
    }
    printf ("\n");
    
}

void setPageNumbers(){
    int proc, pagenum;
    for (proc=0; proc<18; proc++) {
        for (pagenum=0; pagenum<32; pagenum++) {
            pagenumber[proc][pagenum] = proc*32 + pagenum;
        }
    }
}

void setTimeToNextProc() {
    unsigned int temp;
    unsigned int localsecs = *SC_secs;
    unsigned int localns = *SC_ns;
    timeToNextProcSecs = rand_r(&seed) % (maxTimeBetweenProcsSecs + 1);
    timeToNextProcNS = rand_r(&seed) % (maxTimeBetweenProcsNS + 1);
    spawnNextProcSecs = localsecs + timeToNextProcSecs;
    spawnNextProcNS = localns + timeToNextProcNS;
    if (spawnNextProcNS >= BILLION) { //roll ns to s if > bill
        spawnNextProcSecs++;
        temp = spawnNextProcNS - BILLION;
        spawnNextProcNS = temp;
    }
}

int isTimeToSpawnProc() {
    int return_val = 0;
    unsigned int localsec = *SC_secs;
    unsigned int localns = *SC_ns;
    if ( (localsec > spawnNextProcSecs) || 
            ( (localsec >= spawnNextProcSecs) && (localns >= spawnNextProcNS) ) ) {
        return_val = 1;
    }
    return return_val;
}

void incrementClock(unsigned int add_secs, unsigned int add_ns) {
    unsigned int localsec = *SC_secs;
    unsigned int localns = *SC_ns;
    unsigned int temp;
    localsec = localsec + add_secs;
    localns = localns + add_ns;
    //rollover nanoseconds offset if needed
    if (localns >= BILLION) {
        localsec++;
        temp = localns - BILLION;
        localns = temp;
    }
    //update the sim clock in shared memory
    *SC_secs = localsec;
    *SC_ns = localns;
}

void initIPC() {
    printf("OSS: Allocating IPC resources...\n");
    //sim clock seconds
    shmid_sim_secs = shmget(SHMKEY_sim_s, BUFF_SZ, 0777 | IPC_CREAT);
        if (shmid_sim_secs == -1) { //terminate if shmget failed
            perror("OSS: error in shmget shmid_sim_secs");
            exit(1);
        }
    SC_secs = (unsigned int*) shmat(shmid_sim_secs, 0, 0);
    //sim clock nanoseconds
    shmid_sim_ns = shmget(SHMKEY_sim_ns, BUFF_SZ, 0777 | IPC_CREAT);
        if (shmid_sim_ns == -1) { //terminate if shmget failed
            perror("OSS: error in shmget shmid_sim_ns");
            exit(1);
        }
    SC_ns = (unsigned int*) shmat(shmid_sim_ns, 0, 0);
    //shared memory for memory struct
    shmid_mem = shmget(SHMKEY_memstruct, sizeof(memstruct), 0777 | IPC_CREAT);
    if (shmid_mem == -1) { //terminate if shmget failed
            perror("OSS: error in shmget for memory struct");
            exit(1);
        }
    mem = (struct memory *) shmat(shmid_mem, NULL, 0);
    if (mem == (struct memory *)(-1) ) {
        perror("OSS: error in shmat liveState");
        exit(1);
    }
    //message queue
    if ( (shmid_qid = msgget(SHMKEY_msgq, 0777 | IPC_CREAT)) == -1 ) {
        perror("OSS: Error generating message queue");
        exit(0);
    }

}

void clearIPC() {
    printf("OSS: Clearing IPC resources...\n");
    //close shared memory (sim clock)
    if ( shmctl(shmid_sim_secs, IPC_RMID, NULL) == -1) {
        perror("OSS: error removing shared memory");
    }
    if ( shmctl(shmid_sim_ns, IPC_RMID, NULL) == -1) {
        perror("OSS: error removing shared memory");
    }
    //close shared memory system struct
    if ( shmctl(shmid_mem, IPC_RMID, NULL) == -1) {
        perror("OSS: error removing shared memory");
    }
    //close message queue
    if ( msgctl(shmid_qid, IPC_RMID, NULL) == -1 ) {
        perror("OSS: Error removing message queue");
        exit(0);
    }
    //fflush(mlog);
    //fclose(mlog);
}

void helpmessage(){
    printf("Usage: ./oss [-p <int 1-18>]\n");
    printf("p: max concurrent user processes (maximum of 18)\n");
    exit(0);
}

/************************* INTERRUPT HANDLING *********************************/
//this function taken from UNIX text
static int setperiodic(double sec) {
    timer_t timerid;
    struct itimerspec value;
    
    if (timer_create(CLOCK_REALTIME, NULL, &timerid) == -1)
        return -1;
    value.it_interval.tv_sec = (long)sec;
    value.it_interval.tv_nsec = (sec - value.it_interval.tv_sec)*BILLION;
    if (value.it_interval.tv_nsec >= BILLION) {
        value.it_interval.tv_sec++;
        value.it_interval.tv_nsec -= BILLION;
    }
    value.it_value = value.it_interval;
    return timer_settime(timerid, 0, &value, NULL);
}

//this function taken from UNIX text
static int setinterrupt() {
    struct sigaction act;
    
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = interrupt;
    if ((sigemptyset(&act.sa_mask) == -1) ||
            (sigaction(SIGALRM, &act, NULL) == -1))
        return -1;
    return 0;
}

static void interrupt(int signo, siginfo_t *info, void *context) {
    printf("OSS: Timer Interrupt Detected! signo = %d\n", signo);
    //fprintf(mlog, "OSS: Terminated: Timed Out\n");
    //killchildren();
    //printStats();
    clearIPC();
    printf("OSS: Terminated: Timed Out\n");
    exit(0);
}

static void siginthandler(int sig_num) {
    printf("\nOSS: Interrupt detected! signo = %d\n", getpid(), sig_num);
    //fprintf(mlog, "OSS: Terminated: Interrupted by SIGINT\n");
    //killchildren();
    //printStats();
    clearIPC();
    printf("OSS: Terminated: Interrupted\n");
    exit(0);
}

