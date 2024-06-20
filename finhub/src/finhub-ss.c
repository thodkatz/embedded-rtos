#include <libwebsockets.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

extern int test_result;

typedef struct range {
    uint64_t        sum;
    uint64_t        lowest;
    uint64_t        highest;
    unsigned int    samples;
} range_t;

LWS_SS_USER_TYPEDEF
    lws_sorted_usec_list_t    sul_hz;
    range_t            e_lat_range;
    range_t            price_range;
} finnhub_t;

static void range_reset(range_t *r) {
    r->sum = r->highest = 0;
    r->lowest = 999999999999ull;
    r->samples = 0;
}

static uint64_t get_us_timeofday(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)((lws_usec_t)tv.tv_sec * LWS_US_PER_SEC) +
              (uint64_t)tv.tv_usec;
}

static void sul_hz_cb(lws_sorted_usec_list_t *sul) {
    finnhub_t *fh = lws_container_of(sul, finnhub_t, sul_hz);

    lws_sul_schedule(lws_ss_get_context(fh->ss), 0, &fh->sul_hz, sul_hz_cb, LWS_US_PER_SEC);

    if (fh->price_range.samples)
        lwsl_ss_user(lws_ss_from_user(fh),
            "price: min: %llu, max: %llu, avg: %llu, (%d prices/s)",
            (unsigned long long)fh->price_range.lowest,
            (unsigned long long)fh->price_range.highest,
            (unsigned long long)(fh->price_range.sum / fh->price_range.samples),
            fh->price_range.samples);
    if (fh->e_lat_range.samples)
        lwsl_ss_user(lws_ss_from_user(fh),
            "elatency: min: %llums, max: %llums, avg: %llums, (%d msg/s)",
            (unsigned long long)fh->e_lat_range.lowest / 1000,
            (unsigned long long)fh->e_lat_range.highest / 1000,
            (unsigned long long)(fh->e_lat_range.sum / fh->e_lat_range.samples) / 1000,
            fh->e_lat_range.samples);

    range_reset(&fh->e_lat_range);
    range_reset(&fh->price_range);

    test_result = 0;
}

static lws_ss_state_return_t finnhub_rx(void *userobj, const uint8_t *in, size_t len, int flags) {
    finnhub_t *fh = (finnhub_t *)userobj;
    uint64_t latency_us, now_us;
    char numbuf[16];
    uint64_t price;
    const char *p;
    size_t alen;

    now_us = (uint64_t)get_us_timeofday();

    p = lws_json_simple_find((const char *)in, len, "\"price\":", &alen);
    if (!p) {
        return LWSSSSRET_OK;
    }

    lws_strnncpy(numbuf, p, alen, sizeof(numbuf));
    price = (uint64_t)atoll(numbuf);

    if (price < fh->price_range.lowest)
        fh->price_range.lowest = price;
    if (price > fh->price_range.highest)
        fh->price_range.highest = price;

    fh->price_range.sum += price;
    fh->price_range.samples++;

    return LWSSSSRET_OK;
}

static lws_ss_state_return_t finnhub_state(void *userobj, void *h_src, lws_ss_constate_t state, lws_ss_tx_ordinal_t ack) {
    finnhub_t *fh = (finnhub_t *)userobj;

    lwsl_ss_info(fh->ss, "%s (%d), ord 0x%x",
                 lws_ss_state_name((int)state), state, (unsigned int)ack);

    switch (state) {
        case LWSSSCS_CONNECTED:
            lws_sul_schedule(lws_ss_get_context(fh->ss), 0, &fh->sul_hz, sul_hz_cb, LWS_US_PER_SEC);
            range_reset(&fh->e_lat_range);
            range_reset(&fh->price_range);
            return LWSSSSRET_OK;

        case LWSSSCS_DISCONNECTED:
            lws_sul_cancel(&fh->sul_hz);
            break;

        default:
            break;
    }

    return LWSSSSRET_OK;
}

LWS_SS_INFO("finnhub", finnhub_t)
    .rx = finnhub_rx,
    .state = finnhub_state,
};
