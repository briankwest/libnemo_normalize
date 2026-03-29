dnl configure.ac for libnemo_normalize
AC_PREREQ([2.69])
AC_INIT([libnemo_normalize], [1.0.0], [brian@mcalester.net])
AC_CONFIG_SRCDIR([nemo_normalize.cpp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AM_SILENT_RULES([yes])

dnl Required for libtool shared library versioning
AM_PROG_AR
LT_INIT

dnl Compiler checks
AC_PROG_CXX
AC_PROG_CC
AC_LANG([C++])

dnl Verify C++17 support (compile a small snippet)
AC_MSG_CHECKING([whether $CXX supports C++17])
save_CXXFLAGS="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS -std=c++17"
AC_COMPILE_IFELSE(
  [AC_LANG_PROGRAM([[
    #include <variant>
    #include <optional>
  ]], [[
    std::variant<int, double> v = 42;
    std::optional<int> o = 7;
  ]])],
  [AC_MSG_RESULT([yes])],
  [AC_MSG_RESULT([no])
   AC_MSG_ERROR([A C++17 capable compiler is required])])
CXXFLAGS="$save_CXXFLAGS"

dnl Check for OpenFst via pkg-config, with --with-openfst-prefix fallback
AC_ARG_WITH([openfst-prefix],
  [AS_HELP_STRING([--with-openfst-prefix=DIR],
    [prefix where OpenFst is installed (e.g. /opt/conda)])],
  [openfst_prefix="$withval"],
  [openfst_prefix=""])

AS_IF([test -n "$openfst_prefix"],
  [FST_CFLAGS="-I${openfst_prefix}/include"
   FST_LIBS="-L${openfst_prefix}/lib -lfst -lfstfar"
   AC_SUBST([FST_CFLAGS])
   AC_SUBST([FST_LIBS])],
  [PKG_CHECK_MODULES([FST], [fst], [],
    [dnl pkg-config failed; try direct header/library check (Debian has no .pc)
     AC_MSG_NOTICE([pkg-config fst not found, trying direct detection])
     AC_CHECK_HEADER([fst/fstlib.h], [],
       [AC_MSG_ERROR([fst/fstlib.h not found. Install libfst-dev or use --with-openfst-prefix])])
     save_LIBS="$LIBS"
     LIBS="$LIBS -lfst -lfstfar"
     AC_LANG_PUSH([C++])
     save_CXXFLAGS2="$CXXFLAGS"
     CXXFLAGS="$CXXFLAGS -std=c++17"
     AC_LINK_IFELSE(
       [AC_LANG_PROGRAM([[#include <fst/fstlib.h>]],
                        [[fst::VectorFst<fst::StdArc> f;]])],
       [FST_CFLAGS=""
        FST_LIBS="-lfst -lfstfar"
        AC_SUBST([FST_CFLAGS])
        AC_SUBST([FST_LIBS])],
       [AC_MSG_ERROR([Cannot link against OpenFst. Install libfst-dev or use --with-openfst-prefix])])
     CXXFLAGS="$save_CXXFLAGS2"
     AC_LANG_POP([C++])
     LIBS="$save_LIBS"])])

dnl Check for dlopen
AC_CHECK_LIB([dl], [dlopen], [],
  [AC_MSG_ERROR([libdl is required])])

dnl Output files
AC_CONFIG_FILES([
  Makefile
  nemo_normalize.pc
])
AC_OUTPUT
