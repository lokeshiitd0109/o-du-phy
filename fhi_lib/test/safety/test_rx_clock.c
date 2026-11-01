/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#define XRAN_MAX_NUM_MU 5
#include "../../lib/src/xran_rx_clock.h"
static _Atomic int done;
static void *writer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100000; ++i) {
        xran_publish_rx_clock(1, 27999, 900);
        xran_publish_rx_clock(1, 0, 1000);
    }
    atomic_store(&done, 1);
    return NULL;
}
int main(void)
{
    uint64_t boundaries, symbols, slots, worst;
    xran_note_timer_lateness(-1000000, 100); // early is not late
    xran_note_timer_lateness(0, 100);
    xran_note_timer_lateness(200, 100); // exact threshold is not exceeded
    xran_note_timer_lateness(201, 100);
    xran_note_timer_lateness(2800, 100);
    xran_note_timer_lateness(2801, 100);
    xran_note_timer_lateness(10000, 0); // invalid interval ignored
    xran_get_timer_lateness_stats_v1(&boundaries, &symbols, &slots, &worst);
    assert(boundaries == 6 && symbols == 3 && slots == 1 && worst == 2801);
    uint32_t index; uint16_t sfn;
    assert(!xran_read_rx_clock(1, &index, &sfn));
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, writer, NULL));
    while (!atomic_load(&done)) {
        if (xran_read_rx_clock(1, &index, &sfn))
            assert((index == 27999 && sfn == 900) || (index == 0 && sfn == 1000));
    }
    pthread_join(thread, NULL);
    xran_publish_rx_clock(1, 0, 76); // SFN wrap at the next second
    assert(xran_read_rx_clock(1, &index, &sfn) && index == 0 && sfn == 76);
    assert(!xran_read_rx_clock(5, &index, &sfn));
    puts("xRAN coherent clock, second/SFN rollover, uninitialised clock: PASS");
}
