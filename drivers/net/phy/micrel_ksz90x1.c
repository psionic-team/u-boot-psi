// SPDX-License-Identifier: GPL-2.0+
/*
 * Micrel PHY drivers
 *
 * Copyright 2010-2011 Freescale Semiconductor, Inc.
 * author Andy Fleming
 * (C) 2012 NetModule AG, David Andrey, added KSZ9031
 * (C) Copyright 2017 Adaptrum, Inc.
 * Written by Alexandru Gagniuc <alex.g@adaptrum.com> for Adaptrum, Inc.
 */
#include <common.h>
#include <dm.h>
#include <env.h>
#include <errno.h>
#include <micrel.h>
#include <phy.h>

/*
 * KSZ9021 - KSZ9031 common
 */

#define MII_KSZ90xx_PHY_CTL		0x1f
#define MIIM_KSZ90xx_PHYCTL_1000	(1 << 6)
#define MIIM_KSZ90xx_PHYCTL_100		(1 << 5)
#define MIIM_KSZ90xx_PHYCTL_10		(1 << 4)
#define MIIM_KSZ90xx_PHYCTL_DUPLEX	(1 << 3)

/* KSZ9021 PHY Registers */
#define MII_KSZ9021_EXTENDED_CTRL	0x0b
#define MII_KSZ9021_EXTENDED_DATAW	0x0c
#define MII_KSZ9021_EXTENDED_DATAR	0x0d

#define CTRL1000_PREFER_MASTER		(1 << 10)
#define CTRL1000_CONFIG_MASTER		(1 << 11)
#define CTRL1000_MANUAL_CONFIG		(1 << 12)

#define KSZ9021_PS_TO_REG		120

/* KSZ9031 PHY Registers */
#define MII_KSZ9031_MMD_ACCES_CTRL	0x0d
#define MII_KSZ9031_MMD_REG_DATA	0x0e

#define KSZ9031_PS_TO_REG		60

static int ksz90xx_startup(struct phy_device *phydev)
{
    /*
     * This section is highly modified from what it was originally.
     *
     * The "phy_extended_write" calls adjust some clock skew values
     * and this is a sensible place to do it.
     *
     * Usually there's code here which waits for autonegotiation to
     * complete, then sets ethernet speed/duplex appropriately.
     *
     * For now, we don't want that, because it delays boot a lot.
     * Additionally, this is a one-time check. So this leads
     * to problems if ethernet is not plugged in when the system
     * turns on, because ethernet init fails and is never fixed.
     *
     * What we do for now is just hardcode the speed to 100 Mbps
     * (our hardware does not support 1Gbps without a mod), and
     * force initialization to happen even when no link has been
     * established (accomplished by commenting out a return in
     * zynqmp_gem.c).
     *
     * Eventually, what we want is to have an ISR handler in Deos
     * which handles this setup whenever the interrupt related
     * to autonegotation completion fires. This includes initializing
     * the clocks at least, and may or may not include setting the
     * registers for the phy/GEM correctly (I'm not sure if they
     * are automatically set by hardware when autoneg is done).
     *
     *
     */
    phydev->speed = SPEED_100;
    phydev->duplex = DUPLEX_FULL;


    // This line disables advertisement of 1 Gbps capability. This shaves
    // ~10-15 seconds off autonegotiation. Obviously must be removed if
    // that is enabled at some point.
    phy_write(phydev, MDIO_DEVAD_NONE, MII_CTRL1000, 0);

	ksz9031_phy_extended_write(phydev, 0x02,
				   MII_KSZ9031_EXT_RGMII_RX_DATA_SKEW,
				   MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x0000);
	/* tx data pad skew - devaddr = 0x02, register = 0x05 */
	ksz9031_phy_extended_write(phydev, 0x02,
				   MII_KSZ9031_EXT_RGMII_TX_DATA_SKEW,
				   MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x0000);
	/* gtx and rx clock pad skew - devaddr = 0x02, register = 0x08 */
	ksz9031_phy_extended_write(phydev, 0x02,
				   MII_KSZ9031_EXT_RGMII_CLOCK_SKEW,
				   MII_KSZ9031_MOD_DATA_NO_POST_INC, 0x03FF);
	return 0;
}

/* Common OF config bits for KSZ9021 and KSZ9031 */
struct ksz90x1_reg_field {
	const char	*name;
	const u8	size;	/* Size of the bitfield, in bits */
	const u8	off;	/* Offset from bit 0 */
	const u8	dflt;	/* Default value */
};

struct ksz90x1_ofcfg {
	const u16			reg;
	const u16			devad;
	const struct ksz90x1_reg_field	*grp;
	const u16			grpsz;
};

static const struct ksz90x1_reg_field ksz90x1_rxd_grp[] = {
	{ "rxd0-skew-ps", 4, 0, 0x7 }, { "rxd1-skew-ps", 4, 4, 0x7 },
	{ "rxd2-skew-ps", 4, 8, 0x7 }, { "rxd3-skew-ps", 4, 12, 0x7 }
};

static const struct ksz90x1_reg_field ksz90x1_txd_grp[] = {
	{ "txd0-skew-ps", 4, 0, 0x7 }, { "txd1-skew-ps", 4, 4, 0x7 },
	{ "txd2-skew-ps", 4, 8, 0x7 }, { "txd3-skew-ps", 4, 12, 0x7 },
};

static const struct ksz90x1_reg_field ksz9021_clk_grp[] = {
	{ "txen-skew-ps", 4, 0, 0x7 }, { "txc-skew-ps", 4, 4, 0x7 },
	{ "rxdv-skew-ps", 4, 8, 0x7 }, { "rxc-skew-ps", 4, 12, 0x7 },
};

static const struct ksz90x1_reg_field ksz9031_ctl_grp[] = {
	{ "txen-skew-ps", 4, 0, 0x7 }, { "rxdv-skew-ps", 4, 4, 0x7 }
};

static const struct ksz90x1_reg_field ksz9031_clk_grp[] = {
	{ "rxc-skew-ps", 5, 0, 0xf }, { "txc-skew-ps", 5, 5, 0xf }
};

static int ksz90x1_of_config_group(struct phy_device *phydev,
				   struct ksz90x1_ofcfg *ofcfg,
				   int ps_to_regval)
{
	struct udevice *dev = phydev->dev;
	struct phy_driver *drv = phydev->drv;
	int val[4];
	int i, changed = 0, offset, max;
	u16 regval = 0;
	ofnode node;

	if (!drv || !drv->writeext)
		return -EOPNOTSUPP;

	node = phydev->node;

	if (!ofnode_valid(node)) {
		/* Look for a PHY node under the Ethernet node */
		node = dev_read_subnode(dev, "ethernet-phy");
	}

	if (!ofnode_valid(node)) {
		/* No node found, look in the Ethernet node */
		node = dev_ofnode(dev);
	}

	for (i = 0; i < ofcfg->grpsz; i++) {
		val[i] = ofnode_read_u32_default(node, ofcfg->grp[i].name, ~0);
		offset = ofcfg->grp[i].off;
		if (val[i] == -1) {
			/* Default register value for KSZ9021 */
			regval |= ofcfg->grp[i].dflt << offset;
		} else {
			changed = 1;	/* Value was changed in OF */
			/* Calculate the register value and fix corner cases */
			max = (1 << ofcfg->grp[i].size) - 1;
			if (val[i] > ps_to_regval * max) {
				regval |= max << offset;
			} else {
				regval |= (val[i] / ps_to_regval) << offset;
			}
		}
	}

	if (!changed)
		return 0;

	return drv->writeext(phydev, 0, ofcfg->devad, ofcfg->reg, regval);
}

static int ksz9021_of_config(struct phy_device *phydev)
{
	struct ksz90x1_ofcfg ofcfg[] = {
		{ MII_KSZ9021_EXT_RGMII_RX_DATA_SKEW, 0, ksz90x1_rxd_grp, 4 },
		{ MII_KSZ9021_EXT_RGMII_TX_DATA_SKEW, 0, ksz90x1_txd_grp, 4 },
		{ MII_KSZ9021_EXT_RGMII_CLOCK_SKEW, 0, ksz9021_clk_grp, 4 },
	};
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(ofcfg); i++) {
		ret = ksz90x1_of_config_group(phydev, &ofcfg[i],
					      KSZ9021_PS_TO_REG);
		if (ret)
			return ret;
	}

	return 0;
}

static int ksz9031_of_config(struct phy_device *phydev)
{
	struct ksz90x1_ofcfg ofcfg[] = {
		{ MII_KSZ9031_EXT_RGMII_CTRL_SIG_SKEW, 2, ksz9031_ctl_grp, 2 },
		{ MII_KSZ9031_EXT_RGMII_RX_DATA_SKEW, 2, ksz90x1_rxd_grp, 4 },
		{ MII_KSZ9031_EXT_RGMII_TX_DATA_SKEW, 2, ksz90x1_txd_grp, 4 },
		{ MII_KSZ9031_EXT_RGMII_CLOCK_SKEW, 2, ksz9031_clk_grp, 2 },
	};
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(ofcfg); i++) {
		ret = ksz90x1_of_config_group(phydev, &ofcfg[i],
					      KSZ9031_PS_TO_REG);
		if (ret)
			return ret;
	}

	return 0;
}

static int ksz9031_center_flp_timing(struct phy_device *phydev)
{
	struct phy_driver *drv = phydev->drv;
	int ret = 0;

	if (!drv || !drv->writeext)
		return -EOPNOTSUPP;

	ret = drv->writeext(phydev, 0, 0, MII_KSZ9031_FLP_BURST_TX_LO, 0x1A80);
	if (ret)
		return ret;

	ret = drv->writeext(phydev, 0, 0, MII_KSZ9031_FLP_BURST_TX_HI, 0x6);
	return ret;
}

/*
 * KSZ9021
 */
int ksz9021_phy_extended_write(struct phy_device *phydev, int regnum, u16 val)
{
	/* extended registers */
	phy_write(phydev, MDIO_DEVAD_NONE,
		  MII_KSZ9021_EXTENDED_CTRL, regnum | 0x8000);
	return phy_write(phydev, MDIO_DEVAD_NONE,
			 MII_KSZ9021_EXTENDED_DATAW, val);
}

int ksz9021_phy_extended_read(struct phy_device *phydev, int regnum)
{
	/* extended registers */
	phy_write(phydev, MDIO_DEVAD_NONE, MII_KSZ9021_EXTENDED_CTRL, regnum);
	return phy_read(phydev, MDIO_DEVAD_NONE, MII_KSZ9021_EXTENDED_DATAR);
}


static int ksz9021_phy_extread(struct phy_device *phydev, int addr, int devaddr,
			       int regnum)
{
	return ksz9021_phy_extended_read(phydev, regnum);
}

static int ksz9021_phy_extwrite(struct phy_device *phydev, int addr,
				int devaddr, int regnum, u16 val)
{
	return ksz9021_phy_extended_write(phydev, regnum, val);
}

static int ksz9021_config(struct phy_device *phydev)
{
	unsigned ctrl1000 = 0;
	const unsigned master = CTRL1000_PREFER_MASTER |
	CTRL1000_CONFIG_MASTER | CTRL1000_MANUAL_CONFIG;
	unsigned features = phydev->drv->features;
	int ret;

	ret = ksz9021_of_config(phydev);
	if (ret)
		return ret;

	if (env_get("disable_giga"))
		features &= ~(SUPPORTED_1000baseT_Half |
		SUPPORTED_1000baseT_Full);
	/* force master mode for 1000BaseT due to chip errata */
	if (features & SUPPORTED_1000baseT_Half)
		ctrl1000 |= ADVERTISE_1000HALF | master;
	if (features & SUPPORTED_1000baseT_Full)
		ctrl1000 |= ADVERTISE_1000FULL | master;
	phydev->advertising = features;
	phydev->supported = features;
	phy_write(phydev, MDIO_DEVAD_NONE, MII_CTRL1000, ctrl1000);
	genphy_config_aneg(phydev);
	genphy_restart_aneg(phydev);
	return 0;
}

U_BOOT_PHY_DRIVER(ksz9021) = {
	.name = "Micrel ksz9021",
	.uid  = 0x221610,
	.mask = 0xfffffe,
	.features = PHY_GBIT_FEATURES,
	.config = &ksz9021_config,
	.startup = &ksz90xx_startup,
	.shutdown = &genphy_shutdown,
	.writeext = &ksz9021_phy_extwrite,
	.readext = &ksz9021_phy_extread,
};

/*
 * KSZ9031
 */
int ksz9031_phy_extended_write(struct phy_device *phydev,
			       int devaddr, int regnum, u16 mode, u16 val)
{
	/*select register addr for mmd*/
	phy_write(phydev, MDIO_DEVAD_NONE,
		  MII_KSZ9031_MMD_ACCES_CTRL, devaddr);
	/*select register for mmd*/
	phy_write(phydev, MDIO_DEVAD_NONE,
		  MII_KSZ9031_MMD_REG_DATA, regnum);
	/*setup mode*/
	phy_write(phydev, MDIO_DEVAD_NONE,
		  MII_KSZ9031_MMD_ACCES_CTRL, (mode | devaddr));
	/*write the value*/
	return	phy_write(phydev, MDIO_DEVAD_NONE,
			  MII_KSZ9031_MMD_REG_DATA, val);
}

int ksz9031_phy_extended_read(struct phy_device *phydev, int devaddr,
			      int regnum, u16 mode)
{
	phy_write(phydev, MDIO_DEVAD_NONE,
		  MII_KSZ9031_MMD_ACCES_CTRL, devaddr);
	phy_write(phydev, MDIO_DEVAD_NONE,
		  MII_KSZ9031_MMD_REG_DATA, regnum);
	phy_write(phydev, MDIO_DEVAD_NONE,
		  MII_KSZ9031_MMD_ACCES_CTRL, (devaddr | mode));
	return phy_read(phydev, MDIO_DEVAD_NONE, MII_KSZ9031_MMD_REG_DATA);
}

static int ksz9031_phy_extread(struct phy_device *phydev, int addr, int devaddr,
			       int regnum)
{
	return ksz9031_phy_extended_read(phydev, devaddr, regnum,
					 MII_KSZ9031_MOD_DATA_NO_POST_INC);
}

static int ksz9031_phy_extwrite(struct phy_device *phydev, int addr,
				int devaddr, int regnum, u16 val)
{
	return ksz9031_phy_extended_write(phydev, devaddr, regnum,
					  MII_KSZ9031_MOD_DATA_POST_INC_RW, val);
}

static int ksz9031_config(struct phy_device *phydev)
{
	int ret;

	ret = ksz9031_of_config(phydev);
	if (ret)
		return ret;
	ret = ksz9031_center_flp_timing(phydev);
	if (ret)
		return ret;

	/* add an option to disable the gigabit feature of this PHY */
	if (env_get("disable_giga")) {
		unsigned features;
		unsigned bmcr;

		/* disable speed 1000 in features supported by the PHY */
		features = phydev->drv->features;
		features &= ~(SUPPORTED_1000baseT_Half |
				SUPPORTED_1000baseT_Full);
		phydev->advertising = phydev->supported = features;

		/* disable speed 1000 in Basic Control Register */
		bmcr = phy_read(phydev, MDIO_DEVAD_NONE, MII_BMCR);
		bmcr &= ~(1 << 6);
		phy_write(phydev, MDIO_DEVAD_NONE, MII_BMCR, bmcr);

		/* disable speed 1000 in 1000Base-T Control Register */
		phy_write(phydev, MDIO_DEVAD_NONE, MII_CTRL1000, 0);

		/* start autoneg */
		genphy_config_aneg(phydev);
		genphy_restart_aneg(phydev);

		return 0;
	}

	return genphy_config(phydev);
}

U_BOOT_PHY_DRIVER(ksz9031) = {
	.name = "Micrel ksz9031",
	.uid  = PHY_ID_KSZ9031,
	.mask = MII_KSZ9x31_SILICON_REV_MASK,
	.features = PHY_GBIT_FEATURES,
	.config   = &ksz9031_config,
	.startup  = &ksz90xx_startup,
	.shutdown = &genphy_shutdown,
	.writeext = &ksz9031_phy_extwrite,
	.readext = &ksz9031_phy_extread,
};

/*
 * KSZ9131
 */

#define KSZ9131RN_MMD_COMMON_CTRL_REG	2
#define KSZ9131RN_RXC_DLL_CTRL		76
#define KSZ9131RN_TXC_DLL_CTRL		77
#define KSZ9131RN_DLL_CTRL_BYPASS	BIT_MASK(12)
#define KSZ9131RN_DLL_ENABLE_DELAY	0
#define KSZ9131RN_DLL_DISABLE_DELAY	BIT(12)

static int ksz9131_config_rgmii_delay(struct phy_device *phydev)
{
	struct phy_driver *drv = phydev->drv;
	u16 rxcdll_val, txcdll_val, val;
	int ret;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rxcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		rxcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		rxcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rxcdll_val = KSZ9131RN_DLL_DISABLE_DELAY;
		txcdll_val = KSZ9131RN_DLL_ENABLE_DELAY;
		break;
	default:
		return 0;
	}

	val = drv->readext(phydev, 0, KSZ9131RN_MMD_COMMON_CTRL_REG,
			   KSZ9131RN_RXC_DLL_CTRL);
	val &= ~KSZ9131RN_DLL_CTRL_BYPASS;
	val |= rxcdll_val;
	ret = drv->writeext(phydev, 0, KSZ9131RN_MMD_COMMON_CTRL_REG,
			    KSZ9131RN_RXC_DLL_CTRL, val);
	if (ret)
		return ret;

	val = drv->readext(phydev, 0, KSZ9131RN_MMD_COMMON_CTRL_REG,
			   KSZ9131RN_TXC_DLL_CTRL);

	val &= ~KSZ9131RN_DLL_CTRL_BYPASS;
	val |= txcdll_val;
	ret = drv->writeext(phydev, 0, KSZ9131RN_MMD_COMMON_CTRL_REG,
			    KSZ9131RN_TXC_DLL_CTRL, val);

	return ret;
}

static int ksz9131_config(struct phy_device *phydev)
{
	int ret;

	if (phy_interface_is_rgmii(phydev)) {
		ret = ksz9131_config_rgmii_delay(phydev);
		if (ret)
			return ret;
	}

	/* add an option to disable the gigabit feature of this PHY */
	if (env_get("disable_giga")) {
		unsigned features;
		unsigned bmcr;

		/* disable speed 1000 in features supported by the PHY */
		features = phydev->drv->features;
		features &= ~(SUPPORTED_1000baseT_Half |
				SUPPORTED_1000baseT_Full);
		phydev->advertising = phydev->supported = features;

		/* disable speed 1000 in Basic Control Register */
		bmcr = phy_read(phydev, MDIO_DEVAD_NONE, MII_BMCR);
		bmcr &= ~(1 << 6);
		phy_write(phydev, MDIO_DEVAD_NONE, MII_BMCR, bmcr);

		/* disable speed 1000 in 1000Base-T Control Register */
		phy_write(phydev, MDIO_DEVAD_NONE, MII_CTRL1000, 0);

		/* start autoneg */
		genphy_config_aneg(phydev);
		genphy_restart_aneg(phydev);

		return 0;
	}

	return genphy_config(phydev);
}

U_BOOT_PHY_DRIVER(ksz9131) = {
	.name = "Micrel ksz9131",
	.uid  = PHY_ID_KSZ9131,
	.mask = MII_KSZ9x31_SILICON_REV_MASK,
	.features = PHY_GBIT_FEATURES,
	.config   = &ksz9131_config,
	.startup  = &ksz90xx_startup,
	.shutdown = &genphy_shutdown,
	.writeext = &ksz9031_phy_extwrite,
	.readext = &ksz9031_phy_extread,
};

int ksz9xx1_phy_get_id(struct phy_device *phydev)
{
	unsigned int phyid;

	get_phy_id(phydev->bus, phydev->addr, MDIO_DEVAD_NONE, &phyid);

	return phyid;
}
