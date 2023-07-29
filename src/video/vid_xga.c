/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		IBM XGA emulation.
 *
 *
 *
 * Authors:	TheCollector1995.
 *
 *		Copyright 2022 TheCollector1995.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/bswap.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/rom.h>
#include <86box/mca.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/vid_xga_device.h>
#include "cpu.h"

#define XGA_BIOS_PATH  "roms/video/xga/XGA_37F9576_Ver200.BIN"
#define XGA2_BIOS_PATH "roms/video/xga/xga2_v300.bin"
#define INMOS_XGA_BIOS_PATH "roms/video/xga/InMOS XGA - Fairchild NM27C256Q-150.BIN"

static video_timings_t timing_xga_isa = { .type = VIDEO_ISA, .write_b = 3, .write_w = 3, .write_l = 6, .read_b = 5, .read_w = 5, .read_l = 10 };
static video_timings_t timing_xga_mca = { .type = VIDEO_MCA, .write_b = 4, .write_w = 5, .write_l = 10, .read_b = 5, .read_w = 5, .read_l = 10 };

static void    xga_ext_outb(uint16_t addr, uint8_t val, void *p);
static uint8_t xga_ext_inb(uint16_t addr, void *p);

static void xga_writew(uint32_t addr, uint16_t val, void *p);
static uint16_t xga_readw(uint32_t addr, void *p);

int xga_has_vga = 0;

void
svga_xga_out(uint16_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *)p;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                return;
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
            if (old != val) {
                if (svga->crtcreg < 0xe || svga->crtcreg > 0x10) {
                    if ((svga->crtcreg == 0xc) || (svga->crtcreg == 0xd)) {
                        svga->fullchange = 3;
                        svga->ma_latch   = ((svga->crtc[0xc] << 8) | svga->crtc[0xd]) + ((svga->crtc[8] & 0x60) >> 5);
                    } else {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
            }
            break;
    }
    svga_out(addr, val, svga);
}

uint8_t
svga_xga_in(uint16_t addr, void *p)
{
    svga_t *svga = (svga_t *)p;
    uint8_t temp;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    switch (addr) {
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        case 0x3D5:
            if (svga->crtcreg & 0x20)
                temp = 0xff;
            else
                temp = svga->crtc[svga->crtcreg];
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }
    return temp;
}

void
xga_updatemapping(svga_t *svga)
{
    xga_t *xga = &svga->xga;

    //pclog("OpMode = %x, linear base = %08x, aperture cntl = %d, access mode = %x, map = %x, endian reverse = %d, a5test = %d, XGA on = %d.\n", xga->op_mode, xga->linear_base, xga->aperture_cntl, xga->access_mode, svga->gdcreg[6] & 0x0c, xga->linear_endian_reverse, xga->a5_test, xga->on);
    if (((xga->op_mode & 7) >= 4) || ((xga->op_mode & 7) == 0)) {
        if ((xga->aperture_cntl == 1) || (xga->aperture_cntl == 2)) {
            mem_mapping_disable(&svga->mapping);
            if (xga->aperture_cntl == 1)
                mem_mapping_set_addr(&xga->video_mapping, 0xa0000, 0x10000);
            else
                mem_mapping_set_addr(&xga->video_mapping, 0xb0000, 0x10000);

            mem_mapping_enable(&xga->video_mapping);
            xga->banked_mask = 0xffff;
            if (!xga->linear_endian_reverse)
                mem_mapping_disable(&xga->linear_mapping);
        } else if (xga->aperture_cntl == 0) {
            mem_mapping_disable(&svga->mapping);
            mem_mapping_set_addr(&xga->video_mapping, 0xa0000, 0x10000);
            mem_mapping_enable(&xga->video_mapping);
            xga->banked_mask = 0xffff;
            if (xga->base_addr_1mb)
                mem_mapping_set_addr(&xga->linear_mapping, xga->base_addr_1mb, 0x100000);
            else
                mem_mapping_set_addr(&xga->linear_mapping, xga->linear_base, 0x400000);
            if (((xga->op_mode & 7) == 4) && ((svga->gdcreg[6] & 0x0c) == 0x0c) && !xga->a5_test && xga->on)
                xga->linear_endian_reverse = 1;
            else if (((xga->op_mode & 7) == 0) && ((svga->gdcreg[6] & 0x0c) == 0x0c) && !xga->a5_test && !xga->on) {
                xga->linear_endian_reverse = 1;
            }
            if (xga->a5_test && (xga->access_mode & 8) && !xga->linear_endian_reverse) {
                xga->on = 0;
                vga_on  = !xga->on;
            }
        }
        //pclog("XGA opmode (extended) = %d, disp mode = %d, aperture = %d.\n", xga->op_mode & 7, xga->disp_cntl_2 & 7, xga->aperture_cntl);
    }
    //pclog("VGA on = %d.\n", vga_on);
}

void
xga_recalctimings(svga_t *svga)
{
    xga_t *xga = &svga->xga;

    if (xga->on) {
        xga->v_total      = xga->vtotal + 1;
        xga->dispend      = xga->vdispend + 1;
        xga->v_syncstart  = xga->vsyncstart + 1;
        xga->split        = xga->linecmp + 1;
        xga->v_blankstart = xga->vblankstart + 1;

        xga->h_disp = (xga->hdisp + 1) << 3;

        xga->rowoffset = (xga->hdisp + 1);

        xga->interlace = !!(xga->disp_cntl_1 & 0x08);
        xga->rowcount  = (xga->disp_cntl_2 & 0xc0) >> 6;

        if (xga->interlace) {
            xga->v_total >>= 1;
            xga->dispend >>= 1;
            xga->v_syncstart >>= 1;
            xga->split >>= 1;
            xga->v_blankstart >>= 1;
        }

        xga->ma_latch = xga->disp_start_addr;

        switch ((xga->clk_sel_1 >> 2) & 3) {
            case 0:
                if (xga->clk_sel_2 & 0x80) {
                    svga->clock = (cpuclock * (double) (1ull << 32)) / 41539000.0;
                } else {
                    svga->clock = (cpuclock * (double) (1ull << 32)) / 25175000.0;
                }
                break;
            case 1:
                svga->clock = (cpuclock * (double) (1ull << 32)) / 28322000.0;
                break;
            case 3:
                svga->clock = (cpuclock * (double) (1ull << 32)) / 44900000.0;
                break;
        }
    }
}

static void
xga_ext_out_reg(xga_t *xga, svga_t *svga, uint8_t idx, uint8_t val)
{
    uint8_t index;

    switch (idx) {
        case 0x10:
            xga->htotal = (xga->htotal & 0xff00) | val;
            break;
        case 0x11:
            xga->htotal = (xga->htotal & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x12:
            xga->hdisp = (xga->hdisp & 0xff00) | val;
            break;
        case 0x13:
            xga->hdisp = (xga->hdisp & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x20:
            xga->vtotal = (xga->vtotal & 0xff00) | val;
            break;
        case 0x21:
            xga->vtotal = (xga->vtotal & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x22:
            xga->vdispend = (xga->vdispend & 0xff00) | val;
            break;
        case 0x23:
            xga->vdispend = (xga->vdispend & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x24:
            xga->vblankstart = (xga->vblankstart & 0xff00) | val;
            break;
        case 0x25:
            xga->vblankstart = (xga->vblankstart & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x28:
            xga->vsyncstart = (xga->vsyncstart & 0xff00) | val;
            break;
        case 0x29:
            xga->vsyncstart = (xga->vsyncstart & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x2c:
            xga->linecmp = (xga->linecmp & 0xff00) | val;
            break;
        case 0x2d:
            xga->linecmp = (xga->linecmp & 0xff) | (val << 8);
            svga_recalctimings(svga);
            break;

        case 0x30:
            xga->hwc_pos_x  = (xga->hwc_pos_x & 0x0700) | val;
            xga->hwcursor.x = xga->hwc_pos_x;
            break;
        case 0x31:
            xga->hwc_pos_x  = (xga->hwc_pos_x & 0xff) | ((val & 0x07) << 8);
            xga->hwcursor.x = xga->hwc_pos_x;
            break;

        case 0x32:
            xga->hwc_hotspot_x = val & 0x3f;
            xga->hwcursor.xoff = val & 0x3f;
            break;

        case 0x33:
            xga->hwc_pos_y  = (xga->hwc_pos_y & 0x0700) | val;
            xga->hwcursor.y = xga->hwc_pos_y;
            break;
        case 0x34:
            xga->hwc_pos_y  = (xga->hwc_pos_y & 0xff) | ((val & 0x07) << 8);
            xga->hwcursor.y = xga->hwc_pos_y;
            break;

        case 0x35:
            xga->hwc_hotspot_y = val & 0x3f;
            xga->hwcursor.yoff = val & 0x3f;
            break;

        case 0x36:
            xga->hwc_control  = val;
            xga->hwcursor.ena = xga->hwc_control & 1;
            break;

        case 0x38:
            xga->hwc_color0 = (xga->hwc_color0 & 0xffff00) | val;
            break;
        case 0x39:
            xga->hwc_color0 = (xga->hwc_color0 & 0xff00ff) | (val << 8);
            break;
        case 0x3a:
            xga->hwc_color0 = (xga->hwc_color0 & 0x00ffff) | (val << 16);
            break;

        case 0x3b:
            xga->hwc_color1 = (xga->hwc_color1 & 0xffff00) | val;
            break;
        case 0x3c:
            xga->hwc_color1 = (xga->hwc_color1 & 0xff00ff) | (val << 8);
            break;
        case 0x3d:
            xga->hwc_color1 = (xga->hwc_color1 & 0x00ffff) | (val << 16);
            break;

        case 0x40:
            xga->disp_start_addr = (xga->disp_start_addr & 0x7ff00) | val;
            break;
        case 0x41:
            xga->disp_start_addr = (xga->disp_start_addr & 0x700ff) | (val << 8);
            break;
        case 0x42:
            xga->disp_start_addr = (xga->disp_start_addr & 0x0ffff) | ((val & 0x07) << 16);
            svga_recalctimings(svga);
            break;

        case 0x43:
            xga->pix_map_width = (xga->pix_map_width & 0x700) | val;
            break;
        case 0x44:
            xga->pix_map_width = (xga->pix_map_width & 0xff) | ((val & 0x07) << 8);
            break;

        case 0x50:
            xga->disp_cntl_1 = val;
            svga_recalctimings(svga);
            break;

        case 0x51:
            xga->disp_cntl_2 = val;
            xga->on = ((val & 7) >= 3);
            vga_on = !xga->on;
            svga_recalctimings(svga);
            break;

        case 0x54:
            xga->clk_sel_1 = val;
            svga_recalctimings(svga);
            break;

        case 0x55:
            xga->border_color = val;
            break;

        case 0x59:
            xga->direct_color = val;
            break;

        case 0x60:
            xga->sprite_pal_addr_idx = (xga->sprite_pal_addr_idx & 0x3f00) | val;
            svga->dac_pos            = 0;
            svga->dac_addr           = val & 0xff;
            break;
        case 0x61:
            xga->sprite_pal_addr_idx = (xga->sprite_pal_addr_idx & 0xff) | ((val & 0x3f) << 8);
            xga->sprite_pos          = xga->sprite_pal_addr_idx & 0x1ff;
            if ((xga->sprite_pos >= 0) && (xga->sprite_pos <= 16)) {
                if ((xga->op_mode & 7) >= 5)
                    xga->cursor_data_on = 1;
                else if ((xga->sprite_pos >= 1) || ((xga->disp_cntl_2 & 7) > 3))
                    xga->cursor_data_on = 1;
                else if (xga->aperture_cntl == 0) {
                    if (xga->linear_endian_reverse && !(xga->access_mode & 8))
                        xga->cursor_data_on = 0;
                }
            }

            if ((xga->sprite_pos > 16) && (xga->sprite_pos <= 0x1ff)) {
                if (xga->aperture_cntl) {
                    if (xga->sprite_pos & 0x0f)
                        xga->cursor_data_on = 1;
                    else
                        xga->cursor_data_on = 0;
                } else {
                    xga->cursor_data_on = 0;
                }
            }
            // pclog("Sprite POS = %d, data on = %d, idx = %d, apcntl = %d\n", xga->sprite_pos, xga->cursor_data_on, xga->sprite_pal_addr_idx, xga->aperture_cntl);
            break;

        case 0x62:
            xga->sprite_pal_addr_idx_prefetch = (xga->sprite_pal_addr_idx_prefetch & 0x3f00) | val;
            svga->dac_pos                     = 0;
            svga->dac_addr                    = val & 0xff;
            break;
        case 0x63:
            xga->sprite_pal_addr_idx_prefetch = (xga->sprite_pal_addr_idx_prefetch & 0xff) | ((val & 0x3f) << 8);
            xga->sprite_pos_prefetch          = xga->sprite_pal_addr_idx_prefetch & 0x1ff;
            break;

        case 0x64:
            svga->dac_mask = val;
            break;

        case 0x65:
            svga->fullchange = svga->monitor->mon_changeframecount;
            switch (svga->dac_pos) {
                case 0:
                    svga->dac_r = val;
                    svga->dac_pos++;
                    break;
                case 1:
                    svga->dac_g = val;
                    svga->dac_pos++;
                    break;
                case 2:
                    xga->pal_b            = val;
                    index                 = svga->dac_addr & 0xff;
                    svga->vgapal[index].r = svga->dac_r;
                    svga->vgapal[index].g = svga->dac_g;
                    svga->vgapal[index].b = xga->pal_b;
                    svga->pallook[index]  = makecol32(svga->vgapal[index].r, svga->vgapal[index].g, svga->vgapal[index].b);
                    svga->dac_pos         = 0;
                    svga->dac_addr        = (svga->dac_addr + 1) & 0xff;
                    break;
            }
            break;

        case 0x66:
            xga->pal_seq = val;
            break;

        case 0x67:
            svga->dac_r = val;
            break;
        case 0x68:
            xga->pal_b = val;
            break;
        case 0x69:
            svga->dac_g = val;
            break;

        case 0x6a:
            xga->sprite_data[xga->sprite_pos] = val;
            xga->sprite_pos                   = (xga->sprite_pos + 1) & 0x3ff;
            break;

        case 0x70:
            xga->clk_sel_2 = val;
            svga_recalctimings(svga);
            break;
    }
}

static void
xga_ext_outb(uint16_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    //pclog("[%04X:%08X]: EXT OUTB = %02x, val = %02x\n", CS, cpu_state.pc, addr, val);
    switch (addr & 0x0f) {
        case 0:
            xga->op_mode = val;
            break;
        case 1:
            xga->aperture_cntl = val;
            xga_updatemapping(svga);
            break;
        case 4:
            xga->access_mode &= ~8;
            if ((xga->disp_cntl_2 & 7) == 4)
                xga->aperture_cntl = 0;
            break;
        case 8:
            xga->ap_idx = val;
            //pclog("Aperture CNTL = %d, val = %02x, up to bit6 = %02x\n", xga->aperture_cntl, val, val & 0x3f);
            if ((xga->op_mode & 7) < 4) {
                xga->write_bank = xga->read_bank = 0;
            } else {
                xga->write_bank = (xga->ap_idx & 0x3f) << 16;
                xga->read_bank  = xga->write_bank;
            }
            break;
        case 9:
            xga->access_mode = val;
            break;
        case 0x0a:
            xga->regs_idx = val;
            break;
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            xga->regs[xga->regs_idx] = val;
            xga_ext_out_reg(xga, svga, xga->regs_idx, xga->regs[xga->regs_idx]);
            break;
    }
}

static uint8_t
xga_ext_inb(uint16_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;
    uint8_t ret;
    uint8_t index;

    switch (addr & 0x0f) {
        case 0:
            ret = xga->op_mode;
            break;
        case 1:
            ret = xga->aperture_cntl;
            break;
        case 8:
            ret = xga->ap_idx;
            break;
        case 9:
            ret = xga->access_mode;
            break;
        case 0x0a:
            ret = xga->regs_idx;
            break;
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            switch (xga->regs_idx) {
                case 4:
                    if (xga->bus & DEVICE_MCA)
                        ret = 0x01; /*32-bit MCA*/
                    else
                        ret = 0x10; /*16-bit ISA*/
                    break;
                case 0x10:
                    ret = xga->htotal & 0xff;
                    break;
                case 0x11:
                    ret = xga->htotal >> 8;
                    break;
                case 0x12:
                    ret = xga->hdisp & 0xff;
                    break;
                case 0x13:
                    ret = xga->hdisp >> 8;
                    break;
                case 0x20:
                    ret = xga->vtotal & 0xff;
                    break;
                case 0x21:
                    ret = xga->vtotal >> 8;
                    break;
                case 0x22:
                    ret = xga->vdispend & 0xff;
                    break;
                case 0x23:
                    ret = xga->vdispend >> 8;
                    break;
                case 0x24:
                    ret = xga->vblankstart & 0xff;
                    break;
                case 0x25:
                    ret = xga->vblankstart >> 8;
                    break;
                case 0x28:
                    ret = xga->vsyncstart & 0xff;
                    break;
                case 0x29:
                    ret = xga->vsyncstart >> 8;
                    break;
                case 0x2c:
                    ret = xga->linecmp & 0xff;
                    break;
                case 0x2d:
                    ret = xga->linecmp >> 8;
                    break;
                case 0x30:
                    ret = xga->hwc_pos_x & 0xff;
                    break;
                case 0x31:
                    ret = xga->hwc_pos_x >> 8;
                    break;
                case 0x32:
                    ret = xga->hwc_hotspot_x;
                    break;
                case 0x33:
                    ret = xga->hwc_pos_y & 0xff;
                    break;
                case 0x34:
                    ret = xga->hwc_pos_y >> 8;
                    break;
                case 0x35:
                    ret = xga->hwc_hotspot_y;
                    break;
                case 0x36:
                    ret = xga->hwc_control;
                    break;
                case 0x38:
                    ret = xga->hwc_color0 & 0xff;
                    break;
                case 0x39:
                    ret = xga->hwc_color0 >> 8;
                    break;
                case 0x3a:
                    ret = xga->hwc_color0 >> 16;
                    break;
                case 0x3b:
                    ret = xga->hwc_color1 & 0xff;
                    break;
                case 0x3c:
                    ret = xga->hwc_color1 >> 8;
                    break;
                case 0x3d:
                    ret = xga->hwc_color1 >> 16;
                    break;
                case 0x40:
                    ret = xga->disp_start_addr & 0xff;
                    break;
                case 0x41:
                    ret = xga->disp_start_addr >> 8;
                    break;
                case 0x42:
                    ret = xga->disp_start_addr >> 16;
                    break;
                case 0x43:
                    ret = xga->pix_map_width & 0xff;
                    break;
                case 0x44:
                    ret = xga->pix_map_width >> 8;
                    break;
                case 0x50:
                    ret = xga->disp_cntl_1 | 0x20;
                    break;
                case 0x51:
                    ret = xga->disp_cntl_2;
                    break;
                case 0x52:
                    ret = xga->type ? 0xfa : 0xea;
                    break;
                case 0x53:
                    ret = xga->type ? 0x53 : 0x30;
                    break;
                case 0x54:
                    ret = xga->clk_sel_1;
                    break;
                case 0x55:
                    ret = xga->border_color;
                    break;

                case 0x59:
                    ret = xga->direct_color;
                    break;

                case 0x60:
                    ret = xga->sprite_pal_addr_idx & 0xff;
                    break;
                case 0x61:
                    ret = xga->sprite_pal_addr_idx >> 8;
                    break;

                case 0x62:
                    ret = xga->sprite_pal_addr_idx_prefetch & 0xff;
                    break;
                case 0x63:
                    ret = xga->sprite_pal_addr_idx_prefetch >> 8;
                    break;

                case 0x64:
                    ret = svga->dac_mask;
                    break;

                case 0x65:
                    index = svga->dac_addr & 0xff;
                    switch (svga->dac_pos) {
                        case 0:
                            svga->dac_pos++;
                            ret = svga->vgapal[index].r;
                            break;
                        case 1:
                            svga->dac_pos++;
                            ret = svga->vgapal[index].g;
                            break;
                        case 2:
                            svga->dac_pos  = 0;
                            svga->dac_addr = (svga->dac_addr + 1) & 0xff;
                            ret            = svga->vgapal[index].b;
                            break;
                    }
                    break;

                case 0x66:
                    ret = xga->pal_seq;
                    break;

                case 0x67:
                    ret = svga->dac_r;
                    break;
                case 0x68:
                    ret = xga->pal_b;
                    break;
                case 0x69:
                    ret = svga->dac_g;
                    break;

                case 0x6a:
                    // pclog("Sprite POS Read = %d, addr idx = %04x\n", xga->sprite_pos, xga->sprite_pal_addr_idx_prefetch);
                    ret                      = xga->sprite_data[xga->sprite_pos_prefetch];
                    xga->sprite_pos_prefetch = (xga->sprite_pos_prefetch + 1) & 0x3ff;
                    break;

                case 0x70:
                    ret = xga->clk_sel_2;
                    break;

                case 0x74:
                    if (xga->bus & DEVICE_MCA)
                        ret = xga->regs[xga->regs_idx];
                    else {
                        ret = (xga->dma_channel << 1);
                        if (xga->dma_channel)
                            ret |= 1;
                    }
                    break;

                default:
                    ret = xga->regs[xga->regs_idx];
                    break;
            }
            break;
    }

    //pclog("[%04X:%08X]: EXT INB = %02x, ret = %02x\n", CS, cpu_state.pc, addr, ret);
    return ret;
}

#define READ(addr, dat) \
    dat = xga->vram[(addr) & (xga->vram_mask)];

#define WRITE(addr, dat)                                         \
    xga->vram[((addr)) & (xga->vram_mask)]                = dat; \
    xga->changedvram[(((addr)) & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;

#define READW(addr, dat) \
    dat = *(uint16_t *) &xga->vram[(addr) & (xga->vram_mask)];

#define READW_INV(addr, dat) \
    dat = xga->vram[(addr + 1) & (xga->vram_mask)]; \
    dat |= (xga->vram[(addr) & (xga->vram_mask)] << 8);

#define WRITEW(addr, dat)                                        \
    *(uint16_t *) &xga->vram[((addr)) & (xga->vram_mask)] = dat; \
    xga->changedvram[(((addr)) & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;

#define WRITEW_INV(addr, dat)                                        \
    xga->vram[((addr + 1)) & (xga->vram_mask)] = dat & 0xff; \
    xga->vram[((addr)) & (xga->vram_mask)] = dat >> 8; \
    xga->changedvram[(((addr)) & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;

#define ROP(mix, d, s)                                                                 \
    {                                                                                  \
        switch ((mix) ? (xga->accel.frgd_mix & 0x1f) : (xga->accel.bkgd_mix & 0x1f)) { \
            case 0x00:                                                                 \
                d = 0;                                                                 \
                break;                                                                 \
            case 0x01:                                                                 \
                d = s & d;                                                             \
                break;                                                                 \
            case 0x02:                                                                 \
                d = s & ~d;                                                            \
                break;                                                                 \
            case 0x03:                                                                 \
                d = s;                                                                 \
                break;                                                                 \
            case 0x04:                                                                 \
                d = ~s & d;                                                            \
                break;                                                                 \
            case 0x05:                                                                 \
                d = d;                                                                 \
                break;                                                                 \
            case 0x06:                                                                 \
                d = s ^ d;                                                             \
                break;                                                                 \
            case 0x07:                                                                 \
                d = s | d;                                                             \
                break;                                                                 \
            case 0x08:                                                                 \
                d = ~s & ~d;                                                           \
                break;                                                                 \
            case 0x09:                                                                 \
                d = s ^ ~d;                                                            \
                break;                                                                 \
            case 0x0a:                                                                 \
                d = ~d;                                                                \
                break;                                                                 \
            case 0x0b:                                                                 \
                d = s | ~d;                                                            \
                break;                                                                 \
            case 0x0c:                                                                 \
                d = ~s;                                                                \
                break;                                                                 \
            case 0x0d:                                                                 \
                d = ~s | d;                                                            \
                break;                                                                 \
            case 0x0e:                                                                 \
                d = ~s | ~d;                                                           \
                break;                                                                 \
            case 0x0f:                                                                 \
                d = ~0;                                                                \
                break;                                                                 \
            case 0x10:                                                                 \
                d = MAX(s, d);                                                         \
                break;                                                                 \
            case 0x11:                                                                 \
                d = MIN(s, d);                                                         \
                break;                                                                 \
            case 0x12:                                                                 \
                d = MIN(0xff, s + d);                                                  \
                break;                                                                 \
            case 0x13:                                                                 \
                d = MAX(0, d - s);                                                     \
                break;                                                                 \
            case 0x14:                                                                 \
                d = MAX(0, s - d);                                                     \
                break;                                                                 \
            case 0x15:                                                                 \
                d = (s + d) >> 1;                                                      \
                break;                                                                 \
        }                                                                              \
    }

static uint32_t
xga_accel_read_pattern_map_pixel(svga_t *svga, int x, int y, int map, uint32_t base, int width)
{
    xga_t   *xga  = &svga->xga;
    uint32_t addr = base;
    int      bits;
    uint32_t byte;
    uint8_t  px;
    int      skip = 0;

    if (xga->base_addr_1mb) {
        if (addr < xga->base_addr_1mb || (addr > (xga->base_addr_1mb + 0xfffff)))
            skip = 1;
    } else {
        if (addr < xga->linear_base || (addr > (xga->linear_base + 0xfffff)))
            skip = 1;
    }

    addr += (y * (width >> 3));
    addr += (x >> 3);
    if (!skip) {
        READ(addr, byte);
    } else {
        byte = mem_readb_phys(addr);
    }
    if (xga->linear_endian_reverse) {
        bits = 7 - (x & 7);
    } else {
        if ((xga->accel.px_map_format[map] & 8) && !(xga->access_mode & 8))
            bits = (x & 7);
        else {
            bits = 7 - (x & 7);
        }
    }
    px = (byte >> bits) & 1;
    return px;
}

static uint32_t
xga_accel_read_map_pixel(svga_t *svga, int x, int y, int map, uint32_t base, int width, int usesrc)
{
    xga_t   *xga  = &svga->xga;
    uint32_t addr = base;
    int      bits;
    uint32_t byte;
    uint8_t  px;
    int      skip = 0;

    if (xga->base_addr_1mb) {
        if (addr < xga->base_addr_1mb || (addr > (xga->base_addr_1mb + 0xfffff)))
            skip = 1;
    } else {
        if (addr < xga->linear_base || (addr > (xga->linear_base + 0xfffff)))
            skip = 1;
    }

    switch (xga->accel.px_map_format[map] & 7) {
        case 0: /*1-bit*/
            addr += (y * (width >> 3));
            addr += (x >> 3);
            if (!skip) {
                READ(addr, byte);
            } else {
                byte = mem_readb_phys(addr);
            }
            if (xga->linear_endian_reverse) {
                bits = 7 - (x & 7);
            } else {
                if ((xga->accel.px_map_format[map] & 8) && !(xga->access_mode & 8))
                    bits = (x & 7);
                else {
                    bits = 7 - (x & 7);
                }
            }
            px = (byte >> bits) & 1;
            return px;
        case 3: /*8-bit*/
            addr += (y * width);
            addr += x;
            if (!skip) {
                READ(addr, byte);
            } else {
                byte = mem_readb_phys(addr);
            }
            return byte;
        case 4: /*16-bit*/
            addr += (y * (width << 1));
            addr += (x << 1);
            if (xga->linear_endian_reverse) {
                byte = mem_readw_phys(addr);
                if ((xga->access_mode & 7) == 4)
                    byte = ((byte & 0xff00) >> 8) | ((byte & 0x00ff) << 8);
                else if (xga->access_mode & 8)
                    byte = ((byte & 0xff00) >> 8) | ((byte & 0x00ff) << 8);
            } else {
                if (!skip) {
                    READW(addr, byte);
                } else {
                    byte = mem_readb_phys(addr) | (mem_readb_phys(addr + 1) << 8);
                }
            }
            return byte;
    }
    return 0;
}

static void
xga_accel_write_map_pixel(svga_t *svga, int x, int y, int map, uint32_t base, uint32_t pixel, int width)
{
    xga_t   *xga  = &svga->xga;
    uint32_t addr = base;
    uint8_t  byte;
    uint8_t  mask;
    int      skip = 0;

    if (xga->base_addr_1mb) {
        if (addr < xga->base_addr_1mb || (addr > (xga->base_addr_1mb + 0xfffff)))
            skip = 1;
    } else {
        if (addr < xga->linear_base || (addr > (xga->linear_base + 0xfffff)))
            skip = 1;
    }

    switch (xga->accel.px_map_format[map] & 7) {
        case 0: /*1-bit*/
            addr += (y * width >> 3);
            addr += (x >> 3);
            if (!skip) {
                READ(addr, byte);
            } else {
                byte = mem_readb_phys(addr);
            }
            if (xga->linear_endian_reverse) {
                mask = 1 << (7 - (x & 7));
            } else {
                if ((xga->accel.px_map_format[map] & 8) && !(xga->access_mode & 8)) {
                    mask = 1 << (x & 7);
                } else {
                    mask = 1 << (7 - (x & 7));
                }
            }
            byte = (byte & ~mask) | ((pixel ? 0xff : 0) & mask);
            if (pixel & 1) {
                if (!skip) {
                    xga->vram[addr & (xga->vram_mask)] |= mask;
                    xga->changedvram[(addr & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;
                }
            } else {
                if (!skip) {
                    xga->vram[addr & (xga->vram_mask)] &= ~mask;
                    xga->changedvram[(addr & (xga->vram_mask)) >> 12] = svga->monitor->mon_changeframecount;
                }
            }
            mem_writeb_phys(addr, byte);
            break;
        case 3: /*8-bit*/
            addr += (y * width);
            addr += x;
            if (!skip) {
                WRITE(addr, pixel & 0xff);
            }
            mem_writeb_phys(addr, pixel & 0xff);
            break;
        case 4: /*16-bit*/
            addr += (y * width << 1);
            addr += (x << 1);
            if (xga->linear_endian_reverse) {
                if ((xga->access_mode & 7) == 4)
                    pixel = ((pixel & 0xff00) >> 8) | ((pixel & 0x00ff) << 8);
                else if (xga->access_mode & 8)
                    pixel = ((pixel & 0xff00) >> 8) | ((pixel & 0x00ff) << 8);

                mem_writew_phys(addr, pixel);
            } else {
                if (!skip) {
                    WRITEW(addr, pixel);
                }
                mem_writew_phys(addr, pixel);
            }
            break;
    }
}

static void
xga_short_stroke(svga_t *svga, uint8_t ssv)
{
    xga_t   *xga = &svga->xga;
    uint32_t src_dat;
    uint32_t dest_dat;
    uint32_t old_dest_dat;
    uint32_t color_cmp  = xga->accel.color_cmp;
    uint32_t plane_mask = xga->accel.plane_mask;
    uint32_t dstbase    = xga->accel.px_map_base[xga->accel.dst_map];
    uint32_t srcbase    = xga->accel.px_map_base[xga->accel.src_map];
    int      y          = ssv & 0x0f;
    int      x          = 0;
    int      dx;
    int      dy;
    int      dirx = 0;
    int      diry = 0;

    dx = xga->accel.dst_map_x & 0x1fff;
    if (xga->accel.dst_map_x >= 0x1800)
        dx |= ~0x17ff;

    dy = xga->accel.dst_map_y & 0x1fff;
    if (xga->accel.dst_map_y >= 0x1800)
        dy |= ~0x17ff;

    switch ((ssv >> 5) & 7) {
        case 0:
            dirx = 1;
            diry = 0;
            break;
        case 1:
            dirx = 1;
            diry = -1;
            break;
        case 2:
            dirx = 0;
            diry = -1;
            break;
        case 3:
            dirx = -1;
            diry = -1;
            break;
        case 4:
            dirx = -1;
            diry = 0;
            break;
        case 5:
            dirx = -1;
            diry = 1;
            break;
        case 6:
            dirx = 0;
            diry = 1;
            break;
        case 7:
            dirx = 1;
            diry = 1;
            break;
    }

    if (xga->accel.pat_src == 8) {
        while (y >= 0) {
            if (xga->accel.command & 0xc0) {
                if ((dx >= xga->accel.mask_map_origin_x_off) && (dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dy >= xga->accel.mask_map_origin_y_off) && (dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                    src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1, 1) : xga->accel.frgd_color;
                    dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1, 0);

                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(1, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        if ((xga->accel.command & 0x30) == 0) {
                            if (ssv & 0x10)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        } else if (((xga->accel.command & 0x30) == 0x10) && x) {
                            if (ssv & 0x10)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        } else if (((xga->accel.command & 0x30) == 0x20) && y) {
                            if (ssv & 0x10)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        }
                    }
                }
            } else {
                src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1, 1) : xga->accel.frgd_color;
                dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1, 0);

                if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                    old_dest_dat = dest_dat;
                    ROP(1, dest_dat, src_dat);
                    dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                    if ((xga->accel.command & 0x30) == 0) {
                        if (ssv & 0x10)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    } else if (((xga->accel.command & 0x30) == 0x10) && x) {
                        if (ssv & 0x10)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    } else if (((xga->accel.command & 0x30) == 0x20) && y) {
                        if (ssv & 0x10)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    }
                }
            }

            if (!y) {
                break;
            }

            dx += dirx;
            dy += diry;

            x++;
            y--;
        }
    }

    xga->accel.dst_map_x = dx;
    xga->accel.dst_map_y = dy;
}

#define SWAP(a, b) \
    tmpswap = a;   \
    a       = b;   \
    b       = tmpswap;

static void
xga_line_draw_write(svga_t *svga)
{
    xga_t   *xga = &svga->xga;
    uint32_t src_dat;
    uint32_t dest_dat;
    uint32_t old_dest_dat;
    uint32_t color_cmp  = xga->accel.color_cmp;
    uint32_t plane_mask = xga->accel.plane_mask;
    uint32_t dstbase    = xga->accel.px_map_base[xga->accel.dst_map];
    uint32_t srcbase    = xga->accel.px_map_base[xga->accel.src_map];
    int      dminor;
    int      destxtmp;
    int      dmajor;
    int      err;
    int      tmpswap;
    int      steep = 1;
    int      xdir;
    int      ydir;
    int      y = xga->accel.blt_width;
    int      x = 0;
    int      dx;
    int      dy;

    dminor = ((int16_t) xga->accel.bres_k1);
    if (xga->accel.bres_k1 & 0x2000)
        dminor |= ~0x1fff;
    dminor >>= 1;

    destxtmp = ((int16_t) xga->accel.bres_k2);
    if (xga->accel.bres_k2 & 0x2000)
        destxtmp |= ~0x1fff;

    dmajor = -(destxtmp - (dminor << 1)) >> 1;

    err = ((int16_t) xga->accel.bres_err_term);
    if (xga->accel.bres_err_term & 0x2000)
        destxtmp |= ~0x1fff;

    if (xga->accel.octant & 0x02) {
        ydir = -1;
    } else {
        ydir = 1;
    }

    if (xga->accel.octant & 0x04) {
        xdir = -1;
    } else {
        xdir = 1;
    }

    dx = xga->accel.dst_map_x & 0x1fff;
    if (xga->accel.dst_map_x >= 0x1800)
        dx |= ~0x17ff;

    dy = xga->accel.dst_map_y & 0x1fff;
    if (xga->accel.dst_map_y >= 0x1800)
        dy |= ~0x17ff;

    if (xga->accel.octant & 0x01) {
        steep = 0;
        SWAP(dx, dy);
        SWAP(xdir, ydir);
    }

    if (xga->accel.pat_src == 8) {
        while (y >= 0) {
            if (xga->accel.command & 0xc0) {
                if (steep) {
                    if ((dx >= xga->accel.mask_map_origin_x_off) && (dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dy >= xga->accel.mask_map_origin_y_off) && (dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                        src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1, 1) : xga->accel.frgd_color;
                        dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1, 0);

                        if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                            old_dest_dat = dest_dat;
                            ROP(1, dest_dat, src_dat);
                            dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                            if ((xga->accel.command & 0x30) == 0)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                            else if (((xga->accel.command & 0x30) == 0x10) && x)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                            else if (((xga->accel.command & 0x30) == 0x20) && y)
                                xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        }
                    }
                } else {
                    if ((dy >= xga->accel.mask_map_origin_x_off) && (dy <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (dx >= xga->accel.mask_map_origin_y_off) && (dx <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                        src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1, 1) : xga->accel.frgd_color;
                        dest_dat = xga_accel_read_map_pixel(svga, dy, dx, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1, 0);

                        if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                            old_dest_dat = dest_dat;
                            ROP(1, dest_dat, src_dat);
                            dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                            if ((xga->accel.command & 0x30) == 0)
                                xga_accel_write_map_pixel(svga, dy, dx, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                            else if (((xga->accel.command & 0x30) == 0x10) && x)
                                xga_accel_write_map_pixel(svga, dy, dx, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                            else if (((xga->accel.command & 0x30) == 0x20) && y)
                                xga_accel_write_map_pixel(svga, dy, dx, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        }
                    }
                }
            } else {
                if (steep) {
                    src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1, 1) : xga->accel.frgd_color;
                    dest_dat = xga_accel_read_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1, 0);

                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(1, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        if ((xga->accel.command & 0x30) == 0)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        else if (((xga->accel.command & 0x30) == 0x10) && x)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        else if (((xga->accel.command & 0x30) == 0x20) && y)
                            xga_accel_write_map_pixel(svga, dx, dy, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    }
                } else {
                    src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.src_map_x & 0xfff, xga->accel.src_map_y & 0xfff, xga->accel.src_map, srcbase, xga->accel.px_map_width[xga->accel.src_map] + 1, 1) : xga->accel.frgd_color;
                    dest_dat = xga_accel_read_map_pixel(svga, dy, dx, xga->accel.dst_map, dstbase, xga->accel.px_map_width[xga->accel.dst_map] + 1, 0);

                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(1, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        if ((xga->accel.command & 0x30) == 0)
                            xga_accel_write_map_pixel(svga, dy, dx, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        else if (((xga->accel.command & 0x30) == 0x10) && x)
                            xga_accel_write_map_pixel(svga, dy, dx, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                        else if (((xga->accel.command & 0x30) == 0x20) && y)
                            xga_accel_write_map_pixel(svga, dy, dx, xga->accel.dst_map, dstbase, dest_dat, xga->accel.px_map_width[xga->accel.dst_map] + 1);
                    }
                }
            }

            if (!y) {
                break;
            }

            while (err > 0) {
                dy += ydir;
                err -= (dmajor << 1);
            }

            dx += xdir;
            err += (dminor << 1);

            x++;
            y--;
        }
    }

    if (steep) {
        xga->accel.dst_map_x = dx;
        xga->accel.dst_map_y = dy;
    } else {
        xga->accel.dst_map_x = dy;
        xga->accel.dst_map_y = dx;
    }
}

static void
xga_bitblt(svga_t *svga)
{
    xga_t   *xga = &svga->xga;
    uint32_t src_dat;
    uint32_t dest_dat;
    uint32_t old_dest_dat;
    uint32_t color_cmp  = xga->accel.color_cmp;
    uint32_t plane_mask = xga->accel.plane_mask;
    uint32_t patbase    = xga->accel.px_map_base[xga->accel.pat_src];
    uint32_t dstbase    = xga->accel.px_map_base[xga->accel.dst_map];
    uint32_t srcbase    = xga->accel.px_map_base[xga->accel.src_map];
    uint32_t patwidth   = xga->accel.px_map_width[xga->accel.pat_src];
    uint32_t dstwidth   = xga->accel.px_map_width[xga->accel.dst_map];
    uint32_t srcwidth   = xga->accel.px_map_width[xga->accel.src_map];
    uint32_t patheight  = xga->accel.px_map_height[xga->accel.pat_src];
    uint32_t srcheight  = xga->accel.px_map_height[xga->accel.src_map];
    uint32_t dstheight  = xga->accel.px_map_height[xga->accel.dst_map];
    uint32_t frgdcol    = xga->accel.frgd_color;
    uint32_t bkgdcol    = xga->accel.bkgd_color;
    int      mix        = 0;
    int      xdir;
    int      ydir;

    if (xga->accel.octant & 0x02) {
        ydir = -1;
    } else {
        ydir = 1;
    }

    if (xga->accel.octant & 0x04) {
        xdir = -1;
    } else {
        xdir = 1;
    }

    xga->accel.x = xga->accel.blt_width & 0xfff;
    xga->accel.y = xga->accel.blt_height & 0xfff;

    xga->accel.sx = xga->accel.src_map_x & 0xfff;
    xga->accel.sy = xga->accel.src_map_y & 0xfff;
    xga->accel.px = xga->accel.pat_map_x & 0xfff;
    xga->accel.py = xga->accel.pat_map_y & 0xfff;
    xga->accel.dx = xga->accel.dst_map_x & 0x1fff;
    if (xga->accel.dst_map_x >= 0x1800)
        xga->accel.dx |= ~0x17ff;
    xga->accel.dy = xga->accel.dst_map_y & 0x1fff;
    if (xga->accel.dst_map_y >= 0x1800)
        xga->accel.dy |= ~0x17ff;

    xga->accel.pattern = 0;

    //pclog("XGA bitblt linear endian reverse=%d, access_mode=%x, octanty=%d, src command = %08x, pxsrcmap=%x, pxpatmap=%x, pxdstmap=%x, srcmap=%d, patmap=%d, dstmap=%d, usesrcvramfr=%d, usevrambk=%d.\n",
    //      xga->linear_endian_reverse, xga->access_mode & 0x0f, ydir, xga->accel.command, xga->accel.px_map_format[xga->accel.src_map] & 0x0f, xga->accel.px_map_format[xga->accel.pat_src] & 0x0f, xga->accel.px_map_format[xga->accel.dst_map] & 0x0f, xga->accel.src_map, xga->accel.pat_src, xga->accel.dst_map, ((xga->accel.command >> 28) & 3), ((xga->accel.command >> 30) & 3));

    if (xga->accel.pat_src == 8) {
        if (srcheight == 7)
            xga->accel.pattern = 1;
        else {
            if ((dstwidth == (xga->h_disp - 1)) && (srcwidth == 1)) {
                if ((xga->accel.dst_map == 1) && (xga->accel.src_map == 2) && xga->linear_endian_reverse) {
                    if ((xga->accel.px_map_format[xga->accel.dst_map] >= 0x0b) && (xga->accel.px_map_format[xga->accel.src_map] >= 0x0b)) {
                        xga->accel.pattern = 1;
                    }
                }
            }
        }
        // pclog("Pattern Map = 8: CMD = %08x: SRCBase = %08x, DSTBase = %08x, from/to vram dir = %d, cmd dir = %06x\n", xga->accel.command, srcbase, dstbase, xga->from_to_vram, xga->accel.dir_cmd);
        // pclog("CMD = %08x: Y = %d, X = %d, patsrc = %02x, srcmap = %d, dstmap = %d, py = %d, sy = %d, dy = %d, width0 = %d, width1 = %d, width2 = %d, width3 = %d\n", xga->accel.command, xga->accel.y, xga->accel.x, xga->accel.pat_src, xga->accel.src_map, xga->accel.dst_map, xga->accel.py, xga->accel.sy, xga->accel.dy, xga->accel.px_map_width[0], xga->accel.px_map_width[1], xga->accel.px_map_width[2], xga->accel.px_map_width[3]);
        while (xga->accel.y >= 0) {
            if (xga->accel.command & 0xc0) {
                if ((xga->accel.dx >= xga->accel.mask_map_origin_x_off) && (xga->accel.dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (xga->accel.dy >= xga->accel.mask_map_origin_y_off) && (xga->accel.dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                    src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1, 1) : frgdcol;
                    dest_dat = xga_accel_read_map_pixel(svga, xga->accel.dx, xga->accel.dy, xga->accel.dst_map, dstbase, dstwidth + 1, 0);
                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(1, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        xga_accel_write_map_pixel(svga, xga->accel.dx, xga->accel.dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                    }
                }
            } else {
                src_dat  = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1, 1) : frgdcol;
                dest_dat = xga_accel_read_map_pixel(svga, xga->accel.dx, xga->accel.dy, xga->accel.dst_map, dstbase, dstwidth + 1, 0);
                if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                    old_dest_dat = dest_dat;
                    ROP(1, dest_dat, src_dat);
                    dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                    xga_accel_write_map_pixel(svga, xga->accel.dx, xga->accel.dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                }
            }

            if (xga->accel.pattern)
                xga->accel.sx = ((xga->accel.sx + xdir) & srcwidth) | (xga->accel.sx & ~srcwidth);
            else
                xga->accel.sx += xdir;
            xga->accel.dx += xdir;
            xga->accel.x--;
            if (xga->accel.x < 0) {
                xga->accel.x = (xga->accel.blt_width & 0xfff);

                xga->accel.dx = xga->accel.dst_map_x & 0x1fff;
                if (xga->accel.dst_map_x >= 0x1800)
                    xga->accel.dx |= ~0x17ff;
                xga->accel.sx = xga->accel.src_map_x & 0xfff;

                xga->accel.dy += ydir;
                if (xga->accel.pattern)
                    xga->accel.sy = ((xga->accel.sy + ydir) & srcheight) | (xga->accel.sy & ~srcheight);
                else
                    xga->accel.sy += ydir;

                xga->accel.y--;

                if (xga->accel.y < 0) {
                    xga->accel.dst_map_x = xga->accel.dx;
                    xga->accel.dst_map_y = xga->accel.dy;
                    return;
                }
            }
        }
    } else if (xga->accel.pat_src >= 1) {
        if (patheight == 7)
            xga->accel.pattern = 1;
        else {
            if (dstwidth == (xga->h_disp - 1)) {
                if (srcwidth == (xga->h_disp - 1)) {
                    if ((xga->accel.src_map == 1) && (xga->accel.dst_map == 1) && (xga->accel.pat_src == 2) && xga->linear_endian_reverse) {
                        if ((xga->accel.px_map_format[xga->accel.dst_map] >= 0x0b) && (xga->accel.px <= 7) && (xga->accel.py <= 3)) {
                            xga->accel.pattern = 1;
                        }
                    }
                } else {
                    if (!xga->accel.src_map && (xga->accel.dst_map == 1) && (xga->accel.pat_src == 2) && xga->linear_endian_reverse) {
                        if ((xga->accel.px_map_format[xga->accel.dst_map] >= 0x0b) && (xga->accel.px <= 7) && (xga->accel.py <= 3)) {
                            if ((patwidth >= 7) && ((xga->accel.command & 0xc0) == 0x40))
                                xga->accel.pattern = 0;
                            else
                                xga->accel.pattern = 1;
                        }
                    }
                }
            }
        }

        // pclog("XGA bitblt linear endian reverse=%d, octanty=%d, src command = %08x, pxsrcmap=%x, pxdstmap=%x, srcmap=%d, patmap=%d, dstmap=%d, dstwidth=%d, dstheight=%d, srcwidth=%d, srcheight=%d, dstbase=%08x, srcbase=%08x.\n", xga->linear_endian_reverse, ydir, xga->accel.command, xga->accel.px_map_format[xga->accel.src_map] & 0x0f, xga->accel.px_map_format[xga->accel.dst_map] & 0x0f, xga->accel.src_map, xga->accel.pat_src, xga->accel.dst_map, dstwidth, dstheight, srcwidth, srcheight, dstbase, srcbase);
        // pclog("Pattern Map = %d: CMD = %08x: PATBase = %08x, SRCBase = %08x, DSTBase = %08x\n", xga->accel.pat_src, xga->accel.command, patbase, srcbase, dstbase);
        // pclog("CMD = %08x: Y = %d, X = %d, patsrc = %02x, srcmap = %d, dstmap = %d, py = %d, sy = %d, dy = %d, width0 = %d, width1 = %d, width2 = %d, width3 = %d\n", xga->accel.command, xga->accel.y, xga->accel.x, xga->accel.pat_src, xga->accel.src_map, xga->accel.dst_map, xga->accel.py, xga->accel.sy, xga->accel.dy, xga->accel.px_map_width[0], xga->accel.px_map_width[1], xga->accel.px_map_width[2], xga->accel.px_map_width[3]);
        while (xga->accel.y >= 0) {
            mix = xga_accel_read_pattern_map_pixel(svga, xga->accel.px, xga->accel.py, xga->accel.pat_src, patbase, patwidth + 1);

            if (xga->accel.command & 0xc0) {
                if ((xga->accel.dx >= xga->accel.mask_map_origin_x_off) && (xga->accel.dx <= ((xga->accel.px_map_width[0] & 0xfff) + xga->accel.mask_map_origin_x_off)) && (xga->accel.dy >= xga->accel.mask_map_origin_y_off) && (xga->accel.dy <= ((xga->accel.px_map_height[0] & 0xfff) + xga->accel.mask_map_origin_y_off))) {
                    if (mix) {
                        src_dat = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1, 1) : frgdcol;
                    } else {
                        src_dat = (((xga->accel.command >> 30) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1, 1) : bkgdcol;
                    }
                    dest_dat = xga_accel_read_map_pixel(svga, xga->accel.dx, xga->accel.dy, xga->accel.dst_map, dstbase, dstwidth + 1, 0);
                    if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                        old_dest_dat = dest_dat;
                        ROP(mix, dest_dat, src_dat);
                        dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                        xga_accel_write_map_pixel(svga, xga->accel.dx, xga->accel.dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                    }
                }
            } else {
                if (mix) {
                    src_dat = (((xga->accel.command >> 28) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1, 1) : frgdcol;
                } else {
                    src_dat = (((xga->accel.command >> 30) & 3) == 2) ? xga_accel_read_map_pixel(svga, xga->accel.sx, xga->accel.sy, xga->accel.src_map, srcbase, srcwidth + 1, 1) : bkgdcol;
                }
                dest_dat = xga_accel_read_map_pixel(svga, xga->accel.dx, xga->accel.dy, xga->accel.dst_map, dstbase, dstwidth + 1, 0);
                if ((xga->accel.cc_cond == 4) || ((xga->accel.cc_cond == 1) && (dest_dat > color_cmp)) || ((xga->accel.cc_cond == 2) && (dest_dat == color_cmp)) || ((xga->accel.cc_cond == 3) && (dest_dat < color_cmp)) || ((xga->accel.cc_cond == 5) && (dest_dat >= color_cmp)) || ((xga->accel.cc_cond == 6) && (dest_dat != color_cmp)) || ((xga->accel.cc_cond == 7) && (dest_dat <= color_cmp))) {
                    old_dest_dat = dest_dat;
                    ROP(mix, dest_dat, src_dat);
                    dest_dat = (dest_dat & plane_mask) | (old_dest_dat & ~plane_mask);
                    xga_accel_write_map_pixel(svga, xga->accel.dx, xga->accel.dy, xga->accel.dst_map, dstbase, dest_dat, dstwidth + 1);
                }
            }

            xga->accel.sx += xdir;
            if (xga->accel.pattern)
                xga->accel.px = ((xga->accel.px + xdir) & patwidth) | (xga->accel.px & ~patwidth);
            else
                xga->accel.px += xdir;
            xga->accel.dx += xdir;
            xga->accel.x--;
            if (xga->accel.x < 0) {
                xga->accel.y--;
                xga->accel.x = (xga->accel.blt_width & 0xfff);

                xga->accel.dx = xga->accel.dst_map_x & 0x1fff;
                if (xga->accel.dst_map_x >= 0x1800)
                    xga->accel.dx |= ~0x17ff;
                xga->accel.sx = xga->accel.src_map_x & 0xfff;
                xga->accel.px = xga->accel.pat_map_x & 0xfff;

                xga->accel.sy += ydir;
                if (xga->accel.pattern)
                    xga->accel.py = ((xga->accel.py + ydir) & patheight) | (xga->accel.py & ~patheight);
                else
                    xga->accel.py += ydir;
                xga->accel.dy += ydir;

                if (xga->accel.y < 0) {
                    xga->accel.dst_map_x = xga->accel.dx;
                    xga->accel.dst_map_y = xga->accel.dy;
                    return;
                }
            }
        }
    }
}

static void
xga_mem_write(uint32_t addr, uint32_t val, xga_t *xga, svga_t *svga, int len)
{
    addr &= 0x1fff;

    if (addr >= 0x1800) {
        switch (addr & 0x7f) {
            case 0x11:
                xga->accel.control = val;
                break;

            case 0x12:
                xga->accel.px_map_idx = val & 3;
                break;

            case 0x14:
                if (len == 4)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = val;
                else if (len == 2)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0xffff0000) | val;
                else
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0xffffff00) | val;
                break;
            case 0x15:
                if (len == 1)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0xffff00ff) | (val << 8);
                break;
            case 0x16:
                if (len == 2)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0x0000ffff) | (val << 16);
                else
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0xff00ffff) | (val << 16);
                break;
            case 0x17:
                if (len == 1)
                    xga->accel.px_map_base[xga->accel.px_map_idx] = (xga->accel.px_map_base[xga->accel.px_map_idx] & 0x00ffffff) | (val << 24);
                break;

            case 0x18:
                if (len == 4) {
                    xga->accel.px_map_width[xga->accel.px_map_idx]  = val & 0xffff;
                    xga->accel.px_map_height[xga->accel.px_map_idx] = (val >> 16) & 0xffff;
                } else if (len == 2) {
                    xga->accel.px_map_width[xga->accel.px_map_idx] = val & 0xffff;
                } else
                    xga->accel.px_map_width[xga->accel.px_map_idx] = (xga->accel.px_map_width[xga->accel.px_map_idx] & 0xff00) | val;
                break;
            case 0x19:
                if (len == 1)
                    xga->accel.px_map_width[xga->accel.px_map_idx] = (xga->accel.px_map_width[xga->accel.px_map_idx] & 0xff) | (val << 8);
                break;

            case 0x1a:
                if (len == 2)
                    xga->accel.px_map_height[xga->accel.px_map_idx] = val & 0xffff;
                else
                    xga->accel.px_map_height[xga->accel.px_map_idx] = (xga->accel.px_map_height[xga->accel.px_map_idx] & 0xff00) | val;
                break;
            case 0x1b:
                if (len == 1)
                    xga->accel.px_map_height[xga->accel.px_map_idx] = (xga->accel.px_map_height[xga->accel.px_map_idx] & 0xff) | (val << 8);
                break;

            case 0x1c:
                xga->accel.px_map_format[xga->accel.px_map_idx] = val;
                break;

            case 0x20:
                if (len >= 2) {
                    xga->accel.bres_err_term = val & 0x3fff;
                    if (val & 0x2000)
                        xga->accel.bres_err_term |= ~0x3fff;
                } else
                    xga->accel.bres_err_term = (xga->accel.bres_err_term & 0x3f00) | val;
                break;
            case 0x21:
                if (len == 1) {
                    xga->accel.bres_err_term = (xga->accel.bres_err_term & 0xff) | ((val & 0x3f) << 8);
                    if (val & 0x20)
                        xga->accel.bres_err_term |= ~0x3fff;
                }
                break;

            case 0x24:
                if (len >= 2) {
                    xga->accel.bres_k1 = val & 0x3fff;
                    if (val & 0x2000)
                        xga->accel.bres_k1 |= ~0x3fff;
                } else
                    xga->accel.bres_k1 = (xga->accel.bres_k1 & 0x3f00) | val;
                break;
            case 0x25:
                if (len == 1) {
                    xga->accel.bres_k1 = (xga->accel.bres_k1 & 0xff) | ((val & 0x3f) << 8);
                    if (val & 0x20)
                        xga->accel.bres_k1 |= ~0x3fff;
                }
                break;

            case 0x28:
                if (len >= 2) {
                    xga->accel.bres_k2 = val & 0x3fff;
                    if (val & 0x2000)
                        xga->accel.bres_k2 |= ~0x3fff;
                } else
                    xga->accel.bres_k2 = (xga->accel.bres_k2 & 0x3f00) | val;
                break;
            case 0x29:
                if (len == 1) {
                    xga->accel.bres_k2 = (xga->accel.bres_k2 & 0xff) | ((val & 0x3f) << 8);
                    if (val & 0x20)
                        xga->accel.bres_k2 |= ~0x3fff;
                }
                break;

            case 0x2c:
                if (len == 4) {
                    xga->accel.short_stroke         = val;
                    xga->accel.short_stroke_vector1 = xga->accel.short_stroke & 0xff;
                    xga->accel.short_stroke_vector2 = (xga->accel.short_stroke >> 8) & 0xff;
                    xga->accel.short_stroke_vector3 = (xga->accel.short_stroke >> 16) & 0xff;
                    xga->accel.short_stroke_vector4 = (xga->accel.short_stroke >> 24) & 0xff;

                    // pclog("1Vector = %02x, 2Vector = %02x, 3Vector = %02x, 4Vector = %02x\n", xga->accel.short_stroke_vector1, xga->accel.short_stroke_vector2, xga->accel.short_stroke_vector3, xga->accel.short_stroke_vector4);
                    xga_short_stroke(svga, xga->accel.short_stroke_vector1);
                    xga_short_stroke(svga, xga->accel.short_stroke_vector2);
                    xga_short_stroke(svga, xga->accel.short_stroke_vector3);
                    xga_short_stroke(svga, xga->accel.short_stroke_vector4);
                } else if (len == 2)
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0xffff0000) | val;
                else
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0xffffff00) | val;
                break;
            case 0x2d:
                if (len == 1)
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0xffff00ff) | (val << 8);
                break;
            case 0x2e:
                if (len == 2) {
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0x0000ffff) | (val << 16);
                } else
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0xff00ffff) | (val << 16);
                break;
            case 0x2f:
                if (len == 1) {
                    xga->accel.short_stroke = (xga->accel.short_stroke & 0x00ffffff) | (val << 24);
                }
                break;

            case 0x48:
                xga->accel.frgd_mix = val & 0xff;
                if (len == 4) {
                    xga->accel.bkgd_mix = (val >> 8) & 0xff;
                    xga->accel.cc_cond  = (val >> 16) & 0x07;
                } else if (len == 2) {
                    xga->accel.bkgd_mix = (val >> 8) & 0xff;
                }
                break;

            case 0x49:
                xga->accel.bkgd_mix = val & 0xff;
                break;

            case 0x4a:
                xga->accel.cc_cond = val & 0x07;
                break;

            case 0x4c:
                if (len == 4)
                    xga->accel.color_cmp = val;
                else if (len == 2)
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0xffff0000) | val;
                else
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0xffffff00) | val;
                break;
            case 0x4d:
                if (len == 1)
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0xffff00ff) | (val << 8);
                break;
            case 0x4e:
                if (len == 2)
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0x0000ffff) | (val << 16);
                else
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0xff00ffff) | (val << 16);
                break;
            case 0x4f:
                if (len == 1)
                    xga->accel.color_cmp = (xga->accel.color_cmp & 0x00ffffff) | (val << 24);
                break;

            case 0x50:
                if (len == 4)
                    xga->accel.plane_mask = val;
                else if (len == 2)
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0xffff0000) | val;
                else
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0xffffff00) | val;
                break;
            case 0x51:
                if (len == 1)
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0xffff00ff) | (val << 8);
                break;
            case 0x52:
                if (len == 2)
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0x0000ffff) | (val << 16);
                else
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0xff00ffff) | (val << 16);
                break;
            case 0x53:
                if (len == 1)
                    xga->accel.plane_mask = (xga->accel.plane_mask & 0x00ffffff) | (val << 24);
                break;

            case 0x58:
                if (len == 4)
                    xga->accel.frgd_color = val;
                else if (len == 2)
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0xffff0000) | val;
                else
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0xffffff00) | val;
                break;
            case 0x59:
                if (len == 1)
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0xffff00ff) | (val << 8);
                break;
            case 0x5a:
                if (len == 2)
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0x0000ffff) | (val << 16);
                else
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0xff00ffff) | (val << 16);
                break;
            case 0x5b:
                if (len == 1)
                    xga->accel.frgd_color = (xga->accel.frgd_color & 0x00ffffff) | (val << 24);
                break;

            case 0x5c:
                if (len == 4)
                    xga->accel.bkgd_color = val;
                else if (len == 2)
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0xffff0000) | val;
                else
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0xffffff00) | val;
                break;
            case 0x5d:
                if (len == 1)
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0xffff00ff) | (val << 8);
                break;
            case 0x5e:
                if (len == 2)
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0x0000ffff) | (val << 16);
                else
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0xff00ffff) | (val << 16);
                break;
            case 0x5f:
                if (len == 1)
                    xga->accel.bkgd_color = (xga->accel.bkgd_color & 0x00ffffff) | (val << 24);
                break;

            case 0x60:
                if (len == 4) {
                    xga->accel.blt_width  = val & 0xffff;
                    xga->accel.blt_height = (val >> 16) & 0xffff;
                } else if (len == 2) {
                    xga->accel.blt_width = val;
                } else
                    xga->accel.blt_width = (xga->accel.blt_width & 0xff00) | val;
                break;
            case 0x61:
                if (len == 1)
                    xga->accel.blt_width = (xga->accel.blt_width & 0xff) | (val << 8);
                break;

            case 0x62:
                if (len == 2)
                    xga->accel.blt_height = val;
                else
                    xga->accel.blt_height = (xga->accel.blt_height & 0xff00) | val;
                break;
            case 0x63:
                if (len == 1)
                    xga->accel.blt_height = (xga->accel.blt_height & 0xff) | (val << 8);
                break;

            case 0x6c:
                if (len == 4) {
                    xga->accel.mask_map_origin_x_off = val & 0xffff;
                    xga->accel.mask_map_origin_y_off = (val >> 16) & 0xffff;
                } else if (len == 2) {
                    xga->accel.mask_map_origin_x_off = val;
                } else
                    xga->accel.mask_map_origin_x_off = (xga->accel.mask_map_origin_x_off & 0xff00) | val;
                break;
            case 0x6d:
                if (len == 1)
                    xga->accel.mask_map_origin_x_off = (xga->accel.mask_map_origin_x_off & 0xff) | (val << 8);
                break;

            case 0x6e:
                if (len == 2)
                    xga->accel.mask_map_origin_y_off = val;
                else
                    xga->accel.mask_map_origin_y_off = (xga->accel.mask_map_origin_y_off & 0xff00) | val;
                break;
            case 0x6f:
                if (len == 1)
                    xga->accel.mask_map_origin_y_off = (xga->accel.mask_map_origin_y_off & 0xff) | (val << 8);
                break;

            case 0x70:
                if (len == 4) {
                    xga->accel.src_map_x = val & 0xffff;
                    xga->accel.src_map_y = (val >> 16) & 0xffff;
                } else if (len == 2)
                    xga->accel.src_map_x = val;
                else
                    xga->accel.src_map_x = (xga->accel.src_map_x & 0xff00) | val;
                break;
            case 0x71:
                if (len == 1)
                    xga->accel.src_map_x = (xga->accel.src_map_x & 0xff) | (val << 8);
                break;

            case 0x72:
                if (len == 2)
                    xga->accel.src_map_y = val;
                else
                    xga->accel.src_map_y = (xga->accel.src_map_y & 0xff00) | val;
                break;
            case 0x73:
                if (len == 1)
                    xga->accel.src_map_y = (xga->accel.src_map_y & 0xff) | (val << 8);
                break;

            case 0x74:
                if (len == 4) {
                    xga->accel.pat_map_x = val & 0xffff;
                    xga->accel.pat_map_y = (val >> 16) & 0xffff;
                } else if (len == 2)
                    xga->accel.pat_map_x = val;
                else
                    xga->accel.pat_map_x = (xga->accel.pat_map_x & 0xff00) | val;
                break;
            case 0x75:
                if (len == 1)
                    xga->accel.pat_map_x = (xga->accel.pat_map_x & 0xff) | (val << 8);
                break;

            case 0x76:
                if (len == 2)
                    xga->accel.pat_map_y = val;
                else
                    xga->accel.pat_map_y = (xga->accel.pat_map_y & 0xff00) | val;
                break;
            case 0x77:
                if (len == 1)
                    xga->accel.pat_map_y = (xga->accel.pat_map_y & 0xff) | (val << 8);
                break;

            case 0x78:
                if (len == 4) {
                    xga->accel.dst_map_x = val & 0xffff;
                    xga->accel.dst_map_y = (val >> 16) & 0xffff;
                } else if (len == 2)
                    xga->accel.dst_map_x = val;
                else
                    xga->accel.dst_map_x = (xga->accel.dst_map_x & 0xff00) | val;
                break;
            case 0x79:
                if (len == 1)
                    xga->accel.dst_map_x = (xga->accel.dst_map_x & 0xff) | (val << 8);
                break;

            case 0x7a:
                if (len == 2)
                    xga->accel.dst_map_y = val;
                else
                    xga->accel.dst_map_y = (xga->accel.dst_map_y & 0xff00) | val;
                break;
            case 0x7b:
                if (len == 1)
                    xga->accel.dst_map_y = (xga->accel.dst_map_y & 0xff) | (val << 8);
                break;

            case 0x7c:
                if (len == 4) {
                    xga->accel.command = val;
exec_command:
                    xga->accel.octant    = xga->accel.command & 0x07;
                    xga->accel.draw_mode = xga->accel.command & 0x30;
                    xga->accel.pat_src   = ((xga->accel.command >> 12) & 0x0f);
                    xga->accel.dst_map   = ((xga->accel.command >> 16) & 0x0f);
                    xga->accel.src_map   = ((xga->accel.command >> 20) & 0x0f);

                    // if (xga->accel.pat_src) {
                    //     pclog("[%04X:%08X]: Accel Command = %02x, full = %08x, patwidth = %d, dstwidth = %d, srcwidth = %d, patheight = %d, dstheight = %d, srcheight = %d, px = %d, py = %d, dx = %d, dy = %d, sx = %d, sy = %d, patsrc = %d, dstmap = %d, srcmap = %d, dstbase = %08x, srcbase = %08x, patbase = %08x, dstformat = %x, srcformat = %x, planemask = %08x\n",
                    //       CS, cpu_state.pc, ((xga->accel.command >> 24) & 0x0f), xga->accel.command, xga->accel.px_map_width[xga->accel.pat_src],
                    //       xga->accel.px_map_width[xga->accel.dst_map], xga->accel.px_map_width[xga->accel.src_map],
                    //       xga->accel.px_map_height[xga->accel.pat_src], xga->accel.px_map_height[xga->accel.dst_map],
                    //       xga->accel.px_map_height[xga->accel.src_map],
                    //       xga->accel.pat_map_x, xga->accel.pat_map_y,
                    //       xga->accel.dst_map_x, xga->accel.dst_map_y,
                    //       xga->accel.src_map_x, xga->accel.src_map_y,
                    //       xga->accel.pat_src, xga->accel.dst_map, xga->accel.src_map,
                    //       xga->accel.px_map_base[xga->accel.dst_map], xga->accel.px_map_base[xga->accel.src_map], xga->accel.px_map_base[xga->accel.pat_src],
                    //       xga->accel.px_map_format[xga->accel.dst_map] & 0x0f, xga->accel.px_map_format[xga->accel.src_map] & 0x0f, xga->accel.plane_mask);
                    //     //pclog("\n");
                    // }
                    switch ((xga->accel.command >> 24) & 0x0f) {
                        case 3: /*Bresenham Line Draw Read*/
                            //pclog("Line Draw Read\n");
                            break;
                        case 4: /*Short Stroke Vectors*/
                            //pclog("Short Stroke Vectors.\n");
                            break;
                        case 5: /*Bresenham Line Draw Write*/
                            //pclog("Line Draw Write.\n");
                            xga_line_draw_write(svga);
                            break;
                        case 8: /*BitBLT*/
                            xga_bitblt(svga);
                            break;
                        case 9: /*Inverting BitBLT*/
                            //pclog("Inverting BitBLT\n");
                            break;
                        case 0x0a: /*Area Fill*/
                            //pclog("Area Fill.\n");
                            break;
                    }
                } else if (len == 2) {
                    xga->accel.command = (xga->accel.command & 0xffff0000) | val;
                } else
                    xga->accel.command = (xga->accel.command & 0xffffff00) | val;
                break;
            case 0x7d:
                if (len == 1)
                    xga->accel.command = (xga->accel.command & 0xffff00ff) | (val << 8);
                break;
            case 0x7e:
                if (len == 2) {
                    xga->accel.command = (xga->accel.command & 0x0000ffff) | (val << 16);
                    goto exec_command;
                } else
                    xga->accel.command = (xga->accel.command & 0xff00ffff) | (val << 16);
                break;
            case 0x7f:
                if (len == 1) {
                    xga->accel.command = (xga->accel.command & 0x00ffffff) | (val << 24);
                    goto exec_command;
                }
                break;
        }
    }
}

static void
xga_memio_writeb(uint32_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    xga_mem_write(addr, val, xga, svga, 1);
    // pclog("Write MEMIOB = %04x, val = %02x\n", addr & 0x7f, val);
}

static void
xga_memio_writew(uint32_t addr, uint16_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    xga_mem_write(addr, val, xga, svga, 2);
    // pclog("Write MEMIOW = %04x, val = %04x\n", addr & 0x7f, val);
}

static void
xga_memio_writel(uint32_t addr, uint32_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    xga_mem_write(addr, val, xga, svga, 4);
    // pclog("Write MEMIOL = %04x, val = %08x\n", addr & 0x7f, val);
}

static uint8_t
xga_mem_read(uint32_t addr, xga_t *xga, svga_t *svga)
{
    uint8_t temp = 0;

    addr &= 0x1fff;
    if (addr < 0x1800) {
        if (!xga_has_vga)
            temp = xga->bios_rom.rom[addr];
        else
            temp = xga->vga_bios_rom.rom[addr];
    } else {
        switch (addr & 0x7f) {
            case 0x11:
                temp = xga->accel.control;
                if (xga->accel.control & 0x08)
                    temp |= 0x10;
                else
                    temp &= ~0x10;
                break;

            case 0x20:
                temp = xga->accel.bres_err_term & 0xff;
                break;
            case 0x21:
                temp = xga->accel.bres_err_term >> 8;
                break;
            case 0x22:
                temp = xga->accel.bres_err_term >> 16;
                break;
            case 0x23:
                temp = xga->accel.bres_err_term >> 24;
                break;

            case 0x70:
                temp = xga->accel.src_map_x & 0xff;
                break;
            case 0x71:
                temp = xga->accel.src_map_x >> 8;
                break;

            case 0x72:
                temp = xga->accel.src_map_y & 0xff;
                break;
            case 0x73:
                temp = xga->accel.src_map_y >> 8;
                break;

            case 0x74:
                temp = xga->accel.pat_map_x & 0xff;
                break;
            case 0x75:
                temp = xga->accel.pat_map_x >> 8;
                break;

            case 0x76:
                temp = xga->accel.pat_map_y & 0xff;
                break;
            case 0x77:
                temp = xga->accel.pat_map_y >> 8;
                break;

            case 0x78:
                temp = xga->accel.dst_map_x & 0xff;
                break;
            case 0x79:
                temp = xga->accel.dst_map_x >> 8;
                break;

            case 0x7a:
                temp = xga->accel.dst_map_y & 0xff;
                break;
            case 0x7b:
                temp = xga->accel.dst_map_y >> 8;
                break;
        }
    }

    return temp;
}

static uint8_t
xga_memio_readb(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;
    uint8_t temp;

    temp = xga_mem_read(addr, xga, svga);

    // pclog("[%04X:%08X]: Read MEMIOB = %04x, temp = %02x\n", CS, cpu_state.pc, addr, temp);
    return temp;
}

static uint16_t
xga_memio_readw(uint32_t addr, void *p)
{
    svga_t  *svga = (svga_t *) p;
    xga_t   *xga  = &svga->xga;
    uint16_t temp;

    temp = xga_mem_read(addr, xga, svga);
    temp |= (xga_mem_read(addr + 1, xga, svga) << 8);

    // pclog("[%04X:%08X]: Read MEMIOW = %04x, temp = %04x\n", CS, cpu_state.pc, addr, temp);
    return temp;
}

static uint32_t
xga_memio_readl(uint32_t addr, void *p)
{
    svga_t  *svga = (svga_t *) p;
    xga_t   *xga  = &svga->xga;
    uint32_t temp;

    temp = xga_mem_read(addr, xga, svga);
    temp |= (xga_mem_read(addr + 1, xga, svga) << 8);
    temp |= (xga_mem_read(addr + 2, xga, svga) << 16);
    temp |= (xga_mem_read(addr + 3, xga, svga) << 24);

    // pclog("Read MEMIOL = %04x, temp = %08x\n", addr, temp);
    return temp;
}

static void
xga_hwcursor_draw(svga_t *svga, int displine)
{
    xga_t    *xga    = &svga->xga;
    uint8_t   dat    = 0;
    int       offset = xga->hwcursor_latch.x - xga->hwcursor_latch.xoff;
    int       x_pos;
    int       y_pos;
    int       comb = 0;
    uint32_t *p;
    int       idx = (xga->cursor_data_on) ? 32 : 0;

    if (xga->interlace && xga->hwcursor_oddeven)
        xga->hwcursor_latch.addr += 16;

    y_pos = displine;
    x_pos = offset + svga->x_add;
    p     = buffer32->line[y_pos];

    for (int x = 0; x < xga->hwcursor_latch.cur_xsize; x++) {
        if (x >= idx) {
            if (!(x & 0x03))
                dat = xga->sprite_data[xga->hwcursor_latch.addr & 0x3ff];

            comb = (dat >> ((x & 0x03) << 1)) & 0x03;

            x_pos = offset + svga->x_add + x;

            switch (comb) {
                case 0x00:
                    /* Cursor Color 1 */
                    p[x_pos] = xga->hwc_color0;
                    break;
                case 0x01:
                    /* Cursor Color 2 */
                    p[x_pos] = xga->hwc_color1;
                    break;
                case 0x03:
                    /* Complement */
                    p[x_pos] ^= 0xffffff;
                    break;
            }
        }

        if ((x & 0x03) == 0x03)
            xga->hwcursor_latch.addr++;
    }

    if (xga->interlace && !xga->hwcursor_oddeven)
        xga->hwcursor_latch.addr += 16;
}

static void
xga_render_overscan_left(xga_t *xga, svga_t *svga)
{
    if ((xga->displine + svga->y_add) < 0)
        return;

    if (svga->scrblank || (xga->h_disp == 0))
        return;

    uint32_t *line_ptr = svga->monitor->target_buffer->line[xga->displine + svga->y_add];
    for (int i = 0; i < svga->x_add; i++)
        *line_ptr++ = svga->overscan_color;
}

static void
xga_render_overscan_right(xga_t *xga, svga_t *svga)
{
    int right;

    if ((xga->displine + svga->y_add) < 0)
        return;

    if (svga->scrblank || (xga->h_disp == 0))
        return;

    uint32_t *line_ptr = &svga->monitor->target_buffer->line[xga->displine + svga->y_add][svga->x_add + xga->h_disp];
    right              = (overscan_x >> 1);
    for (int i = 0; i < right; i++)
        *line_ptr++ = svga->overscan_color;
}

static void
xga_render_8bpp(xga_t *xga, svga_t *svga)
{
    uint32_t *p;
    uint32_t  dat;

    if ((xga->displine + svga->y_add) < 0)
        return;

    if (xga->changedvram[xga->ma >> 12] || xga->changedvram[(xga->ma >> 12) + 1] || svga->fullchange) {
        p = &svga->monitor->target_buffer->line[xga->displine + svga->y_add][svga->x_add];

        if (xga->firstline_draw == 2000)
            xga->firstline_draw = xga->displine;
        xga->lastline_draw = xga->displine;

        for (int x = 0; x <= xga->h_disp; x += 8) {
            dat  = *(uint32_t *) (&xga->vram[xga->ma & xga->vram_mask]);
            p[0] = svga->pallook[dat & 0xff];
            p[1] = svga->pallook[(dat >> 8) & 0xff];
            p[2] = svga->pallook[(dat >> 16) & 0xff];
            p[3] = svga->pallook[(dat >> 24) & 0xff];

            dat  = *(uint32_t *) (&xga->vram[(xga->ma + 4) & xga->vram_mask]);
            p[4] = svga->pallook[dat & 0xff];
            p[5] = svga->pallook[(dat >> 8) & 0xff];
            p[6] = svga->pallook[(dat >> 16) & 0xff];
            p[7] = svga->pallook[(dat >> 24) & 0xff];

            xga->ma += 8;
            p += 8;
        }
        xga->ma &= xga->vram_mask;
    }
}

static void
xga_render_16bpp(xga_t *xga, svga_t *svga)
{
    int       x;
    uint32_t *p;
    uint32_t  dat;

    if ((xga->displine + svga->y_add) < 0)
        return;

    if (xga->changedvram[xga->ma >> 12] || xga->changedvram[(xga->ma >> 12) + 1] || svga->fullchange) {
        p = &svga->monitor->target_buffer->line[xga->displine + svga->y_add][svga->x_add];

        if (xga->firstline_draw == 2000)
            xga->firstline_draw = xga->displine;
        xga->lastline_draw = xga->displine;

        for (x = 0; x <= (xga->h_disp); x += 8) {
            dat      = *(uint32_t *) (&xga->vram[(xga->ma + (x << 1)) & xga->vram_mask]);
            p[x]     = video_16to32[dat & 0xffff];
            p[x + 1] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&xga->vram[(xga->ma + (x << 1) + 4) & xga->vram_mask]);
            p[x + 2] = video_16to32[dat & 0xffff];
            p[x + 3] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&xga->vram[(xga->ma + (x << 1) + 8) & xga->vram_mask]);
            p[x + 4] = video_16to32[dat & 0xffff];
            p[x + 5] = video_16to32[dat >> 16];

            dat      = *(uint32_t *) (&xga->vram[(xga->ma + (x << 1) + 12) & xga->vram_mask]);
            p[x + 6] = video_16to32[dat & 0xffff];
            p[x + 7] = video_16to32[dat >> 16];
        }
        xga->ma += x << 1;
        xga->ma &= xga->vram_mask;
    }
}

static void
xga_write(uint32_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    if (!xga->on) {
        svga_write(addr, val, svga);
        return;
    }

    addr &= xga->banked_mask;
    addr += xga->write_bank;

    if (addr >= xga->vram_size)
        return;

    cycles -= video_timing_write_b;

    if (xga->access_mode & 8) {
        if ((xga->access_mode & 7) == 4)
            addr ^= 1;
    }

    xga->changedvram[(addr & xga->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
    xga->vram[addr & xga->vram_mask] = val;
}

static void
xga_writew(uint32_t addr, uint16_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    if (!xga->on) {
        svga_writew(addr, val, svga);
        return;
    }

    xga_write(addr, val & 0xff, svga);
    xga_write(addr + 1, val >> 8, svga);
}

static void
xga_writel(uint32_t addr, uint32_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    if (!xga->on) {
        svga_writel(addr, val, svga);
        return;
    }

    xga_write(addr, val & 0xff, svga);
    xga_write(addr + 1, (val >> 8) & 0xff, svga);
    xga_write(addr + 2, (val >> 16) & 0xff, svga);
    xga_write(addr + 3, (val >> 24) & 0xff, svga);
}


static uint8_t
xga_read(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;
    uint8_t ret = 0xff;

    if (!xga->on)
        return svga_read(addr, svga);

    addr &= xga->banked_mask;
    addr += xga->read_bank;

    if (addr >= xga->vram_size)
        return ret;

    cycles -= video_timing_read_b;

    if (xga->access_mode & 8) {
        if ((xga->access_mode & 7) == 4)
            addr ^= 1;
    }

    ret = xga->vram[addr & xga->vram_mask];

    return ret;
}

static uint16_t
xga_readw(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;
    uint16_t ret = 0xffff;

    if (!xga->on)
        return svga_readw(addr, svga);

    ret = xga_read(addr, svga);
    ret |= (xga_read(addr + 1, svga) << 8);

    return ret;
}

static uint32_t
xga_readl(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;
    uint32_t ret = 0xffffffff;

    if (!xga->on)
        return svga_readl(addr, svga);

    ret = xga_read(addr, svga);
    ret |= (xga_read(addr + 1, svga) << 8);
    ret |= (xga_read(addr + 2, svga) << 16);
    ret |= (xga_read(addr + 3, svga) << 24);

    return ret;
}

static void
xga_write_linear(uint32_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    if (!xga->on) {
        svga_write_linear(addr, val, svga);
        return;
    }

    addr &= svga->decode_mask;

    if (addr >= xga->vram_size)
        return;

    cycles -= video_timing_write_b;

    if (xga->linear_endian_reverse) {
        if ((xga->access_mode & 7) == 4) {
            if ((xga->accel.px_map_format[xga->accel.dst_map] & 7) == 4)
                addr ^= 1;
        } else if (xga->access_mode & 8) {
            if ((xga->accel.px_map_format[xga->accel.dst_map] & 7) == 4)
                addr ^= 1;
        }
    }

    xga->changedvram[(addr & xga->vram_mask) >> 12] = svga->monitor->mon_changeframecount;
    xga->vram[addr & xga->vram_mask]                = val;
}

static void
xga_writew_linear(uint32_t addr, uint16_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    if (!xga->on) {
        svga_writew_linear(addr, val, svga);
        return;
    }

    xga_write_linear(addr, val, p);
    xga_write_linear(addr + 1, val >> 8, p);
}

static void
xga_writel_linear(uint32_t addr, uint32_t val, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    if (!xga->on) {
        svga_writel_linear(addr, val, svga);
        return;
    }

    xga_write_linear(addr, val, p);
    xga_write_linear(addr + 1, val >> 8, p);
    xga_write_linear(addr + 2, val >> 16, p);
    xga_write_linear(addr + 3, val >> 24, p);
}

static uint8_t
xga_read_linear(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;
    uint8_t ret = 0xff;

    if (!xga->on)
        return svga_read_linear(addr, svga);

    addr &= svga->decode_mask;

    if (addr >= xga->vram_size)
        return ret;

    cycles -= video_timing_read_b;

    if (xga->linear_endian_reverse) {
        if ((xga->access_mode & 7) == 4) {
            if ((xga->accel.px_map_format[xga->accel.dst_map] & 7) == 4)
                addr ^= 1;
        } else if (xga->access_mode & 8) {
            if ((xga->accel.px_map_format[xga->accel.dst_map] & 7) == 4)
                addr ^= 1;
        }
    }

    return xga->vram[addr & xga->vram_mask];
}

static uint16_t
xga_readw_linear(uint32_t addr, void *p)
{
    svga_t  *svga = (svga_t *) p;
    xga_t   *xga  = &svga->xga;
    uint16_t ret;

    if (!xga->on)
        return svga_readw_linear(addr, svga);

    ret = xga_read_linear(addr, svga);
    ret |= (xga_read_linear(addr + 1, svga) << 8);

    return ret;
}

static uint32_t
xga_readl_linear(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;
    uint32_t ret;

    if (!xga->on)
        return svga_readl_linear(addr, svga);

    ret = xga_read_linear(addr, svga);
    ret |= (xga_read_linear(addr + 1, svga) << 8);
    ret |= (xga_read_linear(addr + 2, svga) << 16);
    ret |= (xga_read_linear(addr + 3, svga) << 24);

    return ret;
}

static void
xga_do_render(svga_t *svga)
{
    xga_t *xga = &svga->xga;

    switch (xga->disp_cntl_2 & 7) {
        case 3:
            xga_render_8bpp(xga, svga);
            break;
        case 4:
            xga_render_16bpp(xga, svga);
            break;
    }

    svga->x_add = (overscan_x >> 1);
    xga_render_overscan_left(xga, svga);
    xga_render_overscan_right(xga, svga);
    svga->x_add = (overscan_x >> 1);

    if (xga->hwcursor_on) {
        xga_hwcursor_draw(svga, xga->displine + svga->y_add);
        xga->hwcursor_on--;
        if (xga->hwcursor_on && xga->interlace)
            xga->hwcursor_on--;
    }
}

void
xga_poll(xga_t *xga, svga_t *svga)
{
    uint32_t x;
    int      wx;
    int      wy;

    if (!xga->linepos) {
        if (xga->displine == xga->hwcursor_latch.y && xga->hwcursor_latch.ena) {
            xga->hwcursor_on      = xga->hwcursor_latch.cur_ysize - (xga->cursor_data_on ? 32 : 0);
            xga->hwcursor_oddeven = 0;
        }

        if (xga->displine == (xga->hwcursor_latch.y + 1) && xga->hwcursor_latch.ena && xga->interlace) {
            xga->hwcursor_on      = xga->hwcursor_latch.cur_ysize - (xga->cursor_data_on ? 33 : 1);
            xga->hwcursor_oddeven = 1;
        }

        timer_advance_u64(&svga->timer, svga->dispofftime);
        xga->linepos = 1;

        if (xga->dispon) {
            xga->h_disp_on = 1;

            xga->ma &= xga->vram_mask;

            if (xga->firstline == 2000) {
                xga->firstline = xga->displine;
                video_wait_for_buffer();
            }

            if (xga->hwcursor_on) {
                xga->changedvram[xga->ma >> 12] = xga->changedvram[(xga->ma >> 12) + 1] = xga->interlace ? 3 : 2;
            }

            xga_do_render(svga);

            if (xga->lastline < xga->displine)
                xga->lastline = xga->displine;
        }

        xga->displine++;
        if (xga->interlace)
            xga->displine++;
        if (xga->displine > 1500)
            xga->displine = 0;
    } else {
        timer_advance_u64(&svga->timer, svga->dispontime);
        xga->h_disp_on = 0;

        xga->linepos = 0;
        if (xga->dispon) {
            if (xga->sc == xga->rowcount) {
                xga->sc = 0;

                if ((xga->disp_cntl_2 & 7) == 4) {
                    xga->maback += (xga->rowoffset << 4);
                    if (xga->interlace)
                        xga->maback += (xga->rowoffset << 4);
                } else {
                    xga->maback += (xga->rowoffset << 3);
                    if (xga->interlace)
                        xga->maback += (xga->rowoffset << 3);
                }
                xga->maback &= xga->vram_mask;
                xga->ma = xga->maback;
            } else {
                xga->sc++;
                xga->sc &= 0x1f;
                xga->ma = xga->maback;
            }
        }

        xga->vc++;
        xga->vc &= 0x7ff;

        if (xga->vc == xga->split) {
            if (xga->interlace && xga->oddeven)
                xga->ma = xga->maback = (xga->rowoffset << 1);
            else
                xga->ma = xga->maback = 0;
            xga->ma     = (xga->ma << 2);
            xga->maback = (xga->maback << 2);

            xga->sc = 0;
        }
        if (xga->vc == xga->dispend) {
            xga->dispon = 0;

            for (x = 0; x < ((xga->vram_mask + 1) >> 12); x++) {
                if (xga->changedvram[x])
                    xga->changedvram[x]--;
            }
            if (svga->fullchange)
                svga->fullchange--;
        }
        if (xga->vc == xga->v_syncstart) {
            xga->dispon = 0;
            x           = xga->h_disp;

            if (xga->interlace && !xga->oddeven)
                xga->lastline++;
            if (xga->interlace && xga->oddeven)
                xga->firstline--;

            wx = x;

            wy = xga->lastline - xga->firstline;
            svga_doblit(wx, wy, svga);

            xga->firstline = 2000;
            xga->lastline  = 0;

            xga->firstline_draw = 2000;
            xga->lastline_draw  = 0;

            xga->oddeven ^= 1;

            svga->monitor->mon_changeframecount = xga->interlace ? 3 : 2;

            if (xga->interlace && xga->oddeven)
                xga->ma = xga->maback = xga->ma_latch + (xga->rowoffset << 1);
            else
                xga->ma = xga->maback = xga->ma_latch;

            xga->ma     = (xga->ma << 2);
            xga->maback = (xga->maback << 2);
        }
        if (xga->vc == xga->v_total) {
            xga->vc       = 0;
            xga->sc       = 0;
            xga->dispon   = 1;
            xga->displine = (xga->interlace && xga->oddeven) ? 1 : 0;

            svga->x_add = (overscan_x >> 1);

            xga->hwcursor_on    = 0;
            xga->hwcursor_latch = xga->hwcursor;
        }
    }
}

static uint8_t
xga_mca_read(int port, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = &svga->xga;
    uint8_t ret = xga->pos_regs[port & 7];

    if (((port & 7) == 3) && !(ret & 1)) /*Always enable the mapping.*/
        ret |= 1;

    //pclog("[%04X:%08X]: POS Read Port = %x, val = %02x\n", CS, cpu_state.pc, port & 7, xga->pos_regs[port & 7]);
    return ret;
}

static void
xga_mca_write(int port, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = &svga->xga;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    io_removehandler(0x2100 + (xga->instance << 4), 0x0010, xga_ext_inb, NULL, NULL, xga_ext_outb, NULL, NULL, svga);
    mem_mapping_disable(&xga->bios_rom.mapping);
    mem_mapping_disable(&xga->memio_mapping);
    xga->on = 0;
    vga_on  = !xga->on;
    xga->linear_endian_reverse = 0;
    xga->a5_test               = 0;

    /* Save the MCA register value. */
    xga->pos_regs[port & 7] = val;
    if (!(xga->pos_regs[4] & 1) && (mem_size >= 16384)) /*MCA 4MB addressing on systems with more than 16MB of memory*/
        xga->pos_regs[4] |= 1;

    if (xga->pos_regs[2] & 1) {
        xga->instance      = (xga->pos_regs[2] & 0x0e) >> 1;
        xga->base_addr_1mb = (xga->pos_regs[5] & 0x0f) << 20;
        xga->linear_base   = ((xga->pos_regs[4] & 0xfe) * 0x1000000) + (xga->instance << 22);
        xga->rom_addr      = 0xc0000 + (((xga->pos_regs[2] & 0xf0) >> 4) * 0x2000);

        io_sethandler(0x2100 + (xga->instance << 4), 0x0010, xga_ext_inb, NULL, NULL, xga_ext_outb, NULL, NULL, svga);

        if (xga->pos_regs[3] & 1)
            mem_mapping_set_addr(&xga->bios_rom.mapping, xga->rom_addr, 0x2000);
        else
            mem_mapping_set_addr(&xga->memio_mapping, xga->rom_addr + 0x1c00 + (xga->instance * 0x80), 0x80);
    }
    //pclog("[%04X:%08X]: POS Write Port = %x, val = %02x, linear base = %08x, instance = %d, rom addr = %05x\n", CS, cpu_state.pc, port & 7, val, xga->linear_base, xga->instance, xga->rom_addr);
}

static uint8_t
xga_mca_feedb(void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t  *xga  = &svga->xga;

    return xga->pos_regs[2] & 1;
}

static void
xga_mca_reset(void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    xga->on = 0;
    vga_on  = !xga->on;
    xga_mca_write(0x102, 0, svga);
}

static void
xga_reset(void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    mem_mapping_disable(&xga->bios_rom.mapping);
    mem_mapping_disable(&xga->memio_mapping);
    xga->on = 0;
    vga_on  = !xga->on;
    xga->linear_endian_reverse = 0;
    xga->a5_test               = 0;
}

static uint8_t
xga_pos_in(uint16_t addr, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t   *xga  = &svga->xga;
    uint8_t ret = 0xff;

    if (xga_has_vga) {
        switch (addr) {
            case 0x0100:
            case 0x0101:
                if (xga->instance_isa == xga->instance_num)
                    ret = xga->pos_regs[addr & 7];
                else
                    ret = 0xff;
                break;
            case 0x0102:
            case 0x0105:
                ret = xga->pos_regs[addr & 7];
                break;
            case 0x0106:
                ret = xga->pos_idx >> 8;
                break;
            case 0x0107:
                ret = xga->pos_idx & 0xff;
                break;
            case 0x0103:
                if (!(xga->pos_idx & 3)) {
                    ret = xga->pos_regs[3];
                } else
                    ret = 0;
                //pclog("POS IDX for 0103 = %d, ret = %02x.\n", xga->pos_idx & 3, ret);
                break;
            case 0x0104:
                switch (xga->pos_idx & 3) {
                    case 0:
                        ret = xga->pos_regs[4];
                        break;
                    case 1:
                        ret = xga->pos_regs[0];
                        break;
                    case 2:
                        ret = xga->pos_regs[1];
                        break;
                    case 3:
                        ret = 0;
                        break;
                }
                //pclog("POS IDX for 0104 = %d, ret = %02x.\n", xga->pos_idx & 3, ret);
                break;
            case 0x0108:
            case 0x0109:
            case 0x010a:
            case 0x010b:
            case 0x010c:
            case 0x010d:
            case 0x010e:
            case 0x010f:
                xga->instance_num = addr & 7;
                if (xga->instance_isa == xga->instance_num)
                    ret = xga->instance_isa;
                else
                    ret = 0;

                ret |= xga->isa_pos_enable;
                break;
        }
    } else {
        switch (addr) {
            case 0x0100:
            case 0x0101:
                ret = xga->pos_regs[addr & 7];
                break;
            case 0x0103:
                ret = xga->pos_regs[3] | 7;
                ret |= (xga->dma_channel << 3);
                break;
            case 0x0102:
            case 0x0104:
            case 0x0105:
            case 0x0106:
            case 0x0107:
                ret = (xga_mca_read(addr, svga));
                break;
            case 0x0108:
            case 0x0109:
            case 0x010a:
            case 0x010b:
            case 0x010c:
            case 0x010d:
            case 0x010e:
            case 0x010f:
                xga->instance_num = addr & 7;
                if (xga->instance_isa == xga->instance_num)
                    ret = xga->instance_isa;
                else
                    ret = 0;

                ret |= xga->isa_pos_enable;
                break;
        }
    }
    return ret;
}

static void
xga_pos_out(uint16_t addr, uint8_t val, void *priv)
{
    svga_t *svga = (svga_t *) priv;
    xga_t   *xga  = &svga->xga;

    if (xga_has_vga) {
        switch (addr) {
            case 0x0106:
                xga->pos_idx = (xga->pos_idx & 0x00ff) | (val << 8);
                break;
            case 0x0107:
                xga->pos_idx = (xga->pos_idx & 0xff00) | (val);
                //pclog("POS IDX Write = %04x.\n", xga->pos_idx);
                break;
            case 0x0108:
            case 0x0109:
            case 0x010a:
            case 0x010b:
            case 0x010c:
            case 0x010d:
            case 0x010e:
            case 0x010f:
                xga->instance_num = addr & 7;
                xga->isa_pos_enable = val & 0x08;
                break;
        }
    } else {
        switch (addr) {
            case 0x0108:
            case 0x0109:
            case 0x010a:
            case 0x010b:
            case 0x010c:
            case 0x010d:
            case 0x010e:
            case 0x010f:
                xga->instance_num = addr & 7;
                xga->isa_pos_enable = val & 0x08;
                break;
        }
    }
}

static void
    *
    xga_init(const device_t *info)
{
    if (svga_get_pri() == NULL)
        return NULL;

    svga_t  *svga = svga_get_pri();
    xga_t   *xga  = &svga->xga;
    FILE    *f;
    uint32_t temp;
    uint8_t *rom               = NULL;

    xga->ext_mem_addr = device_get_config_hex16("ext_mem_addr");
    xga->instance_isa = device_get_config_int("instance");
    xga->type = device_get_config_int("type");
    xga->dma_channel = device_get_config_int("dma");
    xga->bus  = info->flags;

    xga->vram_size             = (1024 << 10);
    xga->vram_mask             = xga->vram_size - 1;
    xga->vram                  = calloc(xga->vram_size, 1);
    xga->changedvram           = calloc(xga->vram_size >> 12, 1);
    xga->on                    = 0;
    xga->hwcursor.cur_xsize    = 64;
    xga->hwcursor.cur_ysize    = 64;
    xga->bios_rom.sz           = 0x2000;
    xga->linear_endian_reverse = 0;
    xga->a5_test               = 0;

    f = rom_fopen(xga->type ? XGA2_BIOS_PATH : XGA_BIOS_PATH, "rb");
    (void) fseek(f, 0L, SEEK_END);
    temp = ftell(f);
    (void) fseek(f, 0L, SEEK_SET);

    rom = malloc(xga->bios_rom.sz);
    memset(rom, 0xff, xga->bios_rom.sz);
    (void) fread(rom, xga->bios_rom.sz, 1, f);
    temp -= xga->bios_rom.sz;
    (void) fclose(f);

    xga->bios_rom.rom  = rom;
    xga->bios_rom.mask = xga->bios_rom.sz - 1;
    if (f != NULL) {
        free(rom);
    }

    xga->base_addr_1mb = 0;
    if (info->flags & DEVICE_MCA) {
        video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_xga_mca);
        xga->linear_base = 0;
        xga->instance    = 0;
        xga->rom_addr    = 0;
        rom_init(&xga->bios_rom, xga->type ? XGA2_BIOS_PATH : XGA_BIOS_PATH, 0xc0000, 0x2000, 0x1fff, 0, MEM_MAPPING_EXTERNAL);
    } else {
        if (xga_has_vga) {
            rom_init(&xga->vga_bios_rom, INMOS_XGA_BIOS_PATH, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
        } else
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_xga_isa);

        xga->pos_regs[2] = 1 | (xga->instance_isa << 1) | xga->ext_mem_addr;
        xga->instance    = (xga->pos_regs[2] & 0x0e) >> 1;
        xga->pos_regs[4] = 1 | 2;
        xga->linear_base = ((xga->pos_regs[4] & 0xfe) * 0x1000000) + (xga->instance << 22);
        xga->rom_addr = 0xc0000 + (((xga->pos_regs[2] & 0xf0) >> 4) * 0x2000);
    }

    mem_mapping_add(&xga->video_mapping, 0, 0, xga_read, xga_readw, xga_readl,
                    xga_write, xga_writew, xga_writel,
                    NULL, MEM_MAPPING_EXTERNAL, svga);
    mem_mapping_add(&xga->linear_mapping, 0, 0, xga_read_linear, xga_readw_linear, xga_readl_linear,
                    xga_write_linear, xga_writew_linear, xga_writel_linear,
                    NULL, MEM_MAPPING_EXTERNAL, svga);
    mem_mapping_add(&xga->memio_mapping, 0, 0, xga_memio_readb, xga_memio_readw, xga_memio_readl,
                    xga_memio_writeb, xga_memio_writew, xga_memio_writel,
                    xga_has_vga ? xga->vga_bios_rom.rom : xga->bios_rom.rom, MEM_MAPPING_EXTERNAL, svga);

    mem_mapping_disable(&xga->video_mapping);
    mem_mapping_disable(&xga->linear_mapping);
    mem_mapping_disable(&xga->memio_mapping);

    xga->pos_regs[0] = xga->type ? 0xda : 0xdb;
    xga->pos_regs[1] = 0x8f;

    if (xga->bus & DEVICE_MCA) {
        mca_add(xga_mca_read, xga_mca_write, xga_mca_feedb, xga_mca_reset, svga);
    } else {
        io_sethandler(0x0100, 0x0008, xga_pos_in, NULL, NULL, NULL, NULL, NULL, svga);
        if (xga_has_vga)
            io_sethandler(0x0106, 0x0002, NULL, NULL, NULL, xga_pos_out, NULL, NULL, svga);

        io_sethandler(0x2100 + (xga->instance << 4), 0x0010, xga_ext_inb, NULL, NULL, xga_ext_outb, NULL, NULL, svga);
		io_sethandler(0x0108, 0x0008, xga_pos_in, NULL, NULL, xga_pos_out, NULL, NULL, svga);
        mem_mapping_set_addr(&xga->memio_mapping, xga->rom_addr + 0x1c00 + (xga->instance * 0x80), 0x80);
    }
    return svga;
}

static void
    *
    svga_xga_init(const device_t *info)
{
    svga_t *svga = malloc(sizeof(svga_t));
    memset(svga, 0, sizeof(svga_t));

    video_inform(VIDEO_FLAG_TYPE_XGA, &timing_xga_isa);

    svga_init(info, svga, svga, 1 << 18, /*256kB*/
              NULL,
              svga_xga_in, svga_xga_out,
              NULL,
              NULL);

    io_sethandler(0x03c0, 0x0020, svga_xga_in, NULL, NULL, svga_xga_out, NULL, NULL, svga);

    svga->bpp     = 8;
    svga->miscout = 1;
    xga_has_vga = 1;
    xga_enabled = 1;

    return xga_init(info);
}

static void
xga_close(void *p)
{
    svga_t *svga = (svga_t *) p;
    xga_t  *xga  = &svga->xga;

    if (svga) {
        free(xga->vram);
        free(xga->changedvram);
    }
}

static int
xga_available(void)
{
    return rom_present(XGA_BIOS_PATH) && rom_present(XGA2_BIOS_PATH);
}

static int
inmos_xga_available(void)
{
    return rom_present(INMOS_XGA_BIOS_PATH);
}

static void
xga_speed_changed(void *p)
{
    svga_t *svga = (svga_t *) p;

    svga_recalctimings(svga);
}

static void
xga_force_redraw(void *p)
{
    svga_t *svga = (svga_t *) p;

    svga->fullchange = svga->monitor->mon_changeframecount;
}

static const device_config_t xga_mca_configuration[] = {
  // clang-format off
    {
        .name = "type",
        .description = "XGA type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "XGA-1",
                .value = 0
            },
            {
                .description = "XGA-2",
                .value = 1
            },
            { .description = "" }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

static const device_config_t xga_isa_configuration[] = {
  // clang-format off
    {
        .name = "type",
        .description = "XGA type",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "XGA-1",
                .value = 0
            },
            {
                .description = "XGA-2",
                .value = 1
            },
            { .description = "" }
        }
    },
    {
        .name = "instance",
        .description = "Instance",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 6,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "0 (2100h-210Fh)", .value = 0 },
            { .description = "1 (2110h-211Fh)", .value = 1 },
            { .description = "2 (2120h-212Fh)", .value = 2 },
            { .description = "3 (2130h-213Fh)", .value = 3 },
            { .description = "4 (2140h-214Fh)", .value = 4 },
            { .description = "5 (2150h-215Fh)", .value = 5 },
            { .description = "6 (2160h-216Fh)", .value = 6 },
            { .description = "7 (2170h-217Fh)", .value = 7 },
            { .description = ""                      }
        },
    },
    {
        .name = "ext_mem_addr",
        .description = "MMIO address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x00f0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "C800h", .value = 0x0040 },
            { .description = "CA00h", .value = 0x0050 },
            { .description = "CC00h", .value = 0x0060 },
            { .description = "CE00h", .value = 0x0070 },
            { .description = "D000h", .value = 0x0080 },
            { .description = "D200h", .value = 0x0090 },
            { .description = "D400h", .value = 0x00a0 },
            { .description = "D600h", .value = 0x00b0 },
            { .description = "D800h", .value = 0x00c0 },
            { .description = "DA00h", .value = 0x00d0 },
            { .description = "DC00h", .value = 0x00e0 },
            { .description = "DE00h", .value = 0x00f0 },
            { .description = ""                      }
        },
    },
    {
        .name = "dma",
        .description = "DMA channel",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Disabled", .value = 0 },
            { .description = "DMA 6", .value = 6 },
            { .description = "DMA 7", .value = 7 },
            { .description = ""                  }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t xga_device = {
    .name          = "XGA (MCA)",
    .internal_name = "xga_mca",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = xga_init,
    .close         = xga_close,
    .reset         = xga_reset,
    { .available = xga_available },
    .speed_changed = xga_speed_changed,
    .force_redraw  = xga_force_redraw,
    .config        = xga_mca_configuration
};

const device_t xga_isa_device = {
    .name          = "XGA (ISA)",
    .internal_name = "xga_isa",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = xga_init,
    .close         = xga_close,
    .reset         = xga_reset,
    { .available = xga_available },
    .speed_changed = xga_speed_changed,
    .force_redraw  = xga_force_redraw,
    .config        = xga_isa_configuration
};

const device_t inmos_isa_device = {
    .name          = "INMOS XGA (ISA)",
    .internal_name = "inmos_xga_isa",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = svga_xga_init,
    .close         = xga_close,
    .reset         = xga_reset,
    { .available = inmos_xga_available },
    .speed_changed = xga_speed_changed,
    .force_redraw  = xga_force_redraw,
    .config        = xga_isa_configuration
};

void
xga_device_add(void)
{
    if (!xga_enabled || (xga_has_vga && xga_enabled))
        return;

    if (machine_has_bus(machine, MACHINE_BUS_MCA))
        device_add(&xga_device);
    else
        device_add(&xga_isa_device);
}
