"""android_capture.py <serial> <wait_s> <intent args...>

The RenderDoc half of android_capture.sh (run that, not this): launch org.openba.game
under RenderDoc on an attached device, wait <wait_s> seconds, trigger one capture from
the host and print its path on the device as `capture: <path>` (the shell pulls it with
adb: RemoteServer.CopyCaptureFromRemote returned without writing the file on RenderDoc
1.44). The intent args are am-start extras (-e KEY VALUE ...), as tools/android.sh run
passes them.
"""
import os, sys, time
mp = os.environ.get("RENDERDOC_MODULE_PATH", "")
if mp:
    sys.path.insert(0, mp)
    os.add_dll_directory(mp)
import renderdoc as rd

serial, wait_s = sys.argv[1], float(sys.argv[2])
intent = " ".join(sys.argv[3:])

rd.InitialiseReplay(rd.GlobalEnvironment(), [])
dpc = rd.GetDeviceProtocolController("adb")
dpc.GetDevices()
url = dpc.GetProtocolName() + "://" + serial
if not dpc.IsSupported(url):
    sys.exit(f"RenderDoc does not support {url}")
print("StartRemoteServer:", dpc.StartRemoteServer(url), flush=True)

# The server apk takes a few seconds to listen after a (re)start: retry the connection.
remote = None
res = None
for _ in range(30):
    res, remote = rd.CreateRemoteServerConnection(url)
    if res == rd.ResultCode.Succeeded:
        break
    time.sleep(2)
if remote is None or res != rd.ResultCode.Succeeded:
    sys.exit(f"no RenderDoc remote server on {url}: {res}")

opts = rd.GetDefaultCaptureOptions()
opts.captureCallstacks = False
opts.refAllResources = False
app = "org.openba.game/org.openba.game.OpenBAActivity"
er = remote.ExecuteAndInject(app, "", intent, [], opts)
print("ExecuteAndInject:", er.result, "ident:", er.ident, flush=True)
if er.result != rd.ResultCode.Succeeded or er.ident == 0:
    sys.exit("injection failed")

target = None
for _ in range(60):
    target = rd.CreateTargetControl(url, er.ident, "openba-capture", True)
    if target is not None:
        break
    time.sleep(1)
if target is None:
    sys.exit("no target control connection")

t_end = time.time() + wait_s
while time.time() < t_end:
    msg = target.ReceiveMessage(None)
    if msg.type == rd.TargetControlMessageType.Disconnected:
        sys.exit("the game disconnected while waiting (crashed or quit)")
    time.sleep(0.5)

# The API must be Vulkan: OpenGLES means RenderDoc hooked hwui's GL context instead
# (debug.hwui.renderer=skiagl was set), and the capture would be the Android UI.
api = target.GetAPI()
print("API:", api, flush=True)
if api != "Vulkan":
    sys.exit(f"RenderDoc hooked {api!r}, not the game's Vulkan device")

target.TriggerCapture(1)
cap = None
t_end = time.time() + 180
while time.time() < t_end:
    msg = target.ReceiveMessage(None)
    if msg.type == rd.TargetControlMessageType.NewCapture:
        cap = msg.newCapture
        break
    if msg.type == rd.TargetControlMessageType.Disconnected:
        sys.exit("the game disconnected before the capture arrived")
    time.sleep(0.2)
if cap is None:
    sys.exit("no capture within 180 s")
print("frame:", cap.frameNumber, flush=True)
print("capture:", cap.path, flush=True)
target.Shutdown()
remote.ShutdownConnection()
rd.ShutdownReplay()
