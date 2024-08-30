#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <archive.h>
#include <archive_entry.h>
// define required for #include <archive_read_private.h>
#define __LIBARCHIVE_BUILD
#include <archive_read_private.h>

int cat_mmap(const char* filename, size_t offset, size_t size)
{
    int fd = open(filename, O_RDONLY);
    const char* ptr = (const char*)mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);

    //printf("%p %c%c%c\n", ptr, ptr[1], ptr[2], ptr[3]);
    //printf("dd if=%s of=tmp.bin bs=1 skip=%zu count=%zu\n", filename, offset, size);
    //write(1, ptr + offset, size);
    fwrite(ptr + offset, 1, size, stdout);
    //close(fd);
    return 0;
}

int cat_fread(const char* filename, size_t offset, size_t size)
{
    FILE* f = fopen(filename, "rb");
    fseek(f, offset, SEEK_SET);
    char buf[1024];
    while(size > 0)
    {
        int len = fread(buf, 1, sizeof(buf) <= size ? sizeof(buf) : size, f);
        fwrite(buf, 1, len, stdout);
        size -= len;
    }
    fclose(f);
    return 0;
}

int cat_direct(struct archive* a, const void* firstblock_buff, size_t firstblock_len, int64_t firstblock_offset)
{
    fwrite(firstblock_buff, 1, firstblock_len, stdout);
    for(;;)
    {
        int r = archive_read_data_block(a, &firstblock_buff, &firstblock_len, &firstblock_offset);
        if (r == ARCHIVE_EOF || r != ARCHIVE_OK)
            break;
        fwrite(firstblock_buff, 1, firstblock_len, stdout);
    }
    return 0;
}


void* last_file_buff;
size_t last_file_block_size;
size_t last_file_offset;

archive_read_callback* old_file_read;
archive_seek_callback* old_file_seek;

static ssize_t
new_file_read(struct archive *a, void *client_data, const void **buff)
{
    // struct read_file_data copied from https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_filename.c
    struct read_file_data {
            int	 fd;
            size_t	 block_size;
            void	*buffer;
            //mode_t	 st_mode;  /* Mode bits for opened file. */
            //char	 use_lseek;
            //enum fnt_e { FNT_STDIN, FNT_MBS, FNT_WCS } filename_type;
            //union {
            //	char	 m[1];/* MBS filename. */
            //	wchar_t	 w[1];/* WCS filename. */
            //} filename; /* Must be last! */
    } *mine = client_data;
    last_file_buff = mine->buffer;
    last_file_block_size = mine->block_size;
    last_file_offset = old_file_seek(a, client_data, 0, SEEK_CUR);
    
    return old_file_read(a, client_data, buff);
}

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
    
    // struct archive_read in https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_private.h
    struct archive_read *_a = ((struct archive_read *)a);
    old_file_read = _a->client.reader;
    old_file_seek = _a->client.seeker;
    
    a->state = ARCHIVE_STATE_NEW;
    archive_read_set_read_callback(a, new_file_read);
    assert(ARCHIVE_OK ==  archive_read_open1(a));
    
    for(;;)
    {
        struct archive_entry *entry;
        int r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return r; }

        const void* firstblock_buff;
        size_t firstblock_len;
        int64_t firstblock_offset;
        r = archive_read_data_block(a, &firstblock_buff, &firstblock_len, &firstblock_offset);
        
        int filetype = archive_entry_filetype(entry);
        if(filetype == AE_IFREG && archive_entry_size_is_set(entry) != 0 && last_file_buff != NULL && last_file_buff <= firstblock_buff && firstblock_buff < last_file_buff + last_file_block_size)
        {
            size_t byte_size = (size_t)archive_entry_size(entry);
            size_t byte_offset = last_file_offset + (size_t)(firstblock_buff - last_file_buff);
            (void)byte_size;
            (void)byte_offset;
            if(membername == NULL)
                puts(archive_entry_pathname(entry));
            else if(0 == strcmp(membername, archive_entry_pathname(entry)))
                //return cat_direct(a, firstblock_buff, firstblock_len, firstblock_offset);
                //return cat_fread(filename, byte_offset, byte_size);
                return cat_mmap(filename, byte_offset, byte_size);
        }
        
        r = archive_read_data_skip(a);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return r; }
    }
    assert(ARCHIVE_OK == archive_read_close(a));
    assert(ARCHIVE_OK == archive_read_free(a));
    return 0;
}
