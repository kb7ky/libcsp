#!/bin/bash
#
# run this script from the top level csp directory (one containing build)

LD_LIBRARY_PATH=build PYTHONPATH=build python3 $*
