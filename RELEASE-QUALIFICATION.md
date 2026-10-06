# Release qualification: event-correlation-engine 1.0.0

Development/build validation: Windows Server 2025 x64.
Release executable qualification: physical Windows 11 Pro 25H2 x64, build 26200.9550; Windows 10 Pro 22H2 x64, build 19045.6466, in a dedicated QEMU VM. Windows 10 physical hardware and ARM64 were not qualified. Other platform design targets are not additional tested-platform claims.

Qualified executable SHA-256: `5b9a99e0ae8e4a955bee46a5dbb932941287602b8b6610547005ba0311420f0a`.
Windows 11 recorded status: PASS WITH FINDINGS.
Windows 10 final status: PASS.
Windows 11 basis: preserved qualification report.
Windows 10 basis: user-supplied completed qualification statement; historical detailed reports unavailable here.

This final package was assembled after qualification around the exact qualified executable. It has not been executed on Windows after assembly. Earlier packages, where available, have separately verified recorded qualification relationships; their qualification does not constitute execution of this newly assembled ZIP.

PE imports show no separately imported MSVC C++ runtime DLLs; Windows system dependencies remain required.

Historical development validation documents below retain their original dated observations. References there to pending or unavailable client testing describe that development snapshot and are superseded for release executable status by this notice. They do not prove execution of this final package. No new qualification runs or rebuilds were performed during Linux release reconciliation.

The application behavior and safety limits are described by its source and specific documentation. Read-only Windows API calls do not guarantee absence of Windows internal bookkeeping, provider activity, or unrelated background activity. Application report output and documented explicit output modes remain possible.

Evidence limitations: historical Windows 10 detailed reports/ledgers and consolidated Windows 11 Waves 1–2 reports were unavailable in the qualified provenance set. Completion statements are attributed to the user; retained raw evidence and source manifests establish available provenance. Waves 1–3 historical qualification ZIPs were unavailable. VPS development evidence is not a substitute for those missing Windows reports.
