
REM Testbed
bin\kohi.tools.exe importmanifest testbed.kapp\asset_manifest.kson --updated-only
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Kohi runtime
bin\kohi.tools.exe importmanifest kohi.runtime\asset_manifest.kson --updated-only
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Kohi UI
bin\kohi.tools.exe importmanifest kohi.plugin.ui.kui\asset_manifest.kson --updated-only
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

REM Kohi Utils
bin\kohi.tools.exe importmanifest kohi.plugin.utils\asset_manifest.kson --updated-only
IF %ERRORLEVEL% NEQ 0 (echo Error:%ERRORLEVEL% && exit)

ECHO "All assets imported \033[0;32msuccessfully\033[0m.\n"