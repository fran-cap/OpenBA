// src/ext/material_setup.cpp -- material_setup.h
//
// NOT RETAIL.

#include "ext/material_setup.h"

#include "graphics/Material.h"

#include <atomic>

namespace Ext {
namespace MaterialSetup {

namespace {

// Read by Both on the drawing thread while the main thread may change them: atomics.
std::atomic<Fn> g_blossom{nullptr};
std::atomic<Fn> g_clouds{nullptr};

void Both(const Graphics::MaterialInstance& instance, Video::Context& context) {
    if (Fn f = g_blossom.load(std::memory_order_relaxed)) f(instance, context);
    if (Fn f = g_clouds.load(std::memory_order_relaxed)) f(instance, context);
}

void Install() {
    const Fn blossom = g_blossom.load(std::memory_order_relaxed);
    const Fn clouds = g_clouds.load(std::memory_order_relaxed);
    if (blossom != nullptr && clouds != nullptr) {
        Graphics::MaterialInstance::_extAfterSetup = &Both;
    } else {
        Graphics::MaterialInstance::_extAfterSetup = blossom != nullptr ? blossom : clouds;
    }
}

}  // namespace

void SetBlossom(Fn fn) {
    g_blossom.store(fn, std::memory_order_relaxed);
    Install();
}

void SetClouds(Fn fn) {
    g_clouds.store(fn, std::memory_order_relaxed);
    Install();
}

}  // namespace MaterialSetup
}  // namespace Ext
