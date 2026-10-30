# Read-only diagnostic inputs. No runtime, settings, process or device changes.
[CmdletBinding()]
param(
    [string]$SteamPath,
    [string]$VentureDataPath,
    [string]$OpenVrPathsFile,
    [string]$OutputPath,
    [ValidateRange(1024, 16777216)][int]$MaxLogBytes = 8388608
)

& {
    param($SteamRoot, $VentureRoot, $OpenVrPaths, $ArchivePath, $TailLimit)
    Set-StrictMode -Version 2
    $ErrorActionPreference = 'Stop'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $localData = [Environment]::GetFolderPath('LocalApplicationData')
    if (-not $SteamRoot) {
        $steamRegistry = Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
        if ($null -ne $steamRegistry -and $null -ne $steamRegistry.PSObject.Properties['SteamPath']) {
            $SteamRoot = [string]$steamRegistry.SteamPath
        }
        if (-not $SteamRoot) { $SteamRoot = Join-Path ${env:ProgramFiles(x86)} 'Steam' }
    }
    if (-not $VentureRoot) {
        if ($env:VENTURE_DATA_DIR) { $VentureRoot = $env:VENTURE_DATA_DIR }
        elseif ($env:QUESTPORT_DATA_DIR) { $VentureRoot = $env:QUESTPORT_DATA_DIR }
        else {
            $VentureRoot = Join-Path $localData 'Venture'
            $legacy = Join-Path $localData 'QuestPort'
            if (-not (Test-Path -LiteralPath $VentureRoot) -and (Test-Path -LiteralPath $legacy)) { $VentureRoot = $legacy }
        }
    }
    if (-not $ArchivePath) { $ArchivePath = Join-Path $localData 'steam-link-native-passthrough\diagnostics\pc-logs.zip' }
    $SteamRoot = [IO.Path]::GetFullPath($SteamRoot)
    $VentureRoot = [IO.Path]::GetFullPath($VentureRoot)
    $ArchivePath = [IO.Path]::GetFullPath($ArchivePath)
    if ([IO.Path]::GetExtension($ArchivePath) -ine '.zip') { throw 'Diagnostic output must be a .zip file.' }
    $parent = [IO.Path]::GetDirectoryName($ArchivePath)
    [void][IO.Directory]::CreateDirectory($parent)
    $stage = Join-Path $parent ('.pc-log-staging-' + [Guid]::NewGuid().ToString('N'))
    $temporaryZip = $stage + '.zip'
    $manifest = [ordered]@{
        schema = 'sfnp-pc-diagnostics-v1'; created_utc = [DateTime]::UtcNow.ToString('o')
        max_log_bytes = $TailLimit; steam_path = $SteamRoot; venture_data_path = $VentureRoot
        sources = @(); settings = $null; driver_log_found = $false
        settings_scope = 'Saved file snapshot at collection time, not proof of effective settings during earlier gameplay. Prefer contemporaneous native Link BW automatic/manual records.'
        scope = 'Private PC log tails and selected scalar Steam Link settings. No runtime or device changes.'
    }
    function Copy-LogTail([string]$Path, [string]$Member) {
        $item = [ordered]@{ source = $Path; member = $Member; status = 'missing' }
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            $inputFile = $null; $outputFile = $null
            try {
                $destination = Join-Path $stage $Member
                [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
                $inputFile = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                    [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
                $length = $inputFile.Length
                $offset = [Math]::Max(0L, $length - $TailLimit)
                [void]$inputFile.Seek($offset, [IO.SeekOrigin]::Begin)
                $outputFile = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
                $remaining = $length - $offset
                $buffer = New-Object byte[] 65536
                while ($remaining -gt 0) {
                    $read = $inputFile.Read($buffer, 0, [int][Math]::Min($remaining, $buffer.Length))
                    if (-not $read) { break }
                    $outputFile.Write($buffer, 0, $read)
                    $remaining -= $read
                }
                $outputFile.Dispose(); $outputFile = $null
                $item.status = 'copied'; $item.original_bytes = $length; $item.offset = $offset
                $item.bytes = (Get-Item -LiteralPath $destination).Length
                $item.sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($item.bytes -ne $length - $offset) { $item.status = 'changed_during_read' }
            } catch {
                $item.status = 'error'; $item.error_type = $_.Exception.GetType().FullName
            } finally {
                if ($null -ne $outputFile) { $outputFile.Dispose() }
                if ($null -ne $inputFile) { $inputFile.Dispose() }
            }
        }
        $manifest.sources += [pscustomobject]$item
    }
    try {
        [void][IO.Directory]::CreateDirectory($stage)
        $nativeLogs = Join-Path $SteamRoot 'logs'
        foreach ($name in @('driver_vrlink.txt', 'vrserver.txt', 'vrcompositor.txt', 'vrmonitor.txt')) {
            Copy-LogTail (Join-Path $nativeLogs $name) ('steam/' + $name)
        }
        # Some installations keep OpenVR logs outside Steam's own directory.
        $pathsFile = $OpenVrPaths
        if (-not $pathsFile) { $pathsFile = Join-Path $localData 'openvr\openvrpaths.vrpath' }
        if (Test-Path -LiteralPath $pathsFile -PathType Leaf) {
            try {
                if ((Get-Item -LiteralPath $pathsFile).Length -le 65536) {
                    $paths = Get-Content -LiteralPath $pathsFile -Raw | ConvertFrom-Json
                    if ($null -ne $paths.PSObject.Properties['log']) {
                        $index = 0
                        foreach ($directory in @($paths.log) | Select-Object -First 4) {
                            if (-not [IO.Path]::IsPathRooted([string]$directory)) { continue }
                            if ([IO.Path]::GetFullPath([string]$directory).TrimEnd([char]92) -ieq $nativeLogs.TrimEnd([char]92)) { continue }
                            foreach ($name in @('driver_vrlink.txt', 'vrserver.txt', 'vrcompositor.txt')) {
                                Copy-LogTail (Join-Path $directory $name) ('openvr-' + $index + '/' + $name)
                            }
                            $index++
                        }
                    }
                }
            } catch { $manifest.openvr_paths_error = $_.Exception.GetType().FullName }
        }
        $gameLogs = Join-Path $VentureRoot 'logs'
        Copy-LogTail (Join-Path $gameLogs 'launcher.log') 'venture/launcher.log'
        if (Test-Path -LiteralPath $gameLogs -PathType Container) {
            foreach ($file in Get-ChildItem -LiteralPath $gameLogs -File -Filter 'runtime-*.log' |
                Where-Object { $_.Name -notlike '*.prev.log' } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 6) {
                Copy-LogTail $file.FullName ('venture/' + $file.Name)
            }
        }
        # Never copy the full settings document, credentials or account files.
        $settingsPath = Join-Path $SteamRoot 'config\steamvr.vrsettings'
        $settings = [ordered]@{ source = $settingsPath; status = 'missing'; driver_vrlink = [ordered]@{} }
        if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
            try {
                if ((Get-Item -LiteralPath $settingsPath).Length -gt 2097152) { throw 'Settings document too large' }
                $configuration = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
                if ($null -ne $configuration.PSObject.Properties['driver_vrlink']) {
                    foreach ($key in @('automaticBandwidth', 'targetBandwidth', 'recommendedBandwidthMbit',
                        'automaticStreamFormatWidth', 'streamFormatWidth', 'allowMultipleLinks')) {
                        $property = $configuration.driver_vrlink.PSObject.Properties[$key]
                        if ($null -ne $property -and ($property.Value -is [bool] -or $property.Value -is [ValueType])) {
                            $settings.driver_vrlink[$key] = $property.Value
                        }
                    }
                }
                $settings.status = 'read'
            } catch { $settings.status = 'error'; $settings.error_type = $_.Exception.GetType().FullName }
        }
        $manifest.settings = $settings
        $manifest.driver_log_found = @($manifest.sources | Where-Object {
            [IO.Path]::GetFileName($_.source) -eq 'driver_vrlink.txt' -and $_.status -eq 'copied'
        }).Count -gt 0
        $utf8 = New-Object Text.UTF8Encoding $false
        [IO.File]::WriteAllText((Join-Path $stage 'diagnostics.json'), ($manifest | ConvertTo-Json -Depth 8), $utf8)
        [IO.Compression.ZipFile]::CreateFromDirectory($stage, $temporaryZip)
        if (Test-Path -LiteralPath $ArchivePath) { [IO.File]::Replace($temporaryZip, $ArchivePath, [NullString]::Value) }
        else { [IO.File]::Move($temporaryZip, $ArchivePath) }
        Write-Host ('PC diagnostics: ' + $ArchivePath)
        Write-Host ('Native driver_vrlink.txt included: ' + $manifest.driver_log_found)
        if (-not $manifest.driver_log_found) { Write-Warning 'Native driver log missing; diagnostics.json records the checked locations.' }
    } finally {
        if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
        if (Test-Path -LiteralPath $temporaryZip) { Remove-Item -LiteralPath $temporaryZip -Force }
    }
} $SteamPath $VentureDataPath $OpenVrPathsFile $OutputPath $MaxLogBytes
