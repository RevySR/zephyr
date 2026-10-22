/* SPDX-License-Identifier: Apache-2.0 */
/* A210 hardware clock topology. Register provenance: native v7.3 CRG tables. */
static const struct a210_tree_clock clock_descs[] = {
	{A210_FIXED, 0, 0x0, 0, 0, 0, 0, 0, 255, 0},      /* osc-24m */
	{A210_FIXED, 0, 0x0, 0, 0, 0, 0, 0, 255, 0},      /* osc-32k */
	{A210_FACTOR, 0, 0x0, 0, 1, 1, 0, 0, 255, 1},     /* aon_osc_clk_logic */
	{A210_FACTOR, 0, 0x0, 0, 1, 1, 0, 0, 255, 1},     /* top_pll_ref_clk */
	{A210_FACTOR, 0, 0x0, 1, 1, 1, 0, 0, 255, 1},     /* aon_rtc_clk */
	{A210_PLL, 0, 0x0, 0, 0, 0, 0, 0, 255, 0},        /* audio0_pll_foutvco */
	{A210_POSTDIV, 0, 0x0, 5, 0, 0, 0, 0, 255, 1},    /* audio0_pll_foutpostdiv */
	{A210_FACTOR, 0, 0x0, 6, 1, 4, 0, 0, 255, 1},     /* audio0_pll_fout2 */
	{A210_PLL, 0, 0x20, 0, 1, 0, 0, 0, 255, 0},       /* audio1_pll_foutvco */
	{A210_POSTDIV, 0, 0x20, 8, 1, 0, 0, 0, 255, 1},   /* audio1_pll_foutpostdiv */
	{A210_FACTOR, 0, 0x0, 9, 1, 4, 0, 0, 255, 1},     /* audio1_pll_fout2 */
	{A210_PLL, 0, 0x140, 3, 2, 0, 0, 0, 255, 0},      /* video_pll_foutvco */
	{A210_POSTDIV, 0, 0x140, 11, 2, 0, 0, 0, 255, 1}, /* video_pll_foutpostdiv */
	{A210_FACTOR, 0, 0x0, 12, 1, 2, 0, 0, 255, 1},    /* video_pll_fout1ph0 */
	{A210_PLL, 0, 0x40, 3, 3, 0, 0, 0, 255, 0},       /* gmac_pll_foutvco */
	{A210_POSTDIV, 0, 0x40, 14, 3, 0, 0, 0, 255, 1},  /* gmac_pll_foutpostdiv */
	{A210_FACTOR, 0, 0x0, 15, 1, 2, 0, 0, 255, 1},    /* gmac_pll_fout1ph0 */
	{A210_PLL, 0, 0x60, 3, 4, 0, 0, 0, 255, 0},       /* dvfs_pll_foutvco */
	{A210_PLL, 0, 0x80, 3, 5, 0, 0, 0, 255, 0},       /* dpu0_pll_foutvco */
	{A210_POSTDIV, 0, 0x80, 18, 5, 0, 0, 0, 255, 1},  /* dpu0_pll_foutpostdiv */
	{A210_PLL, 0, 0x100, 3, 6, 0, 0, 0, 255, 0},      /* dpu1_pll_foutvco */
	{A210_POSTDIV, 0, 0x100, 20, 6, 0, 0, 0, 255, 1}, /* dpu1_pll_foutpostdiv */
	{A210_PLL, 0, 0x120, 3, 7, 0, 0, 0, 255, 0},      /* dpu2_pll_foutvco */
	{A210_POSTDIV, 0, 0x120, 22, 7, 0, 0, 0, 255, 1}, /* dpu2_pll_foutpostdiv */
	{A210_PLL, 0, 0x160, 3, 8, 0, 0, 0, 255, 0},      /* tee_pll_foutvco */
	{A210_DIV, 1, 0x0, 13, 2, 255, 8, 8, 255, 1},     /* top_cfg_aclk */
	{A210_DIV, 1, 0x18, 11, 4, 15, 8, 4, 255, 1},     /* top_amux_clk_div */
	{A210_GATE, 1, 0x200, 2, 0, 0, 2, 1, 255, 1},     /* top_osc_clk_logic */
	{A210_GATE, 1, 0x200, 26, 0, 0, 0, 1, 255, 1},    /* top_amux_clk */
	{A210_DIV, 1, 0x18, 30, 3, 15, 4, 4, 255, 1},     /* top_gpu_core_clk */
	{A210_MUX, 1, 0x18, 22, 0, 4, 0, 2, 255, 1},      /* top_gpu_core_clk_mux */
	{A210_DIV, 1, 0x1c, 32, 3, 15, 4, 4, 255, 1},     /* pcie_ss_axi_m_aclk */
	{A210_MUX, 1, 0x1c, 5, 4, 2, 0, 1, 255, 1},       /* pcie_ss_axi_m_aclk_mux */
	{A210_DIV, 1, 0x20, 8, 6, 255, 20, 8, 255, 1},    /* peri1_spi_ssi_clk */
	{A210_DIV, 1, 0x20, 13, 2, 7, 16, 3, 255, 1},     /* peri1_mst_aclk */
	{A210_MUX, 1, 0x20, 10, 6, 2, 14, 1, 255, 1},     /* peri2_i2s1_src_clk */
	{A210_DIV, 1, 0x20, 11, 2, 15, 8, 4, 255, 1},     /* peri1_qspi_ssi_clk_div0 */
	{A210_DIV, 1, 0x20, 8, 6, 255, 0, 8, 255, 1},     /* peri1_qspi_ssi_clk_div1 */
	{A210_MUX, 1, 0x20, 37, 8, 2, 12, 1, 255, 1},     /* peri1_qspi_ssi_clk */
	{A210_DIV, 1, 0x24, 8, 6, 255, 24, 8, 255, 1},    /* peri2_spi_ssi_clk */
	{A210_FACTOR, 0, 0x0, 15, 1, 10, 0, 0, 255, 1},   /* uart_sclk_100m */
	{A210_MUX, 1, 0x28, 40, 10, 2, 24, 1, 255, 0},    /* uart_sclk */
	{A210_DIV, 1, 0x28, 11, 2, 15, 16, 4, 255, 1},    /* peri2_qspi_ssi_clk_div0 */
	{A210_DIV, 1, 0x28, 8, 6, 255, 8, 8, 255, 1},     /* peri2_qspi_ssi_clk_div1 */
	{A210_MUX, 1, 0x28, 43, 12, 2, 20, 1, 255, 1},    /* peri2_qspi_ssi_clk */
	{A210_DIV, 1, 0x28, 11, 6, 15, 4, 4, 255, 1},     /* peri3_mst_aclk */
	{A210_DIV, 1, 0x28, 47, 3, 15, 0, 4, 255, 1},     /* emmc_ref_clk */
	{A210_MUX, 1, 0x2c, 5, 14, 2, 20, 1, 255, 1},     /* emmc_ref_clk_mux */
	{A210_FACTOR, 0, 0x0, 16, 1, 10, 0, 0, 255, 1},   /* peri1_pwm_clk */
	{A210_FACTOR, 0, 0x0, 16, 1, 10, 0, 0, 255, 1},   /* peri2_pwm_clk */
	{A210_FACTOR, 0, 0x0, 15, 1, 10, 0, 0, 255, 1},   /* i2c_ic_clk */
	{A210_MUX, 1, 0x30, 11, 16, 2, 0, 1, 255, 1},     /* tee_clk_mux */
	{A210_DIV, 1, 0x30, 51, 7, 255, 4, 8, 255, 1},    /* tee_clk */
	{A210_DIV, 1, 0x34, 13, 2, 7, 8, 3, 255, 1},      /* clkgen_usb_ss_bus_clk */
	{A210_FACTOR, 0, 0x0, 25, 1, 1, 0, 0, 255, 1},    /* gpu_top_sys_clk */
	{A210_GATE, 2, 0x0, 54, 0, 0, 3, 1, 255, 1},      /* gpu_top_aclk */
	{A210_GATE, 2, 0x0, 25, 0, 0, 1, 1, 255, 1},      /* gpu_dfmu_core_clk */
	{A210_GATE, 2, 0x0, 29, 0, 0, 0, 1, 255, 1},      /* gpu_core_clk */
	{A210_FACTOR, 0, 0x0, 15, 1, 10, 0, 0, 255, 1},   /* pcie_ss_apb_clk */
	{A210_GATE, 3, 0x8, 58, 0, 0, 0, 1, 255, 1},      /* e16phy_apbs_pclk */
	{A210_GATE, 3, 0x10, 31, 0, 0, 20, 1, 255, 1},    /* sata_mst_afence_aclk */
	{A210_GATE, 3, 0x10, 25, 0, 0, 16, 1, 255, 1},    /* sata_slv_afence_aclk */
	{A210_GATE, 3, 0x10, 31, 0, 0, 4, 1, 255, 1},     /* x2x_sata_slv_aclk */
	{A210_GATE, 3, 0x10, 25, 0, 0, 0, 1, 255, 1},     /* x2x_sata_mst_aclk */
	{A210_GATE, 3, 0x14, 2, 0, 0, 0, 1, 255, 1},      /* sata_pmalive_clk */
	{A210_GATE, 3, 0x14, 58, 0, 0, 16, 1, 255, 1},    /* sata_rxoob0_clk */
	{A210_GATE, 3, 0x14, 58, 0, 0, 20, 1, 255, 1},    /* sata_rxoob1_clk */
	{A210_GATE, 3, 0x10, 31, 0, 0, 8, 1, 255, 1},     /* sata_gen3x2_aclk */
	{A210_GATE, 3, 0x20, 2, 0, 0, 12, 1, 255, 1},     /* pcie_dm_gen3x4_aux_clk */
	{A210_GATE, 3, 0x20, 25, 0, 0, 0, 1, 255, 1},     /* pcie_dm_gen3x4_slv_aclk */
	{A210_GATE, 3, 0x20, 31, 0, 0, 8, 1, 255, 1},     /* pcie_dm_gen3x4_mst_aclk */
	{A210_GATE, 3, 0x20, 58, 0, 0, 4, 1, 255, 1},     /* pcie_dm_gen3x4_pclk */
	{A210_GATE, 3, 0x24, 2, 0, 0, 12, 1, 255, 1},     /* pcie_rp_gen3x1_aux_clk */
	{A210_GATE, 3, 0x24, 25, 0, 0, 0, 1, 255, 1},     /* pcie_rp_gen3x1_slv_aclk */
	{A210_GATE, 3, 0x24, 31, 0, 0, 8, 1, 255, 1},     /* pcie_rp_gen3x1_mst_aclk */
	{A210_GATE, 3, 0x24, 58, 0, 0, 4, 1, 255, 1},     /* pcie_rp_gen3x1_pclk */
	{A210_GATE, 4, 0x0, 81, 0, 0, 16, 1, 255, 1},     /* usb_axi_mst_pclk */
	{A210_GATE, 4, 0x0, 53, 0, 0, 12, 1, 255, 1},     /* usb_axi_mst_aclk */
	{A210_GATE, 4, 0x0, 53, 0, 0, 8, 1, 255, 1},      /* usb_dfmu_ptw_aclk */
	{A210_GATE, 4, 0x0, 53, 0, 0, 4, 1, 255, 1},      /* usb_dfmu_aclk */
	{A210_GATE, 4, 0x0, 81, 0, 0, 0, 1, 255, 1},      /* usb_dfmu_pclk */
	{A210_FACTOR, 0, 0x0, 15, 1, 10, 0, 0, 255, 1},   /* usb_ss_gtc_clk */
	{A210_GATE, 4, 0x8, 2, 0, 0, 16, 1, 255, 1},      /* usb31_ref_clk */
	{A210_GATE, 4, 0x8, 53, 0, 0, 20, 1, 255, 1},     /* usb31_slv_afence_aclk */
	{A210_FACTOR, 0, 0x0, 2, 1, 24, 0, 0, 255, 1},    /* usb_ss_pad_osc_clk_1m */
	{A210_GATE, 4, 0x8, 84, 0, 0, 8, 1, 255, 1},      /* usb31_suspend_clk */
	{A210_GATE, 4, 0x8, 53, 0, 0, 4, 1, 255, 1},      /* usb31_bus_clk */
	{A210_GATE, 4, 0x8, 81, 0, 0, 0, 1, 255, 1},      /* usb31_pclk */
	{A210_GATE, 4, 0xc, 84, 0, 0, 4, 1, 255, 1},      /* usb_c10phy_suspend_clk */
	{A210_GATE, 4, 0xc, 81, 0, 0, 0, 1, 255, 1},      /* usb_c10phy_pclk */
	{A210_GATE, 4, 0x10, 53, 0, 0, 24, 1, 255, 1},    /* usb20_bus_clk */
	{A210_GATE, 4, 0x10, 53, 0, 0, 0, 1, 255, 1},     /* usb20_x2h_hclk */
	{A210_GATE, 4, 0x10, 53, 0, 0, 4, 1, 255, 1},     /* usb20_x2h_aclk */
	{A210_GATE, 4, 0x10, 53, 0, 0, 8, 1, 255, 1},     /* usb20_slv_afence_aclk */
	{A210_GATE, 4, 0x10, 53, 0, 0, 16, 1, 255, 1},    /* usb20_h2p_hclk */
	{A210_GATE, 4, 0x10, 53, 0, 0, 20, 1, 255, 1},    /* usb20_ahb_slv_hclk */
	{A210_GATE, 4, 0x10, 53, 0, 0, 28, 1, 255, 1},    /* usb20_sysreg_pclk */
	{A210_GATE, 4, 0x14, 53, 0, 0, 0, 1, 255, 1},     /* usb20_wrap0_hclk */
	{A210_GATE, 4, 0x14, 53, 0, 0, 4, 1, 255, 1},     /* usb20_wrap1_hclk */
	{A210_GATE, 4, 0x14, 53, 0, 0, 16, 1, 255, 1},    /* usb20_a2x0_hclk */
	{A210_GATE, 4, 0x14, 53, 0, 0, 20, 1, 255, 1},    /* usb20_a2x0_aclk */
	{A210_GATE, 4, 0x14, 53, 0, 0, 24, 1, 255, 1},    /* usb20_a2x1_hclk */
	{A210_GATE, 4, 0x14, 53, 0, 0, 28, 1, 255, 1},    /* usb20_a2x1_aclk */
	{A210_GATE, 5, 0x0, 113, 0, 0, 20, 1, 255, 1},    /* vo_dfmu_pclk */
	{A210_GATE, 5, 0x0, 110, 0, 0, 21, 1, 255, 1},    /* vo_dfmu_aclk */
	{A210_GATE, 5, 0x0, 110, 0, 0, 22, 1, 255, 1},    /* vo_ptw_aclk */
	{A210_GATE, 5, 0x0, 25, 0, 0, 24, 1, 255, 1},     /* vo_x2h1_clk */
	{A210_GATE, 5, 0x0, 25, 0, 0, 23, 1, 255, 1},     /* vo_x2h0_clk */
	{A210_GATE, 5, 0x0, 110, 0, 0, 19, 1, 255, 1},    /* vo_arb_aclk */
	{A210_GATE, 5, 0x0, 110, 0, 0, 4, 1, 255, 1},     /* vo_dpu_aclk */
	{A210_DIV, 5, 0x8, 11, 3, 15, 0, 4, 4, 1},        /* vo_preocc_aclk */
	{A210_GATE, 5, 0x0, 113, 0, 0, 8, 1, 255, 1},     /* vo_hdmi_pclk */
	{A210_GATE, 5, 0x0, 113, 0, 0, 18, 1, 255, 1},    /* vo_arb_pclk */
	{A210_FACTOR, 0, 0x0, 25, 1, 2, 0, 0, 255, 1},    /* vo_pclk */
	{A210_GATE, 5, 0x0, 2, 0, 0, 11, 1, 255, 1},      /* vo_hdmi_sfrclk */
	{A210_GATE, 5, 0x0, 4, 0, 0, 10, 1, 255, 1},      /* vo_hdmi_cecclk */
	{A210_GATE, 5, 0x0, 117, 0, 0, 0, 1, 255, 1},     /* dpu0_pixclk */
	{A210_DIV, 5, 0x4, 19, 2, 255, 0, 8, 8, 1},       /* vo_pll0_div */
	{A210_GATE, 5, 0x0, 119, 0, 0, 1, 1, 255, 1},     /* dpu1_pixclk */
	{A210_DIV, 5, 0x4, 21, 2, 255, 10, 8, 18, 1},     /* vo_pll1_div */
	{A210_GATE, 5, 0x0, 121, 0, 0, 5, 1, 255, 1},     /* dpu2_pixclk */
	{A210_DIV, 5, 0x4, 23, 2, 255, 20, 8, 28, 1},     /* vo_pll2_div */
	{A210_GATE, 5, 0x0, 25, 0, 0, 2, 1, 255, 1},      /* vo_dpuc_hclk */
	{A210_GATE, 5, 0x0, 124, 0, 0, 3, 1, 255, 1},     /* vo_dpuc_clk */
	{A210_DIV, 5, 0x8, 11, 3, 15, 8, 4, 12, 1},       /* vo_dpuc_div */
	{A210_MUX, 6, 0x0, 116, 18, 3, 2, 2, 255, 0},     /* vo_hdmi_pixclk */
	{A210_GATE, 7, 0x0, 25, 0, 0, 6, 1, 255, 1},      /* peri0_mbox1_pclk */
	{A210_GATE, 7, 0x0, 25, 0, 0, 5, 1, 255, 1},      /* peri0_mbox0_pclk */
	{A210_GATE, 7, 0x0, 25, 0, 0, 4, 1, 255, 1},      /* peri0_wdt0_pclk */
	{A210_GATE, 7, 0x0, 25, 0, 0, 3, 1, 255, 1},      /* peri0_timer1_pclk */
	{A210_GATE, 7, 0x0, 25, 0, 0, 2, 1, 255, 1},      /* peri0_timer1_cclk */
	{A210_GATE, 7, 0x0, 25, 0, 0, 1, 1, 255, 1},      /* peri0_timer0_pclk */
	{A210_GATE, 7, 0x0, 25, 0, 0, 0, 1, 255, 1},      /* peri0_timer0_cclk */
	{A210_GATE, 8, 0x0, 41, 0, 0, 31, 1, 255, 1},     /* peri1_clkgen_uart3_sclk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 30, 1, 255, 1},     /* peri1_clkgen_uart3_pclk */
	{A210_GATE, 8, 0x0, 41, 0, 0, 29, 1, 255, 1},     /* peri1_clkgen_uart2_sclk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 28, 1, 255, 1},     /* peri1_clkgen_uart2_pclk */
	{A210_GATE, 8, 0x0, 41, 0, 0, 27, 1, 255, 1},     /* peri1_clkgen_uart1_sclk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 26, 1, 255, 1},     /* peri1_clkgen_uart1_pclk */
	{A210_GATE, 8, 0x0, 41, 0, 0, 25, 1, 255, 1},     /* peri1_clkgen_uart0_sclk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 24, 1, 255, 1},     /* peri1_clkgen_uart0_pclk */
	{A210_GATE, 8, 0x0, 33, 0, 0, 23, 1, 255, 1},     /* peri1_clkgen_spi0_ssi_clk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 22, 1, 255, 1},     /* peri1_clkgen_spi0_pclk */
	{A210_GATE, 8, 0x0, 38, 0, 0, 21, 1, 255, 1},     /* peri1_clkgen_qspi0_ssi_clk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 20, 1, 255, 1},     /* peri1_clkgen_qspi0_pclk */
	{A210_GATE, 8, 0x0, 48, 0, 0, 19, 1, 255, 1},     /* peri1_clkgen_pwm0_pclk */
	{A210_GATE, 8, 0x0, 145, 0, 0, 18, 1, 255, 1},    /* peri1_clkgen_pwm0_cclk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 13, 1, 255, 1},     /* peri1_clkgen_i2c2_pclk */
	{A210_GATE, 8, 0x0, 50, 0, 0, 12, 1, 255, 1},     /* peri1_clkgen_i2c2_ic_clk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 11, 1, 255, 1},     /* peri1_clkgen_i2c1_pclk */
	{A210_GATE, 8, 0x0, 50, 0, 0, 10, 1, 255, 1},     /* peri1_clkgen_i2c1_ic_clk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 9, 1, 255, 1},      /* peri1_clkgen_i2c0_pclk */
	{A210_GATE, 8, 0x0, 50, 0, 0, 8, 1, 255, 1},      /* peri1_clkgen_i2c0_ic_clk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 7, 1, 255, 1},      /* peri1_clkgen_gpio1_pclk */
	{A210_GATE, 8, 0x0, 4, 0, 0, 6, 1, 255, 1},       /* peri1_clkgen_gpio1_dbclk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 5, 1, 255, 1},      /* peri1_clkgen_gpio0_pclk */
	{A210_GATE, 8, 0x0, 4, 0, 0, 4, 1, 255, 1},       /* peri1_clkgen_gpio0_dbclk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 3, 1, 255, 1},      /* peri1_gmac1_hclk */
	{A210_GATE, 8, 0x0, 34, 0, 0, 2, 1, 255, 1},      /* peri1_gmac1_aclk */
	{A210_GATE, 8, 0x0, 25, 0, 0, 1, 1, 255, 1},      /* peri1_gmac0_hclk */
	{A210_GATE, 8, 0x0, 34, 0, 0, 0, 1, 255, 1},      /* peri1_gmac0_aclk */
	{A210_GATE, 8, 0x4, 25, 0, 0, 7, 1, 255, 1},      /* peri1_pad_ctrl_pclk */
	{A210_GATE, 8, 0x4, 25, 0, 0, 3, 1, 255, 1},      /* peri1_x2h_gmac1_hclk */
	{A210_GATE, 8, 0x4, 25, 0, 0, 2, 1, 255, 1},      /* peri1_x2h_gmac1_aclk */
	{A210_GATE, 8, 0x4, 25, 0, 0, 1, 1, 255, 1},      /* peri1_x2h_gmac0_hclk */
	{A210_GATE, 8, 0x4, 25, 0, 0, 0, 1, 255, 1},      /* peri1_x2h_gmac0_aclk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 27, 1, 255, 1},     /* peri2_clkgen_i2s1_pclk */
	{A210_GATE, 9, 0x0, 35, 0, 0, 26, 1, 255, 1},     /* peri2_clkgen_i2s1_src_clk */
	{A210_GATE, 9, 0x0, 50, 0, 0, 25, 1, 255, 1},     /* peri2_clkgen_i2c7_ic_clk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 24, 1, 255, 1},     /* peri2_clkgen_i2c7_pclk */
	{A210_GATE, 9, 0x0, 50, 0, 0, 23, 1, 255, 1},     /* peri2_clkgen_i2c6_ic_clk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 22, 1, 255, 1},     /* peri2_clkgen_i2c6_pclk */
	{A210_GATE, 9, 0x0, 50, 0, 0, 21, 1, 255, 1},     /* peri2_clkgen_i2c5_ic_clk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 20, 1, 255, 1},     /* peri2_clkgen_i2c5_pclk */
	{A210_GATE, 9, 0x0, 50, 0, 0, 19, 1, 255, 1},     /* peri2_clkgen_i2c4_ic_clk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 18, 1, 255, 1},     /* peri2_clkgen_i2c4_pclk */
	{A210_GATE, 9, 0x0, 50, 0, 0, 17, 1, 255, 1},     /* peri2_clkgen_i2c3_ic_clk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 16, 1, 255, 1},     /* peri2_clkgen_i2c3_pclk */
	{A210_GATE, 9, 0x0, 4, 0, 0, 15, 1, 255, 1},      /* peri2_clkgen_gpio3_dbclk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 14, 1, 255, 1},     /* peri2_clkgen_gpio3_pclk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 13, 1, 255, 1},     /* peri2_clkgen_pad_ctrl_pclk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 12, 1, 255, 1},     /* peri2_clkgen_uart6_pclk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 11, 1, 255, 1},     /* peri2_clkgen_uart5_pclk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 10, 1, 255, 1},     /* peri2_clkgen_uart4_pclk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 9, 1, 255, 1},      /* peri2_clkgen_spi1_pclk */
	{A210_GATE, 9, 0x0, 25, 0, 0, 8, 1, 255, 1},      /* peri2_clkgen_gpio2_pclk */
	{A210_GATE, 9, 0x0, 41, 0, 0, 6, 1, 255, 1},      /* peri2_clkgen_uart6_sclk */
	{A210_GATE, 9, 0x0, 41, 0, 0, 5, 1, 255, 1},      /* peri2_clkgen_uart5_sclk */
	{A210_GATE, 9, 0x0, 41, 0, 0, 4, 1, 255, 1},      /* peri2_clkgen_uart4_sclk */
	{A210_GATE, 9, 0x0, 39, 0, 0, 3, 1, 255, 1},      /* peri2_clkgen_spi1_ssi_clk */
	{A210_GATE, 9, 0x0, 4, 0, 0, 2, 1, 255, 1},       /* peri2_clkgen_gpio2_dbclk */
	{A210_GATE, 9, 0x4, 25, 0, 0, 11, 1, 255, 1},     /* peri2_clkgen_qspi1_pclk */
	{A210_GATE, 9, 0x4, 44, 0, 0, 10, 1, 255, 1},     /* peri2_clkgen_qspi1_ssi_clk */
	{A210_GATE, 9, 0x4, 25, 0, 0, 9, 1, 255, 1},      /* peri2_clkgen_uart9_pclk */
	{A210_GATE, 9, 0x4, 41, 0, 0, 8, 1, 255, 1},      /* peri2_clkgen_uart9_sclk */
	{A210_GATE, 9, 0x4, 25, 0, 0, 7, 1, 255, 1},      /* peri2_clkgen_uart8_pclk */
	{A210_GATE, 9, 0x4, 41, 0, 0, 6, 1, 255, 1},      /* peri2_clkgen_uart8_sclk */
	{A210_GATE, 9, 0x4, 25, 0, 0, 5, 1, 255, 1},      /* peri2_clkgen_uart7_pclk */
	{A210_GATE, 9, 0x4, 41, 0, 0, 4, 1, 255, 1},      /* peri2_clkgen_uart7_sclk */
	{A210_GATE, 9, 0x4, 49, 0, 0, 3, 1, 255, 1},      /* peri2_clkgen_pwm2_pclk */
	{A210_GATE, 9, 0x4, 199, 0, 0, 2, 1, 255, 1},     /* peri2_clkgen_pwm2_cclk */
	{A210_GATE, 9, 0x4, 49, 0, 0, 1, 1, 255, 1},      /* peri2_clkgen_pwm1_pclk */
	{A210_GATE, 9, 0x4, 201, 0, 0, 0, 1, 255, 1},     /* peri2_clkgen_pwm1_cclk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 21, 1, 255, 1},    /* peri3_clkgen_pad_ctrl_pclk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 20, 1, 255, 1},    /* peri3_clkgen_gpio4_pclk */
	{A210_GATE, 10, 0x0, 4, 0, 0, 19, 1, 255, 1},     /* peri3_clkgen_gpio4_dbclk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 15, 1, 255, 1},    /* peri3_clkgen_adc_pclk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 14, 1, 255, 1},    /* peri3_clkgen_axi_mst_pclk */
	{A210_GATE, 10, 0x0, 45, 0, 0, 13, 1, 255, 1},    /* peri3_clkgen_axi_mst_aclk */
	{A210_GATE, 10, 0x0, 45, 0, 0, 12, 1, 255, 1},    /* peri3_clkgen_sdio_x2x_aclk_s */
	{A210_GATE, 10, 0x0, 25, 0, 0, 11, 1, 255, 1},    /* peri3_clkgen_sdio_x2x_aclk_m */
	{A210_GATE, 10, 0x0, 2, 0, 0, 10, 1, 255, 1},     /* peri3_clkgen_sdio_osc_clk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 9, 1, 255, 1},     /* peri3_clkgen_sdio_hclk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 8, 1, 255, 1},     /* peri3_clkgen_sdio_aclk */
	{A210_GATE, 10, 0x0, 45, 0, 0, 7, 1, 255, 1},     /* peri3_clkgen_emmc_x2x_aclk_s */
	{A210_GATE, 10, 0x0, 25, 0, 0, 6, 1, 255, 1},     /* peri3_clkgen_emmc_x2x_aclk_m */
	{A210_GATE, 10, 0x0, 2, 0, 0, 5, 1, 255, 1},      /* peri3_clkgen_emmc_osc_clk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 4, 1, 255, 1},     /* peri3_clkgen_emmc_hclk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 3, 1, 255, 1},     /* peri3_clkgen_emmc_aclk */
	{A210_FACTOR, 0, 0x0, 46, 1, 4, 0, 0, 255, 1},    /* peri3_emmc_sdio_ref_clk */
	{A210_GATE, 10, 0x0, 219, 0, 0, 2, 1, 255, 1},    /* peri3_clkgen_sdio_ref_clk */
	{A210_GATE, 10, 0x0, 25, 0, 0, 1, 1, 255, 1},     /* peri3_clkgen_dmac_hclk */
	{A210_GATE, 10, 0x0, 45, 0, 0, 0, 1, 255, 1},     /* peri3_clkgen_dmac_aclk */
	{A210_GATE, 11, 0x0, 52, 0, 0, 8, 1, 255, 1},     /* tee_dmac_clk */
	{A210_AON, 12, 0x0, 0, 0, 0, 0, 0, 255, 0},       /* aon-sys */
	{A210_GATE, 12, 0x4, 224, 0, 0, 20, 1, 255, 1},   /* aon-i2c1 */
	{A210_GATE, 12, 0x4, 224, 0, 0, 21, 1, 255, 1},   /* aon-i2c1-apb */
};
static const uint16_t clock_parents[] = {22, 11, 17, 5, 5,  11, 10, 7,   37,  36, 40,
					 2,  43, 42, 5, 11, 11, 24, 116, 118, 120};
static const struct a210_tree_pll clock_plls[] = {
	{0x01206201, 0x004dd2f2, 2359296000U}, /* AUDIO0_PLL */
	{0x01206901, 0x005e9e1b, 2528870400U}, /* AUDIO1_PLL */
	{0x01206e01, 0x00000000, 2640000000U}, /* VIDEO_PLL */
	{0x01307d01, 0x00000000, 3000000000U}, /* GMAC_PLL */
	{0x01205001, 0x00000000, 1920000000U}, /* DVFS_PLL */
	{0x01206301, 0x00000000, 2376000000U}, /* DPU0_PLL */
	{0x01206301, 0x00000000, 2376000000U}, /* DPU1_PLL */
	{0x01206301, 0x00000000, 2376000000U}, /* DPU2_PLL */
	{0x01306401, 0x00000000, 2400000000U}, /* TEE_PLL */
};
static const uint16_t clock_map[] = {
	139, 137, 135, 133, 188, 187, 186, 198, 196, 194, 140, 138, 136, 134, 183, 182, 181,
	197, 195, 193, 161, 180, 203, 224, 225, 226, 5,   8,   29,  46,  47,  55,  56,  57,
	59,  60,  61,  67,  62,  63,  66,  65,  64,  68,  70,  71,  69,  72,  74,  75,  73,
	76,  77,  78,  79,  80,  83,  82,  85,  86,  87,  88,  89,  106, 107, 105, 104, 103,
	108, 112, 114, 115, 111, 109, 123, 122, 118, 116, 126, 127, 128, 129, 130, 131, 132,
	141, 142, 143, 144, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158,
	159, 160, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 184,
	185, 189, 190, 191, 192, 200, 202, 204, 205, 206, 207, 208, 209, 210, 211, 212, 214,
	215, 216, 217, 218, 220, 221, 222, 223, 90,  213, 162, 163, 164, 165, 125, 91,  92,
	93,  94,  95,  96,  97,  98,  99,  100, 101, 102};
static const uint16_t clock_critical[] = {14, 11, 127, 126, 27, 28, 188, 183};
static const struct a210_clock_word clock_words[] = {
	{0, 0, 0x00000000, 0x00000000},   {0, 4, 0x00000000, 0x00000000},
	{0, 16, 0x00000000, 0x00000000},  {0, 32, 0x00000000, 0x00000000},
	{0, 36, 0x00000000, 0x00000000},  {0, 48, 0x00000000, 0x00000000},
	{0, 64, 0x00000000, 0x00000000},  {0, 68, 0x00000000, 0x00000000},
	{0, 80, 0x00000000, 0x00000000},  {0, 96, 0x00000000, 0x00000000},
	{0, 100, 0x00000000, 0x00000000}, {0, 112, 0x00000000, 0x00000000},
	{0, 128, 0x00000000, 0x00000000}, {0, 132, 0x00000000, 0x00000000},
	{0, 144, 0x00000000, 0x00000000}, {0, 256, 0x00000000, 0x00000000},
	{0, 260, 0x00000000, 0x00000000}, {0, 272, 0x00000000, 0x00000000},
	{0, 288, 0x00000000, 0x00000000}, {0, 292, 0x00000000, 0x00000000},
	{0, 304, 0x00000000, 0x00000000}, {0, 320, 0x00000000, 0x00000000},
	{0, 324, 0x00000000, 0x00000000}, {0, 336, 0x00000000, 0x00000000},
	{0, 352, 0x00000000, 0x00000000}, {0, 356, 0x00000000, 0x00000000},
	{0, 368, 0x00000000, 0x00000000}, {1, 0, 0x0000ff00, 0x00000000},
	{1, 24, 0x00000ff3, 0x00000000},  {1, 28, 0x000000f1, 0x00000000},
	{1, 32, 0x0ff75fff, 0x00000000},  {1, 36, 0xff000000, 0x00000000},
	{1, 40, 0x011fffff, 0x00000000},  {1, 44, 0x00100000, 0x00000000},
	{1, 48, 0x00000ff1, 0x00000000},  {1, 52, 0x00000700, 0x00000000},
	{1, 512, 0x00000005, 0x00000000}, {2, 0, 0x0000000b, 0x00000000},
	{3, 8, 0x00000001, 0x00000000},   {3, 16, 0x00110111, 0x00000000},
	{3, 20, 0x00110001, 0x00000000},  {3, 32, 0x00001111, 0x00000000},
	{3, 36, 0x00001111, 0x00000000},  {4, 0, 0x00011111, 0x00000000},
	{4, 8, 0x00110111, 0x00000000},   {4, 12, 0x00000011, 0x00000000},
	{4, 16, 0x11110111, 0x00000000},  {4, 20, 0x11110011, 0x00000000},
	{5, 0, 0x01fc0d3f, 0x00000000},   {5, 4, 0x1ff7fdff, 0x10040100},
	{5, 8, 0x00001f1f, 0x00001010},   {6, 0, 0x0000000c, 0x00000000},
	{7, 0, 0x0000007f, 0x00000000},   {8, 0, 0xfffc3fff, 0x00000000},
	{8, 4, 0x0000008f, 0x00000000},   {9, 0, 0x0fffff7c, 0x00000000},
	{9, 4, 0x00000fff, 0x00000000},   {10, 0, 0x0038ffff, 0x00000000},
	{11, 0, 0x00000100, 0x00000000},  {12, 0, 0x00000000, 0x00000000},
	{12, 4, 0x00300000, 0x00000000},
};
