noinst_LIBRARIES = librply.a
noinst_PROGRAMS = convert

AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(NULL)

librply_a_SOURCES = \
	rply.c \
	rply.h

convert_SOURCES = \
	convert.c

convert_LDADD = librply.a
