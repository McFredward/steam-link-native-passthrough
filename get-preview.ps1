# Compatibility alias for the same paired-only setup. The assistant checks both devices before offering changes.
& {
    $ErrorActionPreference = 'Stop'
    $source = Invoke-RestMethod -Uri 'https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/fcefb61275c3fbee72931d2b28935cf87a5e6fcd/get.ps1' -TimeoutSec 30
    if ($source -isnot [string]) { throw 'Unexpected setup bootstrap response.' }
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = ([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($source)))).Replace('-', '').ToLowerInvariant()
    } finally { $hasher.Dispose() }
    if ($digest -cne '70109f51894eabfe7573e3f54f801a8d7ee0b46857ca6c4ba303929389916f36') {
        throw 'Setup bootstrap differs from the reviewed revision. Nothing was run.'
    }
    & ([scriptblock]::Create($source)) -BridgePreview
}
