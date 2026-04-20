// object.c — Content-addressable object store
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++)
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str;
    switch (type) {
        case OBJ_BLOB:   type_str = "blob";   break;
        case OBJ_TREE:   type_str = "tree";   break;
        case OBJ_COMMIT: type_str = "commit"; break;
        default: return -1;
    }

    char header[64];
    int hlen = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    size_t full_len = (size_t)hlen + 1 + len;

    uint8_t *full = malloc(full_len);
    if (!full) return -1;
    memcpy(full, header, (size_t)hlen);
    full[hlen] = '\0';
    memcpy(full + hlen + 1, data, len);

    ObjectID id;
    compute_hash(full, full_len, &id);

    if (object_exists(&id)) {
        if (id_out) *id_out = id;
        free(full);
        return 0;
    }

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&id, hex);
    char shard[512];
    snprintf(shard, sizeof(shard), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard, 0755);

    char final_path[512], tmp_path[512];
    object_path(&id, final_path, sizeof(final_path));
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", final_path);

    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) { free(full); return -1; }

    ssize_t w = write(fd, full, full_len);
    free(full);
    if (w != (ssize_t)full_len) { close(fd); unlink(tmp_path); return -1; }

    fsync(fd);
    close(fd);

    if (rename(tmp_path, final_path) != 0) { unlink(tmp_path); return -1; }

    int dfd = open(shard, O_RDONLY);
    if (dfd >= 0) { fsync(dfd); close(dfd); }

    if (id_out) *id_out = id;
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) { fclose(f); return -1; }

    uint8_t *buf = malloc((size_t)fsize);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(buf); fclose(f); return -1;
    }
    fclose(f);

    ObjectID computed;
    compute_hash(buf, (size_t)fsize, &computed);
    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        fprintf(stderr, "error: object corruption detected\n");
        free(buf); return -1;
    }

    uint8_t *nul = memchr(buf, '\0', (size_t)fsize);
    if (!nul) { free(buf); return -1; }

    if      (strncmp((char *)buf, "blob ",   5) == 0) *type_out = OBJ_BLOB;
    else if (strncmp((char *)buf, "tree ",   5) == 0) *type_out = OBJ_TREE;
    else if (strncmp((char *)buf, "commit ", 7) == 0) *type_out = OBJ_COMMIT;
    else { free(buf); return -1; }

    size_t hdr_len  = (size_t)(nul - buf);
    size_t data_len = (size_t)fsize - hdr_len - 1;

    uint8_t *out = malloc(data_len + 1);
    if (!out) { free(buf); return -1; }
    memcpy(out, nul + 1, data_len);
    out[data_len] = '\0';

    *data_out = out;
    *len_out  = data_len;
    free(buf);
    return 0;
}
// object_write: stores blob/tree/commit with SHA-256 hash
// object_read: verifies integrity before returning data
