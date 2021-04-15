/*
 * JS emulator main
 *
 * Copyright (c) 2016-2017 Fabrice Bellard
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
#include <emscripten.h>

#include "cutils.h"
#include "iomem.h"
#include "virtio.h"
#include "machine.h"
#include "list.h"
#include "fbuf.h"

void virt_machine_run(void *opaque);

/* provided in lib.js */
/* extern void console_write(void *opaque, const uint8_t *buf, int len); */
/* extern void fb_refresh(void *opaque, void *data, */
/*                        int x, int y, int w, int h, int stride); */
/* extern void net_recv_packet(EthernetDevice *bs, */
/*                             const uint8_t *buf, int len); */



static uint8_t console_fifo[1024];
static int console_fifo_windex;
static int console_fifo_rindex;
static int console_fifo_count;
static BOOL console_resize_pending;

static int global_width;
static int global_height;
static VirtMachine *global_vm;
static BOOL global_carrier_state;
static BOOL global_boot_idle;

FILE *out;
FILE *in;

typedef enum {
    BF_MODE_RO,
    BF_MODE_RW,
    BF_MODE_SNAPSHOT,
} BlockDeviceModeEnum;

typedef struct BlockDeviceFile {
    FILE *f;
    int64_t nb_sectors;
    BlockDeviceModeEnum mode;
    uint8_t **sector_table;
} BlockDeviceFile;

#define SECTOR_SIZE 512


static void console_write(void *opaque, const uint8_t *buf, int len) {
  for ( int i = 0; i < len; i++ )
  {
    fprintf(out, "%c",  buf[i]);
  }
}

static int console_read(void *opaque, uint8_t *buf, int len)
{
    int out_len, l;
    len = min_int(len, console_fifo_count);
    console_fifo_count -= len;
    out_len = 0;
    while (len != 0) {
        l = min_int(len, sizeof(console_fifo) - console_fifo_rindex);
        memcpy(buf + out_len, console_fifo + console_fifo_rindex, l);
        len -= l;
        out_len += l;
        console_fifo_rindex += l;
        if (console_fifo_rindex == sizeof(console_fifo))
            console_fifo_rindex = 0;
    }
    return out_len;
}

/* called from JS */
void console_queue_char(int c)
{
    if (console_fifo_count < sizeof(console_fifo)) {
        console_fifo[console_fifo_windex] = c;
        if (++console_fifo_windex == sizeof(console_fifo))
            console_fifo_windex = 0;
        console_fifo_count++;
    }
}

/* static void fb_refresh1(FBDevice *fb_dev, void *opaque, */
/*                         int x, int y, int w, int h) */
/* { */
/*     int stride = fb_dev->stride; */
/*     fb_refresh(opaque, fb_dev->fb_data + y * stride + x * 4, x, y, w, h, */
/*                stride); */
/* } */

static CharacterDevice *console_init(void)
{
    CharacterDevice *dev;
    console_resize_pending = TRUE;
    dev = mallocz(sizeof(*dev));
    dev->write_data = console_write;
    dev->read_data = console_read;
    return dev;
}

typedef struct {
    VirtMachineParams *p;
    int ram_size;
    char *cmdline;
    BOOL has_network;
    char *pwd;
} VMStartState;

static void init_vm(void *arg);
static void init_vm_fs(void *arg);



static int64_t bf_get_sector_count(BlockDevice *bs)
{
    /* printf("bf_get_sector_count\n"); */
    BlockDeviceFile *bf = bs->opaque;
    return bf->nb_sectors;
}

static int bf_read_async(BlockDevice *bs,
                         uint64_t sector_num, uint8_t *buf, int n,
                         BlockDeviceCompletionFunc *cb, void *opaque)
{
    BlockDeviceFile *bf = bs->opaque;
    /* printf("bf_read_async: sector_num=%" PRId64 " n=%d\n", sector_num, n); */
#ifdef DUMP_BLOCK_READ
    {
        static FILE *f;
        if (!f)
            f = fopen("/tmp/read_sect.txt", "wb");
        /* fprintf(f, "%" PRId64 " %d\n", sector_num, n); */
    }
#endif
    /* printf("bf '%s'\n", bf->f); */
    if (!bf->f)
        return -1;
    if (bf->mode == BF_MODE_SNAPSHOT) {
        int i;
        for(i = 0; i < n; i++) {
            if (!bf->sector_table[sector_num]) {
                fseek(bf->f, sector_num * SECTOR_SIZE, SEEK_SET);
                fread(buf, 1, SECTOR_SIZE, bf->f);
            } else {
                memcpy(buf, bf->sector_table[sector_num], SECTOR_SIZE);
            }
            sector_num++;
            buf += SECTOR_SIZE;
        }
    } else {
        /* printf("seek '%n' '%n'\n", sector_num, SECTOR_SIZE); */
        fseek(bf->f, sector_num * SECTOR_SIZE, SEEK_SET);
        fread(buf, 1, n * SECTOR_SIZE, bf->f);
    }
    /* for(int i = 0; i < n * SECTOR_SIZE; i++) */
    /*    printf("%x", buf[i]); */
    /* printf("\n"); */
    /* synchronous read */
    return 0;
}


static int bf_write_async(BlockDevice *bs,
                          uint64_t sector_num, const uint8_t *buf, int n,
                          BlockDeviceCompletionFunc *cb, void *opaque)
{
  /* printf("pf_write_async %n\n", sector_num); */
    BlockDeviceFile *bf = bs->opaque;
    int ret;

    switch(bf->mode) {
    case BF_MODE_RO:
        ret = -1; /* error */
        break;
    case BF_MODE_RW:
        fseek(bf->f, sector_num * SECTOR_SIZE, SEEK_SET);
        fwrite(buf, 1, n * SECTOR_SIZE, bf->f);
        ret = 0;
        break;
    case BF_MODE_SNAPSHOT:
        {
            int i;
            if ((sector_num + n) > bf->nb_sectors)
                return -1;
            for(i = 0; i < n; i++) {
                if (!bf->sector_table[sector_num]) {
                    bf->sector_table[sector_num] = malloc(SECTOR_SIZE);
                }
                memcpy(bf->sector_table[sector_num], buf, SECTOR_SIZE);
                sector_num++;
                buf += SECTOR_SIZE;
            }
            ret = 0;
        }
        break;
    default:
        abort();
    }

    return ret;
}


/* static BlockDevice *block_device_init(const char *filename, */
/*                                       BlockDeviceModeEnum mode) */
/* { */
/*     return bs; */
/* } */


void vm_start(int ram_size)
{
    FILE *f;
    global_boot_idle = FALSE;

    printf("opening stdout.txt and stdin.txt\n");
    out = fopen("stdout.txt", "a");
    in = fopen("stdin.txt", "r");

    VirtMachineParams *p = mallocz(sizeof(VirtMachineParams));
    p->machine_name = "riscv64";
    p->vmc = &riscv_machine_class;
    p->ram_size = (uint64_t)ram_size << 20;
    p->cmdline = strdup("console=hvc0 root=/dev/vda rw");
    p->drive_count = 1;

    printf("opening bbl64.bin\n");
    f = fopen("bbl64.bin", "rb");
    p->files[VM_FILE_BIOS].len = 53786;
    p->files[VM_FILE_BIOS].buf = malloc(p->files[VM_FILE_BIOS].len);
    fread(p->files[VM_FILE_BIOS].buf, 1, p->files[VM_FILE_BIOS].len, f);
    fclose(f);

    printf("opening kernel-riscv64.bin\n");
    f = fopen("kernel-riscv64.bin", "rb");
    p->files[VM_FILE_KERNEL].len = 3979556;
    p->files[VM_FILE_KERNEL].buf = malloc(p->files[VM_FILE_KERNEL].len);
    fread(p->files[VM_FILE_KERNEL].buf, 1, p->files[VM_FILE_KERNEL].len, f);
    fclose(f);

    printf("opening root-riscv64.bin\n");
    BlockDevice *bs;
    BlockDeviceFile *bf;
    f = fopen("root-riscv64.bin", "r+b");
    bf = mallocz(sizeof(*bf));
    bf->mode = BF_MODE_RW;
    bf->nb_sectors = 4194304 / 512;
    bf->f = f;

    printf("initializing block storage\n");

    bs = mallocz(sizeof(*bs));
    bs->opaque = bf;
    bs->get_sector_count = bf_get_sector_count;
    bs->read_async = bf_read_async;
    bs->write_async = bf_write_async;
    p->tab_drive[0].block_dev = bs;


    printf("initializing vm\n");
    // INIT

    VirtMachine *m;

    p->rtc_real_time = TRUE;
    p->ram_size = ram_size << 20;

    p->console = console_init();

    m = p->vmc->virt_machine_init(p);
    global_vm = m;

    printf("running vm\n");
    virt_machine_run(m);
}

int main() {
  vm_start(512);
}




/* need to be long enough to hide the non zero delay of setTimeout(_, 0) */
#define MAX_EXEC_TOTAL_CYCLE 3000000
#define MAX_EXEC_CYCLE        200000

#define MAX_SLEEP_TIME 10 /* in ms */

void virt_machine_run(void *opaque)
{
    VirtMachine *m = opaque;
    int delay, i;

    if (m->console_dev && virtio_console_can_write_data(m->console_dev)) {
        uint8_t buf[128];
        int ret, len;
        len = virtio_console_get_write_len(m->console_dev);
        len = min_int(len, sizeof(buf));
        ret = m->console->read_data(m->console->opaque, buf, len);
        if (ret > 0)
            virtio_console_write_data(m->console_dev, buf, ret);
        if (console_resize_pending) {
            int w = 80;
            int h = 24;
            virtio_console_resize_event(m->console_dev, w, h);
            console_resize_pending = FALSE;
        }
    }

    i = 0;
    for(;;) {
        /* wait for an event: the only asynchronous event is the RTC timer */
        delay = virt_machine_get_sleep_duration(m, MAX_SLEEP_TIME);
        if (delay != 0 || i >= MAX_EXEC_TOTAL_CYCLE / MAX_EXEC_CYCLE)
            break;
        virt_machine_interp(m, MAX_EXEC_CYCLE);
        i++;
    }

    /* printf("delay %x\n", delay); */
    if (delay == 0) {
        /* emscripten_async_call(virt_machine_run, m, 0); */
        virt_machine_run(m);
    } else {
        /* printf("sleep %n\n", MAX_SLEEP_TIME); */
      if(!global_boot_idle) {
        global_boot_idle = TRUE;
        int c;
        while ((c = fgetc(in)) != EOF)
        {
            console_queue_char(c);
        }
      }
        virt_machine_run(m);
    }
}
