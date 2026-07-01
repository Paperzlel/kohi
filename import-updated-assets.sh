#!/bin/bash

# Testbed
./bin/kohi.tools importmanifest testbed.kapp/asset_manifest.kson --updated-only
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
printf "Asset import \033[0;31mfailed\033[0m:${errorlevel}.\n" && exit
fi

# Kohi runtime
./bin/kohi.tools importmanifest kohi.runtime/asset_manifest.kson --updated-only
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
printf "Asset import \033[0;31mfailed\033[0m:${errorlevel}.\n" && exit
fi

# Kohi UI
./bin/kohi.tools importmanifest kohi.plugin.ui.kui/asset_manifest.kson --updated-only
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
printf "Asset import \033[0;31mfailed\033[0m:${errorlevel}.\n" && exit
fi

# Kohi Utils
./bin/kohi.tools importmanifest kohi.plugin.utils/asset_manifest.kson --updated-only
ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
printf "Asset import \033[0;31mfailed\033[0m:${errorlevel}.\n" && exit
fi

printf "All assets imported \033[0;32msuccessfully\033[0m.\n"