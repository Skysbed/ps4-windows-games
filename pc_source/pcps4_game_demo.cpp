#include <d3d11.h>
#include <xinput.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern "C" int pcps4_d3d11_game_run(void)
{
    const uint32_t width = 640;
    const uint32_t height = 360;
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
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    uint32_t shader_signature = 0x12345678u;
    ID3D11InputLayout *input_layout = NULL;
    hr = device->CreateInputLayout(
        layout, 2,
        &shader_signature, sizeof(shader_signature),
        &input_layout);
    if (FAILED(hr) || input_layout == NULL) {
        rtv->Release();
        back_buffer->Release();
        swap_chain->Release();
        device->Release();
        return -4;
    }
    context->IASetInputLayout(input_layout);

    struct Vertex {
        float position[3];
        float color[4];
    };

    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = (UINT)(sizeof(struct Vertex) * 3u);
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ID3D11Buffer *vertex_buffer = NULL;
    hr = device->CreateBuffer(&buffer_desc, NULL, &vertex_buffer);
    if (FAILED(hr) || vertex_buffer == NULL) {
        input_layout->Release();
        rtv->Release();
        back_buffer->Release();
        swap_chain->Release();
        device->Release();
        return -5;
    }

    ID3D11Buffer *vertex_buffers[] = {vertex_buffer};
    const UINT stride = (UINT)sizeof(struct Vertex);
    const UINT offset = 0;
    context->IASetVertexBuffers(0, 1, vertex_buffers, &stride, &offset);

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

        struct Vertex {
            float position[3];
            float color[4];
        } vertices[3];

        float cx = ((float)player_x / (float)width) * 2.0f - 1.0f;
        float cy = 1.0f - ((float)player_y / (float)height) * 2.0f;
        float sx = 0.10f;
        float sy = 0.14f;

        vertices[0] = {{cx,        cy - sy, 0.0f},
                       {0.25f, 0.85f, 1.0f, 1.0f}};
        vertices[1] = {{cx + sx,   cy + sy, 0.0f},
                       {0.15f, 0.45f, 1.0f, 1.0f}};
        vertices[2] = {{cx - sx,   cy + sy, 0.0f},
                       {0.75f, 0.95f, 1.0f, 1.0f}};

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = context->Map(
            vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr) || mapped.pData == NULL)
            break;

        memcpy(mapped.pData, vertices, sizeof(vertices));
        context->Unmap(vertex_buffer, 0);

        const float clear_color[4] = {0.03f, 0.06f, 0.10f, 1.0f};
        context->ClearRenderTargetView(rtv, clear_color);
        context->Draw(3, 0);

        hr = swap_chain->Present(1, 0);
        if (FAILED(hr))
            break;

        ++frame;
        Sleep(16);
    }

    XINPUT_VIBRATION vibration = {0, 0, 0, 0};
    (void)XInputSetState(0, &vibration);

    vertex_buffer->Release();
    input_layout->Release();
    rtv->Release();
    back_buffer->Release();
    context->Release();
    swap_chain->Release();
    device->Release();

    return FAILED(hr) ? -5 : 0;
}
