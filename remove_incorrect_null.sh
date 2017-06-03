
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*NULL[[:space:]]*}# = \{\}#' {} \;
