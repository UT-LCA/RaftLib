list( APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake )

##
# c/c++ std
##
include( CheckSTD )

if( STRING_NAMES )
    set( TESTAPPS
         fitInCacheLine
         doesntFitInCacheLine
         isExtClass
         isInlineClass
         isInlineNonClass
         isExtArray
         isExtAlloc
         streamingData
         noPortException
         portTypeException
         disconnectedGraph
         twiceAdd
         bracketsTest
         #partitionTest
         fixedMatchTest
         stdlibsupport
         armqMini
         #peek
         #peekrange
         allocate
         #allocate_s
         allocateSendPush
         allocateSendRandomPush
         allocatePopPush
         allocatePopInternal
         allocatePopInternalObject
         allocatePopExternal
         lambdatest
         #foreach
         #dynallocate
         #split
         #join
         #parallel
         uniform
         chain
         #parallelchain
         #splitchain
         chainMultiplePorts
         #staticLongSplitChain
         #staticSplitJoin
         #staticSplitChainJoin
         #staticJoin
         #staticContJoin
         #multiPrint
         #poolThreadRaftManip
         #select_in_t
         #splitchainRetStruct
         #staticContJoinRetStruct
         #staticJoinRetStruct
         #staticLongSplitChainRetStruct
         #staticSplitChainJoinRetStruct
         #staticSplitJoinRetStruct
         ###fixedAllocate
         #allocate_s_string
         #allpush
         #nonTrivialAllocatorPop
         #nonTrivialAllocatorPopExternal
         vectorAlloc
         stringAlloc
         #reduction
         )
else()
    set( TESTAPPS
         allocate
         allocatePopExternal
         )
endif()

if( BUILDRANDOM )
    list( APPEND TESTAPP gamma uniform gaussian exponential sequential )
endif( BUILDRANDOM )

foreach( APP ${TESTAPPS} )
    add_executable( ${APP} "${APP}.cpp" )
    target_link_libraries( ${APP}
                           demangle
                           affinity
                           ${CMAKE_THREAD_LIBS_INIT}
                           ${CMAKE_QTHREAD_LIBS}
                           )
    target_include_directories( ${APP}
                                PUBLIC
                                ${PROJECT_SOURCE_DIR}/raftinc
                                ${PROJECT_SOURCE_DIR}
                                ${PROJECT_BINARY_DIR}/raftinc
                                ${QTHREAD_INCLUDE}
                              )
    add_test( NAME "${APP}_test" COMMAND ${APP} )
endforeach( APP ${TESTAPPS} )

file( COPY alice.txt
      DESTINATION ${CMAKE_CURRENT_BINARY_DIR} )
