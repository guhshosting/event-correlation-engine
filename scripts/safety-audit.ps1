$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force artifacts | Out-Null
$allowed = @('EvtClose','EvtRender','EvtOpenPublisherMetadata','EvtFormatMessage','EvtOpenChannelConfig','EvtGetChannelConfigProperty','EvtQuery','EvtNext')
$source = (Get-Content src/engine.cpp,src/main.cpp,src/engine.h -Raw) -join "`n"
$calls = [regex]::Matches($source, '\b(Evt\w+)\s*\(') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
foreach ($call in $calls) { if ($call -notin $allowed) { throw "Unexpected Event Log call: $call" } }
$forbidden = '\b(EvtClearLog|EvtSubscribe|EvtOpenSession|EvtSet\w+|EvtSave\w+|EvtArchive\w+|CreateProcess\w*|ShellExecute\w*|WinHttp\w+|WinInet\w+|WSA\w+|socket|connect|getaddrinfo|RegSet\w+|RegCreate\w+|RegDelete\w+|AdjustTokenPrivileges|StartTrace\w*|ControlTrace\w*|TerminateProcess|SuspendThread|system|_popen)\s*\('
if ($source -match $forbidden) { throw "Forbidden source call: $($Matches[0])" }
$dumpbin = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\dumpbin.exe'
$imports = & $dumpbin /imports build-native/event-correlation-engine.exe
if ($LASTEXITCODE -ne 0) { throw 'Import extraction failed' }
$imports | Set-Content artifacts/imports.txt
if (($imports -join "`n") -match '(?i)(WS2_32\.dll|WINHTTP\.dll|WININET\.dll|URLMON\.dll|EvtClearLog|EvtSubscribe|EvtOpenSession|EvtSetChannel|CreateProcess|ShellExecute|RegSetValue|AdjustTokenPrivileges)') { throw 'Forbidden import' }
@('PASS: production source Event Log calls match read-only allowlist.', 'PASS: no prohibited production source call or network/process/mutation import found.', ('Read-only Event Log calls: ' + ($calls -join ', ')), 'Scope: application source and direct executable imports; not an audit of Windows internals.') | Tee-Object artifacts/safety-audit.txt
