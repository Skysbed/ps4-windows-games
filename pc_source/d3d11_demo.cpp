#include <d3d11.h>

extern "C" int pcps4_d3d11_smoke_test()
{
    DXGI_SWAP_CHAIN_DESC swap_desc = {};
    swap_desc.BufferDesc.Width = 1280;
    swap_desc.BufferDesc.Height = 720;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferCount = 2;
    swap_desc.Windowed = 1;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGISwapChain *swap_chain = NULL;
    D3D_FEATURE_LEVEL feature_level;

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

    if (FAILED(hr) ||
        device == NULL ||
        swap_chain == NULL)
        return -1;

    device->GetImmediateContext(&context);
    if (context == NULL) {
        swap_chain->Release();
        device->Release();
        return -2;
    }

    ID3D11Texture2D *back_buffer = NULL;
    hr = swap_chain->GetBuffer(0, &back_buffer);
    if (FAILED(hr) || back_buffer == NULL) {
        context->Release();
        swap_chain->Release();
        device->Release();
        return -3;
    }

    ID3D11RenderTargetView *rtv = NULL;
    hr = device->CreateRenderTargetView(
        back_buffer,
        NULL,
        &rtv);

    if (FAILED(hr) || rtv == NULL) {
        back_buffer->Release();
        context->Release();
        swap_chain->Release();
        device->Release();
        return -4;
    }

    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.ByteWidth = 1024;
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    ID3D11Buffer *vertex_buffer = NULL;
    hr = device->CreateBuffer(
        &buffer_desc,
        NULL,
        &vertex_buffer);

    if (FAILED(hr) || vertex_buffer == NULL) {
        rtv->Release();
        back_buffer->Release();
        context->Release();
        swap_chain->Release();
        device->Release();
        return -5;
    }

    context->OMSetRenderTargets(1, &rtv, NULL);

    const float clear_color[4] = {
        0.08f, 0.10f, 0.15f, 1.0f
    };

    context->ClearRenderTargetView(
        rtv,
        clear_color);

    UINT stride = 16;
    UINT offset = 0;

    context->IASetVertexBuffers(
        0,
        1,
        &vertex_buffer,
        &stride,
        &offset);

    /* Controlled no-op: shader/command lowering is not ready yet. */
    context->Draw(3, 0);
    context->Flush();

    vertex_buffer->Release();
    rtv->Release();
    back_buffer->Release();
    context->Release();
    swap_chain->Release();
    device->Release();

    return 0;
}
