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
#define SHMKEY_msgq 4020015
#define BILLION 1000000000
#define BUFF_SZ sizeof(unsigned int)

/************************ FUNCTION PROTOTYPES *********************************/

void initIPC(); //initialize IPC entities
void setupRequest(); //roll to decide request nature and set msg struct vars
void sendRequest(); //send the request message to oss

/************************* GLOBAL VARIABLES ***********************************/

int shmid_sim_secs, shmid_sim_ns; //shared memory ID holders for sim clock
int shmid_mem; //shared memory ID holder for the liveState struct
int shmid_qid; //shared memory ID holder for message queue
static unsigned int *SC_secs; //pointer to shm sim clock (seconds)
static unsigned int *SC_ns; //pointer to shm sim clock (nanoseconds)
int seed; //for random rolls
int my_pnum; //my sim process number
int requestcounter; //tallies request count, roll to terminate every 1000 reqs

//struct used in message queues
struct message {
    long msgtyp;
    pid_t user_sys_pis; //actual user system pid (for wait/terminate)
    int userpid; //simulate user pid [0-17]
    int rw; //read/write request r=read w=write
    int userpagenum; //user's point-of-view page number request valid range [0-31]
    int terminating; //1=user is reporting termination
};
struct message msg; //actual struct variable for msg queue

/******************************** MAIN ****************************************/

int main(int argc, char** argv) {
    my_pnum = atoi(argv[1]);
    //set up seed and give it a roll
    seed = (int) getpid();
    rand_r(&seed);
    
    initIPC(); //initialize IPC resources
    
    //main loop: decide what to request, send it, wait for response, repeat
    while(1) {
        setupRequest();
        sendRequest();
        //WAIT for OSS response
        if (msgrcv(shmid_qid, &msg, sizeof(msg), (my_pnum + 100), 0) == -1 ) {
            perror("User: error in msgrcv");
            exit(1);
        }
    }

    return (EXIT_SUCCESS);
}

void setupRequest() {
    requestcounter++;
    int roll = rand_r(&seed) % 100000 + 1; //roll from 1 to 10,000
    
    //these 3 message attributes are always the same regardless of situation
    msg.msgtyp = 99;
    msg.user_sys_pis = getpid();
    msg.userpid = my_pnum;
    
    //roll to terminate every 1000 requests
    if (requestcounter >= 1000) {
        requestcounter = 0;
        //>=9000 means a 10% chance to terminate every 1000 requests
        if (roll >= 90000) {
            //setup the message
            msg.terminating = 1;
            //just send it and terminate right here cuz why not? efficient.
            if ( msgsnd(shmid_qid, &msg, sizeof(msg), 0) == -1 ) {
                perror("User: error sending msg to oss");
                exit(0);
            }
        }
    }
    //rolled to make invalid memory request, 1 means 1/10,000 chance
    if (roll <= 1) {
        msg.rw = 0;
        msg.terminating = 0;
        msg.userpagenum = 33; //invalid request
    }
    //rolled to make a read request
    else if (roll <= 50000) {
        roll = rand_r(&seed) % 32; //roll 0 to 31 to select user page request
        msg.rw = 0;
        msg.terminating = 0;
        msg.userpagenum = roll;
    }
    //rolled to make a write request
    else {
        roll = rand_r(&seed) % 32; //roll 0 to 31 to select user page request
        msg.rw = 1;
        msg.terminating = 0;
        msg.userpagenum = roll;
    }
}

void sendRequest() {
    if ( msgsnd(shmid_qid, &msg, sizeof(msg), 0) == -1 ) {
        perror("User: error sending msg to oss");
        exit(0);
    }
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
    //message queue
    if ( (shmid_qid = msgget(SHMKEY_msgq, 0777 )) == -1 ) {
        perror("OSS: Error generating message queue");
        exit(0);
    }

}

