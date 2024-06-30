#include <libwebsockets.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Define colors for printing
#define KGRN "\033[0;32;32m"
#define KCYN "\033[0;36m"
#define KRED "\033[0;32;31m"
#define KYEL "\033[1;33m"
#define KBLU "\033[0;32;34m"
#define KCYN_L "\033[1;36m"
#define KBRN "\033[0;33m"
#define RESET "\033[0m"

#define QUEUESIZE 100
#define NUM_PRO_THREADS 2
#define NUM_CON_THREADS 2

#define MOVING_AVERAGE_TIMESPAN_MINUTES 15

typedef struct {
  char *price;
  char *symbol;
  char *timestamp;
  char *volume;
} findata;

const findata DEFAULT_FINDATA = {"-1", "-1", "-1", "-1"};

typedef struct {
  findata buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_cond_t *notFull, *notEmpty;
} queue;

typedef struct {
  double open;
  double close;
  double max;
  double min;
  double vol;
  double totalPrice;
  uint64_t numTransactions;
  bool isDefault;
} candlestickMinute;

typedef struct {
  double prices[MOVING_AVERAGE_TIMESPAN_MINUTES];
  double totalPrice;
  uint8_t count;
  uint8_t index;
} movingAverage;

// todo: use this struct
typedef struct {
  candlestickMinute c;
  movingAverage ma;
  FILE *transaction;
  FILE *fp_ma;
  FILE *fp_candlestick;
} symbol;

movingAverage amzn_ma;
movingAverage msft_ma;
movingAverage binance_ma;
movingAverage icm_ma;

static FILE *amzn_fp;
static FILE *msft_fp;
static FILE *binance_fp;
static FILE *icm_fp;

static FILE *amzn_fp_candlestick;
static FILE *msft_fp_candlestick;
static FILE *binance_fp_candlestick;
static FILE *icm_fp_candlestick;

static FILE *amzn_fp_ma;
static FILE *msft_fp_ma;
static FILE *binance_fp_ma;
static FILE *icm_fp_ma;

queue *fifo;

candlestickMinute *amzn_candlestick;
candlestickMinute *msft_candlestick;
candlestickMinute *binance_candlestick;
candlestickMinute *icm_candlestick;

typedef struct {
  findata temp;
  int tid;
} pthread_data;

pthread_t producers[NUM_PRO_THREADS];
pthread_t consumers[NUM_CON_THREADS];
pthread_t scheduler;

pthread_mutex_t *mux;

pthread_data prodData[NUM_PRO_THREADS];
pthread_data conData[NUM_CON_THREADS];

bool areProducersFinished = false; // false if there are producers else true
bool *isConsumerFinished;          // keep track if consumer finished its tasks

uint64_t minutesSinceStart = 0;

void *producer(void *args);
void *consumer(void *args);

queue *queueInit();
void queueDelete(queue *q);
void queueAdd(queue *q, findata *in);
void queueDel(queue *q);

candlestickMinute *candlestickInit();
void movingAverageInit(movingAverage *ma);
void saveCandlestick(candlestickMinute *c, FILE *fp);
double getMean(candlestickMinute *c);
void updateCandlestick(candlestickMinute *c, findata *transaction);
void saveTransaction(findata *transaction);
void saveMovingAverage(movingAverage *ma, FILE *fp);
void *schedule(void *args);
void addDataPoint(movingAverage *ma, double price);
bool areConsumersFinished();
void transactionsHeader(FILE *fp);
void candlesticksHeader(FILE *fp);
void movingAverageHeader(FILE *fp);

// Variable that is =1 if the client should keep running, and =0 to close the
// client
static volatile int keepRunning = 1;

// Variable that is =1 if the client is connected, and =0 if not
static int connection_flag = 0;

// Variable that is =0 if the client should send messages to the server, and =1
// otherwise
static int writeable_flag = 0;

// Function to handle the change of the keepRunning boolean
void intHandler(int dummy) { keepRunning = 0; }

// The JSON paths/labels that we are interested in
static const char *const tok[] = {
    "data[].p",
    "data[].s",
    "data[].t",
    "data[].v",
};

static unsigned long long get_timestamp() {
  struct timeval tv;

  gettimeofday(&tv, NULL);

  unsigned long long millisecondsSinceEpoch =
      (unsigned long long)(tv.tv_sec) * 1000 +
      (unsigned long long)(tv.tv_usec) / 1000;
  return millisecondsSinceEpoch;
}

static void findataFromJson(findata *transaction, const char *label,
                            char *buf) {
  if (strcmp(label, "data[].p") == 0) {
    transaction->price = (char *)malloc(strlen(buf) + 1);
    strcpy(transaction->price, buf);
  } else if (strcmp(label, "data[].s") == 0) {
    transaction->symbol = (char *)malloc(strlen(buf) + 1);
    strcpy(transaction->symbol, buf);
  } else if (strcmp(label, "data[].t") == 0) {
    transaction->timestamp = (char *)malloc(strlen(buf) + 1);
    strcpy(transaction->timestamp, buf);
  } else if (strcmp(label, "data[].v") == 0) {
    transaction->volume = (char *)malloc(strlen(buf) + 1);
    strcpy(transaction->volume, buf);
  } else
    printf("Label undefined\n");
}

// Callback function for the LEJP JSON Parser
static signed char cb(struct lejp_ctx *ctx, char reason) {
  findata *transaction = (findata *)ctx->user;
  if (reason & LEJP_FLAG_CB_IS_VALUE && (ctx->path_match > 0)) {
    int last_element_from_tok = 4;
    findataFromJson(transaction, ctx->path, ctx->buf);
    if (ctx->path_match == last_element_from_tok) {
      queueAdd(fifo, transaction);
    }
  }
  if (reason == LEJPCB_COMPLETE) {
    // fflush(fp);
  }

  return 0;
}

// Function used to "write" to the socket, so to send messages to the server
// @args:
// ws_in        -> the websocket struct
// str          -> the message to write/send
// str_size_in  -> the length of the message
static int websocket_write_back(struct lws *wsi_in, char *str,
                                int str_size_in) {
  if (str == NULL || wsi_in == NULL)
    return -1;
  int m;
  int n;
  int len;
  char *out = NULL;

  if (str_size_in < 1)
    len = strlen(str);
  else
    len = str_size_in;

  out = (char *)malloc(sizeof(char) * (LWS_SEND_BUFFER_PRE_PADDING + len +
                                       LWS_SEND_BUFFER_POST_PADDING));
  //* setup the buffer*/
  memcpy(out + LWS_SEND_BUFFER_PRE_PADDING, str, len);
  //* write out*/
  n = lws_write(wsi_in, out + LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT);

  printf(KBLU "[websocket_write_back] %s\n" RESET, str);
  //* free the buffer*/
  free(out);

  return n;
}

// The websocket callback function
static int ws_service_callback(struct lws *wsi,
                               enum lws_callback_reasons reason, void *user,
                               void *in, size_t len) {

  // Switch-Case structure to check the reason for the callback
  switch (reason) {

  case LWS_CALLBACK_CLIENT_ESTABLISHED:
    printf(KYEL "[Main Service] Connect with server success.\n" RESET);

    // Call the on writable callback, to send the subscribe messages to the
    // server
    lws_callback_on_writable(wsi);
    break;

  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    printf(KRED "[Main Service] Connect with server error: %s.\n" RESET, in);
    // Set the flag to 0, to show that the connection was lost
    connection_flag = 0;
    break;

  case LWS_CALLBACK_CLOSED:
    printf(KYEL "[Main Service] LWS_CALLBACK_CLOSED\n" RESET);
    // Set the flag to 0, to show that the connection was lost
    connection_flag = 0;
    break;

  case LWS_CALLBACK_CLIENT_RECEIVE:;
    // Incoming messages are handled here

    // UNCOMMENT for printing the message on the terminal
    // printf(KCYN_L"[Main Service] Client received:%s\n"RESET, (char *)in);

    // Print that messages are being received
    printf(KCYN_L "\r[Main Service] Client receiving messages" RESET);
    fflush(stdout);

    // Initialize a LEJP JSON parser, and pass it the incoming message
    char *msg = (char *)in;

    struct lejp_ctx ctx;
    findata *transaction = (findata *)malloc(sizeof(findata));
    lejp_construct(&ctx, cb, NULL, tok, LWS_ARRAY_SIZE(tok));
    ctx.user = transaction;
    int m = lejp_parse(&ctx, (uint8_t *)msg, strlen(msg));
    if (m < 0 && m != LEJP_CONTINUE) {
      lwsl_err("parse failed %d\n", m);
    }

    break;

  case LWS_CALLBACK_CLIENT_WRITEABLE:

    // When writeable, send the server the desired trade symbols to subscribe
    // to, if not already subscribed
    printf(KYEL "\n[Main Service] On writeable is called.\n" RESET);

    if (!writeable_flag) {
      char symb_arr[4][50] = {"MSFT\0", "AMZN\0", "BINANCE:BTCUSDT\0",
                              "IC MARKETS:1\0"};
      char str[100];
      for (int i = 0; i < 4; i++) {
        sprintf(str, "{\"type\":\"subscribe\",\"symbol\":\"%s\"}", symb_arr[i]);
        int len = strlen(str);
        websocket_write_back(wsi, str, len);
      }

      // Set the flag to 1, to show that the subscribe request have been sent
      writeable_flag = 1;
    }
    break;
  case LWS_CALLBACK_CLIENT_CLOSED:

    // If the client is closed for some reason, set the connection and writeable
    // flags to 0, so a connection can be re-established
    printf(KYEL "\n[Main Service] Client closed %s.\n" RESET, in);
    connection_flag = 0;
    writeable_flag = 0;

    break;
  default:
    break;
  }

  return 0;
}

// Protocol to be used with the websocket callback
static struct lws_protocols protocols[] = {
    {
        "trade_protocol",
        ws_service_callback,
    },
    {NULL, NULL, 0, 0} /* terminator */
};

struct lws_context *context = NULL;
struct lws_context_creation_info info;
struct lws_client_connect_info clientConnectionInfo;

// Main function
int main(void) {
  // Set intHandle to handle the SIGINT signal
  // (Used for terminating the client)
  signal(SIGINT, intHandler);

  amzn_fp = fopen("logs_amzn.csv", "w");
  msft_fp = fopen("logs_msft.csv", "w");
  binance_fp = fopen("logs_binance.csv", "w");
  icm_fp = fopen("logs_icm.csv", "w");
  transactionsHeader(amzn_fp);
  transactionsHeader(msft_fp);
  transactionsHeader(binance_fp);
  transactionsHeader(icm_fp);

  amzn_fp_candlestick = fopen("logs_amzn_candlestick.csv", "w");
  msft_fp_candlestick = fopen("logs_msft_candlestick.csv", "w");
  binance_fp_candlestick = fopen("logs_binance_candlestick.csv", "w");
  icm_fp_candlestick = fopen("logs_icm_candlestick.csv", "w");
  candlesticksHeader(amzn_fp_candlestick);
  candlesticksHeader(msft_fp_candlestick);
  candlesticksHeader(binance_fp_candlestick);
  candlesticksHeader(icm_fp_candlestick);

  amzn_fp_ma = fopen("logs_amzn_ma.csv", "w");
  msft_fp_ma = fopen("logs_msft_ma.csv", "w");
  binance_fp_ma = fopen("logs_binance_ma.csv", "w");
  icm_fp_ma = fopen("logs_icm_ma.csv", "w");
  movingAverageHeader(amzn_fp_ma);
  movingAverageHeader(msft_fp_ma);
  movingAverageHeader(binance_fp_ma);
  movingAverageHeader(icm_fp_ma);

  memset(&info, 0, sizeof info);

  // Set the context of the websocket
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = protocols;
  info.gid = -1;
  info.uid = -1;
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

  // Set the Finnhub url
  char *api_key = "cpq4731r01qo647ncergcpq4731r01qo647nces0";
  if (strlen(api_key) == 0) {
    printf(" API KEY NOT PROVIDED!\n");
    return -1;
  }

  // Create the websocket context
  context = lws_create_context(&info);
  printf(KGRN "[Main] context created.\n" RESET);

  if (context == NULL) {
    printf(KRED "[Main] context is NULL.\n" RESET);
    return -1;
  }

  // Set up variables for the url
  char inputURL[300];
  sprintf(inputURL, "wss://ws.finnhub.io/?token=%s", api_key);
  const char *urlProtocol, *urlTempPath;

  memset(&clientConnectionInfo, 0, sizeof(clientConnectionInfo));

  // Set the context for the client connection
  clientConnectionInfo.context = context;

  // Parse the url
  if (lws_parse_uri(inputURL, &urlProtocol, &clientConnectionInfo.address,
                    &clientConnectionInfo.port, &urlTempPath)) {
    printf("Couldn't parse URL\n");
  }

  char urlPath[300];
  urlPath[0] = '/';
  strncpy(urlPath + 1, urlTempPath, sizeof(urlPath) - 2);
  urlPath[sizeof(urlPath) - 1] = '\0';

  clientConnectionInfo.port = 443;
  clientConnectionInfo.path = urlPath;
  clientConnectionInfo.ssl_connection = LCCSCF_USE_SSL |
                                        LCCSCF_ALLOW_SELFSIGNED |
                                        LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

  clientConnectionInfo.host = clientConnectionInfo.address;
  clientConnectionInfo.origin = clientConnectionInfo.address;
  clientConnectionInfo.ietf_version_or_minus_one = -1;
  clientConnectionInfo.protocol = protocols[0].name;

  mux = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(mux, NULL);

  fifo = queueInit();
  if (fifo == NULL) {
    fprintf(stderr, "main: Queue Init failed.\n");
    exit(1);
  }

  amzn_candlestick = candlestickInit();
  msft_candlestick = candlestickInit();
  binance_candlestick = candlestickInit();
  icm_candlestick = candlestickInit();

  movingAverageInit(&amzn_ma);
  movingAverageInit(&msft_ma);
  movingAverageInit(&binance_ma);
  movingAverageInit(&icm_ma);

  isConsumerFinished = (bool *)malloc(NUM_CON_THREADS * sizeof(bool));
  for (int i = 0; i < NUM_CON_THREADS; i++) {
    isConsumerFinished[i] = false;
  }

  // todo: check returned code
  pthread_create(&scheduler, NULL, schedule, NULL);

  int rc;
  for (int i = 0; i < NUM_PRO_THREADS; i++) {
    prodData[i].tid = i;
    if (rc = pthread_create(&producers[i], NULL, producer, &prodData[i])) {
      printf("Error creating threads %d\n", rc);
    }
  }

  for (int i = 0; i < NUM_CON_THREADS; i++) {
    conData[i].tid = i;
    if (rc = pthread_create(&consumers[i], NULL, consumer, &conData[i])) {
      printf("Error creating threads %d\n", rc);
    }
  }

  for (int i = 0; i < NUM_PRO_THREADS; i++) {
    pthread_join(producers[i], NULL);
    printf("Joined producer id: %d\n", i);
  }

  areProducersFinished = true;

  // spam signals until all consumer threads have finished
  // Fix: Use broadcast
  while (1) {
    printf("Send signal if someone is blocked\n");
    pthread_cond_signal(fifo->notEmpty); // unblock the waited thread
    if (areConsumersFinished())
      break;
    usleep(10); // hibernate
  }

  for (int i = 0; i < NUM_CON_THREADS; i++) {
    pthread_join(consumers[i], NULL);
    printf("Joined consumer id: %d\n", i);
  }

  pthread_join(scheduler, NULL);

  printf(KRED "\n[Main] Closing client\n" RESET);
  lws_context_destroy(context);
  queueDelete(fifo);
  fclose(amzn_fp);
  fclose(msft_fp);
  fclose(binance_fp);
  fclose(icm_fp);
  return 0;
}

bool areConsumersFinished() {
  bool finish = isConsumerFinished[0];
  for (int i = 1; i < NUM_CON_THREADS; i++) {
    finish = finish && isConsumerFinished[i];
  }
  return finish;
}

void *producer(void *args) {
  pthread_data *data = (pthread_data *)args;
  struct lws *wsi = NULL;
  // time_t start_time = time(NULL);
  // while (difftime(time(NULL), start_time) < 20.0) {
  while (keepRunning) {
    //   If the websocket is not connected, connect
    pthread_mutex_lock(mux);

    if (!connection_flag) {
      printf(KGRN "Connecting to %s://%s:%d%s \n\n" RESET,
             clientConnectionInfo.protocol, clientConnectionInfo.address,
             clientConnectionInfo.port, clientConnectionInfo.path);
      wsi = lws_client_connect_via_info(
          &clientConnectionInfo); // I think this one opens the websocket, and
                                  // you are allowed to have only 1
      if (wsi == NULL) {
        printf(KRED "[Main] wsi create error.\n" RESET);
        return (NULL); // todo exit with error code -1
      }
      printf(KGRN "[Main] wsi creation success.\n" RESET);
      connection_flag = 1;
    }

    while (fifo->full) {
      printf("producer: queue FULL.\n");
      pthread_cond_wait(fifo->notFull, mux);
    }

    // Service websocket activity
    // only one thread should listen maybe?
    lws_service(context, 0);
    pthread_mutex_unlock(mux);
    pthread_cond_signal(fifo->notEmpty);
  }
  return (NULL);
}

void *schedule(void *args) {
  while (1) {
    if (areConsumersFinished())
      break;
    sleep(60);
    minutesSinceStart += 1;
    pthread_mutex_lock(mux);
    fprintf(amzn_fp, "\n");
    fprintf(msft_fp, "\n");
    fprintf(binance_fp, "\n");
    fprintf(icm_fp, "\n");

    saveCandlestick(amzn_candlestick, amzn_fp_candlestick);
    saveCandlestick(msft_candlestick, msft_fp_candlestick);
    saveCandlestick(binance_candlestick, binance_fp_candlestick);
    saveCandlestick(icm_candlestick, icm_fp_candlestick);

    addDataPoint(&amzn_ma, getMean(amzn_candlestick));
    addDataPoint(&msft_ma, getMean(msft_candlestick));
    addDataPoint(&binance_ma, getMean(binance_candlestick));
    addDataPoint(&icm_ma, getMean(icm_candlestick));

    if (minutesSinceStart % MOVING_AVERAGE_TIMESPAN_MINUTES == 0) {
      saveMovingAverage(&amzn_ma, amzn_fp_ma);
      saveMovingAverage(&msft_ma, msft_fp_ma);
      saveMovingAverage(&binance_ma, binance_fp_ma);
      saveMovingAverage(&icm_ma, icm_fp_ma);
    }

    msft_candlestick = candlestickInit();
    amzn_candlestick = candlestickInit();
    binance_candlestick = candlestickInit();
    icm_candlestick = candlestickInit();

    pthread_mutex_unlock(mux);
  }
  return (NULL);
}

double getMean(candlestickMinute *c) {
  if (c->numTransactions == 0)
    return 0;
  return c->totalPrice / c->numTransactions;
}

void addDataPoint(movingAverage *ma, double price) {
  if (price == 0)
    return;
  // Remove the oldest data point from totals if the window is full
  if (ma->count == MOVING_AVERAGE_TIMESPAN_MINUTES) {
    ma->totalPrice -= ma->prices[ma->index];
  } else {
    ma->count++;
  }

  ma->prices[ma->index] = price;
  ma->totalPrice += price;

  ma->index = (ma->index + 1) % MOVING_AVERAGE_TIMESPAN_MINUTES;
}

void saveCandlestick(candlestickMinute *c, FILE *fp) {
  fprintf(fp, "%lf,%lf,%lf,%lf,%lf,%lf,%lu,%lf,%llu\n", c->open, c->close,
          c->min, c->max, c->vol, c->totalPrice, c->numTransactions, getMean(c),
          get_timestamp());
}

void saveMovingAverage(movingAverage *ma, FILE *fp) {
  double value;
  if (ma->count == 0) {
    value = 0;
  } else {
    value = ma->totalPrice / ma->count;
  }
  fprintf(fp, "%lf,%lf,%u,%llu\n", value, ma->totalPrice, ma->count,
          get_timestamp());
}

void *consumer(void *args) {
  pthread_data *data = (pthread_data *)args;

  while (1) {
    pthread_mutex_lock(mux);
    while (fifo->empty && !areProducersFinished) {
      printf("consumer: queue EMPTY\n");
      pthread_cond_wait(fifo->notEmpty, mux);
    }
    queueDel(fifo);
    pthread_mutex_unlock(mux);
    pthread_cond_signal(fifo->notFull);
    if (areProducersFinished && fifo->empty)
      break;
  }

  printf("Consumer Finished id:%d\n", data->tid);
  isConsumerFinished[data->tid] = true;

  return (NULL);
}

void transactionsHeader(FILE *fp) {
  fprintf(fp, "Price,Symbol,Timestamp,Volume,Posttimestamp\n");
}

void candlesticksHeader(FILE *fp) {
  fprintf(
      fp,
      "Open,Close,Low,High,Volume,TotalPrice,NumTransactions,Mean,Timestamp\n");
}

void movingAverageHeader(FILE *fp) {
  fprintf(fp, "Price,Total,Count,Timestamp\n");
}

candlestickMinute *candlestickInit() {
  candlestickMinute *c;
  c = (candlestickMinute *)malloc(sizeof(candlestickMinute));
  c->close = 0;
  c->open = 0;
  c->max = 0;
  c->min = 0;
  c->vol = 0;
  c->totalPrice = 0;
  c->numTransactions = 0;
  c->isDefault = true;
  return c;
}

void movingAverageInit(movingAverage *ma) {
  ma->index = 0;
  ma->count = 0;
  ma->totalPrice = 0.0;
  for (int i = 0; i < MOVING_AVERAGE_TIMESPAN_MINUTES; i++) {
    ma->prices[i] = 0.0;
  }
}

queue *queueInit() {
  queue *q;

  q = (queue *)malloc(sizeof(queue));
  if (q == NULL)
    return (NULL);

  q->empty = 1;
  q->full = 0;
  q->head = 0;
  q->tail = 0;

  q->notFull = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  pthread_cond_init(q->notFull, NULL);

  q->notEmpty = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
  pthread_cond_init(q->notEmpty, NULL);

  for (int i = 0; i < QUEUESIZE; i++) {
    q->buf[i] = DEFAULT_FINDATA;
  }

  return (q);
}

void queueDelete(queue *q) {
  pthread_cond_destroy(q->notFull);
  free(q->notFull);
  pthread_cond_destroy(q->notEmpty);
  free(q->notEmpty);
  pthread_mutex_destroy(mux);
  free(mux);
  free(q);
}

void queueAdd(queue *q, findata *transaction) {
  // todo, maybe memcpy
  q->buf[q->tail].price = transaction->price;
  q->buf[q->tail].symbol = transaction->symbol;
  q->buf[q->tail].timestamp = transaction->timestamp;
  q->buf[q->tail].volume = transaction->volume;

  q->tail++;
  if (q->tail == QUEUESIZE)
    q->tail = 0;
  if (q->tail == q->head)
    q->full = 1;
  q->empty = 0;

  return;
}

void queueDel(queue *q) {
  if (q->empty) {
    printf("\033[1mThere is nothing to delete. Queue empty. Aborting\033[0m\n");
    return;
  }

  findata transaction = q->buf[q->head];
  saveTransaction(&transaction);

  q->head++;
  if (q->head == QUEUESIZE)
    q->head = 0;
  if (q->head == q->tail)
    q->empty = 1;
  q->full = 0;

  return;
}

void saveTransaction(findata *transaction) {
  FILE *fp;
  candlestickMinute *c;
  if (strcmp(transaction->symbol, "AMZN") == 0) {
    fp = amzn_fp;
    c = amzn_candlestick;
  } else if (strcmp(transaction->symbol, "MSFT") == 0) {
    fp = msft_fp;
    c = msft_candlestick;
  } else if (strcmp(transaction->symbol, "BINANCE:BTCUSDT") == 0) {
    fp = binance_fp;
    c = binance_candlestick;
  } else if (strcmp(transaction->symbol, "IC MARKETS:1") == 0) {
    fp = icm_fp;
    c = icm_candlestick;
  } else {
    printf("Error returning file descriptior: Not found\n");
  }

  fprintf(fp, "%s,%s,%s,%s,%llu\n", transaction->price, transaction->symbol,
          transaction->timestamp, transaction->volume, get_timestamp());
  updateCandlestick(c, transaction);
}

void updateCandlestick(candlestickMinute *c, findata *transaction) {
  double price;
  double vol;
  uint64_t timestamp;
  sscanf(transaction->price, "%lf", &price);
  sscanf(transaction->volume, "%lf", &vol);
  sscanf(transaction->timestamp, "%lu", &timestamp);

  if (c->isDefault) {
    c->open = price;
    c->min = price;
    c->max = price;
  } else {
    if (c->min > price) {
      c->min = price;
    }
    if (c->max < price) {
      c->max = price;
    }
  }

  c->close = price;
  c->numTransactions += 1;
  c->totalPrice += price;
  c->vol += vol;
  c->isDefault = false;
}
