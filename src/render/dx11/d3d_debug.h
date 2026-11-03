// ============================================================================
// src/render/dx11/d3d_debug.h -- the D3D11 debug layer's messages (BANISHED_D3D_DEBUG)
// ============================================================================
// NOT RETAIL. With BANISHED_D3D_DEBUG set, devicedata.cpp creates the device with
// D3D11_CREATE_DEVICE_DEBUG and hands it to Attach, which takes its ID3D11InfoQueue.
// Drain (after each Present) moves the stored messages to stderr as `[D3D11] ...`,
// the first 3 lines of each message ID (400 in all), then the counts per severity and per ID at exit.
// Without the flag Attach is never called and Drain returns at once.
// ============================================================================
#pragma once

struct ID3D11Device;

namespace D3dDebug {
void Attach(ID3D11Device* device);   // null: the debug create failed (no SDK layers)
void Drain();
}  // namespace D3dDebug
