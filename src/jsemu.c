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
extern void log(const uint8_t *buf, int len);
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

FILE *cout;

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
	fprintf(stderr, buf);
  fprintf(cout, buf);
}

static void console_get_size(int *pw, int *ph) {
  pw = 80;
  ph = 24;
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
static void init_vm_drive(void *arg);


void vm_start(const char *url, int ram_size, const char *cmdline,
              const char *pwd, int width, int height, BOOL has_network)
{
    fprintf(stderr, "vm start!");
    VMStartState *s;

    global_boot_idle = FALSE;
    cout = fopen("/home/build/out.txt", "a");

    s = mallocz(sizeof(*s));
    s->ram_size = ram_size;
    s->cmdline = strdup(cmdline);
    if (pwd)
        s->pwd = strdup(pwd);
    global_width = width;
    global_height = height;
    s->has_network = has_network;
    s->p = mallocz(sizeof(VirtMachineParams));
    virt_machine_set_defaults(s->p);
    virt_machine_load_config_file(s->p, url, init_vm_fs, s);
}

int main() {
	fprintf(stderr, "main called!");
  vm_start("root-riscv64.cfg", 256, "", "", 0, 0, FALSE);
}

static void init_vm_fs(void *arg)
{
    printf(stderr, "init vm fs");

    VMStartState *s = arg;
    VirtMachineParams *p = s->p;

    if (p->fs_count > 0) {
        assert(p->fs_count == 1);
        printf(stderr, "TODO init fs net: init_vm_fs %s\n", p->tab_fs[0].filename);
        /* p->tab_fs[0].fs_dev = fs_net_init(p->tab_fs[0].filename, */
        /*                                   init_vm_drive, s); */
        /* if (s->pwd) { */
        /*     fs_net_set_pwd(p->tab_fs[0].fs_dev, s->pwd); */
        /* } */
    } else {
        printf(stderr, "no fs_count, init vm_drive\n");
        init_vm_drive(s);
    }
}

static int64_t bf_get_sector_count(BlockDevice *bs)
{
    printf(stderr, "bf_get_sector_count\n");
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
                printf("OHH NOOOOOOOOOOOOOOO\n");
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
  printf(stderr, "pf_write_async %n\n", sector_num);
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


static BlockDevice *block_device_init(const char *filename,
                                      BlockDeviceModeEnum mode)
{
    BlockDevice *bs;
    BlockDeviceFile *bf;
    int64_t file_size;
    FILE *f;
    const char *mode_str;

    if (mode == BF_MODE_RW) {
        mode_str = "r+b";
    } else {
        mode_str = "rb";
    }

    printf(stderr, "block_device_init %s\n", filename);
    f = fopen(filename, mode_str);
    if (!f) {
        perror(filename);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    file_size = ftello(f);
    /* printf("file_size =%" PRId64 "\n", file_size / 512); */

    bs = mallocz(sizeof(*bs));
    bf = mallocz(sizeof(*bf));

    bf->mode = mode;
    bf->nb_sectors = file_size / 512;
    bf->f = f;

    printf(stderr, "%s", f);

    if (mode == BF_MODE_SNAPSHOT) {
        bf->sector_table = mallocz(sizeof(bf->sector_table[0]) *
                                   bf->nb_sectors);
    }

    bs->opaque = bf;
    bs->get_sector_count = bf_get_sector_count;
    bs->read_async = bf_read_async;
    bs->write_async = bf_write_async;
    return bs;
}



static void init_vm_drive(void *arg)
{
    printf(stderr, "init_vm_drive\n");
    VMStartState *s = arg;
    VirtMachineParams *p = s->p;

    if (p->drive_count > 0) {
        assert(p->drive_count == 1);
        /* p->tab_drive[0].block_dev = */
        /*     block_device_init_http(p->tab_drive[0].filename, */
        /*                            131072, */
        /*                            init_vm, s); */
        /* printf("init_vm_drives %x\n", p->drive_count); */
        /* printf("  filename =%s\n", p->tab_drive[0].filename); */
        p->tab_drive[0].block_dev = block_device_init(p->tab_drive[0].filename, BF_MODE_RW);
        init_vm(s);
    } else {
        init_vm(s);
    }
}

static void init_vm(void *arg)
{
    printf(stderr, "init_vm\n");
    VMStartState *s = arg;
    VirtMachine *m;
    VirtMachineParams *p = s->p;
    int i;

    p->rtc_real_time = TRUE;
    p->ram_size = s->ram_size << 20;
    if (s->cmdline && s->cmdline[0] != '\0') {
        vm_add_cmdline(s->p, s->cmdline);
    }

    if (global_width > 0 && global_height > 0) {
        /* enable graphic output if needed */
        if (!p->display_device)
            p->display_device = strdup("simplefb");
        p->width = global_width;
        p->height = global_height;
    } else {
        p->console = console_init();
    }

    if (p->eth_count > 0 && !s->has_network) {
        /* remove the interfaces */
        for(i = 0; i < p->eth_count; i++) {
            free(p->tab_eth[i].ifname);
            free(p->tab_eth[i].driver);
        }
        p->eth_count = 0;
    }

    // DONT expose network
    /* if (p->eth_count > 0) { */
    /*     EthernetDevice *net; */
    /*     int i; */
    /*     assert(p->eth_count == 1); */
    /*     net = mallocz(sizeof(EthernetDevice)); */
    /*     net->mac_addr[0] = 0x02; */
    /*     for(i = 1; i < 6; i++) */
    /*         net->mac_addr[i] = (int)(emscripten_random() * 256); */
    /*     net->write_packet = net_recv_packet; */
    /*     net->opaque = NULL; */
    /*     p->tab_eth[0].net = net; */
    /* } */

    m = virt_machine_init(p);
    global_vm = m;

    virt_machine_free_config(s->p);

    if (m->net) {
        m->net->device_set_carrier(m->net, global_carrier_state);
    }

    free(s->p);
    free(s->cmdline);
    if (s->pwd) {
        memset(s->pwd, 0, strlen(s->pwd));
        free(s->pwd);
    }
    free(s);

    virt_machine_run(m);
}

/* need to be long enough to hide the non zero delay of setTimeout(_, 0) */
#define MAX_EXEC_TOTAL_CYCLE 3000000
#define MAX_EXEC_CYCLE        200000

#define MAX_SLEEP_TIME 10 /* in ms */

void virt_machine_run(void *opaque)
{
    VirtMachine *m = opaque;
    int delay, i;
    FBDevice *fb_dev;

    if (m->console_dev && virtio_console_can_write_data(m->console_dev)) {
        uint8_t buf[128];
        int ret, len;
        len = virtio_console_get_write_len(m->console_dev);
        len = min_int(len, sizeof(buf));
        ret = m->console->read_data(m->console->opaque, buf, len);
        if (ret > 0)
            virtio_console_write_data(m->console_dev, buf, ret);
        if (console_resize_pending) {
            int w, h;
            console_get_size(&w, &h);
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

    printf(stderr, "delay %x\n", delay);
    if (delay == 0) {
        /* emscripten_async_call(virt_machine_run, m, 0); */
        virt_machine_run(m);
    } else {
      printf(stderr, "sleep %n\n", MAX_SLEEP_TIME);
      if(!global_boot_idle) {
        printf(stderr, "IDLE");
        global_boot_idle = TRUE;
        console_queue_char(112);
        console_queue_char(119);
        console_queue_char(100);
        console_queue_char(10);
        console_queue_char(104);
        console_queue_char(97);
        console_queue_char(108);
        console_queue_char(116);
        console_queue_char(32);
        console_queue_char(45);
        console_queue_char(102);
        console_queue_char(10);
      }
      /* emscripten_async_call(virt_machine_run, m, MAX_SLEEP_TIME); */
        virt_machine_run(m);
    }
}
