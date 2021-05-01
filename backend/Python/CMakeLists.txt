target_sources(ScriptX PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/PyEngine.cc
        ${CMAKE_CURRENT_LIST_DIR}/PyEngine.h
        ${CMAKE_CURRENT_LIST_DIR}/PyException.cc
        ${CMAKE_CURRENT_LIST_DIR}/PyLocalReference.cc
        ${CMAKE_CURRENT_LIST_DIR}/PyNative.cc
        ${CMAKE_CURRENT_LIST_DIR}/PyNative.hpp
        ${CMAKE_CURRENT_LIST_DIR}/PyReference.hpp
        ${CMAKE_CURRENT_LIST_DIR}/PyUtils.cc
        ${CMAKE_CURRENT_LIST_DIR}/PyValue.cc
        )