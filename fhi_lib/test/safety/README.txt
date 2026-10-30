Standalone safety regressions (no NIC/EAL/gtest needed)
=====================================================
From repository root:
  cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -g \
    fhi_lib/test/safety/test_io_safety.c -o /tmp/xran-io-test
  /tmp/xran-io-test
  cc -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -pthread \
    fhi_lib/test/safety/test_rx_clock.c -o /tmp/xran-clock-test
  /tmp/xran-clock-test
  cc -D_GNU_SOURCE -std=c11 -Wall -Wextra -fsanitize=undefined -g -pthread \
    $(pkg-config --cflags libdpdk) -Ifhi_lib/lib/api \
    fhi_lib/test/safety/test_rx_ownership.c -o /tmp/xran-rx-test \
    $(pkg-config --libs libdpdk)
  /tmp/xran-rx-test

Tests exercise production helpers: complete/partial/stalled TX, elapsed budget
and cycle wrap, exact accepted-prefix/rejected-suffix ownership, receive window
wrap/boundaries, PRACH offsets, coherent timing publication across second/SFN
wrap, per-section retained mbuf references, delayed consumers, late packet and
late callback handling, generation wrap, and concurrent RX handoff. RX test
uses real DPDK refcount/spinlock operations with a test final-release function
because its mbufs are not allocated from a mempool. No real NIC is used.

TX service budget is one quarter of the smallest configured symbol period or
positive T1a window span, shared conservatively across ports. At mu1 a 35.7 us
symbol with wider windows gives approximately 8.9 us. This replaces the eight
call limit and permits additional partial progress within the budget. It is
an engineering CPU-service bound, NOT an absolute per-packet wire deadline,
not a 3GPP-prescribed constant, and cannot interrupt a stuck PMD call.

Optional additive exported APIs (all counters cumulative, library-wide):
- xran_get_io_safety_stats(tx_unsent, rx_ring_dropped): unchanged API.
- xran_get_io_timing_stats(calls, partial, zero, exhausted, max_ns,
  unclassified, ring_rejected, alloc_failed): all output pointers must be non-NULL.
  exhausted counts batches; unsent counts their rejected packets. Ring
  rejections include generation descriptors as well as packet enqueues, so
  cannot be interpreted as an exact IQ-packet count.
- xran_get_rx_ownership_stats(late, mismatch): late-after-handoff/older
  generation store rejections, and nonempty mismatched callback snapshots.
- xran_rx_snapshot_v1(live, snapshot, tti, cycle), release_v1(snapshot): caller
  uses cycle=256*slots_per_frame and releases every queued snapshot including
  discarded FIFO entries. Control-block layout is unchanged; reserved now
  holds the library's generation/closed tag. The consumer must not reset the
  live count directly when using this API. Old applications still link but
  do not gain synchronized ownership. Rebuild the gNB fronthaul adapter too.
- xran_publish_rx_clock(mu, index, sfn): timer-owner publication only. Normal
  timer publishes internally; offload applications must rebuild the updated
  app_bbu_pool.c loop or publish equivalently. Until first publication RX
  timing is unclassified and not used to drop packets.

Generation ordering uses signed distance in the 256-frame wire cycle, as
required for short fronthaul intervals. Regular callbacks advance closed
slots even without traffic. This is not a general multi-second replay queue.
A missing/restarted timing source still needs operational recovery; timing
classification remains symbol-quantized, not a hardware arrival timestamp.

No per-packet logging or payload allocation is added under RX locks. Snapshot
buffers belong to the bounded gNB FIFO and hold metadata plus mbuf references,
not copies of IQ. Test matched gNB + xRAN binaries before field deployment.
