#include <d3d11.h>
#include <xinput.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void draw_rect(
    uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    int32_t x,
    int32_t y,
    int32_t w,
    int32_t h,
    uint8_t r,
    uint8_t g,
    uint8_t b)
{
    if (pixels == NULL || w <= 0 || h <= 0)
        return;

    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > (int32_t)width ? (int32_t)width : x + w;
    int32_t y1 = y + h > (int32_t)height ? (int32_t)height : y + h;

    for (int32_t py = y0; py < y1; ++py) {
        uint8_t *row = pixels + (size_t)py * width * 4u;
        for (int32_t px = x0; px < x1; ++px) {
            row[(size_t)px * 4u + 0] = r;
            row[(size_t)px * 4u + 1] = g;
            row[(size_t)px * 4u + 2] = b;
            row[(size_t)px * 4u + 3] = 255;
        }
    }
}

static void draw_background(
    uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t frame)
{
    if (pixels == NULL)
        return;

    for (uint32_t y = 0; y < height; ++y) {
        uint8_t *row = pixels + (size_t)y * width * 4u;
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t shade = (uint8_t)(12u + ((x + y + frame) & 15u));
            row[x * 4u + 0] = shade;
            row[x * 4u + 1] = (uint8_t)(24u + ((y + frame) & 15u));
            row[x * 4u + 2] = (uint8_t)(40u + ((x + frame * 2u) & 15u));
            row[x * 4u + 3] = 255;
        }
    }
}

static void draw_crosshair(
    uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    int32_t x,
    int32_t y)
{
    (void)width;
    (void)height;
    draw_rect(pixels, width, height, x - 2, y - 14, 4, 28, 255, 255, 255);
    draw_rect(pixels, width, height, x - 14, y - 2, 28, 4, 255, 255, 255);
}

extern "C" int pcps4_d3d11_game_run(void)
{
    const uint32_t width = 640;
    const uint32_t height = 360;
    const uint32_t pitch = width * 4u;
    const size_t bytes = (size_t)pitch * height;

    DXGI_SWAP_CHAIN_DESC swap_desc = {};
    swap_desc.BufferDesc.Width = width;
    swap_desc.BufferDesc.Height = height;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferCount = 2;
    swap_desc.Windowed = 1;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGISwapChain *swap_chain = NULL;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0,
        NULL,
        0,
        7,
        &swap_desc,
        &swap_chain,
        &device,
        &feature_level,
        &context);

    if (FAILED(hr) || device == NULL || context == NULL || swap_chain == NULL)
        return -1;

    ID3D11Texture2D *back_buffer = NULL;
    hr = swap_chain->GetBuffer(0, &back_buffer);
    if (FAILED(hr) || back_buffer == NULL) {
        swap_chain->Release();
        device->Release();
        return -2;
    }

    ID3D11RenderTargetView *rtv = NULL;
    hr = device->CreateRenderTargetView(back_buffer, NULL, &rtv);
    if (FAILED(hr) || rtv == NULL) {
        back_buffer->Release();
        swap_chain->Release();
        device->Release();
        return -3;
    }

    context->OMSetRenderTargets(1, &rtv, NULL);

    uint8_t *pixels = (uint8_t *)malloc(bytes);
    if (pixels == NULL) {
        rtv->Release();
        back_buffer->Release();
        swap_chain->Release();
        device->Release();
        return -4;
    }

    int32_t player_x = (int32_t)width / 2;
    int32_t player_y = (int32_t)height / 2;
    uint32_t frame = 0;

    for (;;) {
        XINPUT_STATE state = {};
        DWORD input_rc = XInputGetState(0, &state);

        int32_t dx = 0;
        int32_t dy = 0;

        if (input_rc == ERROR_SUCCESS) {
            int32_t lx = (int32_t)state.Gamepad.sThumbLX;
            int32_t ly = (int32_t)state.Gamepad.sThumbLY;

            if (lx > 8000) dx += 3;
            if (lx < -8000) dx -= 3;
            if (ly > 8000) dy -= 3;
            if (ly < -8000) dy += 3;

            if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0)
                dx -= 3;
            if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0)
                dx += 3;
            if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0)
                dy -= 3;
            if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0)
                dy += 3;

            /*
             * Options is the XInput Start button in the PS4 bridge. It exits
             * the demo cleanly instead of requiring the application to crash
             * or be killed.
             */
            if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0)
                break;

            if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0) {
                XINPUT_VIBRATION vibration = {0, 0, 12000, 12000};
                (void)XInputSetState(0, &vibration);
            } else {
                XINPUT_VIBRATION vibration = {0, 0, 0, 0};
                (void)XInputSetState(0, &vibration);
            }
        }

        player_x += dx;
        player_y += dy;

        if (player_x < 24) player_x = 24;
        if (player_y < 24) player_y = 24;
        if (player_x > (int32_t)width - 24) player_x = (int32_t)width - 24;
        if (player_y > (int32_t)height - 24) player_y = (int32_t)height - 24;

        draw_background(pixels, width, height, frame);
        draw_rect(
            pixels, width, height,
            player_x - 12, player_y - 12, 24, 24,
            70, 210, 255);
        draw_crosshair(pixels, width, height, player_x, player_y);

        context->UpdateSubresource(
            back_buffer,
            0,
            NULL,
            pixels,
            pitch,
            (UINT)bytes);

        hr = swap_chain->Present(1, 0);
        if (FAILED(hr))
            break;

        ++frame;
        Sleep(16);
    }

    XINPUT_VIBRATION vibration = {0, 0, 0, 0};
    (void)XInputSetState(0, &vibration);

    free(pixels);
    rtv->Release();
    back_buffer->Release();
    context->Release();
    swap_chain->Release();
    device->Release();

    return FAILED(hr) ? -5 : 0;
}
