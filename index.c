#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

IndexEntry *index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++)
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int rem = index->count - i - 1;
            if (rem > 0)
                memmove(&index->entries[i], &index->entries[i+1],
                        (size_t)rem * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    int n = 0;
    for (int i = 0; i < index->count; i++) {
        printf("    staged: %s\n", index->entries[i].path);
        n++;
    }
    if (n == 0) printf("    (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    n = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("    deleted: %s\n", index->entries[i].path); n++;
        } else if (st.st_mtime != (time_t)index->entries[i].mtime_sec ||
                   st.st_size  != (off_t)index->entries[i].size) {
            printf("    modified: %s\n", index->entries[i].path); n++;
        }
    }
    if (n == 0) printf("    (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    n = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;
            if (strcmp(ent->d_name, "pes")  == 0) continue;
            if (strstr(ent->d_name, ".o")   != NULL) continue;
            int tracked = 0;
            for (int i = 0; i < index->count; i++)
                if (strcmp(index->entries[i].path, ent->d_name) == 0) { tracked = 1; break; }
            if (!tracked) {
                struct stat st;
                stat(ent->d_name, &st);
                if (S_ISREG(st.st_mode)) { printf("    untracked: %s\n", ent->d_name); n++; }
            }
        }
        closedir(dir);
    }
    if (n == 0) printf("    (nothing to show)\n");
    printf("\n");
    return 0;
}

int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (index->count >= MAX_INDEX_ENTRIES) break;
        IndexEntry *e = &index->entries[index->count];
        char hex[HASH_HEX_SIZE + 1];
        unsigned int mode_tmp;
        unsigned long long mtime_tmp;
        unsigned int size_tmp;
        if (sscanf(line, "%o %64s %llu %u %511s",
                   &mode_tmp, hex, &mtime_tmp, &size_tmp, e->path) != 5)
            continue;
        if (hex_to_hash(hex, &e->hash) != 0) continue;
        e->mode      = (uint32_t)mode_tmp;
        e->mtime_sec = (uint64_t)mtime_tmp;
        e->size      = (uint32_t)size_tmp;
        index->count++;
    }
    fclose(f);
    return 0;
}

static int cmp_index(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path, ((const IndexEntry *)b)->path);
}

int index_save(const Index *index) {
    static Index sorted;
    sorted = *index;
    qsort(sorted.entries, (size_t)sorted.count, sizeof(IndexEntry), cmp_index);
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", INDEX_FILE);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    for (int i = 0; i < sorted.count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted.entries[i].hash, hex);
        fprintf(f, "%o %s %llu %u %s\n",
                sorted.entries[i].mode, hex,
                (unsigned long long)sorted.entries[i].mtime_sec,
                (unsigned int)sorted.entries[i].size,
                sorted.entries[i].path);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    return rename(tmp, INDEX_FILE);
}

int index_add(Index *index, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    void *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return -1; }
    fclose(f);
    ObjectID blob_id;
    if (object_write(OBJ_BLOB, buf, (size_t)sz, &blob_id) != 0) { free(buf); return -1; }
    free(buf);
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    uint32_t mode = (st.st_mode & S_IXUSR) ? 0100755 : 0100644;
    IndexEntry *e = index_find(index, path);
    if (e) {
        e->mode = mode; e->hash = blob_id;
        e->mtime_sec = (uint64_t)st.st_mtime;
        e->size      = (uint32_t)st.st_size;
    } else {
        if (index->count >= MAX_INDEX_ENTRIES) { fprintf(stderr, "error: index full\n"); return -1; }
        e = &index->entries[index->count++];
        e->mode = mode; e->hash = blob_id;
        e->mtime_sec = (uint64_t)st.st_mtime;
        e->size      = (uint32_t)st.st_size;
        strncpy(e->path, path, sizeof(e->path) - 1);
        e->path[sizeof(e->path) - 1] = '\0';
    }
    return index_save(index);
}
// index_load: parses text-format index file
// index_save: atomic write using temp file and rename
