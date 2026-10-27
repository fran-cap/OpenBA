// ============================================================================
// src/ext/eye_camera/eye_camera.cpp -- ext.eye_camera: the engine glue
// ============================================================================
// NOT RETAIL. eye_camera.h has the contract and the seams; eye_math.h the
// conventions and the constants. Nothing here writes a serialized field: the
// only engine state touched is the Graphics::Camera member of PlayerController
// (+0x34, not serialized) through SetCameraToWorld / UpdateCamera, the same two
// writes retail's own seams make. Retail's CalcTransform still runs at step 6
// for its +0x00 store.
// ============================================================================

#include "ext/eye_camera/eye_camera.h"
#include "ext/eye_camera/eye_math.h"

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "Component/CharacterComponent.h"
#include "Component/CharacterDescription.h"
#include "Component/CitizenComponent.h"
#include "Component/ModelComponent.h"
#include "Dialog/FollowDialog.h"
#include "Entity/ComponentEntity.h"
#include "Game/GameUpdateLoop.h"
#include "Game/Map.h"
#include "Game/PlayerController.h"
#include "Game/PlayerToolManager.h"
#include "graphics/Camera.h"
#include "graphics/InterpolatedState.h"
#include "graphics/Scene.h"
#include "graphics/SunLight.h"
#include "math/Math.h"
#include "object/Internal.h"
#include "ui/DialogInstance.h"
#include "ui/UserInterface.h"

namespace Ext {
namespace EyeCamera {

namespace {

// ---------------------------------------------------------------------------
// EYECAM3: the identity is the FOLLOWED ENTITY (EYECAM.md §10). Each step it is
// read from the frontmost open FollowDialog -- dialog->citizen->_entity, the
// read retail's FollowDialog::OnUpdate (0x140069250) makes every frame, so the
// eye dereferences nothing that faithful body does not -- and compared by
// pointer with the latched one. The dialog object is not the identity (a
// second FollowDialog on the same citizen just re-attaches), and the
// InternalFactory slot is not a liveness test once latched: a live object can
// drop out of that table (EYECAM.md §10.2), and EYECAM2's weak reference then
// read "destroyed" and handed the camera to retail's follow for good while the
// dialog stayed open. The table is asked only before a NEW entity is first
// dereferenced (a freed object is never in it). An entity seen marked for
// removal (or not in the table) is remembered as `g_deadEntity` and never
// dereferenced again while a dialog targets it.
// ---------------------------------------------------------------------------
struct Latched {
    const ComponentEntity* entity = nullptr;   // compared by value; read only while its dialog is open
    const FollowDialog*    dialog = nullptr;   // the dialog it was read from (trace only, never dereferenced)
    int32_t                hint   = -1;        // its InternalFactory slot (the table-miss note only)
    bool                   tableMissNoted = false;
};

// ---------------------------------------------------------------------------
// The state. None of it is serialized (class A).
// ---------------------------------------------------------------------------
Latched               g_latched;         // the follow we are engaged on
const ComponentEntity* g_deadEntity = nullptr;   // seen marked / gone: never dereferenced again
bool                  g_engaged = false; // the trace's edge detector
Smooth                g_smooth;          // the smoothed feet / heading / distance
float                 g_distance = kDefaultDistance;   // the wheel's target (kept across follows)
float                 g_scale = 1.0f;    // the followed model's age scale (last sample)
Pose                  g_pose;            // the last eye transform built
bool                  g_havePose = false;
std::chrono::steady_clock::time_point g_lastFrame;
bool                  g_haveLastFrame = false;

void ResetFollow() {
    g_latched = Latched();
    g_smooth.valid = false;
    g_havePose = false;
    g_haveLastFrame = false;
}

// The slot of `object` in InternalFactory::_objects, -1 = not in the table.
int FactorySlot(const void* object, int hint) {
    const ::Object::InternalFactory* factory = ::Object::InternalFactory::_singleton;
    const Collection::Array<::Object::Internal*>& objects = factory->_objects;
    return FollowSlot(objects._data, objects._size, object, hint);
}

bool IsRemoved(const UI::UserInterface* ui, const UI::DialogInstance* dialog) {
    const Collection::Array<UI::DialogInstancePtr>& removed = ui->_removedDialogs;
    for (int32_t i = 0; i < removed._size; ++i)
        if (removed._data[i] == dialog) return true;
    return false;
}

// The frontmost open FollowDialog. The UI updates the list back to front, so
// when two citizen panels both follow, the frontmost one's OnUpdate writes
// retail's target last: follow the same citizen retail's camera follows.
FollowDialog* FindFollowDialog(const UI::UserInterface* ui) {
    for (auto* node = ui->_dialogs._tail; node != nullptr; node = node->prev) {
        UI::DialogInstance* dialog = node->element;
        if (dialog == nullptr || !dialog->IsA(FollowDialog::TypeId())) continue;
        if (IsRemoved(ui, dialog)) continue;
        return static_cast<FollowDialog*>(dialog);
    }
    return nullptr;
}

// The citizen's feet and heading from its model, interpolated at `alpha` the
// way the renderer interpolates it (Geometry::_transform, prev -> current),
// and the model's age scale. Nothing here reads the move tick or the path: an
// idle citizen's model keeps its last transform, so the heading holds.
// The model out of the scene (hidden: inside a building): false, unless
// `hiddenToo`, which takes ModelComponent::transform instead (the per-tick
// gameplay copy, which a hidden citizen keeps) -- the first pose of an
// engagement, so a follow (re)latched while the citizen is inside stays the
// eye's at the door rather than handing the camera back to retail's follow.
bool SampleCitizen(const ComponentEntity* entity, float alpha, bool hiddenToo,
                   Vec3& feet, float& heading, float& scale) {
    const ModelComponent* model = entity->FindComponent<ModelComponent>();
    if (model == nullptr) return false;
    Math::Transform t;
    if (model->modelId >= 0) {
        const Graphics::InterpolatedState<Math::Transform>& xf = model->meshInstance._transform;
        t = xf._previous.Lerp(xf._current, Math::Scalar(alpha));
    } else if (hiddenToo) {
        t = model->transform;
    } else {
        return false;
    }
    feet = Vec3{t.translation.x, t.translation.y, t.translation.z};
    // The age scale SetupYoung / SetupAdult / SetupElder wrote (eye_math.h,
    // CONVENTIONS): uniform, so the z component is the height factor.
    scale = model->meshInstance._color[2];
    // Model yaw = heading + CharacterDescription::_f24 (the move tick's offset).
    float offset = 0.0f;
    if (const CharacterComponent* ch = entity->FindComponent<CharacterComponent>())
        if (ch->characterDescription != nullptr) offset = ch->characterDescription->_f24;
    heading = WrapDegrees(ModelYawFromQuaternion(t.rotation.z, t.rotation.w) - offset);
    return true;
}

// Keep the eye above the terrain when it backs off over a slope.
void ClampToTerrain(Pose& pose) {
    Map* map = GameUpdateLoop::_singleton->m_game;
    const float in[2] = {pose.eye.x, pose.eye.y};
    float out[3] = {0.0f, 0.0f, 0.0f};
    map->GetTerrainPos(out, in, 1, nullptr);   // flag 1: the heightmap only (clamped to the map)
    const float floorZ = out[2] + kMinClearance;
    if (pose.eye.z < floorZ) pose.eye.z = floorZ;
}

void BuildPose() {
    g_pose = ComputePose(g_smooth.feet, g_smooth.heading, g_scale, g_smooth.distance);
    ClampToTerrain(g_pose);
    g_havePose = true;
}

void PutOnCamera(PlayerController& pc) {
    const Math::Vector3 eye(g_pose.eye.x, g_pose.eye.y, g_pose.eye.z);
    const Math::Vector3 look(g_pose.look.x, g_pose.look.y, g_pose.look.z);
    const Math::Transform t = Math::Transform::FromLookAt(eye, look);
    pc.camera.SetCameraToWorld(t.AsTransform3());
}

// The trace (EYECAM.md §10.3): one stderr line (standalone_error.log) at each
// edge -- engage, re-attach, and every hand-back to retail's camera with the
// test that caused it. Unconditional: it fires a few times per session.
void TraceEngage(const char* what, const ComponentEntity* entity, const FollowDialog* dialog,
                 const ComponentEntity* from) {
    std::fprintf(stderr, "[EyeCamera] %s entity=%p dialog=%p from=%p\n", what,
                 static_cast<const void*>(entity), static_cast<const void*>(dialog),
                 static_cast<const void*>(from));
}

void HandBack(const char* reason, bool dialogOpen, bool followFlag) {
    if (g_engaged)
        std::fprintf(stderr, "[EyeCamera] disengage reason=%s entity=%p dialog=%p dialogOpen=%d followFlag=%d\n",
                     reason, static_cast<const void*>(g_latched.entity),
                     static_cast<const void*>(g_latched.dialog), dialogOpen ? 1 : 0, followFlag ? 1 : 0);
    g_engaged = false;
    ResetFollow();
}

bool InFactoryTable(const void* entity) { return FactorySlot(entity, -1) >= 0; }

bool MarkedForRemoval(const ComponentEntity* entity) {
    return ((entity->_entityFlags >> 10) & 1) != 0;
}

}  // namespace

const ComponentEntity* FollowedEntity() {
    return g_engaged ? g_latched.entity : nullptr;
}

bool Active() {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    // Retail's follow flag (PlayerToolManager +0x38: FollowDialog::OnOpened
    // sets it, OnClosed clears it, the ctor zeroes it; nothing else writes it)
    // is not a test since EYECAM3: it is one byte for any number of
    // FollowDialogs, so closing one of two clears it while the other still
    // follows. The open dialog list is the truth; the trace reports the flag.
    const bool followFlag = loop->m_toolManager->field_0x38;
    FollowDialog* dialog = FindFollowDialog(loop->m_userInterface);
    const ComponentEntity* front = nullptr;
    if (dialog != nullptr && dialog->citizen != nullptr)
        front = dialog->citizen->_entity.Get();   // FollowDialog::OnUpdate's own read

    const FollowDecision d = DecideFollow(dialog != nullptr, front, g_latched.entity,
                                          g_deadEntity, InFactoryTable, MarkedForRemoval);

    if (d.verdict == FollowVerdict::Off) {
        if (d.reason == FollowReason::NoFollowDialog) g_deadEntity = nullptr;   // nothing targets it any more
        if (d.reason == FollowReason::EntityGone || d.reason == FollowReason::EntityMarked)
            g_deadEntity = front;
        HandBack(FollowReasonName(d.reason), dialog != nullptr, followFlag);
        return false;
    }
    if (d.verdict == FollowVerdict::Latch) {
        // A new follow, or the frontmost follow now targets another citizen.
        const ComponentEntity* from = g_engaged ? g_latched.entity : nullptr;
        g_engaged = false;
        ResetFollow();
        // First step of this follow: start exactly at the citizen (inside a
        // building: at its gameplay transform, the door it went in by).
        Vec3 feet;
        float heading = 0.0f;
        if (!SampleCitizen(front, 1.0f, true, feet, heading, g_scale)) {
            // No ModelComponent: nothing to put an eye on (retail's follow has
            // no camera write for it either). Asked again next step.
            std::fprintf(stderr, "[EyeCamera] engage refused reason=no-model entity=%p dialog=%p\n",
                         static_cast<const void*>(front), static_cast<const void*>(dialog));
            return false;
        }
        g_latched.entity = front;
        g_latched.dialog = dialog;
        g_latched.hint = FactorySlot(front, -1);
        Snap(g_smooth, feet, heading, g_distance);
        BuildPose();
        g_engaged = true;
        TraceEngage("engage", front, dialog, from);
        return true;
    }
    // Keep. Another open FollowDialog on the same citizen (one closed, a second
    // still open): the eye re-attaches to it.
    if (dialog != g_latched.dialog) {
        TraceEngage("reattach", front, dialog, g_latched.entity);
        g_latched.dialog = dialog;
    }
    // Diagnostic only (EYECAM.md §10.2): the latched entity dropped out of
    // InternalFactory's table while its dialog still targets it -- what
    // EYECAM2's weak reference read as "destroyed". Searched only when the slot
    // hint misses; noted once per follow.
    if (!g_latched.tableMissNoted) {
        const int slot = FactorySlot(front, g_latched.hint);
        if (slot < 0) {
            std::fprintf(stderr, "[EyeCamera] note entity=%p not in InternalFactory (kept: its FollowDialog still targets it)\n",
                         static_cast<const void*>(front));
            g_latched.tableMissNoted = true;
        } else {
            g_latched.hint = slot;
        }
    }
    return true;
}

void Apply(PlayerController& pc) {
    pc.CalcTransform();   // 0x1400983f0: retail's +0x00 store, the camera write skipped
    PutOnCamera(pc);
}

void ApplyFrame(PlayerController& pc, float alpha, float fovY, float farZ) {
    const auto now = std::chrono::steady_clock::now();
    float dt = 0.0f;
    if (g_haveLastFrame) dt = std::chrono::duration<float>(now - g_lastFrame).count();
    g_lastFrame = now;
    g_haveLastFrame = true;
    if (dt > kMaxFrameGap) g_smooth.valid = false;   // back after a gap (the toggle was off): snap
    if (dt < 0.0f) dt = 0.0f;

    Vec3 feet;
    float heading = 0.0f;
    // Active() returned true for this very step: the latched entity is the
    // frontmost open follow's and not marked for removal.
    if (SampleCitizen(g_latched.entity, alpha, false, feet, heading, g_scale)) {
        Step(g_smooth, feet, heading, g_distance, dt);
        BuildPose();
    }
    // Hidden (inside a building): the last eye transform stays.
    PutOnCamera(pc);
    pc.UpdateCamera(fovY, kNearPlane, farZ);   // 0x140098660 with the eye's near plane
}

int Zoom(int zoomDir) {
    if (zoomDir != 0) g_distance = ZoomDistance(g_distance, zoomDir);
    return 0;
}

float ZoomBy(float steps) {
    if (steps != 0.0f) g_distance = ZoomDistanceBy(g_distance, steps);
    return 0.0f;
}

void DrawScene(Graphics::Renderer& renderer, Video::Context& context, float alpha,
               const Graphics::Scene& scene, PlayerController& pc) {
    // The fit view: the eye's camera (its frustum) moved to ShadowFitPose,
    // over the smoothed feet along the smoothed heading, so the shadow box
    // moves and turns with the view. Active() made the smoothing valid.
    Graphics::Camera fitView(pc.camera);
    const Pose fit = ShadowFitPose(g_smooth.feet, g_smooth.heading);
    const Math::Vector3 eye(fit.eye.x, fit.eye.y, fit.eye.z);
    const Math::Vector3 look(fit.look.x, fit.look.y, fit.look.z);
    fitView.SetCameraToWorld(Math::Transform::FromLookAt(eye, look).AsTransform3());

    Graphics::SunLight::_extShadowFitView = &fitView;
    renderer.Draw(context, alpha, scene, pc.camera);   // 0x180104bb0, the eye's camera
    Graphics::SunLight::_extShadowFitView = nullptr;
}

}  // namespace EyeCamera
}  // namespace Ext
