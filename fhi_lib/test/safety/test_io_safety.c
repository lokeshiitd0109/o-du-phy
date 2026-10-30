/* SPDX-License-Identifier: Apache-2.0 */
#include "../../lib/src/xran_io_safety.h"
#include <assert.h>
#include <stdio.h>

struct rte_mbuf { int accepted, freed; };
static int calls, mode;
static uint16_t send_mock(uint16_t port, uint16_t queue, struct rte_mbuf **pkts, uint16_t count)
{
    assert(port == 3 && queue == 0 && count);
    ++calls;
    uint16_t n = mode == 0 ? 0 : mode == 1 ? count : mode == 2 ? 1 : calls < 3 ? 0 : count;
    for (int i = 0; i < n; ++i) {
        assert(!pkts[i]->accepted && !pkts[i]->freed);
        pkts[i]->accepted++;
    }
    return n;
}
static void release_mock(struct rte_mbuf *pkt)
{
    assert(!pkt->accepted && !pkt->freed);
    pkt->freed++;
}
static void test_tx(int behavior, int count, int accepted, int attempts)
{
    struct rte_mbuf data[16] = {0};
    struct rte_mbuf *packets[16];
    for (int i = 0; i < 16; ++i) packets[i] = &data[i];
    calls = 0; mode = behavior;
    assert(xran_tx_burst_bounded(3, 0, packets, count, send_mock, release_mock) == count - accepted);
    assert(calls == attempts);
    for (int i = 0; i < count; ++i) {
        assert(data[i].accepted == (i < accepted));
        assert(data[i].freed == (i >= accepted));
    }
}
int main(void)
{
    test_tx(0, 16, 0, XRAN_TX_MAX_ATTEMPTS); // permanently stalled NIC
    test_tx(1, 16, 16, 1);
    test_tx(2, 16, XRAN_TX_MAX_ATTEMPTS, XRAN_TX_MAX_ATTEMPTS); // bounded even with progress
    test_tx(3, 16, 16, 3); // temporary backpressure recovers
    test_tx(0, 0, 0, 0);
    struct rte_mbuf data[4] = {{1,0},{1,0},{0,0},{0,0}};
    struct rte_mbuf *pkts[] = {data,data+1,data+2,data+3};
    assert(xran_release_suffix(pkts, 2, 4, release_mock) == 2);
    assert(!data[0].freed && !data[1].freed && data[2].freed && data[3].freed);
    for (int mu = 0; mu <= 3; ++mu) {
        const int cycle = 256 * (10 << mu) * 14;
        assert(xran_symbol_delta(15, cycle - 2, cycle) == 17); // more than first slot after wrap
        assert(xran_symbol_delta(cycle - 2, 15, cycle) == -17);
        assert(xran_symbol_delta(cycle - 1, cycle - 3, cycle) == 2);
        assert(xran_rx_window_classify(xran_symbol_delta(3, cycle - 2, cycle), 4, 6) == 0);
    }
    assert(xran_rx_window_classify(3, 4, 6) == -1);
    assert(xran_rx_window_classify(4, 4, 6) == 0);
    assert(xran_rx_window_classify(6, 4, 6) == 0);
    assert(xran_rx_window_classify(7, 4, 6) == 1);
    assert(xran_rx_window_classify(-7, -6, -4) == -1); // RU early
    assert(xran_rx_window_classify(-5, -6, -4) == 0);
    assert(xran_rx_window_classify(-3, -6, -4) == 1); // RU late
    assert(xran_prach_symbol_offset(8, 9, 14) == -1);
    assert(xran_prach_symbol_offset(9, 9, 14) == 0);
    assert(xran_prach_symbol_offset(13, 9, 14) == 4);
    assert(xran_prach_symbol_offset(14, 9, 14) == -1);
    assert(xran_prach_symbol_offset(0, -1, 14) == -1);
    assert(xran_prach_symbol_offset(0, 14, 14) == -1);
    puts("xRAN timing and mbuf ownership: PASS");
}
