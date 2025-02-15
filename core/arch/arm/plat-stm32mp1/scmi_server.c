// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2019-2020, STMicroelectronics
 */
#include <assert.h>
#include <compiler.h>
#include <drivers/clk.h>
#include <drivers/scmi-msg.h>
#include <drivers/scmi.h>
#include <drivers/stm32mp1_pmic.h>
#include <drivers/stm32mp1_pwr.h>
#include <drivers/stpmic1.h>
#include <dt-bindings/clock/stm32mp1-clks.h>
#include <dt-bindings/regulator/st,stm32mp15-regulator.h>
#include <dt-bindings/reset/stm32mp1-resets.h>
#include <initcall.h>
#include <kernel/pm.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <platform_config.h>
#include <stdint.h>
#include <speculation_barrier.h>
#include <stm32_util.h>
#include <string.h>
#include <tee_api_defines.h>
#include <util.h>

#define TIMEOUT_US_1MS		1000

#define SCMI_CLOCK_NAME_SIZE	16
#define SCMI_RD_NAME_SIZE	16
#define SCMI_VOLTD_NAME_SIZE	16

/*
 * struct stm32_scmi_clk - Data for the exposed clock
 * @clock_id: Clock identifier in RCC clock driver
 * @name: Clock string ID exposed to agent
 * @enabled: State of the SCMI clock
 */
struct stm32_scmi_clk {
	unsigned long clock_id;
	const char *name;
	bool enabled;
};

/*
 * struct stm32_scmi_rd - Data for the exposed reset controller
 * @reset_id: Reset identifier in RCC reset driver
 * @name: Reset string ID exposed to agent
 */
struct stm32_scmi_rd {
	unsigned long reset_id;
	const char *name;
};

enum voltd_device {
	VOLTD_PWR,
	VOLTD_PMIC,
};

/*
 * struct stm32_scmi_voltd - Data for the exposed voltage domains
 * @name: Power regulator string ID exposed to agent
 * @priv_id: Internal string ID for the regulator
 * @priv_dev: Internal ID for the device implementing the regulator
 */
struct stm32_scmi_voltd {
	const char *name;
	const char *priv_id;
	enum voltd_device priv_dev;

};

/* Locate all non-secure SMT message buffers in last page of SYSRAM */
#define SMT_BUFFER_BASE		CFG_STM32MP1_SCMI_SHM_BASE
#define SMT_BUFFER0_BASE	SMT_BUFFER_BASE
#define SMT_BUFFER1_BASE	(SMT_BUFFER_BASE + 0x200)

#if (SMT_BUFFER1_BASE + SMT_BUF_SLOT_SIZE > \
	CFG_STM32MP1_SCMI_SHM_BASE + CFG_STM32MP1_SCMI_SHM_SIZE)
#error "SCMI shared memory mismatch"
#endif

register_phys_mem(MEM_AREA_IO_NSEC, CFG_STM32MP1_SCMI_SHM_BASE,
		  CFG_STM32MP1_SCMI_SHM_SIZE);

static struct scmi_msg_channel scmi_channel[] = {
	[0] = {
		.agent_name = "stm32mp1-agent-0",
		.shm_addr = { .pa = SMT_BUFFER0_BASE, },
		.shm_size = SMT_BUF_SLOT_SIZE,
	},
	[1] = {
		.agent_name = "stm32mp1-agent-1",
		.shm_addr = { .pa = SMT_BUFFER1_BASE, },
		.shm_size = SMT_BUF_SLOT_SIZE,
	},
};

struct scmi_msg_channel *plat_scmi_get_channel(unsigned int agent_id)
{
	assert(agent_id < ARRAY_SIZE(scmi_channel));

	return &scmi_channel[agent_id];
}

#define CLOCK_CELL(_scmi_id, _id, _name, _init_enabled) \
	[_scmi_id] = { \
		.clock_id = _id, \
		.name = _name, \
		.enabled = _init_enabled, \
	}

static struct stm32_scmi_clk stm32_scmi0_clock[] = {
	CLOCK_CELL(CK_SCMI0_HSE, CK_HSE, "ck_hse", true),
	CLOCK_CELL(CK_SCMI0_HSI, CK_HSI, "ck_hsi", true),
	CLOCK_CELL(CK_SCMI0_CSI, CK_CSI, "ck_csi", true),
	CLOCK_CELL(CK_SCMI0_LSE, CK_LSE, "ck_lse", true),
	CLOCK_CELL(CK_SCMI0_LSI, CK_LSI, "ck_lsi", true),
	CLOCK_CELL(CK_SCMI0_PLL2_Q, PLL2_Q, "pll2_q", true),
	CLOCK_CELL(CK_SCMI0_PLL2_R, PLL2_R, "pll2_r", true),
	CLOCK_CELL(CK_SCMI0_MPU, CK_MPU, "ck_mpu", true),
	CLOCK_CELL(CK_SCMI0_AXI, CK_AXI, "ck_axi", true),
	CLOCK_CELL(CK_SCMI0_BSEC, BSEC, "bsec", true),
	CLOCK_CELL(CK_SCMI0_CRYP1, CRYP1, "cryp1", false),
	CLOCK_CELL(CK_SCMI0_GPIOZ, GPIOZ, "gpioz", false),
	CLOCK_CELL(CK_SCMI0_HASH1, HASH1, "hash1", false),
	CLOCK_CELL(CK_SCMI0_I2C4, I2C4_K, "i2c4_k", false),
	CLOCK_CELL(CK_SCMI0_I2C6, I2C6_K, "i2c6_k", false),
	CLOCK_CELL(CK_SCMI0_IWDG1, IWDG1, "iwdg1", false),
	CLOCK_CELL(CK_SCMI0_RNG1, RNG1_K, "rng1_k", true),
	CLOCK_CELL(CK_SCMI0_RTC, RTC, "ck_rtc", true),
	CLOCK_CELL(CK_SCMI0_RTCAPB, RTCAPB, "rtcapb", true),
	CLOCK_CELL(CK_SCMI0_SPI6, SPI6_K, "spi6_k", false),
	CLOCK_CELL(CK_SCMI0_USART1, USART1_K, "usart1_k", false),
};

static struct stm32_scmi_clk stm32_scmi1_clock[] = {
	CLOCK_CELL(CK_SCMI1_PLL3_Q, PLL3_Q, "pll3_q", true),
	CLOCK_CELL(CK_SCMI1_PLL3_R, PLL3_R, "pll3_r", true),
	CLOCK_CELL(CK_SCMI1_MCU, CK_MCU, "ck_mcu", false),
};

#define RESET_CELL(_scmi_id, _id, _name) \
	[_scmi_id] = { \
		.reset_id = _id, \
		.name = _name, \
	}

static struct stm32_scmi_rd stm32_scmi0_reset_domain[] = {
	RESET_CELL(RST_SCMI0_SPI6, SPI6_R, "spi6"),
	RESET_CELL(RST_SCMI0_I2C4, I2C4_R, "i2c4"),
	RESET_CELL(RST_SCMI0_I2C6, I2C6_R, "i2c6"),
	RESET_CELL(RST_SCMI0_USART1, USART1_R, "usart1"),
	RESET_CELL(RST_SCMI0_STGEN, STGEN_R, "stgen"),
	RESET_CELL(RST_SCMI0_GPIOZ, GPIOZ_R, "gpioz"),
	RESET_CELL(RST_SCMI0_CRYP1, CRYP1_R, "cryp1"),
	RESET_CELL(RST_SCMI0_HASH1, HASH1_R, "hash1"),
	RESET_CELL(RST_SCMI0_RNG1, RNG1_R, "rng1"),
	RESET_CELL(RST_SCMI0_MDMA, MDMA_R, "mdma"),
	RESET_CELL(RST_SCMI0_MCU, MCU_R, "mcu"),
	RESET_CELL(RST_SCMI0_MCU_HOLD_BOOT, MCU_HOLD_BOOT_R, "mcu_hold_boot"),
};

#define VOLTD_CELL(_scmi_id, _dev_id, _priv_id, _name) \
	[_scmi_id] = { \
		.priv_id = (_priv_id), \
		.priv_dev = (_dev_id), \
		.name = (_name), \
	}

#define PWR_REG11_NAME_ID		"0"
#define PWR_REG18_NAME_ID		"1"
#define PWR_USB33_NAME_ID		"2"

struct stm32_scmi_voltd scmi0_voltage_domain[] = {
	VOLTD_CELL(VOLTD_SCMI0_REG11, VOLTD_PWR, PWR_REG11_NAME_ID, "reg11"),
	VOLTD_CELL(VOLTD_SCMI0_REG18, VOLTD_PWR, PWR_REG18_NAME_ID, "reg18"),
	VOLTD_CELL(VOLTD_SCMI0_USB33, VOLTD_PWR, PWR_USB33_NAME_ID, "usb33"),
};

struct scmi_agent_resources {
	struct stm32_scmi_clk *clock;
	size_t clock_count;
	struct stm32_scmi_rd *rd;
	size_t rd_count;
	struct stm32_scmi_voltd *voltd;
	size_t voltd_count;
};

static const struct scmi_agent_resources agent_resources[] = {
	[0] = {
		.clock = stm32_scmi0_clock,
		.clock_count = ARRAY_SIZE(stm32_scmi0_clock),
		.rd = stm32_scmi0_reset_domain,
		.rd_count = ARRAY_SIZE(stm32_scmi0_reset_domain),
		.voltd = scmi0_voltage_domain,
		.voltd_count = ARRAY_SIZE(scmi0_voltage_domain),
	},
	[1] = {
		.clock = stm32_scmi1_clock,
		.clock_count = ARRAY_SIZE(stm32_scmi1_clock),
	},
};

static const struct scmi_agent_resources *find_resource(unsigned int agent_id)
{
	assert(agent_id < ARRAY_SIZE(agent_resources));

	return &agent_resources[agent_id];
}

static size_t __maybe_unused plat_scmi_protocol_count_paranoid(void)
{
	unsigned int n = 0;
	unsigned int count = 0;
	const size_t agent_count = ARRAY_SIZE(agent_resources);

	for (n = 0; n < agent_count; n++)
		if (agent_resources[n].clock_count)
			break;
	if (n < agent_count)
		count++;

	for (n = 0; n < agent_count; n++)
		if (agent_resources[n].rd_count)
			break;
	if (n < agent_count)
		count++;

	for (n = 0; n < agent_count; n++)
		if (agent_resources[n].voltd_count)
			break;
	if (n < agent_count)
		count++;

	return count;
}

static const char vendor[] = "ST";
static const char sub_vendor[] = "";

const char *plat_scmi_vendor_name(void)
{
	return vendor;
}

const char *plat_scmi_sub_vendor_name(void)
{
	return sub_vendor;
}

/* Currently supporting Clocks and Reset Domains */
static const uint8_t plat_protocol_list[] = {
	SCMI_PROTOCOL_ID_CLOCK,
	SCMI_PROTOCOL_ID_RESET_DOMAIN,
	SCMI_PROTOCOL_ID_VOLTAGE_DOMAIN,
	0 /* Null termination */
};

size_t plat_scmi_protocol_count(void)
{
	const size_t count = ARRAY_SIZE(plat_protocol_list) - 1;

	assert(count == plat_scmi_protocol_count_paranoid());

	return count;
}

const uint8_t *plat_scmi_protocol_list(unsigned int agent_id __unused)
{
	assert(plat_scmi_protocol_count_paranoid() ==
	       (ARRAY_SIZE(plat_protocol_list) - 1));

	return plat_protocol_list;
}

/*
 * Platform SCMI clocks
 */
static struct stm32_scmi_clk *find_clock(unsigned int agent_id,
					 unsigned int scmi_id)
{
	const struct scmi_agent_resources *resource = find_resource(agent_id);
	size_t n = 0;

	if (resource) {
		for (n = 0; n < resource->clock_count; n++)
			if (n == scmi_id)
				return &resource->clock[n];
	}

	return NULL;
}

size_t plat_scmi_clock_count(unsigned int agent_id)
{
	const struct scmi_agent_resources *resource = find_resource(agent_id);

	if (!resource)
		return 0;

	return resource->clock_count;
}

const char *plat_scmi_clock_get_name(unsigned int agent_id,
				     unsigned int scmi_id)
{
	struct stm32_scmi_clk *clock = find_clock(agent_id, scmi_id);

	if (!clock || !stm32mp_nsec_can_access_clock(clock->clock_id))
		return NULL;

	return clock->name;
}

int32_t plat_scmi_clock_rates_array(unsigned int agent_id __unused,
				    unsigned int scmi_id __unused,
				    size_t start_index __unused,
				    unsigned long *array __unused,
				    size_t *nb_elts __unused)
{
	/*
	 * Explicitly do not expose clock rates by array since not
	 * fully supported by Linux kernel as of v5.4.24.
	 */
	return SCMI_NOT_SUPPORTED;
}

int32_t plat_scmi_clock_rates_by_step(unsigned int agent_id,
				      unsigned int scmi_id,
				      unsigned long *array)
{
	struct stm32_scmi_clk *clock = find_clock(agent_id, scmi_id);

	if (!clock)
		return SCMI_NOT_FOUND;

	if (!stm32mp_nsec_can_access_clock(clock->clock_id))
		return SCMI_DENIED;

	switch (scmi_id) {
	case CK_SCMI0_MPU:
		/*
		 * Pretend we support all rates for MPU clock,
		 * CLOCK_RATE_SET will reject unsupported rates.
		 */
		array[0] = 0U;
		array[1] = UINT32_MAX;
		array[2] = 1U;
		break;
	default:
		array[0] = clk_get_rate(clock->clock_id);
		array[1] = array[0];
		array[2] = 0U;
		break;
	}

	return SCMI_SUCCESS;
}

int32_t plat_scmi_clock_set_rate(unsigned int agent_id, unsigned int scmi_id,
				 unsigned long rate)
{
	struct stm32_scmi_clk *clock = find_clock(agent_id, scmi_id);
	int ret = SCMI_DENIED;

	if (!clock)
		return SCMI_NOT_FOUND;
	if (!stm32mp_nsec_can_access_clock(clock->clock_id))
		return SCMI_DENIED;

	switch (scmi_id) {
	case CK_SCMI0_MPU:
		ret = stm32mp1_set_opp_khz(rate / 1000);
		if (ret)
			return SCMI_INVALID_PARAMETERS;
		break;
	default:
		if (rate != clk_get_rate(clock->clock_id))
			return SCMI_INVALID_PARAMETERS;
		break;
	}

	return SCMI_SUCCESS;
}

unsigned long plat_scmi_clock_get_rate(unsigned int agent_id,
				       unsigned int scmi_id)
{
	struct stm32_scmi_clk *clock = find_clock(agent_id, scmi_id);

	if (!clock || !stm32mp_nsec_can_access_clock(clock->clock_id))
		return 0;

	return clk_get_rate(clock->clock_id);
}

int32_t plat_scmi_clock_get_state(unsigned int agent_id, unsigned int scmi_id)
{
	struct stm32_scmi_clk *clock = find_clock(agent_id, scmi_id);

	if (!clock || !stm32mp_nsec_can_access_clock(clock->clock_id))
		return 0;

	return (int32_t)clock->enabled;
}

int32_t plat_scmi_clock_set_state(unsigned int agent_id, unsigned int scmi_id,
				  bool enable_not_disable)
{
	struct stm32_scmi_clk *clock = find_clock(agent_id, scmi_id);

	if (!clock)
		return SCMI_NOT_FOUND;

	if (!stm32mp_nsec_can_access_clock(clock->clock_id))
		return SCMI_DENIED;

	if (enable_not_disable) {
		if (!clock->enabled) {
			DMSG("SCMI clock %u enable", scmi_id);
			clk_enable(clock->clock_id);
			clock->enabled = true;
		}
	} else {
		if (clock->enabled) {
			DMSG("SCMI clock %u disable", scmi_id);
			clk_disable(clock->clock_id);
			clock->enabled = false;
		}
	}

	return SCMI_SUCCESS;
}

/*
 * Platform SCMI reset domains
 */
static struct stm32_scmi_rd *find_rd(unsigned int agent_id,
				     unsigned int scmi_id)
{
	const struct scmi_agent_resources *resource = find_resource(agent_id);
	size_t n = 0;

	if (resource) {
		for (n = 0; n < resource->rd_count; n++)
			if (n == scmi_id)
				return &resource->rd[n];
	}

	return NULL;
}

const char *plat_scmi_rd_get_name(unsigned int agent_id, unsigned int scmi_id)
{
	const struct stm32_scmi_rd *rd = find_rd(agent_id, scmi_id);

	if (!rd)
		return NULL;

	return rd->name;
}

size_t plat_scmi_rd_count(unsigned int agent_id)
{
	const struct scmi_agent_resources *resource = find_resource(agent_id);

	if (!resource)
		return 0;

	return resource->rd_count;
}

int32_t plat_scmi_rd_autonomous(unsigned int agent_id, unsigned int scmi_id,
				uint32_t state)
{
	const struct stm32_scmi_rd *rd = find_rd(agent_id, scmi_id);

	if (!rd)
		return SCMI_NOT_FOUND;

	if (!stm32mp_nsec_can_access_reset(rd->reset_id))
		return SCMI_DENIED;

	if (rd->reset_id == MCU_HOLD_BOOT_R)
		return SCMI_NOT_SUPPORTED;

	/* Supports only reset with context loss */
	if (state)
		return SCMI_NOT_SUPPORTED;

	DMSG("SCMI reset %u cycle", scmi_id);

	if (stm32_reset_assert(rd->reset_id, TIMEOUT_US_1MS))
		return SCMI_HARDWARE_ERROR;

	if (stm32_reset_deassert(rd->reset_id, TIMEOUT_US_1MS))
		return SCMI_HARDWARE_ERROR;

	return SCMI_SUCCESS;
}

int32_t plat_scmi_rd_set_state(unsigned int agent_id, unsigned int scmi_id,
			       bool assert_not_deassert)
{
	const struct stm32_scmi_rd *rd = find_rd(agent_id, scmi_id);

	if (!rd)
		return SCMI_NOT_FOUND;

	if (!stm32mp_nsec_can_access_reset(rd->reset_id))
		return SCMI_DENIED;

	if (rd->reset_id == MCU_HOLD_BOOT_R) {
		DMSG("SCMI MCU hold boot %s",
		     assert_not_deassert ? "set" : "release");
		stm32_reset_assert_deassert_mcu(assert_not_deassert);
		return SCMI_SUCCESS;
	}

	if (assert_not_deassert) {
		DMSG("SCMI reset %u set", scmi_id);
		stm32_reset_set(rd->reset_id);
	} else {
		DMSG("SCMI reset %u release", scmi_id);
		stm32_reset_release(rd->reset_id);
	}

	return SCMI_SUCCESS;
}

static TEE_Result stm32_scmi_pm(enum pm_op op, unsigned int pm_hint __unused,
				const struct pm_callback_handle *hdl __unused)
{
	size_t i = 0;

	if (op == PM_OP_RESUME)
		for (i = 0; i < ARRAY_SIZE(scmi_channel); i++)
			scmi_smt_init_agent_channel(&scmi_channel[i]);

	return TEE_SUCCESS;
}
DECLARE_KEEP_PAGER(stm32_scmi_pm);

/*
 * Platform SCMI voltage domains
 */
static struct stm32_scmi_voltd *find_voltd(unsigned int agent_id,
					   unsigned int scmi_id)
{
	const struct scmi_agent_resources *resource = find_resource(agent_id);
	size_t n = 0;

	if (resource) {
		for (n = 0; n < resource->voltd_count; n++)
			if (n == scmi_id)
				return &resource->voltd[n];
	}

	return NULL;
}

size_t plat_scmi_voltd_count(unsigned int agent_id)
{
	const struct scmi_agent_resources *resource = find_resource(agent_id);

	if (!resource)
		return 0;

	return resource->voltd_count;
}

const char *plat_scmi_voltd_get_name(unsigned int agent_id,
				     unsigned int scmi_id)
{
	struct stm32_scmi_voltd *voltd = find_voltd(agent_id, scmi_id);

	/* Currently non-secure is allowed to access all PWR regulators */
	if (!voltd)
		return NULL;

	return voltd->name;
}

static enum pwr_regulator pwr_scmi_to_regu_id(struct stm32_scmi_voltd *voltd)
{
	if (!strcmp(voltd->priv_id, PWR_REG11_NAME_ID))
		return PWR_REG11;
	if (!strcmp(voltd->priv_id, PWR_REG18_NAME_ID))
		return PWR_REG18;
	if (!strcmp(voltd->priv_id, PWR_USB33_NAME_ID))
		return PWR_USB33;

	panic();
}

static long pwr_get_level(struct stm32_scmi_voltd *voltd)
{
	enum pwr_regulator regu_id = pwr_scmi_to_regu_id(voltd);

	return (long)stm32mp1_pwr_regulator_mv(regu_id) * 1000;
}

static int32_t pwr_set_level(struct stm32_scmi_voltd *voltd, long level_uv)
{
	if (level_uv != pwr_get_level(voltd))
		return SCMI_INVALID_PARAMETERS;

	return SCMI_SUCCESS;
}

static int32_t pwr_describe_levels(struct stm32_scmi_voltd *voltd,
				   size_t start_index, long *microvolt,
				   size_t *nb_elts)
{
	if (start_index)
		return SCMI_INVALID_PARAMETERS;

	if (!microvolt) {
		*nb_elts = 1;
		return SCMI_SUCCESS;
	}

	if (*nb_elts < 1)
		return SCMI_GENERIC_ERROR;

	*nb_elts = 1;
	*microvolt = pwr_get_level(voltd);

	return SCMI_SUCCESS;
}

static uint32_t pwr_get_state(struct stm32_scmi_voltd *voltd)
{
	enum pwr_regulator regu_id = pwr_scmi_to_regu_id(voltd);

	if (stm32mp1_pwr_regulator_is_enabled(regu_id))
		return SCMI_VOLTAGE_DOMAIN_CONFIG_ARCH_ON;

	return SCMI_VOLTAGE_DOMAIN_CONFIG_ARCH_OFF;
}

static void pwr_set_state(struct stm32_scmi_voltd *voltd, bool enable)
{
	enum pwr_regulator regu_id = pwr_scmi_to_regu_id(voltd);

	DMSG("%sable PWR %s (was %s)", enable ? "En" : "Dis", voltd->name,
	     stm32mp1_pwr_regulator_is_enabled(regu_id) ? "on" : "off");

	stm32mp1_pwr_regulator_set_state(regu_id, enable);
}

static int32_t pmic_describe_levels(struct stm32_scmi_voltd *voltd,
				    size_t start_index, long *microvolt,
				    size_t *nb_elts)
{
	const uint16_t *levels = NULL;
	size_t full_count = 0;
	size_t out_count = 0;
	size_t i = 0;

	if (!stm32mp_nsec_can_access_pmic_regu(voltd->priv_id))
		return SCMI_DENIED;

	stpmic1_regulator_levels_mv(voltd->priv_id, &levels, &full_count);

	if (!microvolt) {
		*nb_elts = full_count - start_index;
		return SCMI_SUCCESS;
	}

	if (SUB_OVERFLOW(full_count, start_index, &out_count))
		return SCMI_GENERIC_ERROR;

	out_count = MIN(out_count, *nb_elts);

	FMSG("%zu levels: start %zu requested %zu output %zu",
	     full_count, start_index, *nb_elts, out_count);

	for (i = 0; i < out_count; i++)
		microvolt[i] = levels[start_index + i] * 1000;

	*nb_elts = out_count;

	return SCMI_SUCCESS;
}

static long pmic_get_level(struct stm32_scmi_voltd *voltd)
{
	unsigned long level_mv = 0;

	if (!stm32mp_nsec_can_access_pmic_regu(voltd->priv_id))
		return 0;

	stm32mp_get_pmic();
	level_mv = stpmic1_regulator_voltage_get(voltd->priv_id);
	stm32mp_put_pmic();

	return (long)level_mv * 1000;
}

static int32_t pmic_set_level(struct stm32_scmi_voltd *voltd, long level_uv)
{
	int rc = 0;
	unsigned int level_mv = 0;

	if (!stm32mp_nsec_can_access_pmic_regu(voltd->priv_id))
		return SCMI_DENIED;

	if (level_uv < 0 || level_uv > (UINT16_MAX * 1000))
		return SCMI_INVALID_PARAMETERS;

	level_mv = (unsigned int)level_uv / 1000;

	DMSG("Set STPMIC1 regulator %s level to %dmV", voltd->name, level_mv);

	stm32mp_get_pmic();
	rc = stpmic1_regulator_voltage_set(voltd->priv_id, level_mv);
	stm32mp_put_pmic();

	return rc ? SCMI_GENERIC_ERROR : SCMI_SUCCESS;
}

static uint32_t pmic_get_state(struct stm32_scmi_voltd *voltd)
{
	bool enabled = false;

	if (!stm32mp_nsec_can_access_pmic_regu(voltd->priv_id))
		return SCMI_VOLTAGE_DOMAIN_CONFIG_ARCH_OFF;

	stm32mp_get_pmic();
	enabled = stpmic1_is_regulator_enabled(voltd->priv_id);
	stm32mp_put_pmic();

	if (enabled)
		return SCMI_VOLTAGE_DOMAIN_CONFIG_ARCH_ON;

	return SCMI_VOLTAGE_DOMAIN_CONFIG_ARCH_OFF;
}

static int32_t pmic_set_state(struct stm32_scmi_voltd *voltd, bool enable)
{
	int rc = 0;

	if (!stm32mp_nsec_can_access_pmic_regu(voltd->priv_id))
		return SCMI_DENIED;

	stm32mp_get_pmic();

	DMSG("%sable STPMIC1 %s (was %s)", enable ? "En" : "Dis", voltd->name,
	     stpmic1_is_regulator_enabled(voltd->priv_id) ? "on" : "off");

	if (enable)
		rc = stpmic1_regulator_enable(voltd->priv_id);
	else
		rc = stpmic1_regulator_disable(voltd->priv_id);

	stm32mp_put_pmic();

	return rc ? SCMI_GENERIC_ERROR : SCMI_SUCCESS;
}

int32_t plat_scmi_voltd_levels_array(unsigned int agent_id,
				     unsigned int scmi_id, size_t start_index,
				     long *levels, size_t *nb_elts)

{
	struct stm32_scmi_voltd *voltd = find_voltd(agent_id, scmi_id);

	if (!voltd)
		return SCMI_NOT_FOUND;

	switch (voltd->priv_dev) {
	case VOLTD_PWR:
		return pwr_describe_levels(voltd, start_index, levels, nb_elts);
	case VOLTD_PMIC:
		return pmic_describe_levels(voltd, start_index, levels,
					    nb_elts);
	default:
		return SCMI_GENERIC_ERROR;
	}
}

long plat_scmi_voltd_get_level(unsigned int agent_id, unsigned int scmi_id)
{
	struct stm32_scmi_voltd *voltd = find_voltd(agent_id, scmi_id);

	if (!voltd)
		return 0;

	switch (voltd->priv_dev) {
	case VOLTD_PWR:
		return pwr_get_level(voltd);
	case VOLTD_PMIC:
		return pmic_get_level(voltd);
	default:
		panic();
	}
}

int32_t plat_scmi_voltd_set_level(unsigned int agent_id, unsigned int scmi_id,
				  long level)
{
	struct stm32_scmi_voltd *voltd = find_voltd(agent_id, scmi_id);

	if (!voltd)
		return SCMI_NOT_FOUND;

	switch (voltd->priv_dev) {
	case VOLTD_PWR:
		return pwr_set_level(voltd, level);
	case VOLTD_PMIC:
		return pmic_set_level(voltd, level);
	default:
		return SCMI_GENERIC_ERROR;
	}
}

int32_t plat_scmi_voltd_get_config(unsigned int agent_id, unsigned int scmi_id,
				   uint32_t *config)
{
	struct stm32_scmi_voltd *voltd = find_voltd(agent_id, scmi_id);

	if (!voltd)
		return SCMI_NOT_FOUND;

	switch (voltd->priv_dev) {
	case VOLTD_PWR:
		*config = pwr_get_state(voltd);
		break;
	case VOLTD_PMIC:
		*config = pmic_get_state(voltd);
		break;
	default:
		return SCMI_GENERIC_ERROR;
	}

	return SCMI_SUCCESS;
}

int32_t plat_scmi_voltd_set_config(unsigned int agent_id, unsigned int scmi_id,
				   uint32_t config)
{
	struct stm32_scmi_voltd *voltd = find_voltd(agent_id, scmi_id);
	int32_t rc = SCMI_SUCCESS;

	if (!voltd)
		return SCMI_NOT_FOUND;

	switch (voltd->priv_dev) {
	case VOLTD_PWR:
		pwr_set_state(voltd, config);
		break;
	case VOLTD_PMIC:
		rc = pmic_set_state(voltd, config);
		break;
	default:
		return SCMI_GENERIC_ERROR;
	}

	return rc;
}

/*
 * Initialize platform SCMI resources
 */
static TEE_Result stm32mp1_init_scmi_server(void)
{
	size_t i = 0;
	size_t j = 0;

	register_pm_driver_cb(stm32_scmi_pm, NULL);

	for (i = 0; i < ARRAY_SIZE(scmi_channel); i++) {
		struct scmi_msg_channel *chan = &scmi_channel[i];

		/* Enforce non-secure shm mapped as device memory */
		chan->shm_addr.va = (vaddr_t)phys_to_virt(chan->shm_addr.pa,
							  MEM_AREA_IO_NSEC);
		assert(chan->shm_addr.va);

		scmi_smt_init_agent_channel(chan);
	}

	for (i = 0; i < ARRAY_SIZE(agent_resources); i++) {
		const struct scmi_agent_resources *res = &agent_resources[i];

		for (j = 0; j < res->clock_count; j++) {
			struct stm32_scmi_clk *clk = &res->clock[j];

			if (!clk->name ||
			    strlen(clk->name) >= SCMI_CLOCK_NAME_SIZE)
				panic("SCMI clock name invalid");

			/* Sync SCMI clocks with their targeted initial state */
			if (clk->enabled &&
			    stm32mp_nsec_can_access_clock(clk->clock_id))
				clk_enable(clk->clock_id);
		}

		for (j = 0; j < res->rd_count; j++) {
			struct stm32_scmi_rd *rd = &res->rd[j];

			if (!rd->name ||
			    strlen(rd->name) >= SCMI_RD_NAME_SIZE)
				panic("SCMI reset domain name invalid");
		}

		for (j = 0; j < res->voltd_count; j++) {
			struct stm32_scmi_voltd *voltd = &res->voltd[j];

			if (!voltd->name ||
			    strlen(voltd->name) >= SCMI_VOLTD_NAME_SIZE)
				panic("SCMI voltage domain name invalid");
		}
	}

	return TEE_SUCCESS;
}

driver_init_late(stm32mp1_init_scmi_server);
