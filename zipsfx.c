#include <sys/types.h>
#include <sys/stat.h>

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
    if(argc < 2)
        return 1;

    const char *filename = argv[1];

    struct archive *a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_format_iso9660(a);
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
            printf("#dd if=\"%s\" of=\"%s\" bs=1 skip=%zu count=%zu\n", filename, archive_entry_pathname(entry), byte_offset, byte_size);
        }
        else
            printf("#false #%s %d = %s\n", archive_entry_pathname(entry), filetype, filetype == AE_IFMT ? "AE_IFMT" : filetype == AE_IFREG ? "AE_IFREG" : filetype == AE_IFLNK ? "AE_IFLNK" : filetype == AE_IFSOCK ? "AE_IFSOCK" : filetype == AE_IFCHR ? "AE_IFCHR" : filetype == AE_IFBLK ? "AE_IFBLK" : filetype == AE_IFDIR ? "AE_IFDIR" : filetype == AE_IFIFO ? "AE_IFIFO" : "archive_entry_pathname(entry) value is unknown");
        
        r = archive_read_data_skip(a);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return r; }
    }
    assert(ARCHIVE_OK == archive_read_close(a));
    assert(ARCHIVE_OK == archive_read_free(a));
    return 0;
}
