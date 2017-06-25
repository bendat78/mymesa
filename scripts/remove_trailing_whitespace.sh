#!/bin/bash
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.py" \) -exec sed -i 's/[[:space:]]\+$//' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.py" \) -exec sed -i 's/^[[:space:]]*$//' {} \;

