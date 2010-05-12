@echo off

call "%ProgramFiles%\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat"
IF ERRORLEVEL 1 GOTO TRYX86
GOTO HAS_VS9

:TRYX86
call "%ProgramFiles(x86)%\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat"
IF ERRORLEVEL 1 GOTO VS9_NEEDED

:HAS_VS9
IF EXIST "%ProgramFiles%\NSIS" GOTO HAS_NSIS
IF EXIST "%ProgramFiles(x86)%\NSIS" GOTO HAS_NSIS_X86
GOTO NSIS_NEEDED

:HAS_NSIS_X86
set PATH=%PATH%;%ProgramFiles(x86)%\NSIS
GOTO NEXT

:HAS_NSIS
set PATH=%PATH%;%ProgramFiles%\NSIS
GOTO NEXT

:NEXT
set OUT_PATH=C:\kjk\src\sumatrapdf\builds

rem create OUT_PATH if doesn't exist
IF EXIST %OUT_PATH% GOTO DONT_CREATE_OUT_PATH
mkdir %OUT_PATH%
:DONT_CREATE_OUT_PATH

pushd .
set VERSION=%1
IF NOT DEFINED VERSION GOTO VERSION_NEEDED

rem check if makensis exists
makensis /version >nul
IF ERRORLEVEL 1 goto NSIS_NEEDED

rem check if zip exists
zip >nul
IF ERRORLEVEL 1 goto ZIP_NEEDED

start /low /b /wait devenv ..\sumatrapdf-vc2008.vcproj /Rebuild "Release|Win32"
IF ERRORLEVEL 1 goto BUILD_FAILED
echo Compilation ok!
copy ..\obj-rel\SumatraPDF.exe ..\obj-rel\SumatraPDF-uncomp.exe
copy ..\obj-rel\SumatraPDF.pdb %OUT_PATH%\SumatraPDF-%VERSION%.pdb
@rem upx --best --compress-icons=0 ..\obj-rel\SumatraPDF.exe
start /low /b /wait upx --ultra-brute --compress-icons=0 ..\obj-rel\SumatraPDF.exe
IF ERRORLEVEL 1 goto PACK_FAILED

makensis installer
IF ERRORLEVEL 1 goto INSTALLER_FAILED

move SumatraPDF-install.exe ..\obj-rel\SumatraPDF-%VERSION%-install.exe
copy ..\obj-rel\SumatraPDF-%VERSION%-install.exe %OUT_PATH%\SumatraPDF-%VERSION%-install.exe

cd ..\obj-rel
rem don't bother compressing since our *.exe has already been packed
zip -0 SumatraPDF-%VERSION%.zip SumatraPDF.exe
copy SumatraPDF-%VERSION%.zip %OUT_PATH%\SumatraPDF-%VERSION%.zip
goto END

:VS9_NEEDED
echo Visual Studio 2008 (vs9) doesn't seem to be installed
goto END

:INSTALLER_FAILED
echo Installer script failed
goto END

:PACK_FAILED
echo Failed to pack executable with upx. Do you have upx installed?
goto END

:BUILD_FAILED
echo Build failed!
goto END

:VERSION_NEEDED
echo Need to provide version number e.g. build-release.bat 1.0
goto END

:NSIS_NEEDED
echo NSIS doesn't seem to installed in %ProgramFiles% or %ProgramFiles(x86)%\NSIS
echo Get it from http://nsis.sourceforge.net/Download
goto END

:ZIP_NEEDED
echo zip.exe doesn't seem to be available in PATH
goto END

:END
popd
