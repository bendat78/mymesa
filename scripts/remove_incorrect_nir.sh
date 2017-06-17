
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*NIR_SRC_INIT[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*NIR_SRC_INIT[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*NIR_SRC_INIT[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*{[[:space:]]*NIR_SRC_INIT[[:space:]]*}[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*NIR_SRC_INIT[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*NIR_SRC_INIT[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*NIR_DEST_INIT[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*NIR_DEST_INIT[[:space:]]*}[[:space:]]*,[[:space:]]*{[[:space:]]*NIR_DEST_INIT[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*{[[:space:]]*NIR_DEST_INIT[[:space:]]*}[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*{[[:space:]]*NIR_DEST_INIT[[:space:]]*}[[:space:]]*}# = \{\}#' {} \;
find -not -path '*/\.*' -type f -exec sed -i 's#[[:space:]]*=[[:space:]]*{[[:space:]]*NIR_DEST_INIT[[:space:]]*}# = \{\}#' {} \;
