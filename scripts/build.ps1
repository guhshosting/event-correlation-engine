$ErrorActionPreference = 'Stop'
$toolRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVersion = '10.0.26100.0'
$env:PATH = "$toolRoot\bin\Hostx64\x64;$sdkRoot\bin\$sdkVersion\x64;" + $env:PATH
$env:INCLUDE = "$toolRoot\include;$sdkRoot\Include\$sdkVersion\ucrt;$sdkRoot\Include\$sdkVersion\shared;$sdkRoot\Include\$sdkVersion\um"
$env:LIB = "$toolRoot\lib\x64;$sdkRoot\Lib\$sdkVersion\ucrt\x64;$sdkRoot\Lib\$sdkVersion\um\x64"
cmake -S . -B build-native -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release -DECE_LIVE_TESTS=ON "-DCMAKE_MAKE_PROGRAM=$toolRoot\bin\Hostx64\x64\nmake.exe"
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build build-native
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
