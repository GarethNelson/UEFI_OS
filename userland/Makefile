PAWNCC=../pawn/source/compiler/build/pawncc

all: build/init

build/init: sbin/init/init.p
	${PAWNCC} -v2 $< -o$@
	mv $@.amx $@