#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*
 * bldd (backward ldd)
 * Find ELF executables that depend on selected shared libraries.
 */

/* ELF constants */
#define ET_EXEC 2
#define ET_DYN 3
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_STRTAB 5

#define EM_386 3
#define EM_ARM 40
#define EM_X86_64 62
#define EM_AARCH64 183

typedef enum {
    ARCH_UNKNOWN = 0,
    ARCH_X86,
    ARCH_X86_64,
    ARCH_ARMV7,
    ARCH_AARCH64
} Arch;

typedef struct PathNode {
    char *path;
    struct PathNode *next;
} PathNode;

typedef struct LibUsage {
    char *name;
    size_t exec_count;
    PathNode *executables;
    struct LibUsage *next;
} LibUsage;

typedef struct ArchUsage {
    Arch arch;
    LibUsage *libs;
    struct ArchUsage *next;
} ArchUsage;

typedef struct {
    uint64_t vaddr;
    uint64_t offset;
    uint64_t memsz;
} LoadSegment;

typedef struct {
    Arch arch;
    char **needed;
    size_t needed_count;
} ParsedElf;

typedef struct {
    const char *scan_dir;
    bool include_hidden;
    uint32_t arch_filter_mask;
    char **targets;
    size_t target_count;
    ArchUsage *report;
} AppContext;

static AppContext *g_ctx = NULL;

/* ---------- Utility ---------- */

static const char *arch_to_string(Arch arch) {
    switch (arch) {
        case ARCH_X86:
            return "x86";
        case ARCH_X86_64:
            return "x86_64";
        case ARCH_ARMV7:
            return "armv7";
        case ARCH_AARCH64:
            return "aarch64";
        default:
            return "unknown";
    }
}

static Arch arch_from_arg(const char *value) {
    if (strcmp(value, "x86") == 0) return ARCH_X86;
    if (strcmp(value, "x86_64") == 0) return ARCH_X86_64;
    if (strcmp(value, "armv7") == 0) return ARCH_ARMV7;
    if (strcmp(value, "aarch64") == 0) return ARCH_AARCH64;
    return ARCH_UNKNOWN;
}

static uint32_t arch_bit(Arch arch) {
    return arch == ARCH_UNKNOWN ? 0u : (1u << (unsigned)arch);
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool in_bounds(size_t file_size, size_t off, size_t need) {
    return off <= file_size && need <= file_size - off;
}

static uint16_t read_u16(const uint8_t *p, bool little_endian) {
    if (little_endian) {
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    }
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t read_u32(const uint8_t *p, bool little_endian) {
    if (little_endian) {
        return (uint32_t)p[0] |
               ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    }
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint64_t read_u64(const uint8_t *p, bool little_endian) {
    if (little_endian) {
        return (uint64_t)read_u32(p, true) | ((uint64_t)read_u32(p + 4, true) << 32);
    }
    return ((uint64_t)read_u32(p, false) << 32) | (uint64_t)read_u32(p + 4, false);
}

static bool add_unique_string(char ***arr, size_t *count, const char *value) {
    for (size_t i = 0; i < *count; ++i) {
        if (strcmp((*arr)[i], value) == 0) return true;
    }

    char **resized = realloc(*arr, (*count + 1) * sizeof(char *));
    if (!resized) return false;

    char *dup = strdup(value);
    if (!dup) return false;

    resized[*count] = dup;
    *arr = resized;
    *count += 1;
    return true;
}

static void free_string_array(char **arr, size_t count) {
    for (size_t i = 0; i < count; ++i) free(arr[i]);
    free(arr);
}

/* ---------- ELF parsing ---------- */

static bool read_file_bytes(const char *path, uint8_t **buf, size_t *len, mode_t *mode) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return false;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;

    size_t size = (size_t)st.st_size;
    uint8_t *data = malloc(size ? size : 1);
    if (!data) {
        close(fd);
        return false;
    }

    size_t off = 0;
    while (off < size) {
        ssize_t n = read(fd, data + off, size - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        off += (size_t)n;
    }

    close(fd);
    *buf = data;
    *len = off;
    *mode = st.st_mode;
    return true;
}

static Arch detect_arch(uint16_t machine, uint8_t elf_class, uint8_t elf_data, uint32_t e_flags) {
    if (machine == EM_386 && elf_class == 1) return ARCH_X86;
    if (machine == EM_X86_64 && elf_class == 2) return ARCH_X86_64;
    if (machine == EM_AARCH64 && elf_class == 2) return ARCH_AARCH64;

    if (machine == EM_ARM && elf_class == 1 && elf_data == 1) {
        uint32_t eabi = (e_flags >> 24) & 0xffu;
        if (eabi >= 5) return ARCH_ARMV7;
    }

    return ARCH_UNKNOWN;
}

static bool vaddr_to_file_off(const LoadSegment *segments, size_t seg_count, uint64_t vaddr, uint64_t *off_out) {
    for (size_t i = 0; i < seg_count; ++i) {
        const LoadSegment *s = &segments[i];
        if (vaddr >= s->vaddr && vaddr < s->vaddr + s->memsz) {
            *off_out = s->offset + (vaddr - s->vaddr);
            return true;
        }
    }
    return false;
}

static void free_parsed_elf(ParsedElf *elf) {
    free_string_array(elf->needed, elf->needed_count);
    elf->needed = NULL;
    elf->needed_count = 0;
    elf->arch = ARCH_UNKNOWN;
}

static bool parse_elf_file(const char *path, ParsedElf *out) {
    out->arch = ARCH_UNKNOWN;
    out->needed = NULL;
    out->needed_count = 0;

    uint8_t *buf = NULL;
    size_t size = 0;
    mode_t mode = 0;
    if (!read_file_bytes(path, &buf, &size, &mode)) return false;

    bool ok = false;
    LoadSegment *segments = NULL;
    uint64_t *needed_offsets = NULL;

    do {
        if (size < 64) break;
        if (!(buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F')) break;

        uint8_t elf_class = buf[4];
        uint8_t elf_data = buf[5];
        if (!((elf_class == 1 || elf_class == 2) && (elf_data == 1 || elf_data == 2))) break;

        bool little = (elf_data == 1);
        bool is_64 = (elf_class == 2);

        uint16_t e_type = read_u16(buf + 16, little);
        uint16_t e_machine = read_u16(buf + 18, little);

        uint32_t e_flags = 0;
        uint64_t e_phoff = 0;
        uint16_t e_phentsize = 0;
        uint16_t e_phnum = 0;

        if (is_64) {
            if (!in_bounds(size, 32, 8) || !in_bounds(size, 48, 4) || !in_bounds(size, 54, 4)) break;
            e_phoff = read_u64(buf + 32, little);
            e_flags = read_u32(buf + 48, little);
            e_phentsize = read_u16(buf + 54, little);
            e_phnum = read_u16(buf + 56, little);
        } else {
            if (!in_bounds(size, 28, 4) || !in_bounds(size, 36, 4) || !in_bounds(size, 42, 4)) break;
            e_phoff = read_u32(buf + 28, little);
            e_flags = read_u32(buf + 36, little);
            e_phentsize = read_u16(buf + 42, little);
            e_phnum = read_u16(buf + 44, little);
        }

        Arch arch = detect_arch(e_machine, elf_class, elf_data, e_flags);
        if (arch == ARCH_UNKNOWN) break;
        if (!(e_type == ET_EXEC || e_type == ET_DYN)) break;

        bool has_interp = false;
        bool has_dynamic = false;
        uint64_t dyn_off = 0;
        uint64_t dyn_size = 0;
        size_t segment_count = 0;

        for (uint16_t i = 0; i < e_phnum; ++i) {
            uint64_t ph_off = e_phoff + (uint64_t)i * e_phentsize;
            if (!in_bounds(size, (size_t)ph_off, e_phentsize)) goto cleanup;

            uint32_t p_type = read_u32(buf + ph_off, little);

            uint64_t p_offset;
            uint64_t p_vaddr;
            uint64_t p_filesz;
            uint64_t p_memsz;

            if (is_64) {
                if (e_phentsize < 56) goto cleanup;
                p_offset = read_u64(buf + ph_off + 8, little);
                p_vaddr = read_u64(buf + ph_off + 16, little);
                p_filesz = read_u64(buf + ph_off + 32, little);
                p_memsz = read_u64(buf + ph_off + 40, little);
            } else {
                if (e_phentsize < 32) goto cleanup;
                p_offset = read_u32(buf + ph_off + 4, little);
                p_vaddr = read_u32(buf + ph_off + 8, little);
                p_filesz = read_u32(buf + ph_off + 16, little);
                p_memsz = read_u32(buf + ph_off + 20, little);
            }

            if (p_type == PT_LOAD) {
                LoadSegment *tmp = realloc(segments, (segment_count + 1) * sizeof(LoadSegment));
                if (!tmp) goto cleanup;
                segments = tmp;
                segments[segment_count++] = (LoadSegment){
                    .vaddr = p_vaddr,
                    .offset = p_offset,
                    .memsz = p_memsz,
                };
            } else if (p_type == PT_INTERP) {
                has_interp = true;
            } else if (p_type == PT_DYNAMIC) {
                has_dynamic = true;
                dyn_off = p_offset;
                dyn_size = p_filesz;
            }
        }

        if (!has_dynamic) break;
        if (!has_interp && !(mode & (S_IXUSR | S_IXGRP | S_IXOTH))) break;
        if (dyn_off >= size) break;

        uint64_t dyn_end = dyn_off + dyn_size;
        if (dyn_end > size) dyn_end = size;
        size_t dyn_ent_size = is_64 ? 16u : 8u;

        uint64_t strtab_vaddr = 0;
        bool has_strtab = false;
        size_t needed_count = 0;

        for (uint64_t pos = dyn_off; pos + dyn_ent_size <= dyn_end; pos += dyn_ent_size) {
            uint64_t d_tag = is_64 ? read_u64(buf + pos, little) : read_u32(buf + pos, little);
            uint64_t d_val = is_64 ? read_u64(buf + pos + 8, little) : read_u32(buf + pos + 4, little);

            if (d_tag == DT_NULL) break;

            if (d_tag == DT_STRTAB) {
                strtab_vaddr = d_val;
                has_strtab = true;
            } else if (d_tag == DT_NEEDED) {
                uint64_t *tmp = realloc(needed_offsets, (needed_count + 1) * sizeof(uint64_t));
                if (!tmp) goto cleanup;
                needed_offsets = tmp;
                needed_offsets[needed_count++] = d_val;
            }
        }

        if (!has_strtab || needed_count == 0) break;

        uint64_t strtab_file_off = 0;
        if (!vaddr_to_file_off(segments, segment_count, strtab_vaddr, &strtab_file_off)) break;

        char **needed = NULL;
        size_t out_needed_count = 0;
        for (size_t i = 0; i < needed_count; ++i) {
            uint64_t str_off = strtab_file_off + needed_offsets[i];
            if (str_off >= size) continue;

            const char *name = (const char *)(buf + str_off);
            size_t max_len = size - (size_t)str_off;
            size_t len = strnlen(name, max_len);
            if (len == 0 || len == max_len) continue;

            char **tmp = realloc(needed, (out_needed_count + 1) * sizeof(char *));
            if (!tmp) {
                free_string_array(needed, out_needed_count);
                goto cleanup;
            }
            needed = tmp;
            needed[out_needed_count] = strndup(name, len);
            if (!needed[out_needed_count]) {
                free_string_array(needed, out_needed_count);
                goto cleanup;
            }
            out_needed_count += 1;
        }

        if (out_needed_count == 0) {
            free_string_array(needed, out_needed_count);
            break;
        }

        out->arch = arch;
        out->needed = needed;
        out->needed_count = out_needed_count;
        ok = true;
    } while (0);

cleanup:
    free(segments);
    free(needed_offsets);
    free(buf);
    return ok;
}

/* ---------- Report aggregation ---------- */

static bool is_hidden_path(const char *root, const char *path) {
    size_t root_len = strlen(root);
    const char *p = path;

    if (strncmp(path, root, root_len) == 0) p = path + root_len;
    while (*p == '/') p++;

    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        if (start[0] == '.' && p > start + 1) return true;
        while (*p == '/') p++;
    }
    return false;
}

static bool target_matches(const AppContext *ctx, const char *lib_name) {
    const char *base = base_name(lib_name);
    for (size_t i = 0; i < ctx->target_count; ++i) {
        if (strcmp(ctx->targets[i], lib_name) == 0 || strcmp(ctx->targets[i], base) == 0) {
            return true;
        }
    }
    return false;
}

static ArchUsage *get_or_add_arch_usage(AppContext *ctx, Arch arch) {
    for (ArchUsage *a = ctx->report; a; a = a->next) {
        if (a->arch == arch) return a;
    }

    ArchUsage *node = calloc(1, sizeof(ArchUsage));
    if (!node) return NULL;

    node->arch = arch;
    node->next = ctx->report;
    ctx->report = node;
    return node;
}

static LibUsage *get_or_add_lib_usage(ArchUsage *arch_usage, const char *lib) {
    for (LibUsage *l = arch_usage->libs; l; l = l->next) {
        if (strcmp(l->name, lib) == 0) return l;
    }

    LibUsage *node = calloc(1, sizeof(LibUsage));
    if (!node) return NULL;

    node->name = strdup(lib);
    if (!node->name) {
        free(node);
        return NULL;
    }

    node->next = arch_usage->libs;
    arch_usage->libs = node;
    return node;
}

static bool add_executable_path(LibUsage *lib, const char *path) {
    for (PathNode *p = lib->executables; p; p = p->next) {
        if (strcmp(p->path, path) == 0) return true;
    }

    PathNode *node = calloc(1, sizeof(PathNode));
    if (!node) return false;

    node->path = strdup(path);
    if (!node->path) {
        free(node);
        return false;
    }

    node->next = lib->executables;
    lib->executables = node;
    lib->exec_count += 1;
    return true;
}

static int scan_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)ftwbuf;

    if (!g_ctx || typeflag != FTW_F) return 0;
    if (!g_ctx->include_hidden && is_hidden_path(g_ctx->scan_dir, fpath)) return 0;

    ParsedElf elf = {0};
    if (!parse_elf_file(fpath, &elf)) return 0;

    if ((g_ctx->arch_filter_mask & arch_bit(elf.arch)) == 0) {
        free_parsed_elf(&elf);
        return 0;
    }

    ArchUsage *arch_usage = get_or_add_arch_usage(g_ctx, elf.arch);
    if (!arch_usage) {
        free_parsed_elf(&elf);
        return -1;
    }

    for (size_t i = 0; i < elf.needed_count; ++i) {
        if (!target_matches(g_ctx, elf.needed[i])) continue;

        LibUsage *lib_usage = get_or_add_lib_usage(arch_usage, elf.needed[i]);
        if (!lib_usage || !add_executable_path(lib_usage, fpath)) {
            free_parsed_elf(&elf);
            return -1;
        }
    }

    free_parsed_elf(&elf);
    return 0;
}

/* ---------- Sorting / serialization ---------- */

static size_t count_arch_usage(const ArchUsage *head) {
    size_t n = 0;
    for (const ArchUsage *a = head; a; a = a->next) n++;
    return n;
}

static size_t count_lib_usage(const LibUsage *head) {
    size_t n = 0;
    for (const LibUsage *l = head; l; l = l->next) n++;
    return n;
}

static size_t count_paths(const PathNode *head) {
    size_t n = 0;
    for (const PathNode *p = head; p; p = p->next) n++;
    return n;
}

static int cmp_arch_usage(const void *a, const void *b) {
    const ArchUsage *aa = *(const ArchUsage *const *)a;
    const ArchUsage *bb = *(const ArchUsage *const *)b;
    return (int)aa->arch - (int)bb->arch;
}

static int cmp_lib_usage(const void *a, const void *b) {
    const LibUsage *aa = *(const LibUsage *const *)a;
    const LibUsage *bb = *(const LibUsage *const *)b;
    if (aa->exec_count > bb->exec_count) return -1;
    if (aa->exec_count < bb->exec_count) return 1;
    return strcmp(aa->name, bb->name);
}

static int cmp_str(const void *a, const void *b) {
    const char *aa = *(const char *const *)a;
    const char *bb = *(const char *const *)b;
    return strcmp(aa, bb);
}

static void write_iso_utc(FILE *out) {
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    fputs(buf, out);
}

static void write_json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            fprintf(out, "\\%c", c);
        } else if (c == '\n') {
            fputs("\\n", out);
        } else if (c == '\r') {
            fputs("\\r", out);
        } else if (c == '\t') {
            fputs("\\t", out);
        } else if (c < 0x20) {
            fprintf(out, "\\u%04x", (unsigned)c);
        } else {
            fputc(c, out);
        }
    }
    fputc('"', out);
}

static void write_txt_report(FILE *out, const AppContext *ctx) {
    fprintf(out, "Report on dynamic usage of selected shared libraries by ELF executables on %s\n", ctx->scan_dir);
    fprintf(out, "Target libraries: ");
    for (size_t i = 0; i < ctx->target_count; ++i) {
        fprintf(out, "%s%s", i ? ", " : "", ctx->targets[i]);
    }
    fprintf(out, "\nGenerated at (UTC): ");
    write_iso_utc(out);
    fprintf(out, "\n\n");

    size_t arch_count = count_arch_usage(ctx->report);
    if (arch_count == 0) {
        fprintf(out, "No matches found.\n");
        return;
    }

    ArchUsage **arch_list = calloc(arch_count, sizeof(ArchUsage *));
    if (!arch_list) return;

    size_t ai = 0;
    for (ArchUsage *a = ctx->report; a; a = a->next) arch_list[ai++] = a;
    qsort(arch_list, arch_count, sizeof(ArchUsage *), cmp_arch_usage);

    for (size_t i = 0; i < arch_count; ++i) {
        ArchUsage *a = arch_list[i];
        fprintf(out, "------------ %s ------------\n", arch_to_string(a->arch));

        size_t lib_count = count_lib_usage(a->libs);
        if (lib_count == 0) {
            fprintf(out, "(no matches)\n\n");
            continue;
        }

        LibUsage **lib_list = calloc(lib_count, sizeof(LibUsage *));
        if (!lib_list) continue;

        size_t li = 0;
        for (LibUsage *l = a->libs; l; l = l->next) lib_list[li++] = l;
        qsort(lib_list, lib_count, sizeof(LibUsage *), cmp_lib_usage);

        for (size_t j = 0; j < lib_count; ++j) {
            LibUsage *lib = lib_list[j];
            fprintf(out, "%s (%zu execs)\n", lib->name, lib->exec_count);

            size_t path_count = count_paths(lib->executables);
            char **paths = calloc(path_count ? path_count : 1, sizeof(char *));
            if (!paths) continue;

            size_t pi = 0;
            for (PathNode *p = lib->executables; p; p = p->next) paths[pi++] = p->path;
            qsort(paths, path_count, sizeof(char *), cmp_str);

            for (size_t k = 0; k < path_count; ++k) fprintf(out, "-> %s\n", paths[k]);
            free(paths);
        }

        free(lib_list);
        fprintf(out, "\n");
    }

    free(arch_list);
}

static void write_json_report(FILE *out, const AppContext *ctx) {
    size_t arch_count = count_arch_usage(ctx->report);
    ArchUsage **arch_list = calloc(arch_count ? arch_count : 1, sizeof(ArchUsage *));
    if (!arch_list) return;

    size_t ai = 0;
    for (ArchUsage *a = ctx->report; a; a = a->next) arch_list[ai++] = a;
    qsort(arch_list, arch_count, sizeof(ArchUsage *), cmp_arch_usage);

    fprintf(out, "{\n  \"scan_dir\": ");
    write_json_string(out, ctx->scan_dir);
    fprintf(out, ",\n  \"targets\": [");
    for (size_t i = 0; i < ctx->target_count; ++i) {
        if (i) fprintf(out, ", ");
        write_json_string(out, ctx->targets[i]);
    }
    fprintf(out, "],\n  \"generated_at_utc\": \"");
    write_iso_utc(out);
    fprintf(out, "\",\n  \"report\": {\n");

    for (size_t i = 0; i < arch_count; ++i) {
        ArchUsage *a = arch_list[i];
        fprintf(out, "    ");
        write_json_string(out, arch_to_string(a->arch));
        fprintf(out, ": {\n");

        size_t lib_count = count_lib_usage(a->libs);
        LibUsage **lib_list = calloc(lib_count ? lib_count : 1, sizeof(LibUsage *));
        if (!lib_list) {
            fprintf(out, "    }%s\n", (i + 1 == arch_count) ? "" : ",");
            continue;
        }

        size_t li = 0;
        for (LibUsage *l = a->libs; l; l = l->next) lib_list[li++] = l;
        qsort(lib_list, lib_count, sizeof(LibUsage *), cmp_lib_usage);

        for (size_t j = 0; j < lib_count; ++j) {
            LibUsage *lib = lib_list[j];
            fprintf(out, "      ");
            write_json_string(out, lib->name);
            fprintf(out, ": [");

            size_t path_count = count_paths(lib->executables);
            char **paths = calloc(path_count ? path_count : 1, sizeof(char *));
            if (paths) {
                size_t pi = 0;
                for (PathNode *p = lib->executables; p; p = p->next) paths[pi++] = p->path;
                qsort(paths, path_count, sizeof(char *), cmp_str);
                for (size_t k = 0; k < path_count; ++k) {
                    if (k) fprintf(out, ", ");
                    write_json_string(out, paths[k]);
                }
                free(paths);
            }

            fprintf(out, "]%s\n", (j + 1 == lib_count) ? "" : ",");
        }

        free(lib_list);
        fprintf(out, "    }%s\n", (i + 1 == arch_count) ? "" : ",");
    }

    fprintf(out, "  }\n}\n");
    free(arch_list);
}

static void free_report(ArchUsage *head) {
    while (head) {
        ArchUsage *next_arch = head->next;
        LibUsage *lib = head->libs;
        while (lib) {
            LibUsage *next_lib = lib->next;
            PathNode *p = lib->executables;
            while (p) {
                PathNode *next_path = p->next;
                free(p->path);
                free(p);
                p = next_path;
            }
            free(lib->name);
            free(lib);
            lib = next_lib;
        }
        free(head);
        head = next_arch;
    }
}

/* ---------- CLI ---------- */

static void print_help(const char *prog) {
    printf("Usage: %s [options] <library> [library...]\n\n", prog);
    printf("Backward ldd: find ELF executables that depend on selected shared libraries.\n\n");
    printf("Options:\n");
    printf("  -d, --scan-dir DIR       Directory to recursively scan (default: .)\n");
    printf("  -a, --arch ARCH          x86 | x86_64 | armv7 | aarch64 (repeatable)\n");
    printf("      --include-hidden     Include hidden files/directories\n");
    printf("  -f, --format FORMAT      txt | json (default: txt)\n");
    printf("  -o, --output FILE        Output report file (default: bldd_report.txt)\n");
    printf("  -h, --help               Show this help\n\n");
    printf("Examples:\n");
    printf("  %s libc.so.6 --scan-dir /usr/bin --output report.txt\n", prog);
    printf("  %s libc.so.6 libm.so.6 --scan-dir /home --arch x86_64 --arch aarch64\n", prog);
    printf("  %s /lib/x86_64-linux-gnu/libc.so.6 --scan-dir / --format json --output report.json\n", prog);
}

int main(int argc, char **argv) {
    const char *scan_dir = ".";
    const char *format = "txt";
    const char *output = "bldd_report.txt";
    bool include_hidden = false;
    bool arch_filter_given = false;
    uint32_t selected_arch_bits = 0;

    static const struct option long_opts[] = {
        {"scan-dir", required_argument, NULL, 'd'},
        {"arch", required_argument, NULL, 'a'},
        {"include-hidden", no_argument, NULL, 1000},
        {"format", required_argument, NULL, 'f'},
        {"output", required_argument, NULL, 'o'},
        {"help", no_argument, NULL, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "d:a:f:o:h", long_opts, NULL)) != -1) {
        switch (c) {
            case 'd':
                scan_dir = optarg;
                break;
            case 'a': {
                Arch arch = arch_from_arg(optarg);
                if (arch == ARCH_UNKNOWN) {
                    fprintf(stderr, "Unsupported --arch value: %s\n", optarg);
                    return 1;
                }
                arch_filter_given = true;
                selected_arch_bits |= arch_bit(arch);
                break;
            }
            case 'f':
                if (strcmp(optarg, "txt") != 0 && strcmp(optarg, "json") != 0) {
                    fprintf(stderr, "Unsupported --format value: %s\n", optarg);
                    return 1;
                }
                format = optarg;
                break;
            case 'o':
                output = optarg;
                break;
            case 1000:
                include_hidden = true;
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                print_help(argv[0]);
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "At least one library is required.\n\n");
        print_help(argv[0]);
        return 1;
    }

    struct stat st;
    if (stat(scan_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Scan directory does not exist or is not a directory: %s\n", scan_dir);
        return 1;
    }

    AppContext ctx = {0};
    ctx.scan_dir = scan_dir;
    ctx.include_hidden = include_hidden;
    ctx.arch_filter_mask = arch_filter_given ? selected_arch_bits
                                             : arch_bit(ARCH_X86) | arch_bit(ARCH_X86_64) |
                                                   arch_bit(ARCH_ARMV7) | arch_bit(ARCH_AARCH64);

    for (int i = optind; i < argc; ++i) {
        if (!add_unique_string(&ctx.targets, &ctx.target_count, argv[i]) ||
            !add_unique_string(&ctx.targets, &ctx.target_count, base_name(argv[i]))) {
            fprintf(stderr, "Memory allocation failed.\n");
            free_string_array(ctx.targets, ctx.target_count);
            return 1;
        }
    }

    g_ctx = &ctx;
    if (nftw(scan_dir, scan_callback, 32, FTW_PHYS) != 0) {
        fprintf(stderr, "Scan failed: %s\n", strerror(errno));
        free_report(ctx.report);
        free_string_array(ctx.targets, ctx.target_count);
        return 1;
    }

    FILE *out = fopen(output, "w");
    if (!out) {
        fprintf(stderr, "Cannot open output file %s: %s\n", output, strerror(errno));
        free_report(ctx.report);
        free_string_array(ctx.targets, ctx.target_count);
        return 1;
    }

    if (strcmp(format, "json") == 0) {
        write_json_report(out, &ctx);
    } else {
        write_txt_report(out, &ctx);
    }

    fclose(out);
    printf("Report written to: %s\n", output);

    free_report(ctx.report);
    free_string_array(ctx.targets, ctx.target_count);
    return 0;
}
