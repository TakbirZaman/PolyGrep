# MFC front-end. Windows + MSVC with the "C++ MFC" workload installed.
set(CMAKE_MFC_FLAG 2)   # 2 = use MFC in a shared DLL (defines _AFXDLL); 1 = static

add_executable(tf_mfc WIN32
    src/ThreadForgeApp.cpp src/ThreadForgeApp.h
    src/MainDlg.cpp src/MainDlg.h
    res/resource.h res/ThreadForgeMfc.rc)

target_include_directories(tf_mfc PRIVATE src res)
target_compile_definitions(tf_mfc PRIVATE _UNICODE UNICODE)
target_compile_options(tf_mfc PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(tf_mfc PRIVATE threadforge)   # C ABI only: no STL crossing the DLL boundary

add_custom_command(TARGET tf_mfc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:threadforge> $<TARGET_FILE_DIR:tf_mfc>)
