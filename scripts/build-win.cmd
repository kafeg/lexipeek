@echo off
rem The Windows app: the WinUI 3 shell (win\, the default) or, with "qt", the Qt shell (qt\, the fallback build).
rem Usage: scripts\build-win.cmd [build dir, default build-win] [qt]
rem   WinUI: the core and the OS layer (platform\) by CMake, the shell by MSBuild over their libraries; what ships into
rem   <dir>\win\LexiPeek\ (LexiPeek.exe, the Windows App SDK's DLLs, .pri, .xbf, the locale folders' .mui), without what a
rem   run never needs: *.pdb *.lib *.exp *.winmd, WebView2 (not used), the SDK's push-notification proxy and restart agent.
rem   Qt: CMake with -DLEXIPEEK_QT=ON -> <dir>\qt\LexiPeek.exe (one static exe; Qt by vcpkg, its first build is long).
rem Needs Visual Studio 2022 or later (or its Build Tools: C++ x64, Windows SDK), CMake, Ninja, Git; NuGet is fetched when it is not on PATH.
setlocal
set "SRC=%~dp0.."
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%SRC%\build-win"
where cl >nul 2>nul || for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>nul || (echo no MSVC: install Visual Studio 2022 or later, or its Build Tools & exit /b 1)
if /i "%~2"=="qt" (
  cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEXIPEEK_QT=ON || exit /b 1
  cmake --build "%OUT%" --target lexipeek-qt || exit /b 1
  echo %OUT%\qt\LexiPeek.exe
  exit /b 0
)
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEXIPEEK_QT=OFF || exit /b 1
cmake --build "%OUT%" --target lexipeek_platform lexipeek-win-version || exit /b 1
set "NUGET=nuget"
where nuget >nul 2>nul || (
  if not exist "%OUT%\nuget.exe" curl -sSLf -o "%OUT%\nuget.exe" https://dist.nuget.org/win-x86-commandline/latest/nuget.exe || exit /b 1
  set "NUGET=%OUT%\nuget.exe"
)
"%NUGET%" restore "%SRC%\win\packages.config" -PackagesDirectory "%OUT%\packages" -NonInteractive -Verbosity quiet || exit /b 1
msbuild "%SRC%\win\LexiPeek.vcxproj" -nologo -m -v:minimal -p:Configuration=Release -p:Platform=x64 "-p:LexiPeekBuild=%OUT%" || exit /b 1
robocopy "%OUT%\win\bin" "%OUT%\win\LexiPeek" /MIR /NJH /NJS /NFL /NDL /NP /XF *.pdb *.lib *.exp *.winmd Microsoft.Web.WebView2.Core.dll RestartAgent.exe PushNotificationsLongRunningTask.ProxyStub.dll >nul
if errorlevel 8 exit /b 1
echo %OUT%\win\LexiPeek\LexiPeek.exe
exit /b 0
