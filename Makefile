zipsfx: libarchive/.libs/libarchive.a
	cc -static -Wall -o $@ zipsfx.c  -larchive -Llibarchive/.libs -Ilibarchive -Ilibarchive/libarchive

libarchive/.libs/libarchive.a:
	cd libarchive && sh build/autogen.sh && sh configure --without-zlib --without-bz2lib  --without-libb2 --without-iconv --without-lz4  --without-zstd --without-lzma --without-cng  --without-xml2 --without-expat --without-openssl && $(MAKE)

zipsfx.zip:
	zip -0 $@ zipsfx.c Makefile

zipsfxzip: zipsfx zipsfx.zip
	cat zipsfx zipsfx.zip > $@
	chmod +x $@
