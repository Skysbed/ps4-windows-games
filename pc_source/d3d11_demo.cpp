#include <stdio.h>
#include <stdlib.h>

#include <d3d11.h>

static int load_shader_file(
    const char *path,
    void **data,
    size_t *size)
{
    FILE *file;
    long length;
    void *buffer;

    if (path == NULL || data == NULL || size == NULL)
        return -1;

    *data = NULL;
    *size = 0;

    file = fopen(path, "rb");
    if (file == NULL)
        return -2;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -3;
    }

    length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return -4;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -5;
    }

    buffer = malloc((size_t)length);
    if (buffer == NULL) {
        fclose(file);
        return -6;
    }

    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return -7;
    }

    fclose(file);
    *data = buffer;
    *size = (size_t)length;
    return 0;
}

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

    if (FAILED(hr) || device == NULL || swap_chain == NULL)
        return -1;

    /*
     * D3D11CreateDeviceAndSwapChain already returned the immediate context.
     * Do not acquire a second one here; it only adds an unnecessary lifetime
     * edge and obscures failures in the smoke path.
     */
    if (context == NULL)
        return -2;

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

#if PCPS4_ENABLE_OPENGNM
    void *vs_data = NULL;
    void *ps_data = NULL;
    size_t vs_size = 0;
    size_t ps_size = 0;

    if (load_shader_file(
            "/app0/assets/shaders/tri.vert.sb",
            &vs_data,
            &vs_size) != 0 ||
        load_shader_file(
            "/app0/assets/shaders/tri.frag.sb",
            &ps_data,
            &ps_size) != 0) {
        free(vs_data);
        free(ps_data);
        rtv->Release();
        back_buffer->Release();
        context->Release();
        swap_chain->Release();
        device->Release();
        return -5;
    }

    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;

    hr = device->CreateVertexShader(
        vs_data, vs_size, NULL, &vs);

    if (FAILED(hr)) {
        free(vs_data);
        free(ps_data);
        rtv->Release();
        back_buffer->Release();
        context->Release();
        swap_chain->Release();
        device->Release();
        return -6;
    }

    hr = device->CreatePixelShader(
        ps_data, ps_size, NULL, &ps);

    free(vs_data);
    free(ps_data);

    if (FAILED(hr)) {
        vs->Release();
        rtv->Release();
        back_buffer->Release();
        context->Release();
        swap_chain->Release();
        device->Release();
        return -7;
    }

    context->OMSetRenderTargets(1, &rtv, NULL);
    context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context->VSSetShader(vs, NULL, 0);
    context->PSSetShader(ps, NULL, 0);

    const float clear_color[4] = {
        0.05f, 0.08f, 0.12f, 1.0f
    };

    context->ClearRenderTargetView(
        rtv,
        clear_color);

    context->Draw(3, 0);

    hr = swap_chain->Present(1, 0);

    ps->Release();
    vs->Release();

    rtv->Release();
    back_buffer->Release();
    context->Release();
    swap_chain->Release();
    device->Release();

    return FAILED(hr) ? -8 : 0;
#else
    rtv->Release();
    back_buffer->Release();
    context->Release();
    swap_chain->Release();
    device->Release();
    return 0;
#endif
}
