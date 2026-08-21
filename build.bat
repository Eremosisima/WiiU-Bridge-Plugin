@echo off
REM WiiU Bridge Plugin - Build Script

echo ================================
echo WiiU Bridge Plugin Build Script
echo ================================

REM Check if devkitPPC is available
where powerpc-eabi-gcc >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: powerpc-eabi-gcc not found
    echo Please install devkitPro and add to PATH
    pause
    exit /b 1
)

REM Build the plugin
echo.
echo Building Wii U plugin...
cd wiiu-plugin
make clean
make

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo Build successful!
echo.
echo To install:
echo 1. Copy wiiu_bridge_plugin.wps to SD card:
echo    /wiiu/environments/aroma/plugins/
echo 2. Run server on PC:
echo    python server.py
echo 3. Start Wii U and launch game
echo.

pause
