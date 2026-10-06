# event-correlation-engine 1.0.0

> Release preparation update (2026-10-05): see [current executable qualification and package provenance](RELEASE-QUALIFICATION.md). Historical validation and pending-test statements in this document describe the original development snapshot. The final ZIP is assembled after qualification and has not been run on Windows.


Local Windows Event Log evidence correlation, in a portable x64 C++17 command-line application. MIT licensed. Windows 10, Windows 11, and supported Windows Server releases are the target platforms; Windows Server 2025 is not the minimum OS. See [VALIDATION.md](VALIDATION.md) for tested platforms and outstanding qualification.

**Temporal correlation does not establish causation. Shared identifiers do not prove causation either. This tool never identifies a root cause or assigns an invented severity score.**

## Examples

```text
event-correlation-engine --help
event-correlation-engine --version
event-correlation-engine recent
event-correlation-engine recent --minutes 30 --max-events 500 --gap-seconds 3
event-correlation-engine groups --channel System --level 2 --detailed-groups
event-correlation-engine around --channel System --record-id 12345 --minutes 5
event-correlation-engine --json recent --channel Application
```

The default lookback is 60 minutes, with at most 1,000 retained events per channel. Repeat `--channel` to select up to 16 local channels for recent/groups; specifying any replaces the defaults System and Application. Around requires exactly one anchor channel and record ID, then queries System, Application, and the anchor channel (deduplicated) within an inclusive radius of `--minutes`. `--level` filters nearby records, not the selected event. Selected events remain separately available even if filtering or limits exclude them from the nearby records.

Bounds: minutes 1–10,080 (seven days), events per channel 1–10,000, cluster gap 0–300 seconds, exact Windows level 0–5. Record IDs are decimal 1–9,223,372,036,854,775,807, within the local XPath integer range. Channels are queried newest first independently, so limits do not promise the nearest records to an anchor or a complete incident. A single additional record detects count truncation without rendering it. Total retained string payload is capped at 64 MiB (container overhead and serialization add memory); individual XML is capped at 4 MiB UTF-16, publisher messages at 1,048,576 UTF-16 characters. XML also limits depth to 64, attributes per element to 128 and payload elements to 16,384; oversized records are explicitly reported as acquisition failures. Truncation is explicitly reported. EvtNext waits at most five seconds per call. Local API calls themselves have no application-enforced wall-time timeout. Anchor lookup is a one-record exact ID query, independent of the lookback.

## Acquisition and safety

The executable uses EvtQuery, EvtNext and EvtRender directly with local session handles. It opens local publisher metadata and formats messages when available. Handles and XML COM resources use RAII. XmlLite prohibits DTDs and external entities and limits XML depth. No disabled channel is enabled. Unavailable, inaccessible, malformed, and failed acquisitions carry explicit channel status and Windows error codes. Missing publisher metadata/text is a message status, independently of acquisition success.

Strictly read-only: no log clearing, archival, channel configuration, subscription creation, audit changes, tracing/ETW sessions, privilege adjustment, registry writes, services, tasks, devices, or process control. The application invokes no shell, PowerShell, cmd, wevtutil, or other child process. It creates no sockets, resolves no DNS, makes no HTTP requests, opens no remote log session, and has no telemetry or provider downloads. Build/validation scripts are developer tools, separate from the shipped executable; they invoke compilers and test tools.

Run with ordinary user permissions for accessible logs. The manifest requests `asInvoker`; the program never elevates or enables privileges. Some channels such as Security may be denied. Missing access is evidence unavailable, not proof that an event did not happen. Existing Event Log Readers membership and local channel ACLs determine access.

## Analysis algorithms

Retained events sort ascending by UTC timestamp, channel, record ID, and provider, deterministically. Times use Windows FILETIME ticks (100 ns), parsed/converted through Windows system-time conversion with fractional ticks preserved. Provider + event ID + level forms a frequency group; `--detailed-groups` adds task and opcode. Each group reports count, first/last times, channels, representative available message, and count with process/activity metadata. Group keys include explicit separators and quoted provider names.

Temporal clusters are maximal consecutive runs in this sorted sequence whose adjacent gaps are **at most** `--gap-seconds`. Equal-boundary gaps join; one tick beyond starts a new cluster. Singletons remain clusters. Chaining means a cluster's total duration can exceed the gap. Cluster membership means only temporal proximity; bounded acquisition does not make clusters complete. Groups recur by record frequency, and cluster membership permits inspection across repeated windows. These windows are not independently verified incidents.

Correlation evidence indexes available nonzero activity, related activity, process, thread, user SID and computer fields. Activity and related activity values share a normalized case-insensitive activity namespace; other values retain their source representation. Evidence is scoped by an available computer; thread IDs also require the same known process ID. Unknown scopes create no links. Related activity matches mean an identifier reference, not a causal relationship. Process/thread IDs may be reused and require additional contextual verification. Provider semantics differ; shared computer or user is weak contextual evidence. Missing/zero activity IDs never create a fabricated link. No arbitrary event-data value is promoted into a reliable identifier.

`cluster_patterns` groups windows with the same sorted set of group keys, ignoring multiplicity and event order, and reports the window count and cluster indices. This exposes recurring combinations across windows without declaring those windows to be causal incidents. `selected_shared_evidence` explicitly reports identifier matches between the anchor and other retained events, even when the anchor was filtered from nearby results.

## Structured output

`--json` emits UTF-8 JSON, schema version `1.0`, with query parameters, acquisition status, channel results, selected event, events, groups, temporal clusters, correlation evidence, nearby preceding/following/same-time indices, warnings, and limitations. All event indices are zero-based into `events`. Record IDs and event scalar fields are strings to preserve exact values for consumers. Timestamps are UTC ISO 8601 with seven fractional digits. Known unavailable fields and messages are `null`; message status/error distinguishes unavailable text. Structured data keeps element names, paths, attributes and text; full source XML preserves nested/custom payload structure. No sensitive data is redacted automatically.

Exit codes: 0 analysis completed (including useful partial acquisition), 2 usage error, 3 requested channel/anchor absent or disabled, 4 access denied, 5 event acquisition/API failure, 6 internal/output failure. If any analysis channel completes or retains usable records, useful partial output returns 0; otherwise the first failing channel determines the code. Anchor lookup failures return their own code. Inspect `acquisition.status`, each channel status, and warnings even when exit code is zero. Usage/internal errors go to stderr and may have no JSON document. An empty successfully queried log is a completed analysis.

## Privacy and limitations

Logs can contain usernames, SIDs, machine names, file paths, command lines, tokens, document content, and other sensitive data. Messages, raw XML and data are included locally in output. Handle redirected output as sensitive material. Nothing is transmitted. Only System/Application are collected by default.

Retention, log rollover, disabled channels, unavailable publisher resources, permissions, clock adjustments, and bounded truncation can conceal evidence. Queries are not an atomic multi-channel snapshot. Local timestamps can reflect clock error. Formatted messages vary by installed publisher and locale; structured identity is retained when formatting fails. No correlation proves root cause or event absence. Inspect provider documentation and corroborating evidence separately.

## Build and test

From an x64 Visual Studio developer environment with CMake and a Windows SDK:

```text
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release
```

Visual Studio 2022's generator is also supported. All project targets use C++17 and MSVC `/W4 /WX /analyze`, with static CRT for portable deployment. MSVC suppresses C6553 only at the SDK XmlLite SetProperty declaration: its SAL annotation incorrectly describes an integer as a pointer. clang-tidy enables analyzer and bugprone checks except unsafe-to-allow-exceptions, whose mandatory nonthrowing moves contradict MSVC map's allocating move constructor; explicit potentially throwing moves preserve correct allocation-failure handling. `scripts/build.ps1` records this validation machine's exact tool/SDK paths; adapt them on other machines. Tests run through CTest; live tests never modify logs. `-DECE_LIVE_TESTS=ON` adds restricted-token tests of ordinary System access, denied Security access, partial acquisition and event limits. Test-token impersonation is confined to the test executable; production performs no token operations. Static analysis and safety audit commands/results are recorded in VALIDATION.md.

Portable release: extract the ZIP and run the EXE; no installer or runtime download. The executable uses Windows inbox DLLs. The release is unsigned; signing can be a later publishing step.
