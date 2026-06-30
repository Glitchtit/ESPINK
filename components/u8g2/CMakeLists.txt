# U8g2 vendored from upstream/csrc plus the ESPINK RAM-only backend.
# u8g2_fonts.c is large; unused fonts are dropped at link time by --gc-sections.
file(GLOB U8G2_UPSTREAM_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/upstream/csrc/*.c)

idf_component_register(
    SRCS ${U8G2_UPSTREAM_SRCS} "u8g2_espink.c"
    INCLUDE_DIRS "upstream/csrc" "."
)

# U8g2's fonts file is huge; keep optimisation reasonable and silence a couple
# of upstream-only warnings so the project's -Werror does not trip on vendored code.
set_source_files_properties(${U8G2_UPSTREAM_SRCS}
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-but-set-variable;-Wno-implicit-fallthrough")
