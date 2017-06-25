#sed -i '/(~0u)/b; s#[[:space:]]*=[[:space:]]*~0# = (~0u)#'
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#[[:space:]]*==[[:space:]]*~0u# == (~0u)#i' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#[[:space:]]*==[[:space:]]*~0# == (~0u)#i' {} \;


