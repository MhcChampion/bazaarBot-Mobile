# Configure this project's stdio MCP in Codex. Does not alter unrelated servers.
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$node = (Get-Command node -ErrorAction Stop).Source
$server = Join-Path $root 'mobilebpp\debug-mcp\server.mjs'
$dir = Join-Path $root '.codex'
$file = Join-Path $dir 'config.toml'
New-Item -ItemType Directory -Force $dir | Out-Null
$block = "[mcp_servers.bpp_mobile_debug]`ncommand = '$node'`nargs = ['$server']`ncwd = '$root'`nstartup_timeout_sec = 20`ntool_timeout_sec = 30`n"
$old = if (Test-Path $file) { [IO.File]::ReadAllText($file) } else { '' }
if ($old -match '(?m)^\[mcp_servers\.bpp_mobile_debug\]') {
  $new = [regex]::Replace($old, '(?ms)^\[mcp_servers\.bpp_mobile_debug\][^\[]*', [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $block + "`n" })
} else { $new = $old.TrimEnd() + "`n`n" + $block }
[IO.File]::WriteAllText($file, $new, [Text.UTF8Encoding]::new($false))
Write-Host "Configured BPP MCP: $file"
Write-Host 'Reload the MCP connection or reopen the chat to load the new tools.'
