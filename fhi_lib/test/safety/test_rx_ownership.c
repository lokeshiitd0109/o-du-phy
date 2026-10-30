/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <rte_mbuf.h>
#include <rte_spinlock.h>
#include "xran_fh_o_du.h"
#include "../../lib/src/xran_io_safety.h"
/* Exercise production metadata/locking code with real DPDK refcount operations;
 * stack/test buffers don't belong to a mempool, so replace only final release. */
static void release_test(struct rte_mbuf *m)
{
    assert(rte_mbuf_refcnt_read(m) > 0);
    rte_pktmbuf_refcnt_update(m, -1);
}
#define rte_pktmbuf_free release_test
#include "../../lib/src/xran_rx_ownership.h"
static void init_mb(struct rte_mbuf *m) { m->nb_segs = 1; rte_mbuf_refcnt_set(m, 1); }
static void cleanup(struct xran_rx_packet_ctl *c)
{
    for (int i = 0; i < XRAN_MAX_RX_PKT_PER_SYM; ++i)
        if (c->pCtrl[i]) release_test(c->pCtrl[i]);
}
#define N 10000
static struct xran_rx_packet_ctl live;
static struct rte_mbuf buffers[N];
static _Atomic unsigned published;
static _Atomic int done;
static void *producer(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < N; ++i) {
        init_mb(&buffers[i]);
        assert(xran_rx_packet_store(&live, i, 1, i, (uint8_t *)&buffers[i], &buffers[i], 1, i, 1000000) == 0);
        release_test(&buffers[i]);
        atomic_store(&published, i + 1);
    }
    atomic_store(&done, 1);
    return NULL;
}
static void *consumer(void *unused)
{
    (void)unused;
    while (!atomic_load(&done)) {
        unsigned n = atomic_load(&published);
        if (!n) continue;
        struct xran_rx_packet_ctl snap;
        xran_rx_snapshot_v1(&live, &snap, n - 1, 1000000);
        for (int i = 0; i < snap.nRxPkt; ++i) {
            unsigned k = snap.nRBStart[i];
            assert(k < N && snap.pData[i] == (uint8_t *)&buffers[k]);
            assert(snap.pCtrl[i] == &buffers[k] && rte_mbuf_refcnt_read(&buffers[k]) > 0);
        }
        xran_rx_snapshot_release_v1(&snap);
    }
    return NULL;
}
int main(void)
{
    struct xran_rx_packet_ctl c = {0}, snap, empty;
    struct rte_mbuf mb[4] = {0};
    for (int i = 0; i < 4; ++i) init_mb(mb + i);
    // Independent references for two sections sharing the parser's packet.
    assert(!xran_rx_packet_store(&c, 0, 12, 1, (uint8_t *)mb, mb, 1, 2559, 2560));
    assert(!xran_rx_packet_store(&c, 12, 12, 2, (uint8_t *)mb + 1, mb, 1, 2559, 2560));
    release_test(mb);
    assert(xran_rx_snapshot_v1(&c, &snap, 2559, 2560) == 2);
    assert(xran_rx_packet_store(&c, 0, 12, 1, (uint8_t *)mb, mb + 1, 1, 2559, 2560) == -1);
    // Wrap and slot reuse cannot invalidate deferred IQ.
    assert(!xran_rx_packet_store(&c, 24, 12, 3, (uint8_t *)(mb + 1), mb + 1, 1, 0, 2560));
    assert(snap.nRBStart[0] == 0 && snap.nRBStart[1] == 12 && rte_mbuf_refcnt_read(mb) > 0);
    // Late callback must not consume or close the newer slot.
    assert(xran_rx_snapshot_v1(&c, &empty, 2559, 2560) == 0);
    assert(c.nRxPkt == 1);
    xran_rx_snapshot_release_v1(&snap);
    cleanup(&c);
    assert(rte_mbuf_refcnt_read(mb) == 0);
    for (int i = 1; i < 4; ++i) { release_test(mb + i); assert(rte_mbuf_refcnt_read(mb + i) == 0); }
    pthread_t p, q;
    assert(!pthread_create(&p, NULL, producer, NULL));
    assert(!pthread_create(&q, NULL, consumer, NULL));
    pthread_join(p, NULL); pthread_join(q, NULL);
    cleanup(&live);
    for (int i = 0; i < N; ++i) assert(rte_mbuf_refcnt_read(buffers + i) == 0);
    puts("xRAN retained RX, late arrival, wrap and concurrent handoff: PASS");
}
