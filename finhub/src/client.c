// Simple client implemented using libwebsockets, to communicate with Finnhub
// web sockets, and write the incoming trades in a text file. For RTES ECE AUTH,
// 2022

#include <libwebsockets.h>
#include <signal.h>
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

#define QUEUESIZE 25
#define NUM_PRO_THREADS 2
#define NUM_CON_THREADS 2

typedef struct {
  double price;
  char *symbol;
  unsigned long long timestamp;
  double volume;
} findata;

const findata DEFAULT_FINDATA = {0, "", 0, 0};

typedef struct {
  findata buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
} queue;

typedef struct {
  queue *q;
  int tid;
} pthread_data;

pthread_t producers[NUM_PRO_THREADS];
pthread_t consumers[NUM_CON_THREADS];
pthread_data prodData[NUM_PRO_THREADS];
pthread_data conData[NUM_CON_THREADS];

void *producer(void *args);
void *consumer(void *args);

queue *queueInit();
void queueDelete(queue *q);
void queueAdd(queue *q, int in);
void queueDel(queue *q, int *out);

// Variable that is =1 if the client should keep running, and =0 to close the
// client
static volatile int keepRunning = 1;

// Variable that is =1 if the client is connected, and =0 if not
static int connection_flag = 0;

// Variable that is =0 if the client should send messages to the server, and =1
// otherwise
static int writeable_flag = 0;

// The pointer to the output file
static FILE *out_fp;

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

// Callback function for the LEJP JSON Parser
static signed char cb(struct lejp_ctx *ctx, char reason) {

  // If the parsed JSON object is one we are interested in (so in the tok
  // array), write to file
  if (reason & LEJP_FLAG_CB_IS_VALUE && (ctx->path_match > 0)) {
    int last_element_from_tok = 4;
    if (ctx->path_match == last_element_from_tok)
      fprintf(out_fp, "%s,%llu\n", ctx->buf, get_timestamp());
    else
      fprintf(out_fp, "%s,", ctx->buf);
  }
  if (reason == LEJPCB_COMPLETE) {
    fflush(out_fp);
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
    lejp_construct(&ctx, cb, NULL, tok, LWS_ARRAY_SIZE(tok));
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
      char symb_arr[4][50] = {"APPL\0", "AMZN\0", "BINANCE:BTCUSDT\0",
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

// Main function
int main(void) {
  // Set intHandle to handle the SIGINT signal
  // (Used for terminating the client)
  signal(SIGINT, intHandler);

  // Open the output file
  out_fp = fopen("logs.csv", "w");
  fprintf(out_fp, "price,symbol,timestamp,volume,posttimestamp\n");

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

  struct lws_client_connect_info clientConnectionInfo;
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

  queue *fifo;
  fifo = queueInit();

  // While a kill signal is not sent (ctrl+c), keep running
  struct lws *wsi = NULL;
  while (keepRunning) {
    // If the websocket is not connected, connect
    if (!connection_flag || !wsi) {
      printf(KGRN "Connecting to %s://%s:%d%s \n\n" RESET, urlProtocol,
             clientConnectionInfo.address, clientConnectionInfo.port,
             clientConnectionInfo.path);
      wsi = lws_client_connect_via_info(&clientConnectionInfo);
      if (wsi == NULL) {
        printf(KRED "[Main] wsi create error.\n" RESET);
        return -1;
      }
      printf(KGRN "[Main] wsi creation success.\n" RESET);
      connection_flag = 1;
    }

    // Service websocket activity
    lws_service(context, 0);
  }

  printf(KRED "\n[Main] Closing client\n" RESET);
  lws_context_destroy(context);
  fclose(out_fp);
  return 0;
}

void *producer(void *args) {}

void *consumer(void *args) {}

queue *queueInit() {
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
    q->buf[i] = DEFAULT_FINDATA;
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
  // q->buf[q->tail].value = in;
  //  TIC(q->tail) // start counting for q->tail producer

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
    printf("\033[1mThere is nothing to delete. Queue empty. Aborting\033[0m\n");
    return;
  }

  // TOC(q->head) // stop counting for q->head producer

  //*out = q->buf[q->head].value;
  printf("consumer: received %d from %ld\n", *out, q->head);

  q->head++;

  if (q->head == QUEUESIZE)
    q->head = 0;
  if (q->head == q->tail)
    q->empty = 1;
  q->full = 0;

  return;
}