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

/* static int vm_get_str2(JSONValue obj, const char *name, const char **pstr, */
/*                       BOOL is_opt) */
/* { */
/*     JSONValue val; */
/*     val = json_object_get(obj, name); */
/*     if (json_is_undefined(val)) { */
/*         if (is_opt) { */
/*             *pstr = NULL; */
/*             return 0; */
/*         } else { */
/*             vm_error("expecting '%s' property\n", name); */
/*             return -1; */
/*         } */
/*     } */
/*     if (val.type != JSON_STR) { */
/*         vm_error("%s: string expected\n", name); */
/*         return -1; */
/*     } */
/*     *pstr = val.u.str->data; */
/*     return 0; */
/* } */

/* static int vm_get_str(JSONValue obj, const char *name, const char **pstr) */
/* { */
/*     return vm_get_str2(obj, name, pstr, FALSE); */
/* } */
/*  */
/* static int vm_get_str_opt(JSONValue obj, const char *name, const char **pstr) */
/* { */
/*     return vm_get_str2(obj, name, pstr, TRUE); */
/* } */

/* static char *strdup_null(const char *str) */
/* { */
/*     if (!str) */
/*         return NULL; */
/*     else */
/*         return strdup(str); */
/* } */

/* currently only for "TZ" */
/* static char *cmdline_subst(const char *cmdline) */
/* { */
/*     DynBuf dbuf; */
/*     const char *p; */
/*     char var_name[32], *q, buf[32]; */
/*  */
/*     dbuf_init(&dbuf); */
/*     p = cmdline; */
/*     while (*p != '\0') { */
/*         if (p[0] == '$' && p[1] == '{') { */
/*             p += 2; */
/*             q = var_name; */
/*             while (*p != '\0' && *p != '}') { */
/*                 if ((q - var_name) < sizeof(var_name) - 1) */
/*                     *q++ = *p; */
/*                 p++; */
/*             } */
/*             *q = '\0'; */
/*             if (*p == '}') */
/*                 p++; */
/*             if (!strcmp(var_name, "TZ")) { */
/*                 time_t ti; */
/*                 struct tm tm; */
/*                 int n, sg; */
/*                 #<{(| get the offset to UTC |)}># */
/*                 time(&ti); */
/*                 localtime_r(&ti, &tm); */
/*                 n = tm.tm_gmtoff / 60; */
/*                 sg = '-'; */
/*                 if (n < 0) { */
/*                     sg = '+'; */
/*                     n = -n; */
/*                 } */
/*                 snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", */
/*                          sg, n / 60, n % 60); */
/*                 dbuf_putstr(&dbuf, buf); */
/*             } */
/*         } else { */
/*             dbuf_putc(&dbuf, *p++); */
/*         } */
/*     } */
/*     dbuf_putc(&dbuf, 0); */
/*     return (char *)dbuf.buf; */
/* } */

// TODO - delete
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

// TODO - delete
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

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(size);
    if (fread(buf, 1, size, f) != size) {
        fprintf(stderr, "%s: read error\n", filename);
        exit(1);
    }
    fclose(f);
    *pbuf = buf;
    return size;
}


static void config_load_file(VMConfigLoadState *s, const char *filename,
                             FSLoadFileCB *cb, void *opaque)
{
       /* printf("loading %s\n", filename); */
    {
        uint8_t *buf;
        int size;
        size = load_file(&buf, filename);
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
    /* printf("virt_machine_load_config_file %s\n", filename); */

    s = mallocz(sizeof(*s));
    s->vm_params = p;
    s->start_cb = start_cb;
    s->opaque = opaque;
    p->cfg_filename = strdup(filename);

    config_load_file(s, filename, config_file_loaded, s);
}

static void config_file_loaded(void *opaque, uint8_t *buf, int buf_len)
{
    VMConfigLoadState *s = opaque;
    /* VirtMachineParams *p = s->vm_params; */

    /* load the additional files */
    s->file_index = 0;
    /* printf("config_file_loaded\n"); */
    config_additional_file_load(s);
}

static void config_additional_file_load(VMConfigLoadState *s)
{
    VirtMachineParams *p = s->vm_params;
    while (s->file_index < VM_FILE_COUNT &&
           p->files[s->file_index].filename == NULL) {
        s->file_index++;
    }
    if (s->file_index == VM_FILE_COUNT) {
      /* printf("cb \n"); */
        if (s->start_cb)
            s->start_cb(s->opaque);
        free(s);
    } else {
        char *fname;

        fname = get_file_path(p->cfg_filename,
                              p->files[s->file_index].filename);
        /* printf("file %s\n", fname); */
        config_load_file(s, fname,
                         config_additional_file_load_cb, s);
        free(fname);
    }
}

static void config_additional_file_load_cb(void *opaque,
                                           uint8_t *buf, int buf_len)
{
    VMConfigLoadState *s = opaque;
    VirtMachineParams *p = s->vm_params;

    p->files[s->file_index].buf = malloc(buf_len);
    memcpy(p->files[s->file_index].buf, buf, buf_len);
    p->files[s->file_index].len = buf_len;

    /* load the next files */
    s->file_index++;
    /* printf("config_additional_file_load_cb\n"); */
    config_additional_file_load(s);
}

void vm_add_cmdline(VirtMachineParams *p, const char *cmdline)
{
    printf("vm_add_cmdline\n");
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
