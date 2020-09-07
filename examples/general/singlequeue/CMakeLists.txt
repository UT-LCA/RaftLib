list( APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake )

set( APP singlequeue )

add_executable( ${APP} "${APP}.cpp" )

target_link_libraries( ${APP} 
                       ${VL_LIB}
		               ${M5_LIB}
                       raft                       
                       demangle
                       affinity
                       ${CMAKE_THREAD_LIBS_INIT} 
                       ${CMAKE_QTHREAD_LIBS}
                       )
