#!/bin/bash
find -not -path '*/\.*' -type f -exec sed -i 's/[[:space:]]*$//' {} \;

