/* SPDX-License-Identifier: Apache-2.0 */
#ifndef XRAN_RX_OWNERSHIP_H
#define XRAN_RX_OWNERSHIP_H
/* Striped locks leave the public control-block ABI unchanged. Only metadata
 * publication and mbuf refcounts run under a lock; no log, allocation or IQ
 * processing. Consumers must use the snapshot API to join this contract. */
#define RX_LOCKS 4096
static uint64_t rx_late_handoff, rx_generation_mismatch;
void xran_get_rx_ownership_stats(uint64_t *late, uint64_t *mismatch)
{
    *late = __atomic_load_n(&rx_late_handoff, __ATOMIC_RELAXED);
    *mismatch = __atomic_load_n(&rx_generation_mismatch, __ATOMIC_RELAXED);
}
static rte_spinlock_t rx_locks[RX_LOCKS];
static rte_spinlock_t *rx_lock(const struct xran_rx_packet_ctl *ctl)
{
    return &rx_locks[((uintptr_t)ctl >> 6) % RX_LOCKS];
}
int xran_rx_packet_store(struct xran_rx_packet_ctl *ctl, uint16_t start,
    uint16_t size, uint16_t section, uint8_t *data, struct rte_mbuf *mb, int retain, uint32_t tti, uint32_t cycle)
{
    if (!ctl || !mb || !cycle) return -1;
    rte_spinlock_t *lock = rx_lock(ctl);
    rte_spinlock_lock(lock);
    tti %= cycle;
    uint32_t tag = (uint32_t)ctl->reserved;
    uint32_t previous = tag & 0x7fffffffU;
    if (previous && ((previous == tti + 1 && (tag >> 31)) ||
        (previous != tti + 1 && xran_symbol_delta(tti, previous - 1, cycle) <= 0))) {
        rte_spinlock_unlock(lock);
        __atomic_fetch_add(&rx_late_handoff, 1, __ATOMIC_RELAXED);
        return -1; /* late arrival after handoff, or older ring generation */
    }
    if (previous != tti + 1) ctl->nRxPkt = 0;
    ctl->reserved = tti + 1;
    int n = ctl->nRxPkt;
    if (n < 0 || n >= XRAN_MAX_RX_PKT_PER_SYM) {
        rte_spinlock_unlock(lock);
        return -1;
    }
    if (retain) rte_pktmbuf_refcnt_update(mb, 1);
    struct rte_mbuf *old = ctl->pCtrl[n];
    ctl->nRBStart[n] = start;
    ctl->nRBSize[n] = size;
    ctl->nSectid[n] = section;
    ctl->pData[n] = data;
    ctl->pCtrl[n] = mb;
    ctl->nRxPkt = n + 1;
    rte_spinlock_unlock(lock);
    if (old) rte_pktmbuf_free(old);
    return 0;
}
int xran_rx_snapshot_v1(struct xran_rx_packet_ctl *live, struct xran_rx_packet_ctl *snapshot, uint32_t tti, uint32_t cycle)
{
    if (!live || !snapshot || !cycle) return -1;
    rte_spinlock_t *lock = rx_lock(live);
    rte_spinlock_lock(lock);
    *snapshot = *live;
    tti %= cycle;
    uint32_t previous = (uint32_t)live->reserved & 0x7fffffffU;
    int n = previous == tti + 1 ? snapshot->nRxPkt : 0;
    if (previous != tti + 1 && snapshot->nRxPkt > 0)
        __atomic_fetch_add(&rx_generation_mismatch, 1, __ATOMIC_RELAXED);
    if (n < 0 || n > XRAN_MAX_RX_PKT_PER_SYM) n = 0;
    snapshot->nRxPkt = n;
    for (int i = 0; i < n; ++i)
        if (snapshot->pCtrl[i]) rte_pktmbuf_refcnt_update(snapshot->pCtrl[i], 1);
    /* Do not overwrite a newer generation if a consumer callback is late. */
    if (!previous || previous == tti + 1 || xran_symbol_delta(tti, previous - 1, cycle) > 0) {
        live->nRxPkt = 0;
        live->reserved = (tti + 1) | 0x80000000U;
    }
    rte_spinlock_unlock(lock);
    return n;
}
void xran_rx_snapshot_release_v1(struct xran_rx_packet_ctl *snapshot)
{
    for (int i = 0; i < snapshot->nRxPkt; ++i)
        if (snapshot->pCtrl[i]) rte_pktmbuf_free(snapshot->pCtrl[i]);
    snapshot->nRxPkt = 0;
}

#endif
