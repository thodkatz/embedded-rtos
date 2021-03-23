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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/**
 * It should be noted that using global variables is a bad practice!
 */

FILE *results;
/*
 * Requires to create two objects of struct timespec tic, toc to count time
 */
#define TIC(i) clock_gettime(CLOCK_MONOTONIC, &tic[i]);
#define TOC(i)                                                                 \
  clock_gettime(CLOCK_MONOTONIC, &toc[i]);                                     \
  fprintf(results, "%f,%d\n", diff_time(tic[i], toc[i]), i);

#define QUEUESIZE 10
//#define LOOP 1000
int loop;
// #define DEBUG

typedef struct {
  void *(*work)(void *);
  void *arg;
  int value;
} workFunction;

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

void *producer(void *args);
void *consumer(void *args);

queue *queueInit(int numThreads);
void queueDelete(queue *q);
void queueAdd(queue *q, int in);
void queueDel(queue *q, int *out);
bool areConsumersFinished(int numConThreads);
void *helloThread(void *args);
double diff_time(struct timespec start, struct timespec end);

bool isFinished = false;  // false if there are producers else true
bool *isConsumerFinished; // keep track if consumer finished their tasks

struct timespec *tic;
struct timespec *toc;

int main(int argc, char *argv[]) {
  if (argc != 4) {
    printf("USAGE: ./bin/main <number of loops> <number of producers threads> <number of "
           "consumers threads>");
    exit(1);
  }

  loop = atoi(argv[1]);
  int numProThreads = atoi(argv[2]);
  int numConThreads = atoi(argv[3]);

  queue *fifo;
  pthread_t pro[numProThreads];
  pthread_t con[numConThreads];
  pthread_data dataPro[numProThreads];
  pthread_data dataCon[numConThreads];

  tic = (struct timespec *)malloc(QUEUESIZE * sizeof(struct timespec));
  toc = (struct timespec *)malloc(QUEUESIZE * sizeof(struct timespec));

  results = fopen("results.csv", "w");

  isConsumerFinished = (bool *)malloc(numConThreads * sizeof(bool));
  for (int i = 0; i < numConThreads; i++) {
    isConsumerFinished[i] = false;
  }

  printf("Numbero of loops: %d\nNumber of producers: %d\nNumber of consumers: %d\n", loop, numProThreads,
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

  // spam signals until all consumer threads have finished
  // WIP: Fix Busy waiting
  while (1) {
    printf("Send signal if someone is blocked\n");
    pthread_cond_signal(fifo->notEmpty); // unblock the waited thread
    if (areConsumersFinished(numConThreads))
      break;
    usleep(10); // hybernate
  }

  for (int i = 0; i < numConThreads; i++) {
    pthread_join(con[i], NULL);
    printf("Joined consumer id: %d\n", i);
  }

  queueDelete(fifo);
  free(isConsumerFinished);
  free(tic);
  free(toc);
  fclose(results);

  return 0;
}

void *producer(void *args) {
  queue *fifo;

  pthread_data *data = (pthread_data *)args;
  fifo = data->q;

  for (int i = 0; i < loop; i++) {
    pthread_mutex_lock(fifo->mut);
    while (fifo->full) {
      printf("producer: queue FULL.\n");
      pthread_cond_wait(fifo->notFull, fifo->mut);
    }
    queueAdd(fifo, i);
    pthread_mutex_unlock(fifo->mut);
    pthread_cond_signal(fifo->notEmpty);
  }

  return (NULL);
}

void *consumer(void *args) {
  queue *fifo;
  int d;
  pthread_data *data = (pthread_data *)args;
  fifo = data->q;

  while (1) {
    pthread_mutex_lock(fifo->mut);
    while (fifo->empty && !isFinished) {
      printf("consumer: queue EMPTY\n");
      pthread_cond_wait(fifo->notEmpty, fifo->mut);
    }
    queueDel(fifo, &d);
    pthread_mutex_unlock(fifo->mut);
    pthread_cond_signal(fifo->notFull);
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
  for (int i = 1; i < numConThreads; i++) {
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
  TIC(q->tail)
  
  printf("producer: add %d to %ld\n", in, q->tail);

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
  
  TOC(q->head);

  //(q->buf[q->head].work)("Consumer is called");
  *out = q->buf[q->head].value;
  printf("consumer: received %d from %ld\n", *out, q->head);

  q->head++;

  if (q->head == QUEUESIZE)
    q->head = 0;
  if (q->head == q->tail)
    q->empty = 1;
  q->full = 0;

  return;
}

void *helloThread(void *args) {
  char *text = (char *)args;
  printf("%s\n", text);
  return NULL;
}

double diff_time(struct timespec start, struct timespec end) {
  uint32_t diff_sec = (end.tv_sec - start.tv_sec);
  int32_t diff_nsec = (end.tv_nsec - start.tv_nsec);
  if ((end.tv_nsec - start.tv_nsec) < 0) {
    diff_sec -= 1;
    diff_nsec = 1e9 + end.tv_nsec - start.tv_nsec;
  }

  return (1e9 * diff_sec + diff_nsec) / 1e9;
}