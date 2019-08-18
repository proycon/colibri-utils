ACLOCAL_AMFLAGS =-I m4 --install

SUBDIRS = src m4 data #include

EXTRA_DIST = bootstrap.sh README.md

ChangeLog: NEWS
	git pull; git2cl > ChangeLog
