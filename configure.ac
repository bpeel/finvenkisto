AC_PREREQ(2.59)

AC_INIT(finvenkisto, [0.1])
AC_CONFIG_SRCDIR(src/fv-main.c)
AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_HEADERS(config.h)
AC_CANONICAL_HOST
AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.11 foreign -Wno-portability no-define no-dist-gzip
                  dist-bzip2 tar-ustar])
AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_CPP
AC_PROG_RANLIB
AM_PROG_CC_C_O
AC_ISC_POSIX
AC_C_CONST

AC_C_BIGENDIAN([AC_DEFINE([HAVE_BIG_ENDIAN], [1], [System is big-endian])],
               [AC_DEFINE([HAVE_LITTLE_ENDIAN], [1],
                          [System is little-endian])])

AC_CHECK_SIZEOF([unsigned long])

AC_CHECK_LIB([m], sinf)
AC_CHECK_FUNCS([ffs ffsl])

ALL_WARNING_CFLAGS="-Wall -Wuninitialized -Wempty-body -Wformat
                    -Wformat-security -Winit-self -Wundef
                    -Wdeclaration-after-statement -Wvla
                    -Wpointer-arith -Wmissing-declarations"
AS_COMPILER_FLAGS([WARNING_CFLAGS],
                  [$ALL_WARNING_CFLAGS])
AC_SUBST([WARNING_CFLAGS])

PKG_CHECK_MODULES([GL], [gl])
PKG_CHECK_MODULES([X11], [x11])

AC_OUTPUT(
Makefile
src/data/Makefile
src/rply/Makefile
src/Makefile
)
