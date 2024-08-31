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
void* last_file_buff;
size_t last_file_block_size;
size_t last_file_offset;
archive_read_callback* old_file_read;
archive_seek_callback* old_file_seek;
static ssize_t
new_file_read(struct archive *a, void *client_data, const void **buff)
{
    // struct read_file_data copied from https://github.com/libarchive/libarchive/blob/master/libarchive/archive_read_open_filename.c
    struct read_file_data {int fd; size_t block_size; void* buffer;} *mine = client_data;
    last_file_buff = mine->buffer;
    last_file_block_size = mine->block_size;
    last_file_offset = old_file_seek(a, client_data, 0, SEEK_CUR);
    return old_file_read(a, client_data, buff);
}

enum {zipsfx_index_filenames_num = 1024,  zipsfx_index_filenames_len = 128};
struct zipsfx_index
{
    FILE* file; char* mmapptr; size_t mmapsize;
    char filenames[zipsfx_index_filenames_num * zipsfx_index_filenames_len];
    size_t files_num, filenames_lens[zipsfx_index_filenames_num], offsets[zipsfx_index_filenames_num], sizes[zipsfx_index_filenames_num];
};
void zipsfx_list(struct zipsfx_index* index)
{
    size_t filenames_start = 0;
    for(size_t i = 0; i < index->files_num; i++)
    {
        puts(index->filenames + filenames_start);
        filenames_start += index->filenames_lens[i] + 1;
    }
}

FILE* zipsfx_fopen(struct zipsfx_index* index, const char filename[], const char mode[])
{
    if(0 != strcmp("rb", mode))
        return NULL;
    
    size_t filenames_start = 0;
    for(size_t i = 0; i < index->files_num; i++)
    {
        if(0 == strcmp(index->filenames + filenames_start, filename))
        {
            size_t offset = index->offsets[i], size = index->sizes[i];
            if(index->file != NULL)
            {
                FILE* f = fmemopen(NULL, size, "rb+");
                fseek(index->file, offset, SEEK_SET);
                char buf[8192];
                while(size > 0)
                {
                    size_t len = fread(buf, 1, sizeof(buf) <= size ? sizeof(buf) : size, index->file);
                    fwrite(buf, 1, len, f);
                    size -= len;
                }
                fseek(f, 0, SEEK_SET);
                return f;
            }
            else
            {
                return fmemopen(index->mmapptr + offset, size, "rb");
            }
        }
        filenames_start += index->filenames_lens[i] + 1;
    }
    return NULL;
}
void zipsfx_index_destroy(struct zipsfx_index* index)
{
    if(index->file != NULL)
        fclose(index->file);
    if(index->mmapptr != NULL)
        munmap(index->mmapptr, index->mmapsize);
}
int zipsfx_index_build(struct zipsfx_index* index, const char filename[], int use_mmap)
{
    index->files_num = 0;
    if(use_mmap)
    {
        int fd = open(filename, O_RDONLY);
        if(fd < 0) return -1;
        struct stat file_info; assert(fstat(fd, &file_info) >= 0);
        index->file = NULL;
        index->mmapsize = file_info.st_size;
        index->mmapptr = mmap(NULL, index->mmapsize, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
    }
    else
    {
        index->file = fopen(filename, "rb");
        index->mmapsize = 0;
        index->mmapptr = NULL;
    }

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
    
    size_t filenames_lens_total = 0;
    for(;;)
    {
        struct archive_entry *entry;
        int r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return -1; }
        const void* firstblock_buff;
        size_t firstblock_len;
        int64_t firstblock_offset;
        r = archive_read_data_block(a, &firstblock_buff, &firstblock_len, &firstblock_offset);
        int filetype = archive_entry_filetype(entry);
        if(filetype == AE_IFREG && archive_entry_size_is_set(entry) != 0 && last_file_buff != NULL && last_file_buff <= firstblock_buff && firstblock_buff < last_file_buff + last_file_block_size)
        {
            size_t byte_size = (size_t)archive_entry_size(entry);
            size_t byte_offset = last_file_offset + (size_t)(firstblock_buff - last_file_buff);
            const char* entryname = archive_entry_pathname(entry);
            strcpy(index->filenames + filenames_lens_total, entryname);
            index->filenames_lens[index->files_num] = strlen(entryname);
            index->offsets[index->files_num] = byte_offset;
            index->sizes[index->files_num] = byte_size;
            filenames_lens_total += index->filenames_lens[index->files_num] + 1;
            index->files_num++;
        }
        r = archive_read_data_skip(a);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return -1; }
    }
    assert(ARCHIVE_OK == archive_read_close(a));
    assert(ARCHIVE_OK == archive_read_free(a));
    return 0;
}

int
main(int argc, const char **argv)
{
    if(argc != 1 && argc != 2)
        return -1;
    
    const char *filename = argv[0];
    const char* entryname = argc == 2 ? argv[1] : NULL;

    struct zipsfx_index* index = malloc(sizeof(struct zipsfx_index));
    assert(0 == zipsfx_index_build(index, filename,
#ifdef ZIPSFX_USE_MMAP
1
#else
0
#endif
    ));

    if(entryname != NULL)
    {
        FILE* f = zipsfx_fopen(index, entryname, "rb");
        char buf[1024];
        if(!f) return -1;
        do
        {
            fwrite(buf, 1, fread(buf, 1, sizeof(buf), f), stdout);
        }
        while(!feof(f) && !ferror(f));
        fclose(f);
    }
    else
    {
        zipsfx_list(index);
    }
    zipsfx_index_destroy(index);
    free(index);
    return 0;
}
