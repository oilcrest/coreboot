/*
 * This file is part of the coreboot project.
 *
 * Copyright 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <device/mmio.h>
#include <console/console.h>
#include <device/mmio.h>
#include <delay.h>
#include <edid.h>
#include <soc/dsi.h>
#include <timer.h>

static void mtk_dsi_phy_timconfig(u32 data_rate)
{
	u32 timcon0, timcon1, timcon2, timcon3;
	u32 cycle_time, ui, lpx;

	ui = 1000 / data_rate + 0x01;
	cycle_time = 8000 / data_rate + 0x01;
	lpx = 5;

	timcon0 = (8 << 24) | (0xa << 16) | (0x6 << 8) | lpx;
	timcon1 = (7 << 24) | (5 * lpx << 16) | ((3 * lpx) / 2) << 8 |
		  (4 * lpx);
	timcon2 = ((DIV_ROUND_UP(0x64, cycle_time) + 0xa) << 24) |
		  (DIV_ROUND_UP(0x150, cycle_time) << 16);
	timcon3 = (2 * lpx) << 16 |
		  DIV_ROUND_UP(80 + 52 * ui, cycle_time) << 8 |
		  DIV_ROUND_UP(0x40, cycle_time);

	write32(&dsi0->dsi_phy_timecon0, timcon0);
	write32(&dsi0->dsi_phy_timecon1, timcon1);
	write32(&dsi0->dsi_phy_timecon2, timcon2);
	write32(&dsi0->dsi_phy_timecon3, timcon3);
}

static void mtk_dsi_clk_hs_mode_enable(void)
{
	setbits_le32(&dsi0->dsi_phy_lccon, LC_HS_TX_EN);
}

static void mtk_dsi_clk_hs_mode_disable(void)
{
	clrbits_le32(&dsi0->dsi_phy_lccon, LC_HS_TX_EN);
}

static void mtk_dsi_set_mode(u32 mode_flags)
{
	u32 tmp_reg1 = 0;

	if (mode_flags & MIPI_DSI_MODE_VIDEO) {
		tmp_reg1 = SYNC_PULSE_MODE;

		if (mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
			tmp_reg1 = BURST_MODE;

		if (mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
			tmp_reg1 = SYNC_PULSE_MODE;
	}

	write32(&dsi0->dsi_mode_ctrl, tmp_reg1);
}

static void mtk_dsi_rxtx_control(u32 mode_flags, u32 lanes)
{
	u32 tmp_reg = 0;

	switch (lanes) {
	case 1:
		tmp_reg = 1 << 2;
		break;
	case 2:
		tmp_reg = 3 << 2;
		break;
	case 3:
		tmp_reg = 7 << 2;
		break;
	case 4:
	default:
		tmp_reg = 0xf << 2;
		break;
	}

	tmp_reg |= (mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS) << 6;
	tmp_reg |= (mode_flags & MIPI_DSI_MODE_EOT_PACKET) >> 3;

	write32(&dsi0->dsi_txrx_ctrl, tmp_reg);
}

static void mtk_dsi_config_vdo_timing(u32 mode_flags, u32 format,
				      const struct edid *edid)
{
	u32 hsync_active_byte;
	u32 hbp_byte;
	u32 hfp_byte;
	u32 vbp_byte;
	u32 vfp_byte;
	u32 bpp;
	u32 packet_fmt;
	u32 hactive;

	if (format == MIPI_DSI_FMT_RGB565)
		bpp = 2;
	else
		bpp = 3;

	vbp_byte = edid->mode.vbl - edid->mode.vso - edid->mode.vspw -
		   edid->mode.vborder;
	vfp_byte = edid->mode.vso - edid->mode.vborder;

	write32(&dsi0->dsi_vsa_nl, edid->mode.vspw);
	write32(&dsi0->dsi_vbp_nl, vbp_byte);
	write32(&dsi0->dsi_vfp_nl, vfp_byte);
	write32(&dsi0->dsi_vact_nl, edid->mode.va);

	if (mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		hbp_byte = (edid->mode.hbl - edid->mode.hso - edid->mode.hspw -
			    edid->mode.hborder) * bpp - 10;
	else
		hbp_byte = (edid->mode.hbl - edid->mode.hso -
			    edid->mode.hborder) * bpp - 10;

	hsync_active_byte = edid->mode.hspw * bpp - 10;
	hfp_byte = (edid->mode.hso - edid->mode.hborder) * bpp - 12;

	write32(&dsi0->dsi_hsa_wc, hsync_active_byte);
	write32(&dsi0->dsi_hbp_wc, hbp_byte);
	write32(&dsi0->dsi_hfp_wc, hfp_byte);

	switch (format) {
	case MIPI_DSI_FMT_RGB888:
		packet_fmt = PACKED_PS_24BIT_RGB888;
		break;
	case MIPI_DSI_FMT_RGB666:
		packet_fmt = LOOSELY_PS_18BIT_RGB666;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		packet_fmt = PACKED_PS_18BIT_RGB666;
		break;
	case MIPI_DSI_FMT_RGB565:
		packet_fmt = PACKED_PS_16BIT_RGB565;
		break;
	default:
		packet_fmt = PACKED_PS_24BIT_RGB888;
		break;
	}

	hactive = edid->mode.ha;
	packet_fmt |= (hactive * bpp) & DSI_PS_WC;

	write32(&dsi0->dsi_psctrl, packet_fmt);
}

static void mtk_dsi_start(void)
{
	write32(&dsi0->dsi_start, 0);
	/* Only start master DSI */
	write32(&dsi0->dsi_start, 1);
}

int mtk_dsi_init(u32 mode_flags, u32 format, u32 lanes, const struct edid *edid)
{
	int data_rate;

	data_rate = mtk_dsi_phy_clk_setting(format, lanes, edid);

	if (data_rate < 0)
		return -1;

	mtk_dsi_reset();
	mtk_dsi_phy_timconfig(data_rate);
	mtk_dsi_rxtx_control(mode_flags, lanes);
	mtk_dsi_clk_hs_mode_disable();
	mtk_dsi_config_vdo_timing(mode_flags, format, edid);
	mtk_dsi_set_mode(mode_flags);
	mtk_dsi_clk_hs_mode_enable();

	mtk_dsi_start();

	return 0;
}
