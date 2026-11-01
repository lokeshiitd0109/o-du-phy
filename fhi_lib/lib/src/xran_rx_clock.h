/* SPDX-License-Identifier: Apache-2.0 */
#ifndef XRAN_RX_CLOCK_H
#define XRAN_RX_CLOCK_H
#include <stdint.h>
static uint64_t rx_clock[XRAN_MAX_NUM_MU];
static uint64_t rx_clock_missing;
void xran_publish_rx_clock(uint8_t mu, uint32_t index, uint16_t sfn)
{
    if (mu < XRAN_MAX_NUM_MU)
        __atomic_store_n(&rx_clock[mu], (1ULL << 63) | ((uint64_t)sfn << 32) | index, __ATOMIC_RELEASE);
}
int xran_read_rx_clock(uint8_t mu, uint32_t *index, uint16_t *sfn)
{
    uint64_t v = mu < XRAN_MAX_NUM_MU ? __atomic_load_n(&rx_clock[mu], __ATOMIC_ACQUIRE) : 0;
    if (!(v >> 63)) {
        __atomic_fetch_add(&rx_clock_missing, 1, __ATOMIC_RELAXED);
        return 0;
    }
    *index = (uint32_t)v;
    *sfn = (uint16_t)(v >> 32);
    return 1;
}

/* Called once per dispatched timer boundary, not on each early poll. */
static uint64_t timer_boundaries, timer_late_symbols, timer_late_slots, timer_late_max_ns;
void xran_note_timer_lateness(int64_t late_ns, int64_t symbol_ns)
{
    if (symbol_ns <= 0) return;
    __atomic_fetch_add(&timer_boundaries, 1, __ATOMIC_RELAXED);
    if (late_ns <= 0) return;
    if (late_ns > 2 * symbol_ns)
        __atomic_fetch_add(&timer_late_symbols, 1, __ATOMIC_RELAXED);
    if (late_ns > 28 * symbol_ns)
        __atomic_fetch_add(&timer_late_slots, 1, __ATOMIC_RELAXED);
    uint64_t old = __atomic_load_n(&timer_late_max_ns, __ATOMIC_RELAXED);
    while (old < (uint64_t)late_ns && !__atomic_compare_exchange_n(&timer_late_max_ns,
            &old, (uint64_t)late_ns, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
void xran_get_timer_lateness_stats_v1(uint64_t *boundaries, uint64_t *late_symbols,
        uint64_t *late_slots, uint64_t *max_ns)
{
    *boundaries = __atomic_load_n(&timer_boundaries, __ATOMIC_RELAXED);
    *late_symbols = __atomic_load_n(&timer_late_symbols, __ATOMIC_RELAXED);
    *late_slots = __atomic_load_n(&timer_late_slots, __ATOMIC_RELAXED);
    *max_ns = __atomic_load_n(&timer_late_max_ns, __ATOMIC_RELAXED);
}
#endif
