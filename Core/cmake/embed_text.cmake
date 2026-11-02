# Writes IN as a C++ string constant (g_attract_games_xml) to OUT.
file(READ ${IN} hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
file(WRITE ${OUT} "// Generated from ${IN}: do not edit.\nstatic const char kText[] = {${hex}0x00};\nextern const char *const g_attract_games_xml = kText;\n")
