
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*}# = \{\}#ig' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*}[[:space:]]*}# = \{\}#ig' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*NULL[[:space:]]*}[[:space:]]*}# = \{\}#ig' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*NULL[[:space:]]*}# = \{\}#ig' {} \;
