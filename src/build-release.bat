@set PATH=%PATH%;C:\Program Files\Microsoft Visual Studio 8\Common7\IDE
@set PATH=%PATH%;C:\Program Files\NSIS
@set FASTDL_PATH=C:\kjk\src\web\fastdl\www

@set VERSION=%1
@IF NOT DEFINED VERSION GOTO VERSION_NEEDED

devenv vc8proj\xpdf-win.sln /Rebuild "Release|Win32"
@IF ERRORLEVEL 1 goto BUILD_FAILED
echo Compilation ok!
@makensis installer
@IF ERRORLEVEL 1 goto INSTALLER_FAILED

move SumatraPDF-install.exe SumatraPDF-%VERSION%-install.exe
copy SumatraPDF-%VERSION%-install.exe %FASTDL_PATH%\SumatraPDF-%VERSION%-install.exe

@goto END

:INSTALLER_FAILED
echo Installer script failed
@goto END

:BUILD_FAILED
echo Something failed!
@goto END

:VERSION_NEEDED
echo Need to provide version number e.g. build-release.bat 1.0
@goto END

:END
 