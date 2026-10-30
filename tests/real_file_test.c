/* The real library's files, packer and shapes, and Support/Utility's chunk files, against the game data:
   every .crp, .fsh, .psh and .viv is read through FILE_*, unpacked (RefPack) and, by kind, opened as a
   shape file, a BIG directory or a chunk tree. With OPENNFSPU_REF=<manifest> (made from tools/nfsfmt.py:
   file sizes, unpacked sizes and CRCs, shape directories) every result is compared with the reference
   decoder. Skips (exit 0) without data. */
#include "exe.h"
#include "real/file.h"
#include "real/locatbig.h"
#include "real/mem.h"
#include "real/pack.h"
#include "real/shape.h"
#include "support/udata.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails, files, packed, shapes, archives, chunks, records;
#define FAIL(...) do { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)

static bool ends(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    return n >= m && !strcasecmp(s + n - m, ext);
}

/* Every record of a chunk tree lies inside the data, children inside too. */
static void walk(const uint8_t *base, uint32_t size, const UData *r, int depth, const char *path)
{
    records++;
    if (depth > 16) { FAIL("%s: tree deeper than 16", path); return; }
    UData *kids = UData_children(r);
    const uint8_t *k = (const uint8_t *)kids;
    uint32_t n = UData_articles(r) + r->count;
    if (k < base || k + 16 * (size_t)n > base + size) { FAIL("%s: children of %08x out of the data", path, r->id); return; }
    for (unsigned i = 0; i < UData_articles(r); i++) walk(base, size, kids + i, depth + 1, path);
    for (uint32_t i = 0; i < r->count; i++) {
        const UData *it = kids + UData_articles(r) + i;
        records++;
        const uint8_t *d = UData_data(it);
        if (d < base || d + UData_size(it) > base + size) FAIL("%s: item %08x data out of the data", path, it->id);
        /* Each item is found by its id (the first record with it), and an indexed one by its index. */
        const UData *f = UData_find(r, it->id);
        if (f == UData_itemsend(r) || f->id != it->id || f > it) FAIL("%s: UData_find %08x", path, it->id);
        if ((r->info & UDATA_SORTED) && (it->info & UDATA_INDEXED)) {
            uint32_t idx = it->id & 0xffff;
            const UData *g = UData_finditem(r, UData_key(it), idx, idx);
            if (g == UData_itemsend(r) || (g->id & 0xffff) != idx) FAIL("%s: UData_finditem %08x #%u", path, it->id, idx);
        }
    }
    if (r->count && UData_countitems(r, UData_key(kids + UData_articles(r))) < 1) FAIL("%s: UData_countitems", path);
}

static void check_chunks(const char *path, uint32_t want_size, uint32_t want_crc, bool have_ref)
{
    UDataFile f;
    UDataFile_ctor(&f);
    if (!UDataFile_load(&f, path, false)) { FAIL("%s: UDataFile_load", path); return; }
    if (have_ref && (f.size != want_size || crc32(f.data, f.size) != want_crc))
        FAIL("%s: chunk data %u bytes crc %08x, reference %u %08x", path, f.size, crc32(f.data, f.size), want_size, want_crc);
    UData *root = UDataFile_root(&f);
    /* Chunk trees start with a root article (info & 0x1f = 0x1a); FEData/Data/NFS5.CRP is something else. */
    if ((root->info & 0x1f) != 0x1a) {
        UDataFile_dtor(&f);
        return;
    }
    int before = fails;
    walk(f.data, f.size, root, 0, path);
    if (fails != before) {
        UDataFile_dtor(&f);
        return;
    }
    /* The articles' 'Name' items, through both searches. */
    UData *kids = UData_children(root);
    for (unsigned i = 0; i < UData_articles(root); i++) {
        const char *name = UData_name(kids + i);
        for (const char *c = name; c && *c; c++)
            if ((unsigned char)*c < 0x20 || (unsigned char)*c > 0x7e) { FAIL("%s: article name not text", path); break; }
    }
    UDataFile_dtor(&f);
    chunks++;
}

static void check_shapes(const char *path, const char *ref_entries, int ref_count)
{
    uint8_t *shp = SHAPE_loadfile(path, 0, 0);
    if (!shp) { FAIL("%s: SHAPE_loadfile", path); return; }
    int n = (int)SHAPE_count(shp);
    if (ref_entries && n != ref_count) FAIL("%s: %d shapes, reference %d", path, n, ref_count);
    const char *p = ref_entries;
    for (int i = 0; i < n; i++) {
        const uint8_t *e = SHAPE_entry(shp, i);
        if (e < shp || e + 16 > shp + MEM_size(shp)) { FAIL("%s: entry %d out of the file", path, i); break; }
        if (!SHAPE_typevalid(e)) FAIL("%s: entry %d type %02x not a shape type", path, i, e[0]);
        if (p) {
            unsigned name, type, w, h;
            if (sscanf(p, "%x:%x:%u:%u", &name, &type, &w, &h) != 4) { FAIL("%s: reference entry %d", path, i); break; }
            unsigned gw = e[4] | e[5] << 8, gh = e[6] | e[7] << 8;
            if (SHAPE_entryname(shp, i) != name || e[0] != type || gw != w || gh != h)
                FAIL("%s: entry %d %08x %02x %ux%u, reference %08x %02x %ux%u", path, i, SHAPE_entryname(shp, i), e[0], gw, gh, name, type, w, h);
            p = strchr(p, ' ');
            if (p) p++;
        }
        /* Palettised images (8 bits: 0x7b) carry a palette in their chain, or share the file's. */
        if (SHAPE_typevalid(e)) (void)SHAPE_clut(e);
    }
    if (SHAPE_entryname(shp, n) != 0) FAIL("%s: a name past the last entry", path);
    MEM_purge(shp);
    shapes++;
}

static void check_archive(const char *path)
{
    uint8_t *dir = FILE_loadbigheaderfile(path, 0);
    if (!dir) { FAIL("%s: FILE_loadbigheader", path); return; }
    int size = FILE_getsize(path);
    if ((int)BIG_headersize(dir) > 0xa90 && MEM_size(dir) != (int)BIG_headersize(dir))
        FAIL("%s: directory block %d bytes, directory %u", path, MEM_size(dir), BIG_headersize(dir));
    uint32_t count = BIG_count(dir);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off, len;
        if (!BIG_locateentry(dir, NULL, (int)i, &off, &len) || (uint64_t)off + len > (uint64_t)size)
            FAIL("%s: entry %u out of the file", path, i);
    }
    MEM_purge(dir);
    archives++;
}

static void check_file(const char *path, int ref_size, uint32_t ref_unpacked, uint32_t ref_crc, bool have_ref)
{
    files++;
    int size = FILE_getsize(path);
    if (have_ref && size != ref_size) FAIL("%s: size %d, reference %d", path, size, ref_size);
    uint8_t *raw = FILE_loadfile(path, 0);
    if (!raw || MEM_size(raw) != size) { FAIL("%s: FILE_load", path); MEM_purge(raw); return; }
    /* FILE_loadat of the first part reads the same bytes */
    int part = size < 4096 ? size : 4096;
    uint8_t *head = malloc((size_t)part + 1);
    if (FILE_loadatfile(path, head, part) != part || memcmp(head, raw, (size_t)part)) FAIL("%s: FILE_loadat", path);
    free(head);
    uint32_t unpacked = size >= 5 ? UNPACK_size(raw) : 0;
    if (have_ref && unpacked != ref_unpacked) FAIL("%s: unpacked size %u, reference %u", path, unpacked, ref_unpacked);
    MEM_purge(raw);
    uint8_t *data = FILE_loadpackfile(path, 0);
    if (!data) { FAIL("%s: FILE_loadpackfile", path); return; }
    int dsize = MEM_size(data);
    if (unpacked) {
        packed++;
        if ((uint32_t)dsize != unpacked) FAIL("%s: unpacked %d bytes, header says %u", path, dsize, unpacked);
    }
    if (have_ref && crc32(data, (size_t)dsize) != ref_crc) FAIL("%s: data crc %08x, reference %08x", path, crc32(data, (size_t)dsize), ref_crc);
    MEM_purge(data);
    if (ends(path, ".crp")) check_chunks(path, unpacked ? unpacked : (uint32_t)size, ref_crc, have_ref);
    if (ends(path, ".viv")) check_archive(path);
}

/* Without a manifest: every .crp/.fsh/.psh/.viv found by listing the data. */
static char dirs[256][256];
static int ndirs;
static const char *cur;
static void visit(const char *name, bool is_dir, void *user)
{
    char p[256];
    snprintf(p, sizeof p, "%s%s%s", cur, *cur ? "/" : "", name);
    if (is_dir) { if (ndirs < 256) snprintf(dirs[ndirs++], sizeof dirs[0], "%s", p); return; }
    if (ends(p, ".crp") || ends(p, ".viv") || ends(p, ".psh")) check_file(p, 0, 0, 0, false);
    else if (ends(p, ".fsh")) { check_file(p, 0, 0, 0, false); check_shapes(p, NULL, 0); }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("real_file_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    const char *ref = getenv("OPENNFSPU_REF");
    FILE *rf = ref && *ref ? fopen(ref, "r") : NULL;
    if (rf) {
        static char line[65536];
        while (fgets(line, sizeof line, rf)) {
            line[strcspn(line, "\n")] = 0;
            char kind[8], path[256];
            int a;
            unsigned b, c;
            if (sscanf(line, "%7s\t%255[^\t]\t%d\t%u\t%x", kind, path, &a, &b, &c) == 5 && !strcmp(kind, "file"))
                check_file(path, a, b, c, true);
            else if (sscanf(line, "%7s\t%255[^\t]\t%d", kind, path, &a) == 3 && !strcmp(kind, "shp")) {
                const char *ents = strchr(strchr(strchr(line, '\t') + 1, '\t') + 1, '\t');
                check_shapes(path, ents ? ents + 1 : "", a);
            }
        }
        fclose(rf);
    } else {
        snprintf(dirs[ndirs++], sizeof dirs[0], "%s", "");
        for (int i = 0; i < ndirs; i++) { cur = dirs[i]; vfs_list(cur, visit, NULL); }
    }
    if (UData_fourcc('N', 'a', 'm', 'e') != 0x4e616d65) FAIL("fourcc order");
    printf("real_file_test: %d files (%d packed), %d shape files, %d archives, %d chunk files (%d records)%s: %s\n",
           files, packed, shapes, archives, chunks, records, rf ? " against the reference" : "", fails ? "FAILED" : "ok");
    return fails != 0 || files == 0;
}
