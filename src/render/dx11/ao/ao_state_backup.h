// ============================================================================
// src/render/dx11/ao/ao_state_backup.h -- every D3D binding a full-screen ext pass touches, saved and put back
// ============================================================================
// NOT RETAIL. Moved out of ao_pass.cpp (docs/extensions/handins/AOCORE.md) for a second user. The engine's
// ContextData caches what it last bound; restoring the D3D state exactly keeps that cache true, so a pass
// that uses this raises no engine dirty bit.
// ============================================================================
#pragma once

#include <d3d11.h>

namespace AoStage {

// ---------------------------------------------------------------------------
// Every binding the resolve and ASSAO touch, saved and restored exactly (ext.better_clouds' shadow pass,
// src/render/dx11/better_clouds/cloud_shadow.cpp, touches a subset of them and uses it too).
// ---------------------------------------------------------------------------
constexpr UINT kPsSrvs     = 8;   // ASSAO: t0..t5; the resolve: t0
constexpr UINT kPsSamplers = 4;   // ASSAO: s0..s3
constexpr UINT kPsCbs      = 1;   // ASSAO: b0

class StateBackup {
public:
    explicit StateBackup(ID3D11DeviceContext* dc) : _dc(dc) {
        dc->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _rtvs, &_dsv);
        dc->OMGetBlendState(&_bs, _blendFactor, &_sampleMask);
        dc->OMGetDepthStencilState(&_dss, &_stencilRef);
        _numVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        dc->RSGetViewports(&_numVp, _vps);
        _numSc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        dc->RSGetScissorRects(&_numSc, _scs);
        dc->RSGetState(&_rs);
        dc->IAGetInputLayout(&_il);
        dc->IAGetPrimitiveTopology(&_topo);
        dc->IAGetVertexBuffers(0, 1, &_vb, &_vbStride, &_vbOffset);
        dc->IAGetIndexBuffer(&_ib, &_ibFormat, &_ibOffset);
        dc->VSGetShader(&_vs, nullptr, nullptr);
        dc->PSGetShader(&_ps, nullptr, nullptr);
        dc->PSGetShaderResources(0, kPsSrvs, _srvs);
        dc->PSGetSamplers(0, kPsSamplers, _samplers);
        dc->PSGetConstantBuffers(0, kPsCbs, _cbs);
    }
    // The main colour + depth, for the stage (it multiplies into OM slot 0).
    void RebindTargets() const { _dc->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _rtvs, _dsv); }
    ID3D11RenderTargetView* Colour() const { return _rtvs[0]; }

    ~StateBackup() {
        ID3D11DeviceContext* const dc = _dc;
        // SRVs first: the main depth must not sit on an input while it is rebound as the DSV.
        dc->PSSetShaderResources(0, kPsSrvs, _srvs);
        dc->PSSetSamplers(0, kPsSamplers, _samplers);
        dc->PSSetConstantBuffers(0, kPsCbs, _cbs);
        dc->VSSetShader(_vs, nullptr, 0);
        dc->PSSetShader(_ps, nullptr, 0);
        dc->IASetInputLayout(_il);
        dc->IASetPrimitiveTopology(_topo);
        dc->IASetVertexBuffers(0, 1, &_vb, &_vbStride, &_vbOffset);
        dc->IASetIndexBuffer(_ib, _ibFormat, _ibOffset);
        dc->RSSetState(_rs);
        dc->RSSetViewports(_numVp, _vps);
        dc->RSSetScissorRects(_numSc, _scs);
        dc->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, _rtvs, _dsv);
        dc->OMSetBlendState(_bs, _blendFactor, _sampleMask);
        dc->OMSetDepthStencilState(_dss, _stencilRef);
        for (auto*& p : _rtvs) Rel(p);
        for (auto*& p : _srvs) Rel(p);
        for (auto*& p : _samplers) Rel(p);
        for (auto*& p : _cbs) Rel(p);
        Rel(_dsv); Rel(_bs); Rel(_dss); Rel(_rs); Rel(_il); Rel(_vb); Rel(_ib); Rel(_vs); Rel(_ps);
    }

private:
    template <class T> static void Rel(T*& p) {
        if (p) { p->Release(); p = nullptr; }
    }
    ID3D11DeviceContext*      _dc;
    ID3D11RenderTargetView*   _rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView*   _dsv = nullptr;
    ID3D11BlendState*         _bs  = nullptr;
    FLOAT                     _blendFactor[4] = {};
    UINT                      _sampleMask = 0;
    ID3D11DepthStencilState*  _dss = nullptr;
    UINT                      _stencilRef = 0;
    UINT                      _numVp = 0;
    D3D11_VIEWPORT            _vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT                      _numSc = 0;
    D3D11_RECT                _scs[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    ID3D11RasterizerState*    _rs  = nullptr;
    ID3D11InputLayout*        _il  = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY  _topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer*             _vb  = nullptr;
    UINT                      _vbStride = 0, _vbOffset = 0;
    ID3D11Buffer*             _ib  = nullptr;
    DXGI_FORMAT               _ibFormat = DXGI_FORMAT_UNKNOWN;
    UINT                      _ibOffset = 0;
    ID3D11VertexShader*       _vs  = nullptr;
    ID3D11PixelShader*        _ps  = nullptr;
    ID3D11ShaderResourceView* _srvs[kPsSrvs] = {};
    ID3D11SamplerState*       _samplers[kPsSamplers] = {};
    ID3D11Buffer*             _cbs[kPsCbs] = {};
};

}  // namespace AoStage
