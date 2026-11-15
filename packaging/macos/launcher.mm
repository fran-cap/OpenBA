// OpenBA.app launcher (Contents/MacOS/OpenBA).
//
// Not part of the engine: a ~200 line stand-alone program, built by tools/macos/make_app.sh.
// The engine looks for WinData/ and writes UserData/ and its logs beside its own executable
// path (src/platform/darwin/file_darwin.cpp, ExecutablePath). A signed .app must stay
// read-only and must not contain game data, so this launcher:
//   1. finds the user's own Banished WinData folder (lookup order below, READ-ONLY use),
//   2. builds ~/Library/Application Support/OpenBA/Run/ holding two symlinks
//      (WinData -> the user's folder, OpenBA -> the engine inside this bundle),
//   3. chdir()s there and execv()s that symlink. Logs therefore live in
//      ~/Library/Application Support/OpenBA/Run/ ; saves and settings.cfg under ~/Documents/Banished/
//      (src/platform/darwin/settings_darwin.cpp); nothing is copied, nothing is written
//      to the game folder.
// Lookup order for the game folder (first one that contains data0.pkg wins):
//   a. argument  --game-dir <Banished or WinData folder>
//   b. environment BANISHED_DIR (same as the dev scripts use; a launcher, not engine code)
//   c. the remembered choice  ~/Library/Application Support/OpenBA/gamedir.txt
//   d. well-known install places (Steam for Mac, Porting Kit, Whisky, CrossOver, Wineskin, ~/.wine)
//   e. a folder picker (NSOpenPanel); the choice is remembered in gamedir.txt.
#import <Cocoa/Cocoa.h>
#include <glob.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <vector>

static bool IsDir(const std::string& p) {
    struct stat s;
    return stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
}
static bool IsFile(const std::string& p) {
    struct stat s;
    return stat(p.c_str(), &s) == 0 && S_ISREG(s.st_mode);
}

// Accepts the WinData folder or its parent (the Banished folder). Returns the WinData path.
static std::string ResolveWinData(std::string p) {
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    if (IsFile(p + "/data0.pkg")) return p;
    if (IsFile(p + "/WinData/data0.pkg")) return p + "/WinData";
    return "";
}

static void MkdirP(const std::string& p) {
    for (size_t i = 1; i <= p.size(); ++i)
        if (i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0755);
}

static std::string ReadLine(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char buf[4096] = { 0 };
    if (!fgets(buf, sizeof buf, f)) buf[0] = 0;
    fclose(f);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

static void Fail(NSString* msg) {
    NSLog(@"OpenBA: %@", msg);
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"OpenBA cannot start";
    a.informativeText = msg;
    [a runModal];
    exit(1);
}

static std::string FindDefault(const std::string& home) {
    const std::string tails[] = {
        "/drive_c/Program Files (x86)/Steam/steamapps/common/Banished",
        "/drive_c/Program Files/Steam/steamapps/common/Banished",
        "/drive_c/GOG Games/Banished",
        "/drive_c/Program Files (x86)/GOG Galaxy/Games/Banished"
    };
    std::vector<std::string> pats;
    pats.push_back(home + "/Library/Application Support/Steam/steamapps/common/Banished");
    const std::string prefixRoots[] = {
        home + "/.wine", home + "/.local/share/wineprefixes/*",
        home + "/Library/Application Support/CrossOver/Bottles/*",
        home + "/Library/Containers/com.isaacmarovitz.Whisky/Bottles/*",
        home + "/Library/Application Support/com.isaacmarovitz.Whisky/Bottles/*",
        home + "/Library/Application Support/Porting Kit/Ports/*/Contents/SharedSupport/prefix",
        home + "/Library/Application Support/Kegworks/*",
        home + "/Desktop/Porting Kit/Ports/*/Contents/SharedSupport/prefix",
        home + "/Applications/Porting Kit/Ports/*/Contents/SharedSupport/prefix",
        "/Applications/Porting Kit/Ports/*/Contents/SharedSupport/prefix",
        home + "/Applications/Wineskin/*/Contents/SharedSupport/prefix",
        home + "/Applications/*/Contents/SharedSupport/prefix",
        "/Applications/*/Contents/SharedSupport/prefix"
    };
    for (const auto& r : prefixRoots)
        for (const auto& t : tails)
            pats.push_back(r + t);
    for (const auto& pat : pats) {
        glob_t* g = (glob_t*)calloc(1, sizeof(glob_t));   // calloc: ARC deletes glob_t's ctor
        if (glob(pat.c_str(), 0, nullptr, g) == 0) {
            for (size_t i = 0; i < g->gl_pathc; ++i) {
                std::string w = ResolveWinData(g->gl_pathv[i]);
                if (!w.empty()) {
                    globfree(g);
                    free(g);
                    return w;
                }
            }
            globfree(g);
        }
        free(g);
    }
    return "";
}

static std::string Pick() {
    for (;;) {
        [NSApp activateIgnoringOtherApps:YES];
        NSOpenPanel* p = [NSOpenPanel openPanel];
        p.title = @"Where is your copy of Banished?";
        p.message = @"Choose the Banished folder (the one that contains WinData), or the WinData folder itself. OpenBA only reads from it.";
        p.prompt = @"Use this folder";
        p.canChooseDirectories = YES;
        p.canChooseFiles = NO;
        p.allowsMultipleSelection = NO;
        p.showsHiddenFiles = NO;
        if ([p runModal] != NSModalResponseOK) return "";
        std::string w = ResolveWinData(p.URL.path.UTF8String);
        if (!w.empty()) return w;
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"That folder does not contain Banished game data";
        a.informativeText = @"Expected a WinData folder containing data0.pkg. Try again.";
        [a runModal];
    }
}

static void Relink(const std::string& link, const std::string& target) {
    char cur[4096];
    ssize_t n = readlink(link.c_str(), cur, sizeof cur - 1);
    if (n >= 0 && std::string(cur, n) == target) return;
    unlink(link.c_str());
    if (symlink(target.c_str(), link.c_str()) != 0) Fail([NSString stringWithFormat:@"Cannot create %s", link.c_str()]);
}

int main(int argc, char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        const std::string home = NSHomeDirectory().UTF8String;
        // Test-only: --run-dir <dir> keeps EVERYTHING (Run folder, remembered game folder, saves
        // and settings via USERPROFILE, shader cache via TMPDIR) under <dir>; the real home is untouched.
        std::string testDir;
        for (int i = 1; i + 1 < argc; ++i)
            if (std::string(argv[i]) == "--run-dir") testDir = argv[i + 1];
        std::string support = home + "/Library/Application Support/OpenBA";
        if (!testDir.empty()) {
            support = testDir + "/support";
            MkdirP(testDir + "/home/Documents");
            MkdirP(testDir + "/tmp");
            setenv("USERPROFILE", (testDir + "/home").c_str(), 1);
            setenv("TMPDIR", (testDir + "/tmp/").c_str(), 1);
        }
        const std::string run = support + "/Run";
        MkdirP(run);

        std::string winData;
        std::vector<char*> passthrough{ argv[0] };
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--game-dir") {
                if (i + 1 == argc) Fail(@"--game-dir requires a path.");
                const char* path = argv[++i];
                winData = ResolveWinData(path);
                if (winData.empty()) Fail([NSString stringWithFormat:@"No Banished data at --game-dir path: %s", path]);
            } else if (std::string(argv[i]) == "--run-dir" && i + 1 < argc)
                ++i;
            else if (std::string(argv[i]).rfind("-psn_", 0) == 0)
                continue;  // Finder launch token
            else
                passthrough.push_back(argv[i]);
        }
        NSString* override = [NSProcessInfo processInfo].environment[@"BANISHED_DIR"];
        if (winData.empty() && override.length) {
            winData = ResolveWinData(override.UTF8String);
            if (winData.empty()) Fail([NSString stringWithFormat:@"No Banished data at BANISHED_DIR path: %@", override]);
        }
        if (winData.empty()) winData = ResolveWinData(ReadLine(support + "/gamedir.txt"));
        if (winData.empty()) winData = FindDefault(home);
        if (winData.empty()) {
            winData = Pick();
            if (winData.empty()) Fail(@"No Banished game data was chosen. OpenBA needs your own copy of the game.");
            // Only an explicit folder-picker choice becomes the permanent preference.
            const std::string preference = support + "/gamedir.txt";
            if (FILE* f = fopen(preference.c_str(), "w")) {
                const bool writeFailed = fprintf(f, "%s\n", winData.c_str()) < 0;
                const bool closeFailed = fclose(f) != 0;
                if (writeFailed || closeFailed) {
                    const int writeError = errno;
                    NSLog(@"OpenBA: Cannot write %s: %s", preference.c_str(), strerror(writeError));
                    fprintf(stderr, "OpenBA: Cannot write %s: %s\n", preference.c_str(), strerror(writeError));
                }
            } else {
                const int writeError = errno;
                NSLog(@"OpenBA: Cannot write %s: %s", preference.c_str(), strerror(writeError));
                fprintf(stderr, "OpenBA: Cannot write %s: %s\n", preference.c_str(), strerror(writeError));
            }
        }

        char self[4096];
        uint32_t sz = sizeof self;
        if (_NSGetExecutablePath(self, &sz) != 0) Fail(@"Cannot locate the app bundle.");
        char real[4096];
        if (!realpath(self, real)) Fail(@"Cannot resolve the app bundle path.");
        std::string dir = real;
        dir.resize(dir.find_last_of('/'));
        const std::string engine = dir + "/OpenBA-engine";
        if (!IsFile(engine)) Fail(@"The app bundle is damaged (engine missing). Rebuild with tools/macos/make_app.sh.");

        Relink(run + "/WinData", winData);
        Relink(run + "/OpenBA", engine);
        // The shader converter ships inside the bundle (Contents/Frameworks).
        const std::string conv = dir + "/../Frameworks/libairconv.dylib";
        if (!IsFile(conv)) Fail(@"The app bundle is damaged (shader converter missing). Rebuild with tools/macos/make_app.sh.");
        setenv("BANISHED_AIRCONV", conv.c_str(), 0);
        if (chdir(run.c_str()) != 0) Fail(@"Cannot enter the OpenBA data folder.");
        passthrough.push_back(nullptr);
        execv((run + "/OpenBA").c_str(), passthrough.data());
        Fail([NSString stringWithFormat:@"Cannot start the engine: %s", strerror(errno)]);
    }
    return 1;
}
