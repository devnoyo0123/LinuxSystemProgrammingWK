#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <string.h>

#include "share.h"

#define TIMEOUT_SEC 10

/* for state of train */
#define TRAIN_STATE_READY	0
#define TRAIN_STATE_RUNNING	1
#define TRAIN_STATE_BROKEN	2

#define BAD_TRAIN 3

int bad(int);

int gTrainRunning;

struct st_train {
	int state;
	time_t start_time;
	int passengers;
};

int main(void)
{
	int i;

	int myqid;
	key_t myqkey;
	struct msgbuf wmsg;
	struct msgbuf rmsg;

	struct st_train train[MAX_TRAIN];

	int train_id = 0;

	gTrainRunning = 5;

	/* initialize state */
	for(i=0; i<MAX_TRAIN; i++) {
		train[i].state = TRAIN_STATE_READY;
	}
	
	/* create message queue */
	myqkey = ftok(MQ_KEY, 1);
	myqid = msgget(myqkey, IPC_CREAT|S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	printf("CT: message queue created (myqid=0x%x, key=%d)\n", myqid, myqkey);
	
	/* monitoring */
	while(1) {
		if(msgrcv(myqid, &rmsg, MSIZE, train_id + 1, IPC_NOWAIT) > 0) 
		{
			switch(train[train_id].state) 
			{
				case TRAIN_STATE_READY:
					if(rmsg.msg == MSG_REQ_RUNNING) 
					{
					  printf("Got MSG_REQ_RUNNING from train %d\n", train_id);
						train[train_id].state = TRAIN_STATE_RUNNING;
						train[train_id].passengers = rmsg.passengers;
						wmsg.mtype = train_id + MAX_TRAIN + 1;
						wmsg.msg = MSG_ACK_RUNNING;
						if(msgsnd(myqid, &wmsg, MSIZE, IPC_NOWAIT) == -1) 
						{
							printf("CT: can't send message to message queue\n");
							exit(-1);
						}
					  printf("Send MSG_ACK_RUNNING from train %d\n", train_id);
						train[train_id].start_time = time(NULL);
						printf("CT: %d passengers in train %d started at %ld\n",  train[train_id].passengers, train_id, train[train_id].start_time);
					}
					break;
				case TRAIN_STATE_RUNNING:
					if(rmsg.msg == MSG_REQ_READY) 
					{
					  printf("Got MSG_REQ_READY from train %d\n", train_id);
						if(bad(87)) 
						{
						  gTrainRunning--;
							train[train_id].state = TRAIN_STATE_BROKEN;
							wmsg.mtype = train_id + MAX_TRAIN + 1;
							wmsg.msg = MSG_NAK_READY;
							if(msgsnd(myqid, &wmsg, MSIZE, IPC_NOWAIT) == -1) 
							{
								printf("CT: can't send message to message queue\n");
								exit(-1);
							}
					  	    printf("Send MSG_NAK_READY from train %d\n", train_id);
						}
						else {
							train[train_id].state = TRAIN_STATE_READY;
							wmsg.mtype = train_id + MAX_TRAIN + 1;
							wmsg.msg = MSG_ACK_READY;
							if(msgsnd(myqid, &wmsg, MSIZE, IPC_NOWAIT) == -1) {
								printf("CT: can't send message to message queue\n");
								exit(-1);
							}
					  	    printf("Send MSG_ACK_READY from train %d\n", train_id);
						}
					}
					break;
				case TRAIN_STATE_BROKEN:
					break;
				default:
					break;
			}
		}

		train_id++;
		if(train_id == MAX_TRAIN) 
		{
			train_id = 0;
		}
	}

	return 0;
}

int bad(int p)
{
	return (((rand()%100) > p) && (gTrainRunning > 2))?1:0;
}

