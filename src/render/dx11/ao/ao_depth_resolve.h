// ============================================================================
// src/render/dx11/ao/ao_depth_resolve.h -- the main depth (MSAA or 1x) -> 1x R32F raw depth
// ============================================================================
// NOT RETAIL. ext.ambient_occlusion (docs/extensions/AO.md §3 "Depth", §6.1). The shader is
// ao_depth_resolve.hlsl (why it stays raw, not linear: its banner).
// ============================================================================
#pragma once

#include <d3d11.h>

namespace AoStage {

class DepthResolve {
public:
    DepthResolve() = default;
    // `tag` names the user in the log lines (ext.better_water's scene depth copy is the second).
    explicit DepthResolve(const char* tag) : _tag(tag) {}
    DepthResolve(const DepthResolve&) = delete;
    DepthResolve& operator=(const DepthResolve&) = delete;
    ~DepthResolve() { Release(); }

    // Shaders and states on `device`. False = unusable (logged).
    bool Create(ID3D11Device* device);
    void Release();

    // The w x h R32F target; re-created only when the size changes.
    bool Resize(ID3D11Device* device, unsigned width, unsigned height);

    // Draw `depth` (the main depth's R24_UNORM_X8 view; TEXTURE2DMS when `samples` > 1) into
    // the target. Binds its own RTV (no DSV), viewport, shaders and states, and unbinds the
    // depth SRV again; the caller restores everything else. Returns the target's SRV.
    ID3D11ShaderResourceView* Run(ID3D11DeviceContext* context, ID3D11ShaderResourceView* depth,
                                  unsigned samples);

    unsigned Width() const { return _width; }
    unsigned Height() const { return _height; }

private:
    ID3D11VertexShader*       _vs   = nullptr;
    ID3D11PixelShader*        _psMS = nullptr;
    ID3D11PixelShader*        _ps1x = nullptr;
    ID3D11RasterizerState*    _rs   = nullptr;
    ID3D11DepthStencilState*  _dss  = nullptr;
    ID3D11BlendState*         _bs   = nullptr;
    ID3D11Texture2D*          _tex  = nullptr;
    ID3D11RenderTargetView*   _rtv  = nullptr;
    ID3D11ShaderResourceView* _srv  = nullptr;
    unsigned                  _width  = 0;
    unsigned                  _height = 0;
    const char*               _tag    = "AmbientOcclusion";
};

}  // namespace AoStage
