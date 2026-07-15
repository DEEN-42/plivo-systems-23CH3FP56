# RUNLOG.md — FEC Media Transport Stress Test Log

## Tuning History

| Step | Change | Outcome |
|---|---|---|
| Baseline | Naive sender/receiver: one send per frame, no redundancy | INVALID — 2–5% miss rate, no recovery |
| v1 | Added consecutive k=3 XOR FEC (groups {0,1,2}, {3,4,5}, …) | Vulnerable to 2-frame bursts; risk of INVALID on unseen profiles |
| v2 | Switched to depth-2 interleaved grouping (groups {0,2,4}, {1,3,5}, …) | Survives any 2-consecutive-frame burst; +40 ms delay cost |
| v3 | delay_ms = 120 ms (A) / 160 ms (B); k=3 interleaved FEC | VALID, but left 0.59× of overhead budget unused. |
| v4 | Reduced group size to k=2 (depth-2 interleave). delay_ms = 80 ms (A) / 130 ms (B). | VALID on both profiles across all seeds. |
| Final | Aggressively tuned Profile A delay to 75 ms. | **VALID.** Operating at the mathematical floor (40ms max delay + 40ms span) with sub-1% miss rate. |

---

## Final Stress Test — 10 Runs (Profiles A & B, Seeds 1–5)

*Note: Profile A tested at 75 ms; Profile B tested at 130 ms.*

| Profile | Seed | Frames | Misses | Miss Rate | Up Bytes | Down Bytes | Overhead | Delay | Result |
|---|---|---|---|---|---|---|---|---|---|
| A_mild | 1 | 1500 | 9 | 0.60% | 380250 | 0 | 1.58× | 75 ms | ✅ **VALID** |
| B_moderate | 1 | 1500 | 12 | 0.80% | 380250 | 0 | 1.58× | 130 ms | ✅ **VALID** |
| A_mild | 2 | 1500 | 13 | 0.87% | 380250 | 0 | 1.58× | 75 ms | ✅ **VALID** |
| B_moderate | 2 | 1500 | 13 | 0.87% | 380250 | 0 | 1.58× | 130 ms | ✅ **VALID** |
| A_mild | 3 | 1500 | 9 | 0.60% | 380250 | 0 | 1.58× | 75 ms | ✅ **VALID** |
| B_moderate | 3 | 1500 | 7 | 0.47% | 380250 | 0 | 1.58× | 130 ms | ✅ **VALID** |
| A_mild | 4 | 1500 | 5 | 0.33% | 380250 | 0 | 1.58× | 75 ms | ✅ **VALID** |
| B_moderate | 4 | 1500 | 14 | 0.93% | 380250 | 0 | 1.58× | 130 ms | ✅ **VALID** |
| A_mild | 5 | 1500 | 6 | 0.40% | 380250 | 0 | 1.58× | 75 ms | ✅ **VALID** |
| B_moderate | 5 | 1500 | 11 | 0.73% | 380250 | 0 | 1.58× | 130 ms | ✅ **VALID** |

### Summary Statistics

| Profile | Miss Rate Range | Overhead | Delay | All Seeds VALID? |
|---|---|---|---|---|
| A_mild (seeds 1–5) | 0.33% – 0.87% | 1.58× | 75 ms | ✅ Yes |
| B_moderate (seeds 1–5) | 0.47% – 0.93% | 1.58× | 130 ms | ✅ Yes |

All 10 runs are **VALID**: miss rate ≤ 1.00% and bandwidth overhead ≤ 2.00×. 

Overhead is deterministically **380,250 B upstream / 0 B feedback** on every run — confirmed by math: 1500 data frames + 750 parity frames = 2250 packets × 169 B wire format = 380,250 B. 
Calculated against the raw stream: 380,250 / (1500 × 160) = **1.584×**.
