@echo off
setlocal
cd /d "%~dp0"

REM ---------------------------------------------------------------------------
REM  minidisk build - the only entry point (ADR-002). MSVC + Windows SDK only.
REM  Usage: build.bat [debug|release|test|check|analyze|bench|clean]
REM ---------------------------------------------------------------------------

set MODE=%1
if "%MODE%"=="" set MODE=debug
if "%MODE%"=="clean" goto :clean

REM --- locate Visual Studio through vswhere, never trust the caller's env -----
if defined MD_VCVARS goto :vcvars_done
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" goto :no_vswhere
set VSINSTALL=
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\minidisk_vsinstall.txt"
set /p VSINSTALL=<"%TEMP%\minidisk_vsinstall.txt"
del "%TEMP%\minidisk_vsinstall.txt"
if not defined VSINSTALL goto :no_vs
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set MD_VCVARS=1
:vcvars_done

if not exist build mkdir build

REM --- common flags ----------------------------------------------------------
REM  4201 anonymous struct/union (C11, used by V2)   4100 unused parameter
REM  4189 unused local                               4505 unreferenced static function
set WARN=/W4 /wd4201 /wd4189 /wd4100 /wd4505
set COMMON=/nologo /std:c11 /Zi /Isrc /FC /diagnostics:column %WARN%
set DEFS=/DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX
set REL_CL=/DBUILD_DEBUG=0 /DBUILD_NO_CRT=1 /O2 /Oi /Gy /GS- /Gs9999999 /GR- /EHa- /GL
set DBG_CL=/DBUILD_DEBUG=1 /DBUILD_NO_CRT=0 /Od /MTd /fsanitize=address
set REL_LINK=/LTCG /INCREMENTAL:NO /NODEFAULTLIB /ENTRY:entry_point /SUBSYSTEM:WINDOWS ^
 /OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /STACK:0x100000,0x10000 ^
 /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /PDBALTPATH:%%_PDB%% ^
 /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest

if "%MODE%"=="release" goto :release
if "%MODE%"=="debug"   goto :debug
if "%MODE%"=="test"    goto :test
if "%MODE%"=="bench"   goto :bench
if "%MODE%"=="check"   goto :check
if "%MODE%"=="analyze" goto :analyze
echo ERREUR: mode inconnu "%MODE%"
exit /b 1

REM ---------------------------------------------------------------------------
:release
echo [release] build...
cl %COMMON% %DEFS% %REL_CL% /c src\third_party.c /Fobuild\third_party.obj /Fdbuild\minidisk.pdb || exit /b 1
cl %COMMON% %DEFS% %REL_CL% src\main.c /Fobuild\main.obj /Fdbuild\minidisk.pdb ^
   /link %REL_LINK% /OUT:build\minidisk.exe build\third_party.obj ^
   kernel32.lib user32.lib || exit /b 1
goto :size

REM ---------------------------------------------------------------------------
:debug
echo [debug] build...
cl %COMMON% %DEFS% %DBG_CL% /c src\third_party.c /Fobuild\third_party_debug.obj /Fdbuild\minidisk_debug.pdb || exit /b 1
cl %COMMON% %DEFS% %DBG_CL% src\main.c /Fobuild\main_debug.obj /Fdbuild\minidisk_debug.pdb ^
   /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup ^
   /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest ^
   /OUT:build\minidisk_debug.exe build\third_party_debug.obj ^
   kernel32.lib user32.lib || exit /b 1
REM  ASan est toujours dynamique chez MSVC : sans cette DLL a cote, l'exe debug
REM  ne demarre pas depuis l'explorateur.
for /f "delims=" %%i in ('where clang_rt.asan_dynamic-x86_64.dll 2^>nul') do copy /y "%%i" build\ >nul
goto :size

REM ---------------------------------------------------------------------------
:test
echo [test] build ^(debug + ASan^)...
cl %COMMON% %DEFS% %DBG_CL% /DBUILD_TEST=1 tests\test_main.c /Fobuild\tests.obj /Fdbuild\tests.pdb ^
   /link /INCREMENTAL:NO /SUBSYSTEM:CONSOLE /OUT:build\tests.exe kernel32.lib user32.lib || exit /b 1
echo [test] run...
build\tests.exe || exit /b 1
exit /b 0

REM ---------------------------------------------------------------------------
:bench
echo [bench] build...
cl %COMMON% %DEFS% /DBUILD_DEBUG=0 /DBUILD_NO_CRT=0 /DBUILD_BENCH=1 /O2 /Oi /Gy /MT ^
   tests\bench_main.c /Fobuild\bench.obj /Fdbuild\bench.pdb ^
   /link /INCREMENTAL:NO /SUBSYSTEM:CONSOLE /OUT:build\bench.exe kernel32.lib user32.lib || exit /b 1
echo [bench] run...
build\bench.exe || exit /b 1
exit /b 0

REM ---------------------------------------------------------------------------
REM  check: structural rules (ADR-001) enforced by grep. Any hit is an error.
:check
echo [check] regles de dependance...
set CHECK_FAIL=0
REM  windows.h : interdit dans core/ et ui/ (ADR-001)
call :forbid "windows.h" "windows.h inclus dans core/ ou ui/" src\core src\ui
REM  CRT et tas : interdits hors platform/ et third_party.c
call :forbid "malloc(" "malloc hors platform/" src\core src\ui src\base
call :forbid "free(" "free hors platform/" src\core src\ui src\base
call :forbid "printf" "printf hors platform/" src\core src\ui src\base
if not "%CHECK_FAIL%"=="0" exit /b 1
echo   OK
exit /b 0

REM  :forbid <pattern> <message> <dir> [dir] [dir]
:forbid
set PATTERN=%~1
set MESSAGE=%~2
for %%D in (%3 %4 %5) do (
  if exist "%%~D" call :forbid_dir "%%~D"
)
goto :eof

:forbid_dir
findstr /S /I /M /C:"%PATTERN%" "%~1\*.c" "%~1\*.h" >nul 2>&1
if errorlevel 1 goto :eof
echo   ERREUR: %MESSAGE% ^(motif "%PATTERN%"^) :
findstr /S /I /M /C:"%PATTERN%" "%~1\*.c" "%~1\*.h" 2>nul
set CHECK_FAIL=1
goto :eof

REM ---------------------------------------------------------------------------
:analyze
echo [analyze] cl /W4 /WX /analyze...
REM  /external: le SDK et le CRT ne sont pas notre code, on n'analyse que src/.
cl %COMMON% /WX %DEFS% /DBUILD_DEBUG=0 /DBUILD_NO_CRT=1 /O2 /Oi /GS- /Gs9999999 /GR- /EHa- ^
   /external:anglebrackets /external:W0 /analyze /analyze:external- ^
   /c src\main.c /Fobuild\analyze.obj /Fdbuild\analyze.pdb || exit /b 1
set CLANG_TIDY=%VSINSTALL%\VC\Tools\Llvm\x64\bin\clang-tidy.exe
if not exist "%CLANG_TIDY%" goto :no_clang_tidy
echo [analyze] clang-tidy...
"%CLANG_TIDY%" --quiet src\main.c -- --driver-mode=cl /std:c11 /Isrc %DEFS% ^
   /DBUILD_DEBUG=0 /DBUILD_NO_CRT=1 || exit /b 1
echo   OK
exit /b 0

REM ---------------------------------------------------------------------------
:size
for %%F in (build\minidisk*.exe) do echo   %%~nxF : %%~zF octets
exit /b 0

:clean
if exist build rmdir /s /q build
echo   build/ supprime
exit /b 0

:no_vswhere
echo ERREUR: vswhere introuvable: "%VSWHERE%"
exit /b 1

:no_vs
echo ERREUR: aucune installation Visual Studio avec les outils C++ x64
exit /b 1

:no_clang_tidy
echo ERREUR: clang-tidy introuvable: "%CLANG_TIDY%"
exit /b 1
