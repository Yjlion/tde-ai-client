#################################################
#
#  tde-ai-client - chat with AI services
#  for the Trinity Desktop Environment
#
#  This file is released under GPL >= 2
#
#################################################

##### set project version ########################

include( TDEVersion )
cmake_minimum_required( VERSION ${TDE_CMAKE_MINIMUM_VERSION} )

set( VERSION "0.1.0" )


#### general package setup

project( tde-ai-client )


#### include essential cmake modules

include( FindPkgConfig          )
include( CheckIncludeFile       )
include( CheckLibraryExists     )
include( CheckSymbolExists      )
include( CheckCSourceCompiles   )
include( CheckCXXSourceCompiles )


#### include our cmake modules

include( TDEMacros )


##### setup install paths

include( TDESetupPaths )
tde_setup_paths( )


##### optional stuff ############################

option( WITH_ALL_OPTIONS "Enable all optional support" OFF                                          )
option( WITH_GCC_VISIBILITY "Enable fvisibility and fvisibility-inlines-hidden" ${WITH_ALL_OPTIONS} )


##### user requested modules #####

option( BUILD_ALL "Build all" ON                     )
option( BUILD_DOC "Build documentation" ${BUILD_ALL} )


##### configure checks

include( ConfigureChecks.cmake )


###### global compiler settings

add_definitions( -DHAVE_CONFIG_H )

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TQT_CXX_FLAGS}" )
set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-undefined" )
set( CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,--no-undefined" )


##### directories

add_subdirectory( src )
add_subdirectory( icons )


##### other data ################################

tde_conditional_add_project_docs( BUILD_DOC )


##### write configure files

configure_file( config.h.cmake config.h @ONLY )
