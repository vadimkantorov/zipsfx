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

enum {zipsfx_index_filenames_num = 100,  zipsfx_index_filenames_len = 128, zipsfx_open_file_limit = 100, zipsfx_buffer_size = 8192};
struct zipsfx_index
{
#ifdef ZIPSFX_USE_MMAP
    int file;
    char* ptr;
    size_t ptrsize;
#else
    FILE* file;
    char buf[zipsfx_buffer_size];
#endif

    char filename[zipsfx_index_filenames_len]; 
    
    char filenames[zipsfx_index_filenames_num * zipsfx_index_filenames_len];
    size_t filenames_lens[zipsfx_index_filenames_num];
    size_t filenames_lens_total;
    
    size_t offsets[zipsfx_index_filenames_num], sizes[zipsfx_index_filenames_num];
    size_t files_num;
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
        if(0 == strncmp(index->filenames + filenames_start, filename, index->filenames_lens[i]))
        {
            size_t offset = index->offsets[i], size = index->sizes[i];
#if ZIPSFX_USE_MMAP
            FILE* f = fmemopen(index->ptr + offset, size, "rb");
#else
            FILE* f = fmemopen(NULL, size, "rb+");
            fseek(index->file, offset, SEEK_SET);
            while(size > 0)
            {
                size_t len = fread(index->buf, 1, sizeof(index->buf) <= size ? sizeof(index->buf) : size, index->file);
                fwrite(index->buf, 1, len, f);
                size -= len;
            }
            fseek(f, 0, SEEK_SET);
#endif
            return f;
        }
        filenames_start += index->filenames_lens[i] + 1;
    }
    return NULL;
}
void zipsfx_index_destroy(struct zipsfx_index* index)
{
#if ZIPSFX_USE_MMAP
    munmap(index->ptr, index->ptrsize);
    close(index->file);
#else
    fclose(index->file);
#endif
    free(index);
}
struct zipsfx_index* zipsfx_index_build(const char filename[])
{
    struct zipsfx_index* index = calloc(1, sizeof(struct zipsfx_index));
    strcpy(index->filename, filename);
#if ZIPSFX_USE_MMAP
    index->file = open(filename, O_RDONLY);
    struct stat file_info; assert(fstat(index->file, &file_info) >= 0);
    index->ptrsize = file_info.st_size;
    index->ptr = mmap(NULL, index->ptrsize, PROT_READ, MAP_PRIVATE, index->file, 0);
#else
    index->file = fopen(filename, "rb");
#endif

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
        if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return NULL; }
        const void* block_buff;
        size_t block_len;
        int64_t block_offset;
        r = archive_read_data_block(a, &block_buff, &block_len, &block_offset);
        int filetype = archive_entry_filetype(entry);
        if(filetype == AE_IFREG && archive_entry_size_is_set(entry) != 0 && last_file_buff != NULL && last_file_buff <= block_buff && block_buff < last_file_buff + last_file_block_size)
        {
            size_t byte_size = (size_t)archive_entry_size(entry);
            size_t byte_offset = last_file_offset + (size_t)(block_buff - last_file_buff);
            const char* entryname = archive_entry_pathname(entry);
            strcpy(index->filenames + index->filenames_lens_total, entryname);
            index->filenames_lens[index->files_num] = strlen(entryname);
            index->filenames_lens_total += index->filenames_lens[index->files_num] + 1;
            index->offsets[index->files_num] = byte_offset;
            index->sizes[index->files_num] = byte_size;
            index->files_num++;
        }
        r = archive_read_data_skip(a);
        if (r == ARCHIVE_EOF) break;
        if (r != ARCHIVE_OK) { fprintf(stderr, "%s\n", archive_error_string(a)); return NULL; }
    }
    assert(ARCHIVE_OK == archive_read_close(a));
    assert(ARCHIVE_OK == archive_read_free(a));
    return index;
}

int
main(int argc, const char **argv)
{
    if(argc != 1 && argc != 2)
        return -1;
    
    const char *filename = argv[0];
    const char* entryname = argc == 2 ? argv[1] : NULL;

    struct zipsfx_index* index = zipsfx_index_build(filename);
    assert(index != NULL);

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
    return 0;
}
