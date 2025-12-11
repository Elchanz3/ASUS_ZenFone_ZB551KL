#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of The Linux Foundation nor
#       the names of its contributors may be used to endorse or promote
#       products derived from this software without specific prior written
#       permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Invoke gcc, looking for warnings, and causing a failure if there are
# non-whitelisted warnings.

import errno
import re
import os
import sys
import subprocess

# TODO: Force consistent locale for gcc output
# Set environment variables to ensure consistent warning messages
env = os.environ.copy()
env['LANG'] = 'en_US.UTF-8'
env['LC_ALL'] = 'en_US.UTF-8'

allowed_warnings = set([
    "return_address.c:62",
    "hci_conn.c:407",
])

# Capture the name of the object file
ofile = None

warning_re = re.compile(r'''(.*/|)([^/]+\.[a-z]+:\d+):(\d+:)? warning:''')
def interpret_warning(line):
    """Decode the message from gcc. The messages we care about have a filename, and a warning"""
    line = line.rstrip('\n')
    m = warning_re.match(line)
    if m and m.group(2) not in allowed_warnings:
        print("error, forbidden warning:", m.group(2))

        # If there is a warning, remove any object if it exists.
        if ofile:
            try:
                os.remove(ofile)
            except OSError:
                pass
        sys.exit(1)

def run_gcc():
    global ofile
    args = sys.argv[1:]
    
    # Look for -o to capture output file
    try:
        i = args.index('-o')
        ofile = args[i+1]
    except (ValueError, IndexError):
        pass

    try:
        # Execute gcc with controlled environment and text-mode pipes
        proc = subprocess.Popen(
            args,
            stderr=subprocess.PIPE,
            universal_newlines=True,  # Python 3 text mode
            env=env  # Use consistent locale
        )
        
        # Process stderr line-by-line
        for line in proc.stderr:
            print(line, end='')  # Preserve original formatting
            interpret_warning(line)
        
        result = proc.wait()
    except OSError as e:
        result = e.errno
        if result == errno.ENOENT:
            print(f"{args[0]}: {e.strerror}")
            print('Is your PATH set correctly?')
        else:
            print(' '.join(args), str(e))

    return result

if __name__ == '__main__':
    status = run_gcc()
    sys.exit(status)
