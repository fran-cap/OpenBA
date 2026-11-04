// Replays src/render/vulkan/async_gpu_create/async_create.cpp:41-66 (and the dx11 twin,
// async_create.cpp:70-73 / 120-124): namespace-scope mutex / condition_variable / deque, the
// workers detached and parked in g_workCv.wait, then the process returns from main.
// exit() runs ~condition_variable / ~mutex / ~deque while the workers still wait on them.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace {
std::mutex g_mutex;
std::condition_variable g_workCv;
std::deque<std::function<void()>> g_queue;

void WorkerMain() {
    for (;;) {
        std::function<void()> j;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_workCv.wait(lock, [] { return !g_queue.empty(); });
            j = std::move(g_queue.front());
            g_queue.pop_front();
        }
        j();
    }
}
}  // namespace

int main() {
    for (int i = 0; i < 2; ++i) std::thread(WorkerMain).detach();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.push_back([] { std::puts("job ran"); });
    }
    g_workCv.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));   // both workers parked again
    std::puts("main returns; static destructors run with parked waiters");
    return 0;
}
