@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

REM ============================================================
REM 将 Release 版 UsbCanTools.exe 打成可拷贝文件夹（Qt + MinGW 运行时 + lib 下 DLL）
REM 用法（在「命令提示符」中）:
REM   deploy_mingw.bat "D:\path\to\build-...\release\UsbCanTools.exe"
REM
REM 请先修改下面的 QT_BIN 为你的 Qt 5.9 MinGW 安装路径（与 qmake 同目录）
REM ============================================================

set "QT_BIN=C:\Qt\Qt5.9.0\5.9\mingw53_32\bin"

if "%~1"=="" (
  echo 用法: %~nx0 "完整路径\UsbCanTools.exe"
  echo 示例: %~nx0 "D:\E-develop\develop\cantest_windows_qt\build-UsbCanTools-Desktop_Qt_5_9_0_MinGW_32bit-Release\release\UsbCanTools.exe"
  exit /b 1
)

set "EXE=%~1"
if not exist "%EXE%" (
  echo 找不到文件: %EXE%
  exit /b 1
)

for %%I in ("%EXE%") do set "OUTDIR=%%~dpI"
set "OUTDIR=%OUTDIR:~0,-1%"

set "WINDEPLOY=%QT_BIN%\windeployqt.exe"
if not exist "%WINDEPLOY%" (
  echo 未找到 windeployqt: %WINDEPLOY%
  echo 请编辑本脚本，将 QT_BIN 改为你的 Qt bin 目录。
  exit /b 1
)

echo [1/3] windeployqt（Qt 插件与 DLL、MinGW 运行库）...
"%WINDEPLOY%" --release --compiler-runtime "%EXE%"
if errorlevel 1 exit /b 1

echo [2/3] 复制工程 lib 目录下的厂商 DLL（如 ECanVci.dll）...
set "LIBDIR=%~dp0..\lib"
if exist "%LIBDIR%\*.dll" (
  copy /Y "%LIBDIR%\*.dll" "%OUTDIR%\"
) else (
  echo 警告: %LIBDIR% 下没有 .dll，请自行把 ECanVci.dll 拷到 %OUTDIR%
)

echo [3/3] 完成。
echo 可分发目录: %OUTDIR%
echo 将整个文件夹打成 zip 或再用 Inno Setup 做安装包即可。
exit /b 0
