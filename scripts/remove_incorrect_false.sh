#false
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([a-zA-Z_]*\)[[:space:]]*!=[[:space:]]*true[[:space:]]*)#(!\1)#' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([a-zA-Z_]*\)[[:space:]]*==[[:space:]]*false[[:space:]]*)#(!\1)#' {} \;
#true
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([a-zA-Z_]*\)[[:space:]]*==[[:space:]]*true[[:space:]]*)#(\1)#' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#([[:space:]]*\([a-zA-Z_]*\)[[:space:]]*!=[[:space:]]*false[[:space:]]*)#(\1)#' {} \;

