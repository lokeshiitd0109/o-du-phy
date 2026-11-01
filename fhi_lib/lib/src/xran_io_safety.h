/* SPDX-License-Identifier: Apache-2.0 */
#ifndef XRAN_IO_SAFETY_H
#define XRAN_IO_SAFETY_H
#include <stdint.h>

struct rte_mbuf;
typedef uint16_t (*xran_tx_attempt_fn)(uint16_t, uint16_t, struct rte_mbuf **, uint16_t);
typedef void (*xran_mbuf_release_fn)(struct rte_mbuf *);

/* Time budget bounds submission work, not time spent inside a PMD call.
 * It is not a packet's absolute O-RAN transmission deadline. */
typedef uint64_t (*xran_clock_fn)(void);
struct xran_tx_result { uint64_t elapsed; uint32_t calls, partial, zero; };
static inline uint16_t xran_release_suffix(struct rte_mbuf **pkts, uint16_t accepted,
                                           uint16_t count, xran_mbuf_release_fn release)
{
    for (uint16_t i = accepted; i < count; ++i)
        release(pkts[i]);
    return count - accepted;
}

static inline uint16_t xran_tx_burst_budget(uint16_t port, uint16_t queue,
        struct rte_mbuf **pkts, uint16_t count, xran_tx_attempt_fn send,
        xran_mbuf_release_fn release, xran_clock_fn clock, uint64_t budget,
        struct xran_tx_result *result)
{
    *result = (struct xran_tx_result){0};
    if (!count) return 0;
    uint64_t start = clock();
    uint16_t sent = 0;
    do {
        uint16_t n = send(port, queue, pkts + sent, count - sent);
        ++result->calls;
        result->zero += n == 0;
        result->partial += n != 0 && n < count - sent;
        sent += n;
        result->elapsed = clock() - start;
    } while (sent < count && result->elapsed < budget);
    return xran_release_suffix(pkts, sent, count, release);
}
uint16_t xran_tx_submit(uint16_t port, uint16_t queue, struct rte_mbuf **pkts, uint16_t count);
void xran_io_set_tx_budget(uint32_t ns);
/* Processing-time deadline diagnostics; not NIC wire timestamps.
 * Getter output pointers must be non-NULL. Counts are cumulative. */
void xran_note_timer_lateness(int64_t late_ns, int64_t symbol_ns);
void xran_get_timer_lateness_stats_v1(uint64_t *boundaries, uint64_t *late_symbols,
        uint64_t *late_slots, uint64_t *max_ns);
void xran_publish_rx_clock(uint8_t mu, uint32_t index, uint16_t sfn);
int xran_read_rx_clock(uint8_t mu, uint32_t *index, uint16_t *sfn);
struct xran_rx_packet_ctl;
int xran_rx_packet_store(struct xran_rx_packet_ctl *ctl, uint16_t start,
    uint16_t size, uint16_t section, uint8_t *data, struct rte_mbuf *mb, int retain, uint32_t tti, uint32_t cycle);

/* Packet frameId wraps at 256 frames, independently of the 1-second timer.
 * The windows are much shorter than half that cycle. */
static inline int xran_symbol_delta(int now, int packet, int cycle)
{
    int delta = (now - packet) % cycle;
    if (delta > cycle / 2) delta -= cycle;
    if (delta < -cycle / 2) delta += cycle;
    return delta;
}

/* Negative = early, positive = late. Both boundaries are inclusive. */
static inline int xran_rx_window_classify(int delta, int lower, int upper)
{
    return delta < lower ? -1 : delta > upper ? 1 : 0;
}

static inline int xran_prach_symbol_offset(int symbol, int start, int symbols)
{
    if (start < 0 || start >= symbols || symbol < start || symbol >= symbols)
        return -1;
    return symbol - start;
}

void xran_io_note_tx_drop(uint16_t count);
void xran_io_note_tx_ring_drop(void);
void xran_io_note_alloc_failure(void);
/* Additive API: no changes to existing public counter/context layouts. */
void xran_get_io_safety_stats(uint64_t *tx_unsent, uint64_t *rx_ring_dropped);
#endif
