/*
 * VM utilities
 *
 * Copyright (c) 2017 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include "cutils.h"
#include "iomem.h"
#include "virtio.h"
#include "machine.h"
#include "fs_utils.h"

void __attribute__((format(printf, 1, 2))) vm_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#ifdef EMSCRIPTEN
    vprintf(fmt, ap);
#else
    vfprintf(stderr, fmt, ap);
#endif
    va_end(ap);
}

int vm_get_int(JSONValue obj, const char *name, int *pval)
{
    JSONValue val;
    val = json_object_get(obj, name);
    if (json_is_undefined(val)) {
        vm_error("expecting '%s' property\n", name);
        return -1;
    }
    if (val.type != JSON_INT) {
        vm_error("%s: integer expected\n", name);
        return -1;
    }
    *pval = val.u.int32;
    return 0;
}

int vm_get_int_opt(JSONValue obj, const char *name, int *pval, int def_val)
{
    JSONValue val;
    val = json_object_get(obj, name);
    if (json_is_undefined(val)) {
        *pval = def_val;
        return 0;
    }
    if (val.type != JSON_INT) {
        vm_error("%s: integer expected\n", name);
        return -1;
    }
    *pval = val.u.int32;
    return 0;
}

static int vm_get_str2(JSONValue obj, const char *name, const char **pstr,
                      BOOL is_opt)
{
    printf("HHH %s\n", name);
    JSONValue val;
    val = json_object_get(obj, name);
    printf("H01 %s\n", name);
    if (json_is_undefined(val)) {
        printf("H01 %s\n", name);
        if (is_opt) {
            printf("H02 %s\n", name);
            *pstr = NULL;
            return 0;
        } else {
            printf("H03 %s\n", name);
            vm_error("expecting '%s' property\n", name);
            return -1;
        }
    }
    if (val.type != JSON_STR) {
        printf("H04\n");
        vm_error("%s: string expected\n", name);
        return -1;
    }
        printf("H05\n");
    *pstr = val.u.str->data;
    return 0;
}

static int vm_get_str(JSONValue obj, const char *name, const char **pstr)
{
    return vm_get_str2(obj, name, pstr, FALSE);
}

static int vm_get_str_opt(JSONValue obj, const char *name, const char **pstr)
{
    return vm_get_str2(obj, name, pstr, TRUE);
}

static char *strdup_null(const char *str)
{
    if (!str)
        return NULL;
    else
        return strdup(str);
}

/* currently only for "TZ" */
static char *cmdline_subst(const char *cmdline)
{
    DynBuf dbuf;
    const char *p;
    char var_name[32], *q, buf[32];

    dbuf_init(&dbuf);
    p = cmdline;
    while (*p != '\0') {
        if (p[0] == '$' && p[1] == '{') {
            p += 2;
            q = var_name;
            while (*p != '\0' && *p != '}') {
                if ((q - var_name) < sizeof(var_name) - 1)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
            if (*p == '}')
                p++;
            if (!strcmp(var_name, "TZ")) {
                time_t ti;
                struct tm tm;
                int n, sg;
                /* get the offset to UTC */
                time(&ti);
                localtime_r(&ti, &tm);
                n = tm.tm_gmtoff / 60;
                sg = '-';
                if (n < 0) {
                    sg = '+';
                    n = -n;
                }
                snprintf(buf, sizeof(buf), "UTC%c%02d:%02d",
                         sg, n / 60, n % 60);
                dbuf_putstr(&dbuf, buf);
            }
        } else {
            dbuf_putc(&dbuf, *p++);
        }
    }
    dbuf_putc(&dbuf, 0);
    return (char *)dbuf.buf;
}

static BOOL find_name(const char *name, const char *name_list)
{
    size_t len;
    const char *p, *r;

    p = name_list;
    for(;;) {
        r = strchr(p, ',');
        if (!r) {
            if (!strcmp(name, p))
                return TRUE;
            break;
        } else {
            len = r - p;
            if (len == strlen(name) && !memcmp(name, p, len))
                return TRUE;
            p = r + 1;
        }
    }
    return FALSE;
}

static const VirtMachineClass *virt_machine_list[] = {
#if defined(EMSCRIPTEN)
    /* only a single machine in the EMSCRIPTEN target */
#ifndef CONFIG_X86EMU
    &riscv_machine_class,
#endif
#else
    &riscv_machine_class,
#endif /* !EMSCRIPTEN */
#ifdef CONFIG_X86EMU
    &pc_machine_class,
#endif
    NULL,
};

static const VirtMachineClass *virt_machine_find_class(const char *machine_name)
{
    const VirtMachineClass *vmc, **pvmc;

    for(pvmc = virt_machine_list; *pvmc != NULL; pvmc++) {
        vmc = *pvmc;
        if (find_name(machine_name, vmc->machine_names))
            return vmc;
    }
    return NULL;
}

static int virt_machine_parse_config(VirtMachineParams *p,
                                     char *config_file_str, int len)
{
    /* printf("virt_machine_parse_config\n"); */
    int version, val;
    const char *tag_name, *str;
    char buf1[256];
    JSONValue cfg, obj, el;

    /* printf("virt_machine_parse_config0\n"); */
    cfg = json_parse_value_len(config_file_str, len);
    if (json_is_error(cfg)) {
        vm_error("error: %s\n", json_get_error(cfg));
        json_free(cfg);
        return -1;
    }

    if (vm_get_int(cfg, "version", &version) < 0) {
        goto tag_fail;
    }
    if (version != VM_CONFIG_VERSION) {
        if (version > VM_CONFIG_VERSION) {
            vm_error("The emulator is too old to run this VM: please upgrade\n");
            return -1;
        } else {
            vm_error("The VM configuration file is too old for this emulator version: please upgrade the VM configuration file\n");
            return -1;
        }
    }

    printf("F0\n");
    if (vm_get_str(cfg, "machine", &str) < 0) {
        printf("FFFFFFFFFFFFFFFFFFFFFF0 %d\n", vm_get_str(cfg, "machine", &str));
        goto tag_fail;
    }
    printf("F111111\n");
    p->machine_name = strdup(str);
    printf("F2\n");
    p->vmc = virt_machine_find_class(p->machine_name);
    printf("F3\n");
    p->vmc = virt_machine_find_class(p->machine_name);
    printf("F4\n");
    if (!p->vmc) {
        vm_error("Unknown machine name: %s\n", p->machine_name);
        goto tag_fail;
    }
    printf("F5\n");
    p->vmc->virt_machine_set_defaults(p);
    printf("irt_machine_parse_config2\n");

    tag_name = "memory_size";
    if (vm_get_int(cfg, tag_name, &val) < 0)
        goto tag_fail;
    p->ram_size = (uint64_t)val << 20;

    printf("virt_machine_parse_config3\n");
    tag_name = "bios";
    if (vm_get_str_opt(cfg, tag_name, &str) < 0)
        goto tag_fail;
    if (str) {
        p->files[VM_FILE_BIOS].filename = strdup(str);
    }

    tag_name = "kernel";
    if (vm_get_str_opt(cfg, tag_name, &str) < 0)
        goto tag_fail;
    if (str) {
        p->files[VM_FILE_KERNEL].filename = strdup(str);
    }

    printf("virt_machine_parse_config4\n");
    tag_name = "initrd";
    if (vm_get_str_opt(cfg, tag_name, &str) < 0)
        goto tag_fail;
    if (str) {
        p->files[VM_FILE_INITRD].filename = strdup(str);
    }

    if (vm_get_str_opt(cfg, "cmdline", &str) < 0)
        goto tag_fail;
    if (str) {
        p->cmdline = cmdline_subst(str);
    }

    printf("virt_machine_parse_config5\n");
    for(;;) {
        snprintf(buf1, sizeof(buf1), "drive%d", p->drive_count);
        obj = json_object_get(cfg, buf1);
        if (json_is_undefined(obj))
            break;
        if (p->drive_count >= MAX_DRIVE_DEVICE) {
            vm_error("Too many drives\n");
            return -1;
        }
        if (vm_get_str(obj, "file", &str) < 0)
            goto tag_fail;
        p->tab_drive[p->drive_count].filename = strdup(str);
        if (vm_get_str_opt(obj, "device", &str) < 0)
            goto tag_fail;
        p->tab_drive[p->drive_count].device = strdup_null(str);
        p->drive_count++;
    }

    printf("virt_machine_parse_config6\n");
    for(;;) {
        snprintf(buf1, sizeof(buf1), "fs%d", p->fs_count);
        obj = json_object_get(cfg, buf1);
        if (json_is_undefined(obj))
            break;
        if (p->fs_count >= MAX_DRIVE_DEVICE) {
            vm_error("Too many filesystems\n");
            return -1;
        }
        if (vm_get_str(obj, "file", &str) < 0)
            goto tag_fail;
        p->tab_fs[p->fs_count].filename = strdup(str);
        if (vm_get_str_opt(obj, "tag", &str) < 0)
            goto tag_fail;
        if (!str) {
            if (p->fs_count == 0)
                strcpy(buf1, "/dev/root");
            else
                snprintf(buf1, sizeof(buf1), "/dev/root%d", p->fs_count);
            str = buf1;
        }
        p->tab_fs[p->fs_count].tag = strdup(str);
        p->fs_count++;
    }

    printf("virt_machine_parse_config7\n");
    for(;;) {
        snprintf(buf1, sizeof(buf1), "eth%d", p->eth_count);
        obj = json_object_get(cfg, buf1);
        if (json_is_undefined(obj))
            break;
        if (p->eth_count >= MAX_ETH_DEVICE) {
            vm_error("Too many ethernet interfaces\n");
            return -1;
        }
        if (vm_get_str(obj, "driver", &str) < 0)
            goto tag_fail;
        p->tab_eth[p->eth_count].driver = strdup(str);
        if (!strcmp(str, "tap")) {
            if (vm_get_str(obj, "ifname", &str) < 0)
                goto tag_fail;
            p->tab_eth[p->eth_count].ifname = strdup(str);
        }
        p->eth_count++;
    }

    printf("virt_machine_parse_config8\n");
    p->display_device = NULL;
    obj = json_object_get(cfg, "display0");
    if (!json_is_undefined(obj)) {
        if (vm_get_str(obj, "device", &str) < 0)
            goto tag_fail;
        p->display_device = strdup(str);
        if (vm_get_int(obj, "width", &p->width) < 0)
            goto tag_fail;
        if (vm_get_int(obj, "height", &p->height) < 0)
            goto tag_fail;
        if (vm_get_str_opt(obj, "vga_bios", &str) < 0)
            goto tag_fail;
        if (str) {
            p->files[VM_FILE_VGA_BIOS].filename = strdup(str);
        }
    }

    printf("virt_machine_parse_config9\n");
    if (vm_get_str_opt(cfg, "input_device", &str) < 0)
        goto tag_fail;
    p->input_device = strdup_null(str);

    if (vm_get_str_opt(cfg, "accel", &str) < 0)
        goto tag_fail;
    if (str) {
        if (!strcmp(str, "none")) {
            p->accel_enable = FALSE;
        } else if (!strcmp(str, "auto")) {
            p->accel_enable = TRUE;
        } else {
            vm_error("unsupported 'accel' config: %s\n", str);
            return -1;
        }
    }

    printf("virt_machine_parse_config10\n");
    tag_name = "rtc_local_time";
    el = json_object_get(cfg, tag_name);
    if (!json_is_undefined(el)) {
        if (el.type != JSON_BOOL) {
            vm_error("%s: boolean expected\n", tag_name);
            goto tag_fail;
        }
        p->rtc_local_time = el.u.b;
    }

    printf("virt_machine_parse_config11\n");
    json_free(cfg);
    return 0;
 tag_fail:
    json_free(cfg);
    return -1;
}

typedef void FSLoadFileCB(void *opaque, uint8_t *buf, int buf_len);

typedef struct {
    VirtMachineParams *vm_params;
    void (*start_cb)(void *opaque);
    void *opaque;

    FSLoadFileCB *file_load_cb;
    void *file_load_opaque;
    int file_index;
} VMConfigLoadState;

static void config_file_loaded(void *opaque, uint8_t *buf, int buf_len);
static void config_additional_file_load(VMConfigLoadState *s);
static void config_additional_file_load_cb(void *opaque,
                                           uint8_t *buf, int buf_len);

/* XXX: win32, URL */
char *get_file_path(const char *base_filename, const char *filename)
{
    int len, len1;
    char *fname, *p;

    if (!base_filename)
        goto done;
    if (strchr(filename, ':'))
        goto done; /* full URL */
    if (filename[0] == '/')
        goto done;
    p = strrchr(base_filename, '/');
    if (!p) {
    done:
        return strdup(filename);
    }
    len = p + 1 - base_filename;
    len1 = strlen(filename);
    fname = malloc(len + len1 + 1);
    memcpy(fname, base_filename, len);
    memcpy(fname + len, filename, len1 + 1);
    return fname;
}


/* return -1 if error. */
static int load_file(uint8_t **pbuf, const char *filename)
{
    FILE *f;
    int size;
    uint8_t *buf;

    printf("load_file '%s'\n", filename);

    f = fopen(filename, "rb");
    if (!f) {
        printf("no file\n");
        perror(filename);
        exit(1);
    }
    printf("post fopen..\n");
    /* fseek(f, 0, SEEK_END); */
    /* exit(1); */
    printf("post fseek..\n");
    if(filename = "") {
      size = 269;
    } else {
      printf("!!!!post ftell..%s\n", filename);
    }
    printf("post ftell..\n");
    fseek(f, 0, SEEK_SET);
    printf("post fseek..\n");
    buf = malloc(size);
    printf("post malloc..\n");
    if (fread(buf, 1, size, f) != size) {
        fprintf(stderr, "%s: read error\n", filename);
        exit(1);
    }
    fclose(f);
    printf("post close..\n");
    *pbuf = buf;
    return size;
}


static void config_load_file(VMConfigLoadState *s, const char *filename,
                             FSLoadFileCB *cb, void *opaque)
{
   printf("loadinggg %s\n", filename);
   printf("here\n");
    {
        printf("here\n");
        uint8_t *buf;
        int size;
        printf("yes here\n");
        size = load_file(&buf, filename);
        printf("yes yes\n");
        printf("size %d\n", size);
        cb(opaque, buf, size);
        free(buf);
    }
}

void virt_machine_load_config_file(VirtMachineParams *p,
                                   const char *filename,
                                   void (*start_cb)(void *opaque),
                                   void *opaque)
{
    VMConfigLoadState *s;
    printf("virt_machine_load_config_file %s\n", filename);

    s = mallocz(sizeof(*s));
    s->vm_params = p;
    s->start_cb = start_cb;
    s->opaque = opaque;
    p->cfg_filename = strdup(filename);

    config_load_file(s, filename, config_file_loaded, s);
}

static void config_file_loaded(void *opaque, uint8_t *buf, int buf_len)
{
    printf("config_file_loaded\n");
    VMConfigLoadState *s = opaque;
    VirtMachineParams *p = s->vm_params;
    printf("config_file_loaded0\n");

    if (virt_machine_parse_config(p, (char *)buf, buf_len) < 0)
        exit(1);
    printf("config_file_loaded1\n");

    /* load the additional files */
    s->file_index = 0;
    config_additional_file_load(s);
}

static void config_additional_file_load(VMConfigLoadState *s)
{
    printf("config_additional_file_load\n");
    VirtMachineParams *p = s->vm_params;
    while (s->file_index < VM_FILE_COUNT &&
           p->files[s->file_index].filename == NULL) {
        s->file_index++;
    }
    printf("post while\n");
    if (s->file_index == VM_FILE_COUNT) {
      printf("cb \n");
        if (s->start_cb)
            s->start_cb(s->opaque);
        free(s);
    } else {
      printf("else \n");
        char *fname;

        fname = get_file_path(p->cfg_filename,
                              p->files[s->file_index].filename);
        printf("file %s\n", fname);
        config_load_file(s, fname,
                         config_additional_file_load_cb, s);
        free(fname);
    }
}

static void config_additional_file_load_cb(void *opaque,
                                           uint8_t *buf, int buf_len)
{
    printf("config_additional_file_load_cb\n");
    VMConfigLoadState *s = opaque;
    VirtMachineParams *p = s->vm_params;

    p->files[s->file_index].buf = malloc(buf_len);
    memcpy(p->files[s->file_index].buf, buf, buf_len);
    p->files[s->file_index].len = buf_len;

    /* load the next files */
    s->file_index++;
    config_additional_file_load(s);
}

void vm_add_cmdline(VirtMachineParams *p, const char *cmdline)
{
    printf(stderr, "vm_add_cmdline\n");
    char *new_cmdline, *old_cmdline;
    if (cmdline[0] == '!') {
        new_cmdline = strdup(cmdline + 1);
    } else {
        old_cmdline = p->cmdline;
        if (!old_cmdline)
            old_cmdline = "";
        new_cmdline = malloc(strlen(old_cmdline) + 1 + strlen(cmdline) + 1);
        strcpy(new_cmdline, old_cmdline);
        strcat(new_cmdline, " ");
        strcat(new_cmdline, cmdline);
    }
    free(p->cmdline);
    p->cmdline = new_cmdline;
}

void virt_machine_free_config(VirtMachineParams *p)
{
    /* printf("virt_machine_free_config\n"); */
    int i;

    free(p->machine_name);
    free(p->cmdline);
    for(i = 0; i < VM_FILE_COUNT; i++) {
        free(p->files[i].filename);
        free(p->files[i].buf);
    }
    for(i = 0; i < p->drive_count; i++) {
        free(p->tab_drive[i].filename);
        free(p->tab_drive[i].device);
    }
    for(i = 0; i < p->fs_count; i++) {
        free(p->tab_fs[i].filename);
        free(p->tab_fs[i].tag);
    }
    for(i = 0; i < p->eth_count; i++) {
        free(p->tab_eth[i].driver);
        free(p->tab_eth[i].ifname);
    }
    free(p->input_device);
    free(p->display_device);
    free(p->cfg_filename);
}

VirtMachine *virt_machine_init(const VirtMachineParams *p)
{
    /* printf("virt_machine_init\n"); */
    const VirtMachineClass *vmc = p->vmc;
    return vmc->virt_machine_init(p);
}

void virt_machine_set_defaults(VirtMachineParams *p)
{
    memset(p, 0, sizeof(*p));
}

void virt_machine_end(VirtMachine *s)
{
    s->vmc->virt_machine_end(s);
}
