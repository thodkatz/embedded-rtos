/*
 *	File	: pc.c
 *
 *	Title	: Demo Producer/Consumer.
 *
 *	Short	: A solution to the producer consumer problem using
 *		pthreads.
 *
 *	Long 	:
 *
 *	Author	: Andrae Muys
 *
 *	Date	: 18 September 1997
 *
 *	Revised	:
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define QUEUESIZE 10
#define LOOP 10

void *producer(void *args);
void *consumer(void *args);

typedef struct {
  void *(*work)(void *);
  void *arg;
  int value;
} workFunction;

void *helloThread(void *args) {
  char *text = (char *)args;
  printf("%s\n", text);
  return NULL;
}

int leftProFinished = 0; // count how many producers are left to finish
bool isFinished = false; // false if there are producers else true
bool *isConsumerFinished;

bool areConsumersFinished(int numConThreads);


typedef struct {
  workFunction buf[QUEUESIZE];
  // int buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
} queue;

typedef struct {
  queue *q;
  int tid;
} pthread_data;

queue *queueInit(int numThreads);
void queueDelete(queue *q);
void queueAdd(queue *q, int in);
void queueDel(queue *q, int *out);

// #define DEBUG

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf("USAGE: ./bin/main <number of producers threads> <number of "
           "consumers threads>");
    exit(1);
  }
  int numProThreads = atoi(argv[1]);
  int numConThreads = atoi(argv[2]);

  leftProFinished = numProThreads;

  queue *fifo;
  pthread_t pro[numProThreads];
  pthread_t con[numConThreads];
  pthread_data dataPro[numProThreads];
  pthread_data dataCon[numConThreads];

  isConsumerFinished = (bool*)malloc(numConThreads * sizeof(bool));
  for(int i = 0; i < numConThreads; i++) {
    isConsumerFinished[i] = false;
  }

  printf("Number of producers: %d\nNumber of consumers: %d\n", numProThreads,
         numConThreads);

  fifo = queueInit(numConThreads + numProThreads);
  if (fifo == NULL) {
    fprintf(stderr, "main: Queue Init failed.\n");
    exit(1);
  }

  int rc;
  for (int i = 0; i < numProThreads; i++) {
    dataPro[i].tid = i;
    dataPro[i].q = fifo;
    if (rc = pthread_create(&pro[i], NULL, producer, &dataPro[i])) {
      printf("Error creating threads %d\n", rc);
    }
  }

  for (int i = 0; i < numConThreads; i++) {
    dataCon[i].tid = i;
    dataCon[i].q = fifo;
    if (rc = pthread_create(&con[i], NULL, consumer, &dataCon[i])) {
      printf("Error creating threads %d\n", rc);
    }
  }

  for (int i = 0; i < numProThreads; i++) {
    pthread_join(pro[i], NULL);
    printf("Joined producer id: %d\n", i);
  }

  isFinished = true; // all producers exited

  // Make the system to reach final state. Are there stuck threads?
  // Unblock the threads that waiting for producers
  // if queue empty and there are no producers is the final state
  // spam signals until all consumer threads have finished
  // WIP: Fix Busy waiting
  while (1) {
    printf("Send signal if someone is blocked\n");
    pthread_cond_signal(fifo->notEmpty); // unblock the waited thread
    if(areConsumersFinished(numConThreads)) break;
    usleep(10); // hybernate
    #ifdef DEBUG
    usleep(1000000);
    #endif
  }

#ifdef DEBUG
  usleep(100000);
#endif

  for (int i = 0; i < numConThreads; i++) {
    pthread_join(con[i], NULL);
    printf("Joined consumer id: %d\n", i);
  }

  queueDelete(fifo);
  free(isConsumerFinished);

  return 0;
}

void *producer(void *args) {
  queue *fifo;

  pthread_data *data = (pthread_data *)args;
  // fifo = (queue *)q;
  fifo = data->q;

  for (int i = 0; i < LOOP; i++) {
    pthread_mutex_lock(fifo->mut);
    while (fifo->full) {
      printf("producer: queue FULL.\n");
      pthread_cond_wait(fifo->notFull, fifo->mut);
    }
    queueAdd(fifo, i);
    // if (i == LOOP - 1) {
    //   leftProFinished--;
    //   if (leftProFinished == 0) {
    //     isFinished = true; // all producers exited
    //   }
    // }
    pthread_mutex_unlock(fifo->mut);
    pthread_cond_signal(fifo->notEmpty);
#ifdef DEBUG
    usleep(100000);
#endif
  }

  return (NULL);
}

void *consumer(void *args) {
  queue *fifo;
  int d;
  pthread_data *data = (pthread_data *)args;
  // fifo = (queue *)q;
  fifo = data->q;

  while (1) {
    pthread_mutex_lock(fifo->mut);
    while (fifo->empty && !isFinished) {
      printf("consumer: queue EMPTY\n");

#ifdef DEBUG
      printf("\033[1mStart waiting id:%d\033[0m\n", data->tid);
#endif
      pthread_cond_wait(fifo->notEmpty, fifo->mut);
#ifdef DEBUG
      printf("\033[1mFinished waiting id:%d\033[0m\n", data->tid);
#endif
    }
    queueDel(fifo, &d);
    pthread_mutex_unlock(fifo->mut);
    pthread_cond_signal(fifo->notFull);
#ifdef DEBUG
    usleep(100000);
#endif
    if (isFinished && fifo->empty)
      break;
  }

  printf("Consumer Finished id:%d\n", data->tid);
  isConsumerFinished[data->tid] = true;

  return (NULL);
}

// true if all consumer threads finished
bool areConsumersFinished(int numConThreads) {
  bool finish = isConsumerFinished[0];
  for(int i = 1; i < numConThreads; i++) {
    finish = finish && isConsumerFinished[i];
  }
  return finish;
}

queue *queueInit(int numThreads) {
  queue *q;

  q = (queue *)malloc(sizeof(queue));
  if (q == NULL)
    return (NULL);

  q->empty = 1;
  q->full = 0;
  q->head = 0;
  q->tail = 0;
  q->mut = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(q->mut, NULL);
  q->notFull = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  pthread_cond_init(q->notFull, NULL);
  q->notEmpty = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  pthread_cond_init(q->notEmpty, NULL);

  for (int i = 0; i < QUEUESIZE; i++) {
    q->buf[i].work = helloThread;
  }

  return (q);
}

void queueDelete(queue *q) {
  pthread_cond_destroy(q->notFull);
  free(q->notFull);
  pthread_cond_destroy(q->notEmpty);
  free(q->notEmpty);
  pthread_mutex_destroy(q->mut);
  free(q->mut);
  free(q);
}

void queueAdd(queue *q, int in) {
  // (q->buf[q->tail].work)("Producer is called");
  q->buf[q->tail].value = in;
  printf("producer: add %d to %d\n", in, q->tail);

  q->tail++;
  if (q->tail == QUEUESIZE)
    q->tail = 0;
  if (q->tail == q->head)
    q->full = 1;
  q->empty = 0;

  return;
}

void queueDel(queue *q, int *out) {
  if (q->empty) {
    printf(
        "\033[1mThere is notthing to delete. Queue empty. Aborting\033[0m\n");
    return;
  }

  //(q->buf[q->head].work)("Consumer is called");
  *out = q->buf[q->head].value;
  printf("consumer: received %d from %d\n", *out, q->head);

  q->head++;

  if (q->head == QUEUESIZE)
    q->head = 0;
  if (q->head == q->tail)
    q->empty = 1;
  q->full = 0;

  return;
}