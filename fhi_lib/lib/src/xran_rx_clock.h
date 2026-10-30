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

#endif
