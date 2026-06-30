#!/bin/bash

time make all-debug

ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
	printf "There was one or more \033[0;31merror(s)\033[0m when building DEBUG assemblies.\n"
else
	printf "All DEBUG assemblies built \033[0;32msuccessfully\033[0m.\n"
fi

