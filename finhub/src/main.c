#include <libwebsockets.h>
#include <string.h>
#include <signal.h>

static struct lws_context *cx;
static int interrupted;
int test_result = 1;

extern const lws_ss_info_t ssi_finnhub_t;

// static const struct lws_extension extensions[] = {
//     {
//         "permessage-deflate", lws_extension_callback_pm_deflate,
//         "permessage-deflate; client_no_context_takeover; client_max_window_bits"
//     },
//     { NULL, NULL, NULL } // terminator
// };

static void sigint_handler(int sig) {
    lws_default_loop_exit(cx);
}

int main(int argc, const char **argv) {
    struct lws_context_creation_info info;

    lws_context_info_defaults(&info, "src/policy.json");
    lws_cmdline_option_handle_builtin(argc, argv, &info);
    signal(SIGINT, sigint_handler);

    lwsl_user("LWS minimal Secure Streams Finnhub client\n");

    // info.extensions = extensions;

    cx = lws_create_context(&info);
    if (!cx) {
        lwsl_err("lws init failed\n");
        return 1;
    }

    if (lws_ss_create(cx, 0, &ssi_finnhub_t, NULL, NULL, NULL, NULL)) {
        lwsl_cx_err(cx, "failed to create secure stream");
        interrupted = 1;
    }

    lws_context_default_loop_run_destroy(cx);

    return lws_cmdline_passfail(argc, argv, test_result);
}