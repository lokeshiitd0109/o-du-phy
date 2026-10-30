/* SPDX-License-Identifier: Apache-2.0 */
#ifndef XRAN_IO_SAFETY_H
#define XRAN_IO_SAFETY_H
#include <stdint.h>

struct rte_mbuf;
typedef uint16_t (*xran_tx_attempt_fn)(uint16_t, uint16_t, struct rte_mbuf **, uint16_t);
typedef void (*xran_mbuf_release_fn)(struct rte_mbuf *);

/* Bound NIC calls even when each call makes partial progress. Fronthaul work
 * cannot wait indefinitely for TX descriptors. Accepted buffers belong to the
 * driver; only the unaccepted suffix remains ours to release. */
#define XRAN_TX_MAX_ATTEMPTS 8
static inline uint16_t xran_release_suffix(struct rte_mbuf **pkts, uint16_t accepted,
                                           uint16_t count, xran_mbuf_release_fn release)
{
    for (uint16_t i = accepted; i < count; ++i)
        release(pkts[i]);
    return count - accepted;
}

static inline uint16_t xran_tx_burst_bounded(uint16_t port, uint16_t queue,
        struct rte_mbuf **pkts, uint16_t count, xran_tx_attempt_fn send,
        xran_mbuf_release_fn release)
{
    uint16_t sent = 0;
    for (unsigned attempt = 0; sent < count && attempt < XRAN_TX_MAX_ATTEMPTS; ++attempt)
        sent += send(port, queue, pkts + sent, count - sent);
    return xran_release_suffix(pkts, sent, count, release);
}

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
/* Additive API: no changes to existing public counter/context layouts. */
void xran_get_io_safety_stats(uint64_t *tx_unsent, uint64_t *rx_ring_dropped);
#endif
