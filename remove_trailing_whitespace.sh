#!/bin/bash
find -not -path '*/\.*' -type f -name *.c -o -name *.h -o -name *.py -o -name *.html -exec sed -i 's/[[:space:]]*$//' {} \;

