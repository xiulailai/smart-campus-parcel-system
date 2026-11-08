@echo off
rem ===========================================================================
rem run_tests.bat - builds and runs SystemTests.cpp with Visual Studio (MSVC)
rem
rem Open "Developer Command Prompt for VS" (or "x64 Native Tools Command
rem Prompt"), then run:   SMART_CAMPUS_PARCEL_COLLECTION\Tests\run_tests.bat
rem
rem Every test works on a COPY of the Data folder in %TEMP%, so the real CSV
rem files are never changed. For the scripted run of the real program and the
rem sanitizer checks, use run_tests.sh (Linux / WSL / Git Bash with g++).
rem ===========================================================================
setlocal
set "HERE=%~dp0"
set "CODE=%HERE%..\Code"
set "DATA=%HERE%..\..\Data"
set "BUILD=%HERE%build"

where cl >nul 2>nul
if errorlevel 1 (
    echo cl.exe not found. Run this from the "Developer Command Prompt for VS".
    exit /b 2
)

if not exist "%BUILD%" mkdir "%BUILD%"
pushd "%BUILD%"
cl /nologo /EHsc /std:c++20 /W3 /D_CRT_SECURE_NO_WARNINGS ^
   "%HERE%SystemTests.cpp" ^
   "%CODE%\Task1\ParcelIntake.cpp" ^
   "%CODE%\Task2\ServiceRequest.cpp" ^
   "%CODE%\Task3\ResourceQueue.cpp" ^
   "%CODE%\Task4\ActivityLog.cpp" ^
   "%CODE%\Task5\ParcelCategoryTree.cpp" ^
   /Fe:SystemTests.exe
if errorlevel 1 (
    popd
    echo Build failed.
    exit /b 2
)
popd

"%BUILD%\SystemTests.exe" "%DATA%"
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" (echo ALL TESTS PASSED) else (echo Some tests failed - see FAIL lines above)
exit /b %RESULT%
