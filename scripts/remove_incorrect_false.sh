#false
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([0-9a-zA-Z_]*\)[[:space:]]*!=[[:space:]]*true[[:space:]]*)#(!\1)#i' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([0-9a-zA-Z_]*\)[[:space:]]*==[[:space:]]*false[[:space:]]*)#(!\1)#i' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([0-9a-zA-Z_]*\)[[:space:]]*==[[:space:]]*0[[:space:]]*)#(!\1)#i' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([0-9a-zA-Z_]*\)[[:space:]]*==[[:space:]]*NULL[[:space:]]*)#(!\1)#i' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([0-9a-zA-Z_]*\)[[:space:]]*==[[:space:]]*nullptr[[:space:]]*)#(!\1)#i' {} \;
#true
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([0-9a-zA-Z_]*\)[[:space:]]*==[[:space:]]*true[[:space:]]*)#(\1)#i' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([0-9a-zA-Z_]*\)[[:space:]]*!=[[:space:]]*false[[:space:]]*)#(\1)#i' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([0-9a-zA-Z_]*\)[[:space:]]*!=[[:space:]]*0[[:space:]]*)#(\1)#i' {} \;

