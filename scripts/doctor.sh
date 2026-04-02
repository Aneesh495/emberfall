#!/bin/sh
set -eu
uname -smr
if [ "$(uname -s)" = Darwin ]; then
    sw_vers
    sysctl machdep.cpu.brand_string hw.memsize hw.logicalcpu
else
    lscpu
    head -n 1 /proc/meminfo
fi
df -h .
for program in cmake ninja c++ glslangValidator python3; do
    if command -v "$program" >/dev/null; then
        "$program" --version | head -n 1
    else
        printf '%s\n' "$program unavailable"
    fi
done
if command -v vulkaninfo >/dev/null; then
    vulkaninfo --summary
else
    printf '%s\n' 'vulkaninfo unavailable'
fi
