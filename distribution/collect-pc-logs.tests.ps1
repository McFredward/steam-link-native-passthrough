# Fixture inputs only. No local Steam/Frame runtime is inspected or modified.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
Add-Type -AssemblyName System.IO.Compression.FileSystem
$collector = Join-Path $PSScriptRoot 'collect-pc-logs.ps1'
$temporary = Join-Path ([IO.Path]::GetTempPath()) ("sfnp diagnostic 'fixture " + [Guid]::NewGuid().ToString('N'))
$steamFixture = Join-Path $temporary 'Steam with spaces'
$ventureFixture = Join-Path $temporary 'Venture'
$openvrFixture = Join-Path $temporary 'openvrpaths.vrpath'
$archive = Join-Path $temporary 'output\pc-logs.zip'
$utf8 = New-Object Text.UTF8Encoding $false
$script:checks = 0
function Assert-True($Value, [string]$Message) {
    if (-not $Value) { throw $Message }
    $script:checks++
}
function Write-Text([string]$Path, [string]$Text) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, $Text, $utf8)
}
function Read-Zip([string]$Path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $result = @{}
        foreach ($entry in $zip.Entries) {
            $stream = $entry.Open(); $reader = New-Object IO.StreamReader $stream
            try { $result[$entry.FullName.Replace('\', '/')] = $reader.ReadToEnd() }
            finally { $reader.Dispose() }
        }
        return $result
    } finally { $zip.Dispose() }
}
function Collect-Fixture {
    & $collector -SteamPath $steamFixture -VentureDataPath $ventureFixture -OpenVrPathsFile $openvrFixture -OutputPath $archive -MaxLogBytes 1024
}
function Assert-Clean {
    Assert-True (@(Get-ChildItem -LiteralPath (Split-Path $archive) -Filter '.pc-log-staging-*' -Force).Count -eq 0) 'Temporary collection files leaked'
}
try {
    [void][IO.Directory]::CreateDirectory($temporary)
    $tokens = $null; $errors = $null
    [void][Management.Automation.Language.Parser]::ParseFile($collector, [ref]$tokens, [ref]$errors)
    Assert-True ($errors.Count -eq 0) 'Collector does not parse in this PowerShell'
    $driver = Join-Path $steamFixture 'logs\driver_vrlink.txt'
    $driverText = ('early-data' * 400) + "`nLink BW: Est 100 automatic=0`nTHROTTLE EVENT fixture`n"
    Write-Text $driver $driverText
    Write-Text (Join-Path $steamFixture 'logs\vrserver.txt') 'server fixture'
    $settingsPath = Join-Path $steamFixture 'config\steamvr.vrsettings'
    Write-Text $settingsPath '{"driver_vrlink":{"automaticBandwidth":false,"targetBandwidth":100,"streamFormatWidth":1152,"unrelatedSecret":"DO-NOT-COPY"},"account":{"token":"DO-NOT-COPY"}}'
    Write-Text (Join-Path $ventureFixture 'meta-session.dpapi') 'DO-NOT-COPY'
    Write-Text (Join-Path $ventureFixture 'logs\launcher.log') 'launcher fixture'
    Write-Text (Join-Path $ventureFixture 'logs\runtime-game.log') 'current game fixture'
    Write-Text (Join-Path $ventureFixture 'logs\runtime-game.console.log') 'console fixture'
    Write-Text (Join-Path $ventureFixture 'logs\runtime-game.prev.log') 'DO-NOT-COPY'
    $beforeDriver = (Get-FileHash -LiteralPath $driver).Hash
    $beforeSettings = (Get-FileHash -LiteralPath $settingsPath).Hash
    Collect-Fixture
    $files = Read-Zip $archive
    $manifest = $files['diagnostics.json'] | ConvertFrom-Json
    Assert-True ($manifest.schema -eq 'sfnp-pc-diagnostics-v1') 'Missing diagnostic manifest'
    Assert-True $manifest.driver_log_found 'Native link log omitted'
    Assert-True ($files['steam/driver_vrlink.txt'] -eq $driverText.Substring($driverText.Length - 1024)) 'Incorrect or unbounded log tail'
    $record = @($manifest.sources | Where-Object { $_.member -eq 'steam/driver_vrlink.txt' })[0]
    Assert-True ($record.bytes -eq 1024 -and $record.offset -eq $driverText.Length - 1024) 'Tail provenance does not match source bytes'
    Assert-True ($record.sha256.Length -eq 64) 'Missing archive member hash'
    Assert-True ($files['venture/runtime-game.log'] -eq 'current game fixture') 'Current Venture game log omitted'
    Assert-True $files.ContainsKey('venture/runtime-game.console.log') 'Venture console log omitted'
    Assert-True (-not $files.ContainsKey('venture/runtime-game.prev.log')) 'Historical game log included unexpectedly'
    Assert-True (-not (($files.Values -join '') -match 'DO-NOT-COPY')) 'Credentials or unselected settings were collected'
    Assert-True ($manifest.settings.driver_vrlink.automaticBandwidth -eq $false -and $manifest.settings.driver_vrlink.targetBandwidth -eq 100) 'Automatic/manual configuration lost'
    Assert-True (@($manifest.settings.driver_vrlink.PSObject.Properties).Count -eq 3) 'Settings whitelist changed'
    Assert-True ((Get-FileHash -LiteralPath $driver).Hash -eq $beforeDriver -and (Get-FileHash -LiteralPath $settingsPath).Hash -eq $beforeSettings) 'Read-only inputs changed'
    Assert-Clean
    Write-Text $driver 'second snapshot'
    Collect-Fixture
    Assert-True ((Read-Zip $archive)['steam/driver_vrlink.txt'] -eq 'second snapshot') 'Existing archive was not replaced'
    Assert-Clean
    Remove-Item -LiteralPath $driver
    $alternate = Join-Path $temporary 'Alternate OpenVR logs'
    Write-Text (Join-Path $alternate 'driver_vrlink.txt') 'alternate link fixture'
    Write-Text $openvrFixture (@{ log = @($alternate) } | ConvertTo-Json)
    Collect-Fixture
    $files = Read-Zip $archive; $manifest = $files['diagnostics.json'] | ConvertFrom-Json
    Assert-True ($manifest.driver_log_found -and $files['openvr-0/driver_vrlink.txt'] -eq 'alternate link fixture') 'Configured OpenVR log location ignored'
    Remove-Item -LiteralPath $openvrFixture
    Write-Text $settingsPath 'invalid json'
    Collect-Fixture
    $files = Read-Zip $archive; $manifest = $files['diagnostics.json'] | ConvertFrom-Json
    Assert-True (-not $manifest.driver_log_found -and $manifest.settings.status -eq 'error') 'Missing driver or malformed settings concealed'
    Assert-True ($files['steam/vrserver.txt'] -eq 'server fixture') 'Missing driver prevented other logs from being saved'
    Assert-Clean
    $sentinel = Join-Path $temporary 'never-overwrite.txt'; Write-Text $sentinel 'sentinel'
    $rejected = $false
    try { & $collector -SteamPath $steamFixture -VentureDataPath $ventureFixture -OpenVrPathsFile $openvrFixture -OutputPath $sentinel } catch { $rejected = $true }
    Assert-True ($rejected -and [IO.File]::ReadAllText($sentinel) -eq 'sentinel') 'Non-ZIP target was overwritten'
    $beforeArchive = (Get-FileHash -LiteralPath $archive).Hash
    $locked = [IO.File]::Open($archive, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    $rejected = $false
    try { Collect-Fixture } catch { $rejected = $true } finally { $locked.Dispose() }
    Assert-True ($rejected -and (Get-FileHash -LiteralPath $archive).Hash -eq $beforeArchive) 'Failed publication lost the previous archive'
    Assert-Clean
    # Invoke the same downloaded text form, with explicitly quoted fixture paths.
    $text = [IO.File]::ReadAllText($collector)
    $invocation = '& {' + "`n" + $text + "`n}"
    foreach ($pair in @(@('SteamPath', $steamFixture), @('VentureDataPath', $ventureFixture), @('OpenVrPathsFile', $openvrFixture), @('OutputPath', $archive))) {
        $invocation += " -" + $pair[0] + " '" + $pair[1].Replace("'", "''") + "'"
    }
    Invoke-Expression $invocation
    Assert-True ((Read-Zip $archive).ContainsKey('diagnostics.json')) 'Downloaded text entry failed'
    Assert-Clean
    Write-Host ("PC log collection: " + $script:checks + ' checks PASS on PowerShell ' + $PSVersionTable.PSVersion)
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
