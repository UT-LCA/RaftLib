list( APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake )


##
# we could just copy the dir over, but
# this way takes care of permissions too
##

function( install_helper SRC_DIR DST_DIR )
    ##
    # create raftinc dir in includes
    ##
    install( DIRECTORY DESTINATION ${DST_DIR} )
    file( GLOB TEMPLATES   ${SRC_DIR}/*.tcc )
    file( GLOB CPP_HEADERS ${SRC_DIR}/*.hpp )
    file( GLOB C_HEADERS   ${SRC_DIR}/*.h   )
    set( INST_INC ${TEMPLATES} ${CPP_HEADERS} ${C_HEADERS} )
    foreach( INST_FILE ${INST_INC} )
        install( FILES ${INST_FILE} DESTINATION ${DST_DIR} )
    endforeach()

    file( GLOB CHILDREN RELATIVE ${SRC_DIR} ${SRC_DIR}/* )
    foreach( CHILD ${CHILDREN} )
        if( IS_DIRECTORY ${SRC_DIR}/${CHILD} )
            install_helper( ${SRC_DIR}/${CHILD} ${DST_DIR}/${CHILD} )
        endif()
    endforeach()
endfunction()

set( RAFTDIR_NAME "raftinc" )
set( RAFTDIR ${CMAKE_INSTALL_PREFIX}/include/${RAFTDIR_NAME} )

install_helper( ${CMAKE_CURRENT_LIST_DIR} ${RAFTDIR} )

