#pragma once
// ============================================================================
// src/ext/town_screenshot/town_screenshot.h -- ext.town_screenshot (class B, NOT RETAIL)
// ============================================================================
// docs/extensions/handins/TOWNSCREENSHOT.md. One picture of the whole map from an isometric view, taken from
// the Town Screenshot page of the extension window. While a screenshot is taken the game is paused and the
// scene is drawn through an orthographic camera that visits the map a screen at a time (screenshot_math.h);
// each screen is read back, the middles are stitched into one picture and it is written as
// Screenshots\TownScreenshot<N>.png, with the corners outside the map transparent. The player's own camera,
// the pause and the pipelined renderer are put back as they were.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class PlayerController;
namespace Graphics {
class Renderer;
class Scene;
}  // namespace Graphics
namespace Video {
class Context;
}

namespace Ext {
namespace TownScreenshot {

// What the page chose (kept while the game runs; the values are indexes into screenshot_math.h's tables).
struct Choices {
    int quality = 1;   // Draft, Standard, High, Maximum
    int angle = 0;     // classic 30 degrees, true isometric
    int turn = 0;      // the diagonal view and each quarter turn
    bool ignoreWeather = true;   // draw as on a clear day: no rain or snowfall, and none of their lighting
    bool hideSnow = false;       // draw the ground bare even where snow lies on it
};
Choices& Choose();

// Begins a screenshot of the map being played. False, with Status() saying why, when there is no game, a
// screenshot is already being taken, or the view is a citizen's.
bool Start();
// Saves a screenshot of what is on the screen now, the game's own (the request its screenshot key raises, so
// the file is Screenshot<N>.bmp, without the interface). False, with Status() saying why, when there is no
// game or a town screenshot is being taken.
bool TakeViewScreenshot();
// Stops a screenshot being taken and puts everything back.
void Cancel();
// True from Start until the screenshot is written or stopped: GameUpdateLoop::Draw hands the scene to
// DrawScene.
bool Active();
// One line for the page: what is happening, or how the last screenshot ended.
const char* Status();
// Called whenever Status() changes (the page shows it).
void SetStatusHook(void (*hook)());

// Writes `data` as <save directory>\Screenshots\<stem><N>.<extension>, the first N that does not exist (the
// folder the town screenshot uses; other extensions that make a picture put it there too). The full path;
// empty when the file could not be written.
std::string SaveInScreenshots(const char* stem, const char* extension, const void* data, size_t size);
// The same for a picture: `rgba` is width * height * 4 bytes, encoded as a PNG <stem><N>.png. Empty when it
// could not be written.
std::string SavePngInScreenshots(const char* stem, std::vector<uint8_t>& rgba, int width, int height);

// A screenshot leaves out the icons over buildings and citizens and, when asked, the weather: both are options the
// player set (GameSettings::statusIconEnable, showWeather) that the frame's drawing reads. GameUpdateLoop::Draw
// holds one of these for the length of the frame: while it lives and a screenshot is being taken the two options
// read off, and they read the player's values again when it ends, so the Options window, the save of the
// settings and the quit never see the change.
class DrawOptions {
public:
    DrawOptions();
    ~DrawOptions();
    DrawOptions(const DrawOptions&) = delete;
    DrawOptions& operator=(const DrawOptions&) = delete;
private:
    bool m_held = false;
    bool m_hadStatusIcons = false;
    bool m_hadShowWeather = false;
};

// GameUpdateLoop::Draw step 7 while Active(): the scene through the next tile's camera, and the read back of
// the tile once the camera has rested on it. The same Renderer::Draw the game makes, so everything else about
// the frame is the game's own.
void DrawScene(Graphics::Renderer& renderer, Video::Context& context, float alpha, const Graphics::Scene& scene,
               PlayerController& pc);

}  // namespace TownScreenshot
}  // namespace Ext
