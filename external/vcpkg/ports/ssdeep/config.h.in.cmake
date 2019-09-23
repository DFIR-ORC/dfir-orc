/* Define if building universal (internal helper macro) */
//#undef AC_APPLE_UNIVERSAL_BUILD

#ifndef CONFIG_H
#define CONFIG_H

#cmakedefine HAVE_DIRENT_H
#cmakedefine HAVE_DLFCN_H
#cmakedefine HAVE_FCNTL_H
#cmakedefine HAVE_FSEEKO
#cmakedefine HAVE_INTTYPES_H
#cmakedefine HAVE_LIBGEN_H
#cmakedefine HAVE_MEMORY_H
#cmakedefine HAVE_STDINT_H
#cmakedefine HAVE_STDLIB_H
#cmakedefine HAVE_STRINGS_H
#cmakedefine HAVE_STRING_H
#cmakedefine HAVE_SYS_DISK_H
#cmakedefine HAVE_SYS_IOCTL_H
#cmakedefine HAVE_SYS_MOUNT_H
#cmakedefine HAVE_SYS_PARAM_H
#cmakedefine HAVE_SYS_STAT_H
#cmakedefine HAVE_SYS_TYPES_H
#cmakedefine HAVE_UNISTD_H
#cmakedefine HAVE_WCHAR_H

/* Define to 1 if the user chose to disable bit-parallel string operations. */
//#undef SSDEEP_DISABLE_POSITION_ARRAY

/* Define to 1 if you have the ANSI C header files. */
//#undef STDC_HEADERS

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
#  undef WORDS_BIGENDIAN
# endif
#endif

/* Enable large inode numbers on Mac OS X 10.5.  */
#ifndef _DARWIN_USE_64_BIT_INODE
# define _DARWIN_USE_64_BIT_INODE 1
#endif

/* Number of bits in a file offset, on hosts where this is settable. */
//#undef _FILE_OFFSET_BITS

/* Define to 1 to make fseeko visible on some hosts (e.g. glibc 2.2). */
//#undef _LARGEFILE_SOURCE

/* Define for large files, on AIX-style hosts. */
//#undef _LARGE_FILES

/* Linux operating system functions */
#undef __LINUX__

#endif
