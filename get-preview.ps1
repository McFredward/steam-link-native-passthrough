# Compatibility alias for the same paired-only setup. The assistant checks both devices before offering changes.
& {
    $ErrorActionPreference = 'Stop'
    $source = Invoke-RestMethod -Uri 'https://raw.githubusercontent.com/McFredward/steam-link-native-passthrough/590639eac13dcf0c6f5fce91cdff33a205093c46/get.ps1' -TimeoutSec 30
    if ($source -isnot [string]) { throw 'Unexpected setup bootstrap response.' }
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = ([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($source)))).Replace('-', '').ToLowerInvariant()
    } finally { $hasher.Dispose() }
    if ($digest -cne '5b23ad54e61b8ed7c99c2dbe4464cdc145650010d9a46f79364be88e24a08eac') {
        throw 'Setup bootstrap differs from the reviewed revision. Nothing was run.'
    }
    & ([scriptblock]::Create($source)) -BridgePreview
}
