#!/usr/bin/python3

# Build required code:
# $ ./examples/buildall.py
#
# Start zmqproxy (only one instance)
# $ ./build/zmqproxy
#
# Run server, default enabling ZMQ interface:
# $ LD_LIBRARY_PATH=build PYTHONPATH=build python3 examples/python_bindings_example_server.py
#

import os
import time
import sys
import threading

import libcsp_py3 as libcsp

if __name__ == "__main__":

    #initialize libcsp with params:
        # 27              - CSP address of the system (default=1)
        # "test_service"  - Host name, returned by CSP identity requests
        # "bindings"      - Model, returned by CSP identity requests
        # "1.2.3"         - Revision, returned by CSP identity requests
    # See "include\csp\csp.h" - lines 42-80 for more detail
    # See "src\bindings\python\pycsp.c" - lines 128-156 for more detail
    libcsp.init(27, "test_sockets", "bindings", "1.2.3")