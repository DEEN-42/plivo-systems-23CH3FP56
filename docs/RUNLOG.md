# RUNLOG.md — FEC Media Transport Stress Test Log

## Tuning History

| Step | Change | Outcome |
|---|---|---|
| Baseline | Naive sender/receiver: one send per frame, no redundancy | INVALID — 2–5% miss rate, no recovery |
| v1 | Added consecutive k=3 XOR FEC (groups {0,1,2}, {3,4,5}, …) | Vulnerable to 2-frame bursts; risk of INVALID on unseen profiles |
| v2 | Switched to depth-2 interleaved grouping (groups {0,2,4}, {1,3,5}, …) | Survives any 2-consecutive-frame burst; +40 ms delay cost |
| v2 | Starting delay_ms = 80 ms (A) / 120 ms (B) | Needed +40 ms margin for interleave group span |
| Final | delay_ms = 120 ms (A) / 160 ms (B); k=3 interleaved FEC | **VALID on both profiles across all seeds** |

---

## Final Stress Test — 10 Runs (Profiles A & B, Seeds 1–5)

| Profile | Seed | Frames | Dropped | Duplicated | Misses | Miss Rate | Up Bytes | Down Bytes | Overhead | Delay | Result |
|---|---|---|---|---|---|---|---|---|---|---|---|
| A_mild | 1 | 1500 | 45 | 14 | 2 | 0.13% | 338000 | 0 | 1.41× | 120 ms | ✅ **VALID** |
| B_moderate | 1 | 1500 | 113 | 23 | 12 | 0.80% | 338000 | 0 | 1.41× | 160 ms | ✅ **VALID** |
| A_mild | 2 | 1500 | 41 | 12 | 1 | 0.07% | 338000 | 0 | 1.41× | 120 ms | ✅ **VALID** |
| B_moderate | 2 | 1500 | 107 | 23 | 13 | 0.87% | 338000 | 0 | 1.41× | 160 ms | ✅ **VALID** |
| A_mild | 3 | 1500 | 48 | 8 | 3 | 0.20% | 338000 | 0 | 1.41× | 120 ms | ✅ **VALID** |
| B_moderate | 3 | 1500 | 107 | 16 | 14 | 0.93% | 338000 | 0 | 1.41× | 160 ms | ✅ **VALID** |
| A_mild | 4 | 1500 | 28 | 6 | 3 | 0.20% | 338000 | 0 | 1.41× | 120 ms | ✅ **VALID** |
| B_moderate | 4 | 1500 | 79 | 17 | 9 | 0.60% | 338000 | 0 | 1.41× | 160 ms | ✅ **VALID** |
| A_mild | 5 | 1500 | 24 | 11 | 1 | 0.07% | 338000 | 0 | 1.41× | 120 ms | ✅ **VALID** |
| B_moderate | 5 | 1500 | 95 | 17 | 11 | 0.73% | 338000 | 0 | 1.41× | 160 ms | ✅ **VALID** |

### Summary Statistics

| Profile | Miss Rate Range | Overhead | Delay | All Seeds VALID? |
|---|---|---|---|---|
| A_mild (seeds 1–5) | 0.07% – 0.20% | 1.41× | 120 ms | ✅ Yes |
| B_moderate (seeds 1–5) | 0.60% – 0.93% | 1.41× | 160 ms | ✅ Yes |

All 10 runs are **VALID**: miss rate < 1.00% and bandwidth overhead < 2.00×.
Overhead is deterministically **338,000 B upstream / 0 B feedback** on every run — confirmed by math: 1500 data + 500 parity = 2000 packets × 169 B = 338,000 B; 338,000 / (1500 × 160) = **1.4083×**.
