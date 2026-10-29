# Offline NVIDIA mask comparison. No installation, pairing or runtime settings.
& {
    $ErrorActionPreference = 'Stop'
    Set-StrictMode -Version 2
    $archiveCommit = 'a12bc51a2afdb31c60d5e7a9f1ff4e3d16dcb0da'
    $archiveBytes = 740678
    $archiveSha256 = '62a6102fa207b045be96ba1937d1622c23234c81af592e31c16e026ae1b58fc0'
    $exeSha256 = 'f4988f6b665c6bc89f5858a75d52838bc0ae32b1d6bc4577fbd7d3fb26ffd140'
    $sourceRevision = '1b6e0dd73587c99fab16e1c0362b0cb741bf87d8'
    $repo = 'https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/'
    $root = Join-Path $env:LOCALAPPDATA 'steam-link-native-passthrough'
    $work = Join-Path $root 'encoder-probe-work'
    $resultZip = Join-Path $root 'encoder-probe-results.zip'
    $download = Join-Path $root 'encoder-probe-download.zip'
    $exe = Join-Path $root 'sfnp-nvenc-quality-probe.exe'
    $utf8 = New-Object Text.UTF8Encoding($false)
    if (@(Get-Process -Name 'vrserver','vrcompositor','venture-runtime','venture' -ErrorAction SilentlyContinue).Count) {
        throw 'Bitte SteamVR und Venture schliessen und danach den GPU-Vergleich erneut starten.'
    }
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    if (Test-Path -LiteralPath $work) {
        $item = Get-Item -LiteralPath $work -Force
        $marker = Join-Path $work '.sfnp-probe-owned'
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or !(Test-Path -LiteralPath $marker) -or
            [IO.File]::ReadAllText($marker) -cne 'sfnp-nvenc-quality-probe-work-v1') { throw 'Unbekannter Probe-Ordner. Es wurde nichts entfernt.' }
        foreach ($file in @(Get-ChildItem -LiteralPath $work -Force)) {
            if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                ($file.Name -ne '.sfnp-probe-owned' -and $file.Extension -notin '.json','.jsonl','.log','.hevc','.r8')) {
                throw 'Der Probe-Ordner enthaelt fremde Dateien. Es wurde nichts entfernt.'
            }
        }
        Get-ChildItem -LiteralPath $work -Force | Remove-Item -Force
    } else { New-Item -ItemType Directory -Path $work | Out-Null }
    [IO.File]::WriteAllText((Join-Path $work '.sfnp-probe-owned'), 'sfnp-nvenc-quality-probe-work-v1', $utf8)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $status = [ordered]@{ schema='sfnp-nvenc-probe-run-v1'; source_revision=$sourceRevision; exit_code=$null; timed_out=$false; error=$null }
    $downloadedExe = $false
    try {
        Write-Host 'Lade den geprueften NVIDIA-GPU-Vergleich...'
        Invoke-WebRequest -UseBasicParsing -Uri ($repo + $archiveCommit + '/distribution/nvenc-quality-probe.zip') -OutFile $download -TimeoutSec 60
        if ((Get-Item -LiteralPath $download).Length -ne $archiveBytes -or
            (Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant() -cne $archiveSha256) {
            throw 'Pruefsumme oder Groesse des Downloads stimmt nicht. Es wurde nichts ausgefuehrt.'
        }
        $zip = [IO.Compression.ZipFile]::OpenRead($download)
        try {
            if ($zip.Entries.Count -ne 2 -or @($zip.Entries | Where-Object { $_.FullName -notin 'sfnp-nvenc-quality-probe.exe','probe-build.json' }).Count) {
                throw 'Unerwartete Dateien im GPU-Vergleichspaket.'
            }
            $entry = @($zip.Entries | Where-Object { $_.FullName -ceq 'sfnp-nvenc-quality-probe.exe' })
            if ($entry.Count -ne 1 -or $entry[0].Length -gt 8MB) { throw 'Ungueltige Probe-Datei.' }
            $inputStream = $entry[0].Open()
            try { $outputStream = [IO.File]::Create($exe); try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() } }
            finally { $inputStream.Dispose() }
            $downloadedExe = $true
        } finally { $zip.Dispose() }
        if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -cne $exeSha256) { throw 'Die GPU-Probe unterscheidet sich vom geprueften Build.' }
        Write-Host 'Vergleiche zehn Encoder-Konfigurationen mit denselben synthetischen Regen- und Kantenbildern (bis zu 3 Minuten)...'
        $start = New-Object Diagnostics.ProcessStartInfo
        $start.FileName = $exe; $start.Arguments = '--output "' + $work + '"'; $start.WorkingDirectory = $work
        $start.UseShellExecute = $false; $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
        $process = New-Object Diagnostics.Process
        $process.StartInfo = $start
        try {
            if (!$process.Start()) { throw 'GPU-Vergleich konnte nicht gestartet werden.' }
            $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
            if (!$process.WaitForExit(180000)) {
                $status.timed_out = $true; $process.Kill(); $process.WaitForExit()
            }
            $status.exit_code = $process.ExitCode
            [IO.File]::WriteAllText((Join-Path $work 'stdout.log'), $stdout.Result, $utf8)
            [IO.File]::WriteAllText((Join-Path $work 'stderr.log'), $stderr.Result, $utf8)
            Write-Host $stdout.Result
        } finally { $process.Dispose() }
        $reportFile = Join-Path $work 'probe.json'
        if (Test-Path -LiteralPath $reportFile) {
            $report = [IO.File]::ReadAllText($reportFile) | ConvertFrom-Json
            if ($report.schema -cne 'sfnp-nvenc-quality-probe-v1' -or $report.source_revision -cne $sourceRevision) { throw 'Unbekannte GPU-Probe-Ausgabe.' }
        }
        if ($status.timed_out -or $status.exit_code -ne 0) { throw 'Der GPU-Vergleich ist fehlgeschlagen; die vorhandenen Diagnosen werden trotzdem gespeichert.' }
    } catch {
        $status.error = $_.Exception.Message
    } finally {
        [IO.File]::WriteAllText((Join-Path $work 'probe-status.json'), ($status | ConvertTo-Json -Depth 5), $utf8)
        $pins = [ordered]@{}
        foreach ($file in @(Get-ChildItem -LiteralPath $work -File | Where-Object { $_.Name -ne '.sfnp-probe-owned' })) {
            $pins[$file.Name] = [ordered]@{ bytes=$file.Length; sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        }
        $manifest = [ordered]@{ schema='sfnp-nvenc-probe-files-v1'; files=$pins }
        [IO.File]::WriteAllText((Join-Path $work 'probe-manifest.json'), ($manifest | ConvertTo-Json -Depth 6), $utf8)
        if (Test-Path -LiteralPath $resultZip) { Remove-Item -LiteralPath $resultZip -Force }
        [IO.Compression.ZipFile]::CreateFromDirectory($work, $resultZip, [IO.Compression.CompressionLevel]::Optimal, $false)
        # Keep the result archive and small text reports. Remove this run's large
        # owned image/bitstream files and downloaded binary after archiving.
        Get-ChildItem -LiteralPath $work -File | Where-Object { $_.Extension -in '.r8','.hevc' } | Remove-Item -Force
        if ($downloadedExe -and (Test-Path -LiteralPath $exe)) { Remove-Item -LiteralPath $exe -Force }
        if (Test-Path -LiteralPath $download) { Remove-Item -LiteralPath $download -Force }
    }
    Write-Host ('Ergebnis-ZIP: ' + $resultZip)
    if ($status.error) { throw ($status.error + ' Ergebnis-ZIP: ' + $resultZip) }
}
