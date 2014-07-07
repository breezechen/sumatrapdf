/* Copyright 2014 the unarr project authors (see AUTHORS file).
   License: LGPLv3 */

#ifndef zip_zip_h
#define zip_zip_h

#include "../common/unarr-imp.h"
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif
#ifdef HAVE_BZIP2
#include <bzlib.h>
#endif
#ifdef HAVE_LZMA
#include <LzmaDec.h>
#endif

typedef struct ar_archive_zip_s ar_archive_zip;

/***** parse-zip *****/

enum zip_signatures {
    SIG_LOCAL_FILE_HEADER = 0x04034B50,
    SIG_CENTRAL_DIRECTORY = 0x02014B50,
    SIG_END_OF_CENTRAL_DIRECTORY_64 = 0x06064B50,
    SIG_END_OF_CENTRAL_DIRECTORY_64_LOCATOR = 0x07064B50,
    SIG_END_OF_CENTRAL_DIRECTORY = 0x06054B50,
};

enum compression_method {
    METHOD_STORE = 0, METHOD_DEFLATE = 8,
    METHOD_DEFLATE64 = 9, METHOD_BZIP2 = 12, METHOD_LZMA = 14,
};

#define ZIP_DIR_ENTRY_FIXED_SIZE 46

struct zip_entry {
    uint32_t signature;
    uint16_t version;
    uint16_t min_version;
    uint16_t flags;
    uint16_t method;
    uint32_t dosdate;
    uint32_t crc;
    uint64_t datasize;
    uint64_t uncompressed;
    uint16_t namelen;
    uint16_t extralen;
    uint16_t commentlen;
    uint32_t disk;
    uint16_t attr_internal;
    uint32_t attr_external;
    off64_t header_offset;
};

struct zip_eocd64 {
    uint32_t signature;
    uint16_t version;
    uint16_t min_version;
    uint32_t diskno;
    uint32_t diskno_dir;
    uint64_t numentries_disk;
    uint64_t numentries;
    uint64_t dir_size;
    off64_t dir_offset;
    uint16_t commentlen;
};

struct ar_archive_zip_entry {
    off64_t offset;
    uint16_t method;
    uint16_t flags;
    uint32_t crc;
    char *name;
    wchar16_t *name_w;
};

bool zip_seek_to_compressed_data(ar_archive_zip *zip);
bool zip_parse_directory_entry(ar_archive_zip *zip, struct zip_entry *entry);
bool zip_parse_end_of_central_directory(ar_stream *stream, struct zip_eocd64 *eocd);
off64_t zip_find_end_of_central_directory(ar_stream *stream);
const char *zip_get_name(ar_archive *ar);
const wchar16_t *zip_get_name_w(ar_archive *ar);

/***** uncompress-zip *****/

struct ar_archive_zip_uncomp;

typedef uint32_t (* zip_uncomp_uncompress_data_fn)(struct ar_archive_zip_uncomp *uncomp, void *buffer, uint32_t buffer_size);
typedef void (* zip_uncomp_clear_state_fn)(struct ar_archive_zip_uncomp *uncomp);

struct ar_archive_zip_uncomp {
    bool initialized;
    zip_uncomp_uncompress_data_fn uncompress_data;
    zip_uncomp_clear_state_fn clear_state;
    union {
#ifdef HAVE_ZLIB
        z_stream zstream;
#endif
#ifdef HAVE_BZIP2
        bz_stream bstream;
#endif
#ifdef HAVE_LZMA
        CLzmaDec *lzmadec;
#endif
        char _dummy;
    } state;
    struct InputBuffer {
        uint8_t data[4096];
        uint16_t offset;
        uint16_t bytes_left;
    } input;
    uint16_t flags;
};

bool zip_uncompress_part(ar_archive_zip *zip, void *buffer, size_t buffer_size);
void zip_clear_uncompress(struct ar_archive_zip_uncomp *uncomp);

/***** zip *****/

struct ar_archive_zip_dir {
    off64_t offset;
    uint64_t length;
    off64_t seen_last_offset;
    uint64_t seen_count;
};

struct ar_archive_zip_progress {
    size_t data_left;
    size_t bytes_done;
    uint32_t crc;
};

struct ar_archive_zip_s {
    ar_archive super;
    struct ar_archive_zip_dir dir;
    struct ar_archive_zip_entry entry;
    struct ar_archive_zip_uncomp uncomp;
    struct ar_archive_zip_progress progr;
    bool deflateonly;
    off64_t comment_offset;
    uint16_t comment_size;
};

#endif
