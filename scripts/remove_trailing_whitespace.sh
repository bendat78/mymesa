#!/bin/bash
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.h" -o -name "*.py" \) -exec sed -i 's/[[:space:]]*$//' {} \;

