$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
Add-Type -AssemblyName System.IO.Compression.FileSystem
$probeRepo = Split-Path -Parent $PSScriptRoot
$global:sfnpProbeArchiveFixture = Join-Path $probeRepo 'distribution/nvenc-quality-probe.zip'
$probeEntry = Join-Path $probeRepo 'tools/test-mask-encoder.ps1'
if (!(Test-Path -LiteralPath $global:sfnpProbeArchiveFixture) -or !(Test-Path -LiteralPath $probeEntry)) { throw 'Published probe/entry required for fixture test.' }
$global:sfnpProbeTamperFixture = $false
function Invoke-WebRequest {
    param([switch]$UseBasicParsing, [string]$Uri, [string]$OutFile, [int]$TimeoutSec)
    Copy-Item -LiteralPath $global:sfnpProbeArchiveFixture -Destination $OutFile
    if ($global:sfnpProbeTamperFixture) { [IO.File]::AppendAllText($OutFile, 'tampered') }
}
$probeRoot = Join-Path $env:LOCALAPPDATA 'steam-link-native-passthrough'
$probeZip = Join-Path $probeRoot 'encoder-probe-results.zip'
$probeCaught = $null
try { & $probeEntry } catch { $probeCaught = $_.Exception.Message }
if (!(Test-Path -LiteralPath $probeZip)) { throw 'Native failure/success did not retain result archive.' }
$archive = [IO.Compression.ZipFile]::OpenRead($probeZip)
try {
    $names = @($archive.Entries | ForEach-Object { $_.FullName })
    foreach ($name in 'stdout.log','stderr.log','probe-status.json','probe-manifest.json') {
        if ($names -notcontains $name) { throw ('Native execution log was not captured: ' + $name) }
    }
    $reader = New-Object IO.StreamReader($archive.GetEntry('probe-status.json').Open())
    try { $status = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    if ($status.schema -cne 'sfnp-nvenc-probe-run-v1' -or $status.timed_out -or $null -eq $status.exit_code) { throw 'Native process status was not retained.' }
    if ($status.exit_code -ne 0 -and !$probeCaught) { throw 'Native failure was silently swallowed.' }
} finally { $archive.Dispose() }
foreach ($name in 'sfnp-nvenc-quality-probe.exe','encoder-probe-download.zip') {
    if (Test-Path -LiteralPath (Join-Path $probeRoot $name)) { throw 'Downloaded executable/archive was not cleaned.' }
}
$global:sfnpProbeTamperFixture = $true; $probeCaught = $null
try { & $probeEntry } catch { $probeCaught = $_.Exception.Message }
if (!$probeCaught -or $probeCaught -notmatch 'Pruefsumme|Groesse') { throw 'Tampered download was not refused before execution.' }
$archive = [IO.Compression.ZipFile]::OpenRead($probeZip)
try {
    $reader = New-Object IO.StreamReader($archive.GetEntry('probe-status.json').Open())
    try { $status = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    if ($null -ne $status.exit_code) { throw 'Tampered download launched a process.' }
} finally { $archive.Dispose() }
Write-Host 'Actual packaged binary, native error logs, cleanup and tampered download: PASS'
