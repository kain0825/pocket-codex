$ErrorActionPreference='Stop'
$framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$arguments=@('/nologo','/target:exe',('/out:'+(Join-Path $PSScriptRoot 'TaskControl.exe')),
    ('/r:'+(Join-Path $framework 'WPF\WindowsBase.dll')),
    '/r:System.Web.Extensions.dll',
    ('/r:'+(Join-Path $framework 'WPF\UIAutomationClient.dll')),
    ('/r:'+(Join-Path $framework 'WPF\UIAutomationTypes.dll')),
    (Join-Path $PSScriptRoot 'TaskControl.cs'))
& (Join-Path $framework 'csc.exe') @arguments
if($LASTEXITCODE -ne 0){throw 'TaskControl build failed'}
