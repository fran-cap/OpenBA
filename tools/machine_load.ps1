# tools/machine_load.ps1 -- what the perf harness could never say about the machine.
#
# WAVE 22 LANE ALLOC residual 4, closed by WAVE 25 LANE ALLOC.
#
# `standalone_stress.csv` records everything about the game and NOTHING about the box
# it ran on. Two sessions of Wave 22's allocator A/B were destroyed by sibling lanes
# (`find /` processes pinning three of eight cores; a `-j4` build; ten lanes opening)
# and the harness never said a word -- a reader of report.txt alone could not tell a
# regression from a neighbour. The contamination was ~130% of a ~1% treatment.
#
# Prints ONE labelled block: system CPU load, logical core count, process count, and
# the processes that actually burned CPU DURING the sample window (a delta, not the
# cumulative .CPU column -- a long-lived idle browser tops the cumulative list and is
# consuming nothing).
#
#   powershell -NoProfile -File tools/machine_load.ps1 [-Seconds 2] [-Label before]
#
# Deliberately NOT a verdict: it states what it examined and leaves "was this run
# contaminated?" to the reader, because the threshold depends on the measurement.

param(
    [double]$Seconds = 2.0,
    [string]$Label = ""
)

$ErrorActionPreference = "SilentlyContinue"

$cpu = Get-CimInstance Win32_Processor
$cores = ($cpu | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum

# Per-process CPU delta across the window. Cumulative .CPU is seconds of CPU time
# since process start, so (after-before)/elapsed/cores is the share of the box each
# process took while we watched.
$before = @{}
foreach ($p in Get-Process) { if ($p.CPU -ne $null) { $before[$p.Id] = $p.CPU } }
$t0 = Get-Date
Start-Sleep -Seconds $Seconds
$t1 = Get-Date
$elapsed = ($t1 - $t0).TotalSeconds

$rows = @()
foreach ($p in Get-Process) {
    if ($p.CPU -eq $null) { continue }
    $prev = 0.0
    if ($before.ContainsKey($p.Id)) { $prev = $before[$p.Id] }
    $d = $p.CPU - $prev
    if ($d -gt 0.05) {
        $rows += [pscustomobject]@{
            Name = $p.ProcessName
            Id   = $p.Id
            Pct  = [math]::Round(100.0 * $d / $elapsed / $cores, 1)   # share of the whole box
            Core = [math]::Round($d / $elapsed, 2)                    # cores' worth
        }
    }
}
$rows = $rows | Sort-Object -Property Core -Descending

# Busy = the sum over every process listed above; none is excluded. The game itself is
# listed but named, so a reader can subtract it.
$busySum = 0.0
foreach ($r in $rows) { $busySum += $r.Core }

# TWO SOURCES, AND THEY DISAGREE BY ~4x ON THIS BOX (measured, Wave 25 lane ALLOC):
#   \Processor(_Total)\% Processor Time    6.3 / 8.5 / 12.0 %
#   Win32_Processor.LoadPercentage          32 / 40 / 46 %
# on three back-to-back samples of the same idle-ish box. The perf counter is the one
# that CORROBORATES the independent per-process delta above (0.76 busy cores of 16 =
# 4.8%); the CIM property does not, so it is printed as a labelled secondary and MUST
# NOT be quoted as the load. Both are printed rather than one silently chosen, because
# a single number here is exactly the kind of unattributed figure this tool exists to
# stop.
$counter = (Get-Counter '\Processor(_Total)\% Processor Time' -ErrorAction SilentlyContinue).CounterSamples[0].CookedValue
$loads = @()
foreach ($c in $cpu) { $loads += $c.LoadPercentage }
$cimLoad = ($loads | Measure-Object -Average).Average

"[MachineLoad]$(if ($Label) { " label=$Label" }) at $($t1.ToString('yyyy-MM-dd HH:mm:ss'))"
"[MachineLoad] cpu={0}  logical_cores={1}  processes={2}  window={3}s" -f `
    $cpu[0].Name.Trim(), $cores, (Get-Process).Count, $elapsed
"[MachineLoad] load={0:N1}%  (\Processor(_Total)\% Processor Time -- THE figure)" -f $counter
"[MachineLoad] cim_loadpercentage={0}%  (Win32_Processor -- unreliable here, ~4x high; do not quote)" -f [math]::Round($cimLoad,0)
"[MachineLoad] busy_cores_during_window={0:N2} of {1}" -f $busySum, $cores
"[MachineLoad] top consumers during the window (cores' worth, share of box):"
if ($rows.Count -eq 0) {
    "[MachineLoad]   (none above 0.05s CPU -- box idle)"
} else {
    foreach ($r in ($rows | Select-Object -First 10)) {
        "[MachineLoad]   {0,-28} pid={1,-7} {2,5:N2} cores  {3,5:N1}% of box" -f $r.Name, $r.Id, $r.Core, $r.Pct
    }
}
