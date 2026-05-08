#ifndef MIKUPAN_MIKUPAN_CONTROLLER_H
#define MIKUPAN_MIKUPAN_CONTROLLER_H

#include <SDL3/SDL_gamepad.h>

#define MIKUPAN_CONTROLLER_BIND_NONE   (0)
#define MIKUPAN_CONTROLLER_BIND_BUTTON (1)
#define MIKUPAN_CONTROLLER_BIND_AXIS   (2)

typedef struct
{
    /// MIKUPAN_CONTROLLER_BIND_NONE / MIKUPAN_CONTROLLER_BIND_BUTTON / MIKUPAN_CONTROLLER_BIND_AXIS
    int kind;

    /// SDL_GamepadButton or SDL_GamepadAxis (depending on kind)
    int code;
} MikuPan_ControllerBindings;

#define MIKUPAN_CONTROLLER_LOGICAL_COUNT (16)

extern MikuPan_ControllerBindings mikupan_controller_map[MIKUPAN_CONTROLLER_LOGICAL_COUNT];
extern int          mikupan_keyboard_map[MIKUPAN_CONTROLLER_LOGICAL_COUNT];

int MikuPan_OpenController();
int MikuPan_ReadController(unsigned char* rdata);
void MikuPan_ControllerResetBindings(void);
const char *MikuPan_ControllerBindingLabel(MikuPan_ControllerBindings binding);
const char *MikuPan_ControllerScanCodeLabel(int scancode);
SDL_Gamepad* MikuPan_GetController(void);
int MikuPan_ControllerRumble(const unsigned char* data);
void MikuPan_ControllerDrawRemapWindow(void);


#endif//MIKUPAN_MIKUPAN_CONTROLLER_H
