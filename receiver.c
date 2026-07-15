/* FEC RECEIVER — depth-2 interleaved XOR recovery, jitter buffer, playout timer.
 *
 * Ports (all 127.0.0.1):
 *   bind 47002  <- media from sender, via the hostile relay (169B wire packets)
 *   send 47020  -> harness player (164B: 4-byte BE seq + 160-byte payload)
 *
 * Wire format (matches sender):
 *   [0]     pkt_type       0x00=DATA, 0x01=FEC
 *   [1..2]  frame_seq      uint16 BE
 *   [3]     fec_group_size actual k
 *   [4]     fec_index      slot (DATA) or k (FEC)
 *   [5..168] payload       164 bytes
 *
 * Two threads:
 *   1. Ingress — recvfrom loop, fills jitter buffer, does FEC recovery
 *   2. Playout — timer-driven, delivers frames to harness player at deadline
 *
 * Env vars: T0 (epoch float), DURATION_S, DELAY_MS.
 * Harness kills this process with SIGKILL when the run ends.
 */

#define _GNU_SOURCE 

#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>


#define HARNESS_PKT_SZ 164
#define WIRE_HDR_SZ    5
#define WIRE_PKT_SZ    (WIRE_HDR_SZ + HARNESS_PKT_SZ)
#define FEC_K          3
#define INTERLEAVE_D   2
#define BLOCK_SZ       (FEC_K * INTERLEAVE_D)
#define FRAME_MS       20

#define PKT_DATA 0x00
#define PKT_FEC  0x01


typedef struct {
    atomic_int present;              
    uint8_t    payload[HARNESS_PKT_SZ];
} jitter_slot_t;

typedef struct {
    uint8_t data_present[FEC_K];    
    uint8_t has_parity;
    uint8_t data[FEC_K][HARNESS_PKT_SZ];
    uint8_t parity[HARNESS_PKT_SZ];
    uint8_t actual_k;                 
    uint8_t recovered;                
} fec_group_t;


static jitter_slot_t *jbuf;         
static int            n_frames;
static int            n_padded;       
static double         g_t0;
static double         g_delay_ms;


static uint64_t      *seen_bits;
static int            seen_words;     


static fec_group_t   *fec_groups;
static int            n_groups;



static inline void mark_seen(int seq) {
    seen_bits[seq / 64] |= (1ULL << (seq % 64));
}

static inline int is_seen(int seq) {
    return (int)((seen_bits[seq / 64] >> (seq % 64)) & 1);
}

static inline int group_id_of(int frame_seq) {
    return (frame_seq / BLOCK_SZ) * INTERLEAVE_D + (frame_seq % INTERLEAVE_D);
}

static inline int slot_of(int frame_seq) {
    return (frame_seq % BLOCK_SZ) / INTERLEAVE_D;
}


static inline int fec_group_member(int gid, int j) {
    int block = gid / INTERLEAVE_D;
    int cidx  = gid % INTERLEAVE_D;
    return block * BLOCK_SZ + cidx + j * INTERLEAVE_D;
}

static void try_fec_recovery(int gid) {
    fec_group_t *g = &fec_groups[gid];
    if (g->recovered || !g->has_parity)
        return;

    int k = g->actual_k ? g->actual_k : FEC_K;

    
    int missing = -1;
    int missing_count = 0;
    for (int j = 0; j < k; j++) {
        if (!g->data_present[j]) {
            missing = j;
            missing_count++;
        }
    }
    if (missing_count != 1)
        return;  

    
    uint8_t recovered[HARNESS_PKT_SZ];
    memcpy(recovered, g->parity, HARNESS_PKT_SZ);
    for (int j = 0; j < k; j++) {
        if (j == missing) continue;
        for (int b = 0; b < HARNESS_PKT_SZ; b++)
            recovered[b] ^= g->data[j][b];
    }

    
    int fseq = fec_group_member(gid, missing);
    if (fseq >= 0 && fseq < n_padded && !is_seen(fseq)) {
        memcpy(jbuf[fseq].payload, recovered, HARNESS_PKT_SZ);
        atomic_store_explicit(&jbuf[fseq].present, 1, memory_order_release);
        mark_seen(fseq);
    }

    g->recovered = 1;
}



static void *playout_thread(void *arg) {
    (void)arg;

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in player;
    memset(&player, 0, sizeof(player));
    player.sin_family      = AF_INET;
    player.sin_port        = htons(47020);
    player.sin_addr.s_addr = inet_addr("127.0.0.1");

    for (int i = 0; i < n_frames; i++) {
       
        double deadline = g_t0 + g_delay_ms / 1000.0
                        + (double)i * FRAME_MS / 1000.0;
        double target = deadline - 0.001;

        struct timespec ts;
        ts.tv_sec  = (time_t)target;
        ts.tv_nsec = (long)((target - (double)ts.tv_sec) * 1e9);
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        if (atomic_load_explicit(&jbuf[i].present, memory_order_acquire)) {
            sendto(out_fd, jbuf[i].payload, HARNESS_PKT_SZ, 0,
                   (struct sockaddr *)&player, sizeof(player));
        }
      
    }

    close(out_fd);
    return NULL;
}


int main(void)
{
    
    const char *t0_str    = getenv("T0");
    const char *dur_str   = getenv("DURATION_S");
    const char *delay_str = getenv("DELAY_MS");

    g_t0       = t0_str    ? atof(t0_str)    : 0.0;
    double duration_s = dur_str ? atof(dur_str) : 30.0;
    g_delay_ms = delay_str ? atof(delay_str) : 60.0;
    n_frames   = (int)(duration_s * 1000.0 / FRAME_MS);
    if (n_frames <= 0) n_frames = 1500;
    n_padded = n_frames + 16;

    
    jbuf = (jitter_slot_t *)calloc(n_padded, sizeof(jitter_slot_t));
    if (!jbuf) { perror("calloc jbuf"); return 1; }

    seen_words = (n_padded + 63) / 64;
    seen_bits = (uint64_t *)calloc(seen_words, sizeof(uint64_t));
    if (!seen_bits) { perror("calloc seen"); return 1; }

    n_groups = ((n_padded - 1) / BLOCK_SZ + 1) * INTERLEAVE_D + 4;
    fec_groups = (fec_group_t *)calloc(n_groups, sizeof(fec_group_t));
    if (!fec_groups) { perror("calloc fec"); return 1; }

    
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr;
    memset(&in_addr, 0, sizeof(in_addr));
    in_addr.sin_family      = AF_INET;
    in_addr.sin_port        = htons(47002);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0) {
        perror("receiver: bind 47002");
        return 1;
    }

    
    pthread_t pt;
    if (pthread_create(&pt, NULL, playout_thread, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    pthread_detach(pt);

    
    uint8_t buf[2048];
    for (;;) {
        ssize_t n = recvfrom(in_fd, buf, sizeof(buf), 0, NULL, NULL);
        if (n != WIRE_PKT_SZ) continue;

        uint8_t  pkt_type       = buf[0];
        uint16_t frame_seq      = ((uint16_t)buf[1] << 8) | buf[2];
        uint8_t  fec_group_size = buf[3];
        
        uint8_t *payload        = buf + WIRE_HDR_SZ;

        if (pkt_type == PKT_DATA) {
            
            if (frame_seq >= n_padded) continue;
            if (is_seen(frame_seq)) continue;

           
            memcpy(jbuf[frame_seq].payload, payload, HARNESS_PKT_SZ);
            atomic_store_explicit(&jbuf[frame_seq].present, 1,
                                  memory_order_release);
            mark_seen(frame_seq);

            
            int gid  = group_id_of(frame_seq);
            int slot = slot_of(frame_seq);
            if (gid >= 0 && gid < n_groups && slot < FEC_K) {
                fec_group_t *fg = &fec_groups[gid];
                if (!fg->data_present[slot]) {
                    memcpy(fg->data[slot], payload, HARNESS_PKT_SZ);
                    fg->data_present[slot] = 1;
                    try_fec_recovery(gid);
                }
            }

        } else if (pkt_type == PKT_FEC) {
            if (frame_seq >= n_padded) continue;
            int gid = group_id_of(frame_seq);
            if (gid >= 0 && gid < n_groups) {
                fec_group_t *fg = &fec_groups[gid];
                if (!fg->has_parity) {
                    memcpy(fg->parity, payload, HARNESS_PKT_SZ);
                    fg->has_parity = 1;
                    if (fec_group_size > 0 && fec_group_size <= FEC_K)
                        fg->actual_k = fec_group_size;
                    try_fec_recovery(gid);
                }
            }
        }
    }

    free(jbuf);
    free(seen_bits);
    free(fec_groups);
    return 0;
}
