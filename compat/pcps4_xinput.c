#include "pcps4_xinput.h"
#include "pcps4_status.h"

#include <string.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>

static int g_xinput_diag_done = 0;

static int32_t g_pad_handles[XUSER_MAX_COUNT] = { -1, -1, -1, -1 };
static int g_initialized = 0;
static int32_t g_user_id = -1;

static void pcps4_xinput_init(void)
{
    if (g_initialized != 0)
        return;

    /*
     * OpenOrbis's controller sample initializes both libraries before
     * scePadOpen(). The return value from scePadInit is deliberately ignored:
     * another subsystem may already have initialized Pad.
     */
    (void)scePadInit();

    OrbisUserServiceInitializeParams params;
    memset(&params, 0, sizeof(params));
    params.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    (void)sceUserServiceInitialize(&params);

    if (sceUserServiceGetInitialUser(&g_user_id) != 0) {
        g_initialized = -1;
        return;
    }

    g_initialized = 1;
}

static int pcps4_xinput_open(DWORD index)
{
    if (index >= XUSER_MAX_COUNT)
        return ERROR_DEVICE_NOT_CONNECTED;

    pcps4_xinput_init();

    if (g_initialized < 0)
        return ERROR_DEVICE_NOT_CONNECTED;

    if (g_pad_handles[index] >= 0)
        return ERROR_SUCCESS;

    int32_t handle = scePadOpen(
        g_user_id,
        ORBIS_PAD_PORT_TYPE_STANDARD,
        (int32_t)index,
        NULL);

    if (handle < 0)
        return ERROR_DEVICE_NOT_CONNECTED;

    g_pad_handles[index] = handle;
    return ERROR_SUCCESS;
}

static void pcps4_xinput_close(DWORD index)
{
    if (index >= XUSER_MAX_COUNT)
        return;

    if (g_pad_handles[index] >= 0) {
        (void)scePadClose(g_pad_handles[index]);
        g_pad_handles[index] = -1;
    }
}

static int pcps4_xinput_read(
    DWORD index,
    OrbisPadData *pad_data)
{
    if (pad_data == NULL ||
        index >= XUSER_MAX_COUNT)
        return ERROR_DEVICE_NOT_CONNECTED;

    if (pcps4_xinput_open(index) != ERROR_SUCCESS)
        return ERROR_DEVICE_NOT_CONNECTED;

    memset(pad_data, 0, sizeof(*pad_data));

    /* scePadReadState returns 0 on success and fills the CURRENT pad state
     * (XInput semantics). The old scePadRead returns a sample COUNT (>=1),
     * so the previous `rc != 0` check rejected every good read as
     * "disconnected" — the real cause of ERROR_DEVICE_NOT_CONNECTED. */
    int32_t rc = scePadReadState(g_pad_handles[index], pad_data);

    if (!g_xinput_diag_done) {
        g_xinput_diag_done = 1;
        pcps4_status_log("[xinput] diag: user=0x%x handle=%d readstate_rc=%d connected=%d buttons=0x%x lx=%d ly=%d",
                         (unsigned)g_user_id, g_pad_handles[index], (int)rc,
                         (int)pad_data->connected, (unsigned)pad_data->buttons,
                         (int)pad_data->leftStick.x, (int)pad_data->leftStick.y);
    }

    if (rc != 0 || pad_data->connected == 0) {
        pcps4_xinput_close(index);
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    return ERROR_SUCCESS;
}

static SHORT pcps4_xinput_axis(uint8_t value)
{
    int32_t mapped = ((int32_t)value * 65535) / 255 - 32768;

    if (mapped < -32768)
        mapped = -32768;
    if (mapped > 32767)
        mapped = 32767;

    return (SHORT)mapped;
}

static uint8_t pcps4_xinput_trigger(uint8_t value)
{
    return value;
}

static void pcps4_xinput_map(
    const OrbisPadData *pad,
    XINPUT_GAMEPAD *gamepad)
{
    memset(gamepad, 0, sizeof(*gamepad));

    if ((pad->buttons & ORBIS_PAD_BUTTON_UP) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
    if ((pad->buttons & ORBIS_PAD_BUTTON_DOWN) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
    if ((pad->buttons & ORBIS_PAD_BUTTON_LEFT) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
    if ((pad->buttons & ORBIS_PAD_BUTTON_RIGHT) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;

    if ((pad->buttons & ORBIS_PAD_BUTTON_OPTIONS) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_START;
    if ((pad->buttons & ORBIS_PAD_BUTTON_L3) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
    if ((pad->buttons & ORBIS_PAD_BUTTON_R3) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if ((pad->buttons & ORBIS_PAD_BUTTON_L1) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if ((pad->buttons & ORBIS_PAD_BUTTON_R1) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;

    /*
     * Xbox A/B/X/Y -> PlayStation Cross/Circle/Square/Triangle.
     */
    if ((pad->buttons & ORBIS_PAD_BUTTON_CROSS) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_A;
    if ((pad->buttons & ORBIS_PAD_BUTTON_CIRCLE) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_B;
    if ((pad->buttons & ORBIS_PAD_BUTTON_SQUARE) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_X;
    if ((pad->buttons & ORBIS_PAD_BUTTON_TRIANGLE) != 0)
        gamepad->wButtons |= XINPUT_GAMEPAD_Y;

    gamepad->bLeftTrigger = pcps4_xinput_trigger(pad->analogButtons.l2);
    gamepad->bRightTrigger = pcps4_xinput_trigger(pad->analogButtons.r2);

    gamepad->sThumbLX = pcps4_xinput_axis(pad->leftStick.x);
    /*
     * DirectX/XInput uses positive Y for up/down in a normalized signed
     * range; DS4 reports 0 at the top, so invert around the center.
     */
    gamepad->sThumbLY = (SHORT)-pcps4_xinput_axis(pad->leftStick.y);
    gamepad->sThumbRX = pcps4_xinput_axis(pad->rightStick.x);
    gamepad->sThumbRY = (SHORT)-pcps4_xinput_axis(pad->rightStick.y);
}

DWORD pcps4_XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState)
{
    if (pState == NULL || dwUserIndex >= XUSER_MAX_COUNT)
        return ERROR_DEVICE_NOT_CONNECTED;

    OrbisPadData pad;
    if (pcps4_xinput_read(dwUserIndex, &pad) != ERROR_SUCCESS)
        return ERROR_DEVICE_NOT_CONNECTED;

    memset(pState, 0, sizeof(*pState));
    pcps4_xinput_map(&pad, &pState->Gamepad);
    pState->dwPacketNumber = (DWORD)pad.timestamp;
    return ERROR_SUCCESS;
}

DWORD pcps4_XInputSetState(
    DWORD dwUserIndex,
    const XINPUT_VIBRATION *pVibration)
{
    if (pVibration == NULL || dwUserIndex >= XUSER_MAX_COUNT)
        return ERROR_DEVICE_NOT_CONNECTED;

    if (pcps4_xinput_open(dwUserIndex) != ERROR_SUCCESS)
        return ERROR_DEVICE_NOT_CONNECTED;

    OrbisPadVibeParam vibration;
    vibration.lgMotor = (uint8_t)(pVibration->wLeftMotorSpeed >> 8);
    vibration.smMotor = (uint8_t)(pVibration->wRightMotorSpeed >> 8);

    if (scePadSetVibration(g_pad_handles[dwUserIndex], &vibration) != 0) {
        pcps4_xinput_close(dwUserIndex);
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    return ERROR_SUCCESS;
}

DWORD pcps4_XInputGetCapabilities(
    DWORD dwUserIndex,
    DWORD dwFlags,
    XINPUT_CAPABILITIES *pCapabilities)
{
    (void)dwFlags;

    if (pCapabilities == NULL || dwUserIndex >= XUSER_MAX_COUNT)
        return ERROR_DEVICE_NOT_CONNECTED;

    if (pcps4_xinput_open(dwUserIndex) != ERROR_SUCCESS)
        return ERROR_DEVICE_NOT_CONNECTED;

    memset(pCapabilities, 0, sizeof(*pCapabilities));
    pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
    pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    pCapabilities->Flags = XINPUT_CAPS_FFB_SUPPORTED;

    return ERROR_SUCCESS;
}

void pcps4_XInputEnable(BOOL enable)
{
    /*
     * XInputEnable controls background polling on Windows. The PS4 pad API
     * is explicitly polled by XInputGetState, so enable/disable is a no-op.
     */
    (void)enable;
}
