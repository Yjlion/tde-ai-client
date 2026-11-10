# required stuff

tde_setup_compiler_flags( )

include( TestBigEndian )
test_big_endian( WORDS_BIGENDIAN )

tde_setup_largefiles( )

find_package( TQt )
find_package( TDE )


##### check for gcc visibility support #########

if( WITH_GCC_VISIBILITY )
  tde_setup_gcc_visibility( )
endif( )
