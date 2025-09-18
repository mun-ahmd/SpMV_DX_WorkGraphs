:: This file is part of the AMD & HSC Work Graph Playground.
::
:: Copyright (C) 2025 Advanced Micro Devices, Inc. and Coburg University of Applied Sciences and Arts.
:: All rights reserved.
::
:: Permission is hereby granted, free of charge, to any person obtaining a copy
:: of this software and associated documentation files(the "Software"), to deal
:: in the Software without restriction, including without limitation the rights
:: to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
:: copies of the Software, and to permit persons to whom the Software is
:: furnished to do so, subject to the following conditions :
::
:: The above copyright notice and this permission notice shall be included in
:: all copies or substantial portions of the Software.
::
:: THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
:: IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
:: FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
:: AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
:: LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
:: OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
:: THE SOFTWARE.

@echo off
setlocal

REM This script downloads the Microsoft.Direct3D.WARP package from NuGet
REM and extracts the d3d10warp.dll file to the current directory.

set MESH_NODES=0

if "%MESH_NODES%"=="1" (
    set VERSION=1.0.14.1-preview
) else (
    set VERSION=1.0.14.2
)
set DOWNLOAD_URL=https://www.nuget.org/api/v2/package/Microsoft.Direct3D.WARP/%VERSION%
set LICENSE_URL=https://www.nuget.org/packages/Microsoft.Direct3D.WARP/%VERSION%/License

set DLL_NAME=d3d10warp.dll

REM Check if WARP dll already exists
if not exist %DLL_NAME% (
    goto DOWNLOAD
)

echo %DLL_NAME% already exists in the current directory.
set /p OVERWRITE=Do you want to download it again? (y/N): 
if /i not "%OVERWRITE%"=="y" (
    exit /b
)

:DOWNLOAD
echo The Microsoft.Direct3D.WARP package is made available by Microsoft under the Microsoft Software License Terms and is not distributed by AMD.
echo You can view the license terms at: %LICENSE_URL%
echo.
REM Prompt user to accept the license terms
set /p LICENSE_ACCEPT=Do you accept the license terms for Microsoft.Direct3D.WARP? (y/N): 
if /i not "%LICENSE_ACCEPT%"=="y" (
    exit /b
)

REM Download the package
echo Downloading nuget package from %DOWNLOAD_URL%...
powershell -Command "Invoke-WebRequest -Uri '%DOWNLOAD_URL%' -OutFile Microsoft.Direct3D.WARP-%VERSION%.zip"
if errorlevel 1 (
    echo Error: Failed to download the package from %DOWNLOAD_URL%.
    pause
    exit /b 1
)

REM Extract package
echo Extracting Microsoft.Direct3D.WARP package...
powershell -Command "Expand-Archive -Path 'Microsoft.Direct3D.WARP-%VERSION%.zip' -DestinationPath .\Microsoft.Direct3D.WARP-%VERSION% -Force"
if errorlevel 1 (
    echo Error: Failed to extract the package.
    pause
    exit /b 1
)

REM Check if WARP dll exists in the extracted folder
if not exist "Microsoft.Direct3D.WARP-%VERSION%\build\native\bin\x64\%DLL_NAME%" (
    echo Error: %DLL_NAME% not found in the extracted package.
    pause
    exit /b 1
)

REM Copy WARP dll to current directory
copy ".\Microsoft.Direct3D.WARP-%VERSION%\build\native\bin\x64\%DLL_NAME%" ".\%DLL_NAME%" >nul 2>&1
if errorlevel 1 (
    echo Error: Failed to copy %DLL_NAME%.
    pause
    exit /b 1
)

echo Successfully downloaded and copied %DLL_NAME% to the current directory.

REM Clean up downloaded files
del "Microsoft.Direct3D.WARP-%VERSION%.zip"
rmdir /s /q "Microsoft.Direct3D.WARP-%VERSION%"

echo Cleaned up temporary files.
echo Done.

pause

endlocal
