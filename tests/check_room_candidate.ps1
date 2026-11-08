param([string] $Source, [string] $Archive, [string] $StageRoot)
$ErrorActionPreference = 'Stop'
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($Source, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'Bootstrap parsing failed.' }
foreach ($fn in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]}, $true)) {
    Invoke-Expression $fn.Extent.Text
}
$pinNames = @('sfnpRepo','sfnpRepositoryCommit','sfnpRepositoryAsset','sfnpRepositoryBytes','sfnpRepositorySha256')
foreach ($pin in $ast.FindAll({param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left -is [Management.Automation.Language.VariableExpressionAst] -and $node.Left.VariablePath.UserPath -in $pinNames}, $true)) {
    Invoke-Expression $pin.Extent.Text
}
$asset = Get-SfnpRepositoryAsset
if ((Get-Item -LiteralPath $Archive).Length -ne $asset.size -or
    (Get-FileHash -LiteralPath $Archive).Hash.ToLowerInvariant() -cne $asset.digest.Substring(7)) {
    throw 'Candidate archive differs from bootstrap pins.'
}
$target = Join-Path $StageRoot ('room-candidate-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($target)
try {
    $runtime = Expand-SfnpPackage $Archive $target
    foreach ($name in @('frame/room-setup/sfnp-room-overlay','frame/room-setup/sfnp-room-renderer',
            'frame/room-setup/libsfnp-room-vision.so','setup/room_reconstruction/live_worker.py',
            'setup/steam_link_setup_room.py','setup/room-pc-runtime.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $target $name) -PathType Leaf)) { throw ('Missing room payload: ' + $name) }
    }
    $bundle = Get-Content -Raw -LiteralPath (Join-Path $target 'stream-bridge-bundle.json') | ConvertFrom-Json
    if ($bundle.version -cne '0.3.19-roomscan.17.1') { throw 'Unexpected room candidate.' }
    if ($bundle.pc.version -cne '0.3.13' -or $bundle.frame.version -cne '0.3.13-roomscan.16') {
        throw 'Unexpected room maintenance component versions.'
    }
    $pcDll = Join-Path $target 'pc-provider/bin/win64/driver_steam_link_native_passthrough.dll'
    if ((Get-FileHash -LiteralPath $pcDll).Hash.ToLowerInvariant() -cne 'ab11a3cf43f86c9d32f2a96a970707c5322a86b62d55112d164ff3e83f32a57c') {
        throw 'The confirmed PC 0.3.13 binary is missing.'
    }
    if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
        & (Join-Path $target $runtime) -I -B (Join-Path $target 'setup/steam_link_setup.py') --help
        if ($LASTEXITCODE -ne 0) { throw 'The actual packaged setup entry point failed.' }
        & (Join-Path $target $runtime) -I -B -c 'import sys;sys.path.insert(0,sys.argv[1]);import steam_link_setup_room;import room_reconstruction.live_worker' (Join-Path $target 'setup')
        if ($LASTEXITCODE -ne 0) { throw 'The actual packaged PC companion could not be imported.' }
    }
    @{result='PASS'; version=$bundle.version; actual_candidate_archive=$true;
      device_actions=$false; headset_test=$false} | ConvertTo-Json
} finally {
    Remove-Item -LiteralPath $target -Recurse -Force
}
