#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <wait.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "share.h"

#define MAX_PASSENGER 100
#define MAX_PASSENGER_IN_TRAIN 5
#define MAX_TICKET 20
#define MAX_TRAIN_THREAD 5
#define MIN_START_TRAIN 10
#define MAX_TICKET_THREAD 2

void *ticket_thread(int *);
void *train_thread(int *);

pthread_mutex_t gTicketMutex;
int gTicket[MAX_TICKET];
int gTicketIn;
int gTicketOut;

sem_t gStartTrainSem;
sem_t gInAvailableSem;
sem_t gOutAvailableSem;

pthread_t gTicketThread[MAX_TICKET_THREAD];
pthread_t gTrainThread[MAX_TRAIN_THREAD];
int gTicketThreadID[MAX_TICKET_THREAD];
int gTrainThreadID[MAX_TRAIN_THREAD];

key_t myqkey;
int myqid;

int main(void)
{
	int i;


	/* create message queue */
	myqkey = ftok(MQ_KEY, 1);
	myqid = msgget(myqkey, IPC_CREAT|S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	printf("CT: message queue created (myqid=0x%x, key=%d)\n", myqid, myqkey);
	
	/* initialize mutex */
	pthread_mutex_init(&gTicketMutex, NULL);

	/* initialize semaphore */
	sem_init(&gStartTrainSem, 0, 0);
	sem_init(&gInAvailableSem, 0, 20);
	sem_init(&gOutAvailableSem, 0, 0);

	/* create threads */
	for(i=0; i<MAX_TICKET_THREAD; i++) {
		gTicketThreadID[i] = i;
		pthread_create(&gTicketThread[i], NULL, (void *(*)(void *))ticket_thread, (void *)&gTicketThreadID[i]);
	}

	for(i=0; i<MAX_TRAIN_THREAD; i++) {
		gTrainThreadID[i] = i;
		pthread_create(&gTrainThread[i], NULL, (void *(*)(void *))train_thread, (void *)&gTrainThreadID[i]);
	}

	pthread_exit(0);
}

void *ticket_thread(int *arg)
{
	int id = *arg;
	int i, j;

	while(1) {
		usleep((rand()%200+300)*1000);

		sem_wait(&gInAvailableSem);

		pthread_mutex_lock(&gTicketMutex);

		if(gTicketIn == MAX_PASSENGER) {
			pthread_mutex_unlock(&gTicketMutex);
			sem_post(&gOutAvailableSem);
			pthread_exit(0);
		}

		gTicket[gTicketIn % MAX_TICKET] = gTicketIn + 1;
		gTicketIn++;
		printf("ticket %d: passenger %d got ticket\n", id, gTicketIn);
		if(gTicketIn == MIN_START_TRAIN) {
 			for(j=0; j<MAX_TRAIN_THREAD; j++) {
				sem_post(&gStartTrainSem);
			}
		}

		pthread_mutex_unlock(&gTicketMutex);
		sem_post(&gOutAvailableSem);
	}

	return (void *)0;
}

void *train_thread(int *arg)
{
	int i;
	int id = *arg;
	int passengers = 0;
	int passengers_all = 0;
	struct timespec ts;
	int train_start_flag = 0;

	struct msgbuf wmsg;
	struct msgbuf rmsg;

	printf("======> train %d: wait\n", id);
	sem_wait(&gStartTrainSem);
	printf("======> train %d: ready\n", id);


	while(1) {
		sem_wait(&gOutAvailableSem);
		pthread_mutex_lock(&gTicketMutex);
		if(gTicketOut == MAX_PASSENGER) {
			train_start_flag  = 1;
			pthread_mutex_unlock(&gTicketMutex);
		}
		else {
			passengers++;
			passengers_all++;
			printf("==> train %d: passenger %d returned ticket\n", id, gTicket[gTicketOut++ % MAX_TICKET]);
			if(passengers == MAX_PASSENGER_IN_TRAIN) {
				train_start_flag  = 1;
			}
			if(gTicketOut == MAX_PASSENGER) {
				train_start_flag  = 1;
			}
			pthread_mutex_unlock(&gTicketMutex);
		  usleep((rand()%100+100)*1000);
			sem_post(&gInAvailableSem);
		}

		if(train_start_flag == 1) {
			/* send message */
			wmsg.mtype = id + 1;
			wmsg.msg = MSG_REQ_RUNNING;
			wmsg.passengers = passengers;
			if(msgsnd(myqid, &wmsg, MSIZE, IPC_NOWAIT) == -1) {
				printf("==> train %d: can't send message to message queue\n", id);
				exit(-1);
			}

			for(i=0; i<RETRY; i++) {
				if(msgrcv(myqid, &rmsg, MSIZE, id + MAX_TRAIN + 1, IPC_NOWAIT) > 0) {
					if(rmsg.msg == MSG_ACK_RUNNING) {
						break;
					}
				}
			}
			if(i==RETRY) {
				printf("==> train %d: time out (count = %d)\n", id, RETRY);
				pthread_exit(0);
			}

			printf("======> train %d: start with %d passengers\n", id, passengers);
			usleep(5000000);

			/* send message */
			wmsg.mtype = id + 1;
			wmsg.msg = MSG_REQ_READY;
			if(msgsnd(myqid, &wmsg, MSIZE, IPC_NOWAIT) == -1) {
				printf("==> train %d: can't send message to message queue\n", id);
				exit(-1);
			}

			for(i=0; i<RETRY; i++) {
				if(msgrcv(myqid, &rmsg, MSIZE, id + MAX_TRAIN + 1, IPC_NOWAIT) > 0) {
					if(rmsg.msg == MSG_ACK_READY) {
						break;
					}
					else if(rmsg.msg == MSG_NAK_READY) {
						printf("======> train %d: stop\n", id);
						printf("======> train %d: exit (total %d passengers)\n", id, passengers_all);
						pthread_exit(0);
					}
				}
			}
			if(i==RETRY) {
				printf("==> train %d: time out (count = %d)\n", id, RETRY);
				pthread_exit(0);
			}

			printf("======> train %d: stop\n", id);
			passengers = 0;
			train_start_flag = 0;
		}
		pthread_mutex_lock(&gTicketMutex);
		if ((gTicketOut == MAX_PASSENGER)&&(passengers == 0)) {
			pthread_mutex_unlock(&gTicketMutex);
			printf("======> train %d: exit (total %d passengers)\n", id, passengers_all);
			sem_post(&gOutAvailableSem);
			pthread_exit(0);
		}
		pthread_mutex_unlock(&gTicketMutex);
	}
}



