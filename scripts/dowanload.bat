@echo off
setlocal enabledelayedexpansion

rem Usage: dowanload.bat [openocd_path] [bootloader_elf] [app_elf]
rem If arguments omitted, defaults are used relative to the project.

rem Determine OpenOCD executable (arg1 or env OPENOCD or 'openocd')
if "%~1"=="" (
	if defined OPENOCD (
		set "OPENOCD=%OPENOCD%"
	) else (
		set "OPENOCD=openocd"
	)
) else (
	set "OPENOCD=%~1"
)

rem Optional OpenOCD scripts dir: use env OPENOCD_SCRIPTS if set
if defined OPENOCD_SCRIPTS (
	set "SCRIPTS_OPT=-s %OPENOCD_SCRIPTS%"
) else (
	set "SCRIPTS_OPT="
)

rem Resolve default paths relative to this scripts folder
set "SCRIPTS_DIR=%~dp0"
set "PROJ_ROOT=%SCRIPTS_DIR%.."
set "CFG=%PROJ_ROOT%\board\wch-riscv.cfg"

rem Bootloader and app ELF defaults (can be overridden by args)
if "%~2"=="" (
	set "BOOT=%PROJ_ROOT%\obj\bootloader.elf"
) else set "BOOT=%~2"

if "%~3"=="" (
	set "APP=%PROJ_ROOT%\obj\KeyBoard.elf"
) else set "APP=%~3"

rem Validate required files
if not exist "%CFG%" (
	echo ERROR: board config not found: "%CFG%"
	exit /b 2
)

if not exist "%APP%" (
	echo ERROR: application ELF not found: "%APP%"
	exit /b 3
)

rem Build OpenOCD -c command dynamically
set "CMD=init; reset halt;"
if exist "%BOOT%" (
	set "CMD=%CMD% program \"%BOOT%\";"
) else (
	echo Warning: bootloader not found, skipping: "%BOOT%"
)

set "CMD=%CMD% program \"%APP%\" verify;"
set "CMD=%CMD% reset; shutdown"

rem Create a timestamped log file in scripts dir
set "ts=%date%_%time%"
set "ts=%ts: =_%"
set "ts=%ts:/=-%"
set "ts=%ts::=-%"
set "ts=%ts:.=-%"
set "LOG=%SCRIPTS_DIR%download_%ts%.log"

echo Running: %OPENOCD% %SCRIPTS_OPT% -f "%CFG%" -c "%CMD%"
echo Log: "%LOG%"

%OPENOCD% %SCRIPTS_OPT% -f "%CFG%" -c "%CMD%" > "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
if %RC%==0 (
	echo Done. See log at "%LOG%"
) else (
	echo Failed (code %RC%). See log at "%LOG%"
)
exit /b %RC%