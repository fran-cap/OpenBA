---
name: ghidra
description: Ghidra + GhidraMCP for this project - checking the bridge is up, starting it, reading retail bodies with the mcp__ghidra-mcp__* tools (program= per call, never switch_program, the disassembly decides argument counts), the objdump fallback, naming conventions for renaming functions/vtables/globals, and what belongs in Ghidra versus markdown. Load it BEFORE the first mcp__ghidra-mcp__* call of a session and before writing a Ghidra brief for an agent. Use when the user says "ghidra is open", "ghidra is up", "ghidra is back up if you want to check", when a task needs to verify a retail address or read what retail does at 0x140... / 0x180..., when reviewing a PR's retail-address claims, before renaming or annotating anything in Ghidra, or when the Ghidra MCP bridge needs starting.
---

# Ghidra

## Ghidra is the primary documentation tool
Function renames, type annotations, struct layouts and per-function comments go directly
in Ghidra: persistent, searchable, in sync with the analysis. Markdown is for what does
not fit there (architecture, algorithm analysis).

## Setup
- Ghidra 12.0.3 at `C:\tools\ghidra_12.0.3_PUBLIC`; JAVA_HOME `C:\Program Files\Java\jdk-24`
- The project holds 7 programs, including `Runtime-x64-profile.dll` (ToolKit build, debug
  strings + 5,182 exports) — the primary source for naming unexported Runtime internals
- GhidraMCP (bethington/ghidra-mcp v3.0.0, 179 tools): bridge
  `tools/ghidra_mcp/bridge_mcp_ghidra.py`, Python 3.10 venv `tools/ghidra_mcp/.venv/`,
  extension `tools/ghidra_mcp/GhidraMCP-3.0.0.zip`
- Startup steps: `docs/reference/HOW_TO_START_GHIDRA.md`

## Is it up? Start it only if not
1. Load the tools first: `ToolSearch "select:mcp__ghidra-mcp__check_connection,mcp__ghidra-mcp__decompile_function,mcp__ghidra-mcp__disassemble_function"` (plus whatever else the task needs).
2. `check_connection`. `tasklist` shows no `java.exe` while Ghidra is up, so it is no test;
   launching a second instance has happened that way.
3. Down, and the user did not say to leave it: from the MAIN checkout's root (not a worktree),
   `nohup "C:/tools/ghidra_12.0.3_PUBLIC/ghidraRun.bat" Banished_RE.gpr &`. It restores the
   last CodeBrowser and the bridge answers on `127.0.0.1:8089` in about a minute; poll
   `check_connection`. Agents in a team never start it: the coordinator says whether it is up.
4. Never kill `python` or `java` by image name: the Ghidra and RenderDoc MCP bridges are
   python processes, and an image-name kill has taken both down mid-session.

## Reading retail
- **Pass `program=` on every call**: `"Application-x64.exe"` for `0x140...`,
  `"Runtime-x64.dll"` for `0x180...` (`list_open_programs` shows what is open). **Never
  `switch_program` or `open_program`**: the active program is global to the one Ghidra
  instance, so a switch changes every other agent's answers under it.
- Address-keyed tools (`get_function_by_address`, `disassemble_function(address=)`,
  `decompile_function(address=)`) for anything acted on. `get_function_metrics` ignores
  `address=` and `get_function_callees` resolves by bare name; names collide (185 `Serialize`),
  so they answer confidently about a different function.
- **The disassembly decides argument counts, pointer depth and sign.** The decompiler drops
  arguments at indirect / vtable calls and at register-passthrough forwarders, and an argument
  register can be set before a branch that skips to the call: walk each argument register's
  live range back from the CALL. Before transcribing a call into a slot the port already
  implements, read what our side says its arguments are.
- The retail exe is stripped: a Ghidra name there is an analyst's label, not a retail name.
  Real names come from the profile builds (`Runtime-x64-profile.dll`, `Application-x64-profile.exe`).
- Bulk work (every xref, a census) goes through `run_script_inline` with a Java `GhidraScript`
  writing to the scratchpad; a result over the tool-result limit is saved to a file to parse,
  never read back whole.
- **Ghidra down and not to be started**: disassemble the retail binary in the install with
  `objdump -d --start-address=0x... --stop-address=0x... "C:/GOG Games/Banished/Runtime-x64.dll"`
  (or `Application-x64.exe`); objdump reads PE64 fine. Say in the result which one was used.

## Naming conventions
- Functions: `Namespace_ClassName_MethodName` (e.g. `Engine_MainLoop_Run`)
- Vtables: `vftable_ClassName` or `vtable_Namespace_ClassName`
- Globals: `g_Namespace_variableName`
- Components: keep the full component name (e.g. `CitizenAIComponent_Update`)

## No archaeology needed for import slots
The exe's `0x14015xxxx` dispatch slots are its PE import table; get a slot's mangled name
from Ghidra's imports (`list_imports` / `get_external_location`) or
`objdump -p "C:/GOG Games/Banished/Application-x64.exe"`. (`tools/dispatch_slot_map.py` was
removed with the last slot row.)

Binary facts (bases, sizes, export counts): `docs/reference/binaries.md`.
