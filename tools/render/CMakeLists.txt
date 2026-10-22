# tachyon-render: a page from a file to a PNG. tachyon-render on the host,
# RENDER.EXE (a console program) for Windows 95.
add_library(render_page STATIC render_page.c)
target_include_directories(render_page PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(render_page PUBLIC tachyon_core)

add_executable(render render.c)
target_link_libraries(render PRIVATE render_page)
if(WIN32)
  set_target_properties(render PROPERTIES OUTPUT_NAME RENDER)
  tachyon_win95_check(render)
else()
  set_target_properties(render PROPERTIES OUTPUT_NAME tachyon-render)
endif()
