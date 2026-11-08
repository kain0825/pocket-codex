$ErrorActionPreference='Stop'
$pocketRepo=Split-Path -Parent $PSScriptRoot
$pocketPython=Join-Path $pocketRepo '.venv\Scripts\pythonw.exe'
if(-not (Test-Path -LiteralPath $pocketPython)){throw 'Create .venv and install host/requirements.txt first.'}
$pocketNode=(Get-Command node -ErrorAction Stop).Source
Start-Process -FilePath $pocketPython -ArgumentList @('-u',('"'+(Join-Path $pocketRepo 'host\codex_sync\supervisor.py')+'"')) -WorkingDirectory (Join-Path $pocketRepo 'host\codex_sync') -WindowStyle Hidden
Start-Process -FilePath $pocketNode -ArgumentList ('"'+(Join-Path $pocketRepo 'host\monitor\server.cjs')+'"') -WorkingDirectory (Join-Path $pocketRepo 'host\monitor') -WindowStyle Hidden
Write-Host 'Started local Pocket Codex services. See docs/host-setup.md for connection checks.'
