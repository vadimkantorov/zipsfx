This example takes advantage that ZIP appended to ELF is both a valid ELF and ZIP. I use libarchive to scan the process file and then use either mmap or fread to open the entries embedded in the ZIP.

```shell
make zipsfx
make zipsfx.zip
make zipsfxzip

./zipsfx
./zipsfxzip
./zipsfxzip Makefile
``` 
