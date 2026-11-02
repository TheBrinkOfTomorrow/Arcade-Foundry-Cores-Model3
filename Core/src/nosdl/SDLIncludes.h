/*
 * Stands in for Src/OSD/SDL/SDLIncludes.h in cores built without SDL: the
 * machine's sources use SDL only for debug message boxes, which go to the
 * log instead.
 */
#ifndef INCLUDED_SDLINCLUDES_H
#define INCLUDED_SDLINCLUDES_H

#include "OSD/Logger.h"

#define SDL_MESSAGEBOX_ERROR 0
#define SDL_ShowSimpleMessageBox(flags, title, message, window) (ErrorLog("%s: %s", (title), (message)), 0)

#endif
