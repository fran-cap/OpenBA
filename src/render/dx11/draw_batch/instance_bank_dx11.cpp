// ============================================================================
// src/render/dx11/draw_batch/instance_bank_dx11.cpp -- InstanceBank:: on the DX11 backend
// ============================================================================
// NOT RETAIL. src/render/common/draw_batch/instance_bank.h. The D3D11 transform bank stays retail's 52
// slots (VideoDX11's ContextData uploads Context::_transforms only), so the backend answers "not
// supported": Cap() is 52 and ext.draw_batch never asks for a wide slot.
// ============================================================================
#include "draw_batch/instance_bank.h"

namespace InstanceBank {

int32_t Cap() {
    return 52;
}

void SetWide(Video::Context& context, int32_t slot, const Math::Matrix& m) {
    (void)context;
    (void)slot;
    (void)m;
}

}  // namespace InstanceBank
