/*
 * Copyright © 2025, ArchAdept Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * An example program demonstrating how the value of a byte in memory may be inferred
 * using cache timing analysis.
 *
 * Usage:
 *
 *     clang -O2 inferb.c -o inferb && ./inferb
 *
 * See also: the tunable ``VALUE``, ``SPACING``, ``ITERATIONS``, and ``RETRIES_IF_ZERO``
 * #define's below.
 */

/* The value to place in memory. */
#define VALUE (42)

/*
 * How far apart we want to space each cache line in the probe array; bumping this up to
 * a higher number helps to prevent the CPU from prefetching the next cache line when
 * we're iterating through the probe array timing each cache line access, but comes at
 * the cost of a slower runtime.
 */
#define SPACING (16 * 1024)

/*
 * How many times to probe each cache line; bumping this up to a higher number
 * significantly improves accuracy but comes at the cost of a significantly slower
 * runtime.
 */
#define ITERATIONS (20)

/*
 * As discussed in the original Meltdown paper, there is an inherent bias towards CPUs
 * using value zero as a placeholder value for the byte in memory that we are trying to
 * infer. With that in mind, if we observe value zero then we will retry `inferb()` up
 * to this many times, just to make sure the byte in memory really is value zero.
 */
#define RETRIES_IF_ZERO (50)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <float.h>
#include <mach/mach_time.h>

/*
 * Size of a single cache line. This is pretty much always 64 bytes on arm64-based
 * platforms - including Apple Silicon - though a more robust approach would be to
 * calculate this from ``CTR_EL0.Dminline``.
 */
#define CACHE_LINE_SIZE (64)

/* Struct representing a single cache line in the probe array. */
typedef struct cache_line {
    uint8_t cacheLine;             // First byte of the cache line.
    uint8_t padding[SPACING - 1];  // Padding to the next cache line.
} cache_line_t;

/*
 * How many cache lines to allocate for the probe array; we need least 256 to account
 * for all possible values that an 8-bit byte can take.
 */
#define NUM_CACHE_LINES (UINT8_MAX + 1)

/* Size of the probe array in bytes. */
#define ARRAY_SIZE (NUM_CACHE_LINES * sizeof(cache_line_t))

/*
 * Flushes the entire region enclosed by [addr, addr+len) from the data cache.
 *
 * Note: It is the caller's responsibility to insert any necessary barriers before/after
 *       calling this function.
 */
static void flush_cache_relaxed(void *start, size_t len) {
    uint64_t addr = (uint64_t)start;
    uint64_t limit = addr + len;

    while (addr < limit) {
        asm volatile ("DC CIVAC, %0" :: "r"(addr) : "memory");
        addr += CACHE_LINE_SIZE;
    }
}

/* Struct representing a high resolution timer. */
typedef struct timer {
    uint64_t start;  // Mach Absolute Time units (MATs) since was timer started.
    double rate;     // Nanoseconds per MAT on this device.
} timer_t;

/* Start a new high resolution timer. */
timer_t startTimer(void)
{
    mach_timebase_info_data_t tbi;
    mach_timebase_info(&tbi);

    /*
     * Prevent the timer from being started until everything else before this point in
     * program order has fully completed; note the memory clobber also prevents the
     * compiler from reordering things across this point, too.
     */
    asm volatile ("DSB ISH\nISB" ::: "memory");

    return (timer_t) {
        .start = mach_absolute_time(),
        .rate = (double)tbi.numer / tbi.denom,
    };
}

/*
 * Get the number of nanoseconds that have elapsed between the given high resolution
 * timer being started and now.
 */
double timeElapsed(const timer_t *t)
{
    /*
     * Ensure everything else before this point in program order has fully completed
     * before we sample the current time. As in ``startTimer()``, the memory clobber
     * here also prevents the compiler from reordering things across this point, too.
     */
    asm volatile ("DSB ISH\nISB" ::: "memory");

    return (mach_absolute_time() - t->start) * t->rate;
}

/* Infer the value of a byte in memory using cache timing analysis. */
uint8_t inferb(uint8_t *addr)
{
    /* Cumulative time taken to access each cache line across all iterations. */
    double *sum = (double *) calloc(NUM_CACHE_LINES, sizeof(double));

    /* Index of which cache line was the quickest to access. */
    uint8_t bestIndex;

    for (unsigned try = 0; try < RETRIES_IF_ZERO; try++)
    {
        for (unsigned iter = 0; iter < ITERATIONS; iter++)
        {
            /* Allocate the probe array and flush it out of the data cache. */
            volatile cache_line_t *const array =
                (volatile cache_line_t *const) malloc(ARRAY_SIZE);
            flush_cache_relaxed((void *const)array, ARRAY_SIZE);

            /*
             * Cause an allocation back into the cache based on the value of the byte in
             * memory.
             */
            array[*addr].cacheLine;

            /* Time how long it takes to access each cache line. */
            for (unsigned i = 0; i < NUM_CACHE_LINES; i++) {
                const timer_t start = startTimer();
                array[i].cacheLine;
                sum[i] += timeElapsed(&start);
            }
        }

        /*
         * Find which cache line was the quickest to access, averaged across all
         * iterations.
         */
        double best = DBL_MAX;
        for (unsigned i = 0; i < NUM_CACHE_LINES; i++) {
            if (sum[i] < best) {
                bestIndex = i;
                best = sum[i];
            }
        }

        /*
         * If the inferred value was non-zero then we're done; otherwise, try again up
         * to ``RETRIES_IF_ZERO`` times.
         */
        if (bestIndex != 0) {
            break;
        }
    }

    /* The index of the quickest cache line is the value of the byte in memory! */
    return bestIndex;
}

int main()
{
    uint8_t theByte = VALUE;

    const timer_t start = startTimer();
    const uint8_t inferredValue = inferb(&theByte);
    const double timeTaken = timeElapsed(&start);

    const char *const result = (inferredValue == theByte) ? "✅" : "❌";
    const double secs = timeTaken / (1000 * 1000 * 1000);

    printf("%s Inferred value >>> %3u <<< in %03.04f seconds (spacing=0x%x, %u "
           "iterations, %u retries)\n", result, inferredValue, secs, SPACING,
           ITERATIONS, RETRIES_IF_ZERO);
}

