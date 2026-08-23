/*
 *  tools/probe.c -- the Makefile's link check, and nothing else.
 *
 *  It is not part of st80 and is never linked into it.  `make' compiles
 *  this one file against the whole external surface the real build uses --
 *  pthreads, libm, and SDL3 when SDL3 is configured in -- before it
 *  compiles a single object of its own.  One invocation, about 40ms, and a
 *  missing package is named here rather than surfacing later as a linker
 *  error against a file that has nothing to do with it.
 *
 *  Every call below is here to force a symbol into the link.  sqrt() takes
 *  a volatile so the compiler cannot fold it away and leave libm
 *  unreferenced, which would let the probe pass on a machine that has no
 *  libm to find.
 */

#include <math.h>
#include <pthread.h>

#ifdef ST_HAVE_SDL3
#include <SDL3/SDL.h>
#endif

static void *
body(void *p)
{
    return p;
}

int
main(void)
{
    pthread_t       t;
    volatile double x = 4.0;

    if (pthread_create(&t, NULL, body, NULL) == 0)
        pthread_join(t, NULL);

#ifdef ST_HAVE_SDL3
    (void) SDL_GetVersion();
#endif

    return (int) sqrt(x) - 2;
}
