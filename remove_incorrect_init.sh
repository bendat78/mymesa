
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*0[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*0[[:space:]]*}# = \{\}#' {} \;
