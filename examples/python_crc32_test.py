#!/usr/bin/python3

# Build required code:
# $ ./examples/buildall.py
#
#
# Run test:
# $ echo hi | LD_LIBRARY_PATH=build PYTHONPATH=build python3 examples/python_crc32_test.py
#

import os
import time
import sys
import threading
import argparse
import threading

import libcsp_py3 as libcsp

def getOptions():
    parser = argparse.ArgumentParser(description="Parses command.")
    parser.add_argument("-D", "--debug", default=False, help="Turn on debug")
    return parser.parse_args(sys.argv[1:])


if __name__ == "__main__":

    options = getOptions()
    
    buf = sys.stdin.buffer.read()
    
    print("buf: " + buf.hex())

    print("Len: " + str(len(buf)))
    print("CRC: " + hex(libcsp.crc32_memory(buf, len(buf))))