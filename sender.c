/* FEC SENDER — depth-2 interleaved XOR parity, k=3.
 *
 * Ports (all 127.0.0.1):
 *   bind 47010  <- harness source delivers frame i at t0 + i*20ms
 *                  (format: 4-byte big-endian seq + 160-byte payload = 164B)
 *   send 47001  -> relay uplink toward the receiver
 *
 * Wire format (our custom protocol, 169 bytes):
 *   [0]     pkt_type:       0x00 = DATA, 0x01 = FEC parity
 *   [1..2]  frame_seq:      uint16 BE (DATA: frame#; FEC: first member's seq)
 *   [3]     fec_group_size: actual k for this group (normally 3, less for trailing)
 *   [4]     fec_index:      slot within group (DATA: 0..k-1; FEC: k)
 *   [5..168] payload:       164 bytes (DATA: original harness pkt; FEC: XOR of all k)
 *
 * Interleaving: D=2 concurrent groups. Frame i goes to group (i % D).
 *   Group members are spaced 2 frames apart: {0,2,4}, {1,3,5}, {6,8,10}, ...
 *   This survives any burst of <= 2 consecutive losses.
 *
 * Env vars: T0, DURATION_S, DELAY_MS (only DURATION_S is used here).
 * The harness kills this process with SIGKILL when the run ends.
 */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define HARNESS_PKT_SZ 164   
#define WIRE_HDR_SZ    5
#define WIRE_PKT_SZ    (WIRE_HDR_SZ + HARNESS_PKT_SZ)  
#define FEC_K          3    
#define INTERLEAVE_D   2   
#define FRAME_MS       20

#define PKT_DATA 0x00
#define PKT_FEC  0x01

typedef struct {
    uint8_t  xor_buf[HARNESS_PKT_SZ]; 
    uint16_t first_seq;              
    int      count;                 
} fec_accum_t;

static inline void build_header(uint8_t *out, uint8_t type, uint16_t seq,
                                uint8_t group_sz, uint8_t idx)
{
    out[0] = type;
    out[1] = (uint8_t)(seq >> 8);
    out[2] = (uint8_t)(seq & 0xFF);
    out[3] = group_sz;
    out[4] = idx;
}

static void send_parity(int fd, const struct sockaddr *dest, socklen_t dlen,
                        fec_accum_t *g)
{
    uint8_t pkt[WIRE_PKT_SZ];
    uint8_t actual_k = (uint8_t)g->count;
    build_header(pkt, PKT_FEC, g->first_seq, actual_k, actual_k);
    memcpy(pkt + WIRE_HDR_SZ, g->xor_buf, HARNESS_PKT_SZ);
    sendto(fd, pkt, WIRE_PKT_SZ, 0, dest, dlen);

    g->count = 0;
    memset(g->xor_buf, 0, HARNESS_PKT_SZ);
}

int main(void)
{
   
    const char *dur_str = getenv("DURATION_S");
    double duration_s = dur_str ? atof(dur_str) : 30.0;
    int n_frames = (int)(duration_s * 1000.0 / FRAME_MS);
    if (n_frames <= 0) n_frames = 1500;

    
    int in_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in in_addr;
    memset(&in_addr, 0, sizeof(in_addr));
    in_addr.sin_family      = AF_INET;
    in_addr.sin_port        = htons(47010);
    in_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(in_fd, (struct sockaddr *)&in_addr, sizeof(in_addr)) < 0) {
        perror("sender: bind 47010");
        return 1;
    }

    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in relay;
    memset(&relay, 0, sizeof(relay));
    relay.sin_family      = AF_INET;
    relay.sin_port        = htons(47001);
    relay.sin_addr.s_addr = inet_addr("127.0.0.1");

    
    fec_accum_t groups[INTERLEAVE_D];
    memset(groups, 0, sizeof(groups));

    uint8_t wire_pkt[WIRE_PKT_SZ];

    
    for (;;) {
        uint8_t harness_buf[2048];
        ssize_t n = recvfrom(in_fd, harness_buf, sizeof(harness_buf),
                             0, NULL, NULL);
        if (n != HARNESS_PKT_SZ) continue;

        
        uint32_t seq32 = ((uint32_t)harness_buf[0] << 24) |
                         ((uint32_t)harness_buf[1] << 16) |
                         ((uint32_t)harness_buf[2] <<  8) |
                         ((uint32_t)harness_buf[3]);
        uint16_t frame_seq = (uint16_t)seq32;

        
        int cidx = frame_seq % INTERLEAVE_D;
        fec_accum_t *g = &groups[cidx];
        int slot = g->count;  

        
        if (slot == 0) {
            g->first_seq = frame_seq;
            memset(g->xor_buf, 0, HARNESS_PKT_SZ);
        }

        
        for (int j = 0; j < HARNESS_PKT_SZ; j++)
            g->xor_buf[j] ^= harness_buf[j];
        g->count++;

        
        build_header(wire_pkt, PKT_DATA, frame_seq,
                     (uint8_t)FEC_K, (uint8_t)slot);
        memcpy(wire_pkt + WIRE_HDR_SZ, harness_buf, HARNESS_PKT_SZ);
        sendto(out_fd, wire_pkt, WIRE_PKT_SZ, 0,
               (struct sockaddr *)&relay, sizeof(relay));

        
        if (g->count == FEC_K) {
            send_parity(out_fd, (struct sockaddr *)&relay,
                        sizeof(relay), g);
        }

        
        if ((int)frame_seq == n_frames - 1) {
            for (int i = 0; i < INTERLEAVE_D; i++) {
                if (groups[i].count > 0) {
                    send_parity(out_fd, (struct sockaddr *)&relay,
                                sizeof(relay), &groups[i]);
                }
            }
            
        }
    }

    return 0;
}
