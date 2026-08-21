@echo off
REM WiiU Bridge Server - Run Script

echo ================================
echo WiiU Bridge Server
echo ================================

REM Check if Python is available
where python >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: Python not found
    echo Please install Python 3.x and add to PATH
    pause
    exit /b 1
)

REM Check if pygame is installed
python -c "import pygame" 2>nul
if %errorlevel% neq 0 (
    echo Installing pygame...
    pip install pygame
)

echo.
echo Starting WiiU Bridge Server...
echo Press Ctrl+C to stop
echo.

python server_pygame.py %*

pause
