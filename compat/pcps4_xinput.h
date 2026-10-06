#ifndef PCPS4_XINPUT_H
#define PCPS4_XINPUT_H

#include <stdint.h>

#include "pcps4_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XUSER_MAX_COUNT 4u

#define ERROR_SUCCESS 0u
#define ERROR_DEVICE_NOT_CONNECTED 1167u

#define XINPUT_DEVTYPE_GAMEPAD 0x01u
#define XINPUT_DEVSUBTYPE_GAMEPAD 0x01u
#define XINPUT_CAPS_FFB_SUPPORTED 0x0001u

#define XINPUT_FLAG_GAMEPAD 0x00000001u

#define XINPUT_GAMEPAD_DPAD_UP        0x0001u
#define XINPUT_GAMEPAD_DPAD_DOWN      0x0002u
#define XINPUT_GAMEPAD_DPAD_LEFT      0x0004u
#define XINPUT_GAMEPAD_DPAD_RIGHT     0x0008u
#define XINPUT_GAMEPAD_START          0x0010u
#define XINPUT_GAMEPAD_BACK           0x0020u
#define XINPUT_GAMEPAD_LEFT_THUMB     0x0040u
#define XINPUT_GAMEPAD_RIGHT_THUMB    0x0080u
#define XINPUT_GAMEPAD_LEFT_SHOULDER  0x0100u
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200u
#define XINPUT_GAMEPAD_A              0x1000u
#define XINPUT_GAMEPAD_B              0x2000u
#define XINPUT_GAMEPAD_X              0x4000u
#define XINPUT_GAMEPAD_Y              0x8000u

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} XINPUT_GAMEPAD;

typedef struct {
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

typedef struct {
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION;

typedef struct {
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    XINPUT_GAMEPAD Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES;

DWORD pcps4_XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState);
DWORD pcps4_XInputSetState(DWORD dwUserIndex, const XINPUT_VIBRATION *pVibration);
DWORD pcps4_XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags,
                                  XINPUT_CAPABILITIES *pCapabilities);
void pcps4_XInputEnable(BOOL enable);

#ifdef __cplusplus
}
#endif

#endif
