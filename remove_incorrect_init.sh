
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -not -name "*.py" -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*0[[:space:]]*}# = \{\}#' {} \;
