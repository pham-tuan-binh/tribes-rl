// Random-play throughput benchmark on procedurally generated 11x11 maps.
// Build/run: make bench
#include <stdio.h>
#include <time.h>
#include "../mapgen.h"

int main(void) {
    const int8_t tribes[2] = {TRIBE_XIN_XI, TRIBE_OUMAJI};
    PolyState s;
    PolyAction acts[POLY_MAX_ACTIONS];

    long total_steps = 0, games = 0;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (uint32_t seed = 1; seed <= 200; seed++) {
        poly_reset(&s, 2, tribes, 11, MODE_CAPITALS, seed);
        while (!s.game_over) {
            int n = poly_enumerate_actions(&s, acts, POLY_MAX_ACTIONS);
            PolyAction* a = &acts[poly_rand_int(&s.rng, n)];
            if (poly_rand_int(&s.rng, 8) == 0) {
                for (int i = 0; i < n; i++)
                    if (acts[i].kind == ACT_END_TURN) { a = &acts[i]; break; }
            }
            poly_step(&s, a);
            total_steps++;
        }
        games++;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("%ld games, %ld steps in %.3fs — %.0f steps/sec (with full enumeration each step)\n",
           games, total_steps, secs, total_steps / secs);
    return 0;
}
