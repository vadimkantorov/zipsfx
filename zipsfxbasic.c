#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <archive.h>
#include <archive_entry.h>

int
main(int argc, const char **argv)
{
    if(argc != 1 && argc != 2)
        return -1;
    
    const char *filename = argv[0];
    const char* membername = argc == 2 ? argv[1] : NULL;

    struct archive *a = archive_read_new();
    archive_read_support_format_zip(a);
    
    assert(ARCHIVE_OK == archive_read_open_filename(a, filename, 10240));
    
    for(;;)
    {
        struct archive_entry *entry;
        int r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return r; }

        if(membername == NULL)
        {
            puts(archive_entry_pathname(entry));
        }
        else if(0 == strcmp(membername, archive_entry_pathname(entry)))
        {
            const void* block_buff;
            size_t block_len;
            int64_t block_offset;
            for(;;)
            {
                int r = archive_read_data_block(a, &block_buff, &block_len, &block_offset);
                if (r == ARCHIVE_EOF || r != ARCHIVE_OK)
                    break;
                fwrite(block_buff, 1, block_len, stdout);
            }
            return 0;
        }
        else
        {
            r = archive_read_data_skip(a);
            if (r == ARCHIVE_EOF) break;
            if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return r; }
        }
    }
    assert(ARCHIVE_OK == archive_read_close(a));
    assert(ARCHIVE_OK == archive_read_free(a));
    return 0;
}
