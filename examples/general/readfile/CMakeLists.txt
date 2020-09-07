list( APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake )

set( APP readtest)

add_executable( ${APP} "${APP}.cpp" )

target_link_libraries( ${APP} 
                       ${VL_LIB}
		               ${M5_LIB}
                       ${CMAKE_SCOTCH_LIBS}
                       raft                       
                       demangle
                       affinity
                       ${CMAKE_THREAD_LIBS_INIT} 
                       ${CMAKE_QTHREAD_LIBS}
                       )
