$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force artifacts | Out-Null
$tidyArgs = @('src/engine.cpp', 'src/main.cpp', 'tests/tests.cpp', 'tests/live.cpp', '--extra-arg=-Wno-unknown-warning-option', '--', '-std=c++17', '-Isrc', '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-D_WIN32_WINNT=0x0A00', '-IC:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\include', '-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt', '-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared', '-IC:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um')
& 'C:\Program Files\LLVM\bin\clang-tidy.exe' @tidyArgs 2>&1 | Tee-Object artifacts/clang-tidy.txt
if ($LASTEXITCODE -ne 0) { throw 'clang-tidy failed' }
& 'C:\Program Files\Cppcheck\cppcheck.exe' --enable=warning,performance,portability --error-exitcode=1 --std=c++17 --platform=win64 --inline-suppr -Isrc src tests 2>&1 | Tee-Object artifacts/cppcheck.txt
if ($LASTEXITCODE -ne 0) { throw 'Cppcheck failed' }
