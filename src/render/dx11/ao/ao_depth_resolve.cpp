// ============================================================================
// src/render/dx11/ao/ao_depth_resolve.cpp -- DepthResolve (ao_depth_resolve.h)
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (docs/extensions/handins/AOCORE.md).
// ============================================================================
#include "ao/ao_depth_resolve.h"

#include <cstdio>

#include "ao/shaders/VSFullscreen.h"
#include "ao/shaders/PSResolveDepthMS.h"
#include "ao/shaders/PSResolveDepth1x.h"

namespace AoStage {
namespace {
template <class T> void Rel(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}
}  // namespace

bool DepthResolve::Create(ID3D11Device* dev) {
    Release();
    HRESULT hr = dev->CreateVertexShader(g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), nullptr, &_vs);
    if (hr == S_OK)
        hr = dev->CreatePixelShader(g_ao_PSResolveDepthMS, sizeof(g_ao_PSResolveDepthMS), nullptr, &_psMS);
    if (hr == S_OK)
        hr = dev->CreatePixelShader(g_ao_PSResolveDepth1x, sizeof(g_ao_PSResolveDepth1x), nullptr, &_ps1x);
    if (hr == S_OK) {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        hr = dev->CreateRasterizerState(&rd, &_rs);
    }
    if (hr == S_OK) {
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable    = FALSE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc      = D3D11_COMPARISON_ALWAYS;
        hr = dev->CreateDepthStencilState(&dd, &_dss);
    }
    if (hr == S_OK) {
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = dev->CreateBlendState(&bd, &_bs);
    }
    if (hr != S_OK) {
        std::fprintf(stderr, "[%s] depth resolve: shader / state creation failed (0x%08lx)\n", _tag,
                     static_cast<unsigned long>(hr));
        Release();
        return false;
    }
    return true;
}

void DepthResolve::Release() {
    Rel(_srv); Rel(_rtv); Rel(_tex);
    Rel(_bs); Rel(_dss); Rel(_rs);
    Rel(_ps1x); Rel(_psMS); Rel(_vs);
    _width = _height = 0;
}

bool DepthResolve::Resize(ID3D11Device* dev, unsigned w, unsigned h) {
    if (_tex != nullptr && w == _width && h == _height) return true;
    Rel(_srv); Rel(_rtv); Rel(_tex);
    _width = _height = 0;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = w;
    td.Height           = h;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = dev->CreateTexture2D(&td, nullptr, &_tex);
    if (hr == S_OK) hr = dev->CreateRenderTargetView(_tex, nullptr, &_rtv);
    if (hr == S_OK) hr = dev->CreateShaderResourceView(_tex, nullptr, &_srv);
    std::fprintf(stderr, "[%s] depth resolve target %ux%u R32_FLOAT: 0x%08lx\n", _tag, w, h,
                 static_cast<unsigned long>(hr));
    if (hr != S_OK) {
        Rel(_srv); Rel(_rtv); Rel(_tex);
        return false;
    }
    _width = w;
    _height = h;
    return true;
}

ID3D11ShaderResourceView* DepthResolve::Run(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* depth,
                                            unsigned samples) {
    // The main DSV must be unbound to read the depth through its SRV.
    dc->OMSetRenderTargets(1, &_rtv, nullptr);
    const D3D11_VIEWPORT vp = {0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height),
                               0.0f, 1.0f};
    dc->RSSetViewports(1, &vp);
    dc->RSSetState(_rs);
    dc->OMSetDepthStencilState(_dss, 0);
    dc->OMSetBlendState(_bs, nullptr, 0xffffffffu);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(_vs, nullptr, 0);
    dc->PSSetShader(samples > 1 ? _psMS : _ps1x, nullptr, 0);
    dc->PSSetShaderResources(0, 1, &depth);
    dc->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    dc->PSSetShaderResources(0, 1, &none);
    return _srv;
}

}  // namespace AoStage
