@echo off
rem The Windows app (WinUI 3 shell, win\): the core by CMake, then the shell by MSBuild over the core's libraries.
rem Usage: scripts\build-win.cmd [build dir, default build-win]  ->  <build dir>\win\bin\LexiPeek.exe and what ships with it.
rem Needs Visual Studio 2022 (Build Tools: C++ x64, Windows SDK), CMake, Ninja, Git; NuGet is fetched when it is not on PATH.
setlocal
set "SRC=%~dp0.."
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%SRC%\build-win"
where cl >nul 2>nul || for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>nul || (echo no MSVC: install Visual Studio 2022 or its Build Tools & exit /b 1)
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEXIPEEK_QT=OFF || exit /b 1
cmake --build "%OUT%" --target lexipeek_core || exit /b 1
set "NUGET=nuget"
where nuget >nul 2>nul || (
  if not exist "%OUT%\nuget.exe" curl -sSLf -o "%OUT%\nuget.exe" https://dist.nuget.org/win-x86-commandline/latest/nuget.exe || exit /b 1
  set "NUGET=%OUT%\nuget.exe"
)
"%NUGET%" restore "%SRC%\win\packages.config" -PackagesDirectory "%OUT%\packages" -NonInteractive -Verbosity quiet || exit /b 1
msbuild "%SRC%\win\LexiPeek.vcxproj" -nologo -m -v:minimal -p:Configuration=Release -p:Platform=x64 "-p:LexiPeekBuild=%OUT%" || exit /b 1
echo %OUT%\win\bin\LexiPeek.exe
