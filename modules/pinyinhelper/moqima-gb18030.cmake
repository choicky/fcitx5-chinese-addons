# Pinned identity of the MoQi reverse lookup table (gaboolic/moqima-tables).
#
# Kept in one place so every build system that needs the table uses exactly the
# same source: this addon fetches it for its own install tree, and build systems
# that package the addon can read this file instead of repeating the pin
# (fcitx5-android installs this addon's "config" component into its APK assets).
#
# To update the table: point MOQI_TABLE_COMMIT at the new upstream commit, run
# a build to let the download fail on the old hash, copy the new SHA256 from
# the error, and update both values here.
set(MOQI_TABLE_COMMIT "6d8ba8f1c57466f358e682baefe11bbd0fe389ab")
set(MOQI_TABLE_SHA256 "66deab4aaba1285e3c85eb3a364c21bc08db1911b61df8e934f0d006ca7e7923")
set(MOQI_TABLE_URL "https://raw.githubusercontent.com/gaboolic/moqima-tables/${MOQI_TABLE_COMMIT}/moqima_gb18030.txt")
