#false
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*!=[[:space:]]*true[[:space:]]*)#\1!\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*==[[:space:]]*false[[:space:]]*)#\1!\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*==[[:space:]]*0[[:space:]]*)#\1!\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*==[[:space:]]*NULL[[:space:]]*)#\1!\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*==[[:space:]]*nullptr[[:space:]]*)#\1!\2)#ig' {} \;
#true
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*==[[:space:]]*true[[:space:]]*)#\1\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*!=[[:space:]]*false[[:space:]]*)#\1\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*!=[[:space:]]*0[[:space:]]*)#\1\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*!=[[:space:]]*NULL[[:space:]]*)#\1\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]*!=[[:space:]]*nullptr[[:space:]]*)#\1\2)#ig' {} \;
find -not -path '*/\.*' -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" \) -exec sed -i 's#\([[:space:]]*if[[:space:]]*(\)[[:space:]]*\([[:graph:]]*\)[[:space:]]\+>=[[:space:]]*1[[:space:]]*)#\1\2)#ig' {} \;

