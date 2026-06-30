#!/bin/bash

time make testbed-lib-debug

ERRORLEVEL=$?
if [ $ERRORLEVEL -ne 0 ]
then
	printf "There was one or more \033[0;31merror(s)\033[0m when building TESTBED DEBUG lib assembly.\n"
else
	printf "TESTBED DEBUG lib assembly built \033[0;32msuccessfully\033[0m.\n"
fi
