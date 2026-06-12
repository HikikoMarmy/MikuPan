#include "libcdvd.h"

#include <SDL3/SDL.h>

static u_char DecToBcd(u_char dec)
{
    return (u_char)(((dec / 10) << 4) | (dec % 10));
}

int sceCdInit(int init_mode)
{
}

int sceCdMmode(int media)
{
}

int sceCdReadClock(sceCdCLOCK* rtc)
{
    SDL_Time now;
    SDL_DateTime dt;
    int year;

    /*
        The PS2 RTC returns every sceCdCLOCK field as BCD (0x42 == 42),
        which is why the game decodes them with TimeIsMoney().

        The real RTC ticks in JST; we use the host's local time instead so
        the in-game clock matches the player's wall clock.
    */
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &dt, true))
    {
        rtc->stat = 0x80;
        return 0;
    }

    /* The RTC counts years from 2000 in a single BCD byte (2000-2099). */
    year = dt.year - 2000;
    if (year < 0)
        year = 0;
    else if (year > 99)
        year = 99;

    rtc->stat = 0;
    rtc->second = DecToBcd((u_char)dt.second);
    rtc->minute = DecToBcd((u_char)dt.minute);
    rtc->hour = DecToBcd((u_char)dt.hour);
    rtc->pad = 0;
    rtc->day = DecToBcd((u_char)dt.day);
    rtc->month = DecToBcd((u_char)dt.month);
    rtc->year = DecToBcd((u_char)year);

    return 1;
}

int sceCdStRead(u_int size, u_int* buf, u_int mode, u_int* err)
{
}

int sceCdStStop()
{
}

int sceCdDiskReady(int mode)
{
}

int sceCdStInit(u_int bufmax, u_int bankmax, u_int iop_bufaddr)
{
}

int sceCdSearchFile(sceCdlFILE* fp, const char* name)
{
}

int sceCdStStart(u_int lbn, sceCdRMode* mode)
{
}
