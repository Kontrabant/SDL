/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "SDL_internal.h"

#include "SDL_poll.h"

#include <poll.h>
#include <errno.h>

int SDL_IOReady(int *fd, int fd_cnt, int flags, Sint64 timeoutNS)
{
    int result;

    SDL_assert(flags & (SDL_IOR_READ | SDL_IOR_WRITE));

    /* Note: We don't bother to account for elapsed time if we get EINTR */
    do {
        struct pollfd *info = SDL_stack_alloc(struct pollfd, fd_cnt);

        for (int i = 0; i < fd_cnt; ++i) {
            info[i].fd = fd[i];
            info[i].events = 0;
            if (flags & SDL_IOR_READ) {
                info[i].events |= POLLIN | POLLPRI;
            }
            if (flags & SDL_IOR_WRITE) {
                info[i].events |= POLLOUT;
            }
        }

#ifdef HAVE_PPOLL
        struct timespec ts;

        if (timeoutNS > 0) {
            ts.tv_sec = timeoutNS / SDL_NS_PER_SECOND;
            ts.tv_nsec = timeoutNS % SDL_NS_PER_SECOND;
        } else if (timeoutNS == 0) {
            SDL_zero(ts);
        }

        result = ppoll(info, fd_cnt, timeoutNS < 0 ? NULL : &ts, NULL);
#else
        int timeoutMS;

        if (timeoutNS > 0) {
            timeoutMS = (int)SDL_NS_TO_MS(timeoutNS);
        } else if (timeoutNS == 0) {
            timeoutMS = 0;
        } else {
            timeoutMS = -1;
        }
        result = poll(info, fd_cnt, timeoutMS);
#endif

        SDL_stack_free(info);
    } while (result < 0 && errno == EINTR && !(flags & SDL_IOR_NO_RETRY));

    return result;
}
