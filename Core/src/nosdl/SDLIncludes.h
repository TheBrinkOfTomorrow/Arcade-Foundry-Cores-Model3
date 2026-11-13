/*
 * Stands in for Src/OSD/SDL/SDLIncludes.h in cores built without SDL: the
 * machine's sources use SDL for debug message boxes, which go to the log
 * instead, and the network code uses SDL_net's TCP calls, which
 * sdlnet_posix.cpp provides on POSIX sockets.
 */
#ifndef INCLUDED_SDLINCLUDES_H
#define INCLUDED_SDLINCLUDES_H

#include "OSD/Logger.h"
#include "sdlnet_posix.h"

#define SDL_MESSAGEBOX_ERROR 0
#define SDL_ShowSimpleMessageBox(flags, title, message, window) (ErrorLog("%s: %s", (title), (message)), 0)

#endif
