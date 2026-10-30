Standalone timing and ownership regression (no NIC/EAL/gtest needed)
==================================================================
From the repository root:
  cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    fhi_lib/test/safety/test_io_safety.c -o /tmp/xran-io-safety-test
  /tmp/xran-io-safety-test

Tests exercise the production inline helpers used by the packet paths:
permanent/transient NIC backpressure, partial progress, exact buffer ownership,
empty bursts, RX ring rejection, 256-frame wrap in both directions, inclusive
DU/RU receive windows and invalid PRACH symbol offsets. They do not measure
PMD latency or hardware deadlines. LeakSanitizer needs a non-ptrace environment.

The library exports xran_get_io_safety_stats(uint64_t *tx_unsent,
uint64_t *rx_ring_dropped), cumulative library-wide counters. The gNB's optional
weak-symbol hook prints them as [XRAN-IO-SAFETY]. Both output pointers may be
NULL. Existing public counter/context structure layouts are unchanged.
TX-unsent counts suffix packets dropped after at most eight NIC calls, not
packets accepted by the PMD or all possible TX-path drops. RX-ring-dropped
counts packets rejected by software RX-ring enqueue in the three active I/O
loops. No logging is added inside those packet loops.
