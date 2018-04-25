/* 
 * File:   user.c
 * Author: Michael Beckering
 * Project 6
 * Spring 2018 CS-4760-E01
 * Created on April 19, 2018, 11:33 AM
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>

#define SHMKEY_sim_s 4020012
#define SHMKEY_sim_ns 4020013
#define SHMKEY_memstruct 4020014
#define SHMKEY_msgq 4020015
#define BILLION 1000000000
#define BUFF_SZ sizeof(unsigned int)

/************************ FUNCTION PROTOTYPES *********************************/

void initIPC(); //initialize IPC entities

/************************* GLOBAL VARIABLES ***********************************/

int shmid_sim_secs, shmid_sim_ns; //shared memory ID holders for sim clock
int shmid_mem; //shared memory ID holder for the liveState struct
int shmid_qid; //shared memory ID holder for message queue
static unsigned int *SC_secs; //pointer to shm sim clock (seconds)
static unsigned int *SC_ns; //pointer to shm sim clock (nanoseconds)

//shared memory data structure for our system info needs
struct memory {
    int refbit[256]; //reference bit array: -1=free 0=lastchance 1=referenced
    char dirty[256]; //dirty bit array: .=free U=used(clean) D=dirty
    int bitvector[256]; //0=unused frame 1=occupied frame
    int refptr; //for FIFO enforcement
    int pagetable[18][32]; //maps pages to frames:
                            //[process#][page#] = frame#, -1=unused page
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
struct message msg; //actual struct variable for msg queue

/******************************** MAIN ****************************************/

int main(int argc, char** argv) {

    return (EXIT_SUCCESS);
}

void initIPC() {
    //sim clock seconds
    shmid_sim_secs = shmget(SHMKEY_sim_s, BUFF_SZ, 0777);
        if (shmid_sim_secs == -1) { //terminate if shmget failed
            perror("User: error in shmget shmid_sim_secs");
            exit(1);
        }
    SC_secs = (unsigned int*) shmat(shmid_sim_secs, 0, 0);
    //sim clock nanoseconds
    shmid_sim_ns = shmget(SHMKEY_sim_ns, BUFF_SZ, 0777);
        if (shmid_sim_ns == -1) { //terminate if shmget failed
            perror("User: error in shmget shmid_sim_ns");
            exit(1);
        }
    SC_ns = (unsigned int*) shmat(shmid_sim_ns, 0, 0);
    //shared memory for memory struct
    shmid_mem = shmget(SHMKEY_memstruct, sizeof(memstruct), 0777);
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

