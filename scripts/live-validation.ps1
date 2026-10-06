param([string]$Executable = '.\build-native\event-correlation-engine.exe', [string]$Prefix = 'live')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force artifacts | Out-Null
function Query([string[]]$Arguments, [int]$Expected = 0) {
    $raw = & $Executable --json @Arguments
    if ($LASTEXITCODE -ne $Expected) { throw "Unexpected exit code $LASTEXITCODE for $Arguments" }
    $document = ($raw -join "`n") | ConvertFrom-Json -Depth 100 -DateKind String
    if ($document.schema_version -ne '1.0') { throw 'JSON schema version' }
    return $document
}
$recent = Query @('recent','--minutes','60','--max-events','25')
if ($recent.events.Count -gt 50) { throw 'Per-channel event bound' }
$groups = Query @('groups','--channel','System','--minutes','60','--max-events','10','--detailed-groups')
$limited = Query @('recent','--max-events','1')
if ($limited.events.Count -gt 2) { throw 'Minimum event limit' }
$missing = Query @('recent','--channel','ECE-Nonexistent-Channel-8b88f1d8') 3
if ($missing.channels[0].status -ne 'unavailable') { throw 'Missing channel status' }
$partial = Query @('recent','--channel','System','--channel','ECE-Nonexistent-Channel-8b88f1d8','--max-events','2')
if ($partial.acquisition.status -ne 'partial') { throw 'Partial acquisition status' }
$absent = Query @('around','--channel','System','--record-id','9223372036854775807','--minutes','1') 3
if ($null -ne $absent.selected_event) { throw 'Absent anchor invented' }
$anchorRecords = Query @('recent','--channel','System','--minutes','10080','--max-events','1')
$aroundCount = 0
if ($anchorRecords.events.Count) {
    $anchor = $anchorRecords.events[0].fields
    $around = Query @('around','--channel','System','--record-id', $anchor.record_id,'--minutes','1','--max-events','4')
    if ($around.selected_event.fields.record_id -ne $anchor.record_id) { throw 'Anchor identity' }
    $center = [DateTimeOffset]::Parse($around.selected_event.fields.timestamp)
    if (([DateTimeOffset]::Parse($around.query.end) - $center).TotalSeconds -ne 60 -or ($center - [DateTimeOffset]::Parse($around.query.start)).TotalSeconds -ne 60) { throw 'Anchor radius' }
    foreach ($event in $around.events) {
        $time = [DateTimeOffset]::Parse($event.fields.timestamp)
        if ($time -lt [DateTimeOffset]::Parse($around.query.start) -or $time -gt [DateTimeOffset]::Parse($around.query.end)) { throw 'Event outside selected window' }
    }
    $aroundCount = $around.events.Count
}
$filtered = Query @('recent','--channel','System','--minutes','60','--max-events','3','--level','2')
foreach ($event in $filtered.events) { if ($event.fields.level -ne '2') { throw 'Level filter' } }
@("PASS: UTF-8 JSON parsed; recent retained $($recent.events.Count) events in $($recent.query.start) .. $($recent.query.end).", "PASS: groups, max-event limits, missing channel (3), partial acquisition (0), absent anchor (3), exact level filter.", "PASS: selected-event identity and inclusive +/-60-second window; $aroundCount nearby records.", 'No raw log content is included in this validation summary.') | Tee-Object "artifacts/$Prefix-validation.txt"
