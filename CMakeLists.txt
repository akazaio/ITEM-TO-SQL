cmake_minimum_required(VERSION 3.20)
project(TBLtoSQLStudio LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "TBL to SQL Studio is a Windows desktop application.")
endif()

add_executable(TBLtoSQLStudio WIN32
  src/TBLtoSQLStudio.cpp
  TBLtoSQLStudio.rc
)

target_compile_features(TBLtoSQLStudio PRIVATE cxx_std_17)
target_compile_definitions(TBLtoSQLStudio PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(TBLtoSQLStudio PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /permissive->)
target_link_libraries(TBLtoSQLStudio PRIVATE Ole32 Comdlg32 Shell32 Dwmapi UxTheme)
set_target_properties(TBLtoSQLStudio PROPERTIES OUTPUT_NAME "TBLtoSQLStudio")
