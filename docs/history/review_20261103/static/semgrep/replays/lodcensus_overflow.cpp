// Replays src/diag/scene_watch.cpp:717-721 (the [LodCensus] level line): 8 levels whose
// distances are the absurd values the census itself tests for (> 1.0e6f, line 725).
#include <cstdio>
int main() {
    float dist[8] = {1e30f, 2e30f, 3e30f, 4e30f, 5e30f, 6e30f, 7e30f, 8e30f};
    int tris[8] = {123456, 65432, 32100, 16000, 8000, 4000, 2000, 1000};
    void* vb = (void*)0x7f12a3b4c5d0ull; void* ib = (void*)0x7f12a3b4c5e0ull;
    char buf[512]; int off = 0;
    for (int k = 0; k < 8; ++k) {
        off += std::snprintf(buf + off, sizeof(buf) - off, "%sL%d{d=%.1f tris=%d vb=%p ib=%p}",
                             k ? " " : "", k, dist[k], tris[k], vb, ib);
        std::printf("k=%d off=%d\n", k, off);
    }
    std::printf("%.20s\n", buf);
}
