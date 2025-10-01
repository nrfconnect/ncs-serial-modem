/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include "sm_util.h"
#include "sm_at_gpio.h"
#include "sm_at_host.h"

LOG_MODULE_REGISTER(sm_gpio, CONFIG_SM_LOG_LEVEL);

static const struct device *gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static sys_slist_t sm_gpios = SYS_SLIST_STATIC_INIT(&sm_gpios);

struct sm_gpio_pin_node {
	sys_snode_t node;
	gpio_pin_t pin;
	uint16_t op;
};

#define MAX_GPIO_PIN 31

/* Regular GPIO */
#define SM_GPIOC_OP_DISABLE 0     /* Disables pin for both input and output. */
#define SM_GPIOC_OP_OUT     1     /* Enables pin as output. */
#define SM_GPIOC_OP_IN_PU   21    /* Enables pin as input. Use internal pull up resistor. */
#define SM_GPIOC_OP_IN_PD   22    /* Enables pin as input. Use internal pull down resistor. */

/**@brief GPIO operations. */
enum sm_gpio_operations {
	SM_GPIO_OP_WRITE,
	SM_GPIO_OP_READ,
	SM_GPIO_OP_TOGGLE
};

static gpio_flags_t convert_flags(uint16_t op)
{
	gpio_flags_t gpio_flags = UINT32_MAX;

	switch (op) {
	case SM_GPIOC_OP_DISABLE:
		gpio_flags = GPIO_DISCONNECTED;
		break;
	case SM_GPIOC_OP_OUT:
		gpio_flags = GPIO_OUTPUT;
		break;
	case SM_GPIOC_OP_IN_PU:
		gpio_flags = GPIO_INPUT | GPIO_PULL_UP;
		break;
	case SM_GPIOC_OP_IN_PD:
		gpio_flags = GPIO_INPUT | GPIO_PULL_DOWN;
		break;
	default:
		LOG_ERR("Fail to convert gpio flag");
		break;
	}

	return gpio_flags;
}

static int do_gpio_pin_configure_set(uint16_t op, gpio_pin_t pin)
{
	int err = 0;
	gpio_flags_t gpio_flags = 0;
	struct sm_gpio_pin_node *sm_gpio_pin = NULL, *cur = NULL, *next = NULL;
	gpio_port_pins_t pin_mask = 0;

	LOG_DBG("op:%hu pin:%hu", op, pin);

	/* Verify pin correctness */
	if (pin > MAX_GPIO_PIN) {
		LOG_ERR("Incorrect <pin>: %d", pin);
		return -EINVAL;
	}

	/* Convert Serial Modem GPIO flag to zephyr gpio pin configuration flag */
	gpio_flags = convert_flags(op);
	if (gpio_flags == UINT32_MAX) {
		LOG_ERR("Fail to configure pin.");
		return -EINVAL;
	}

	err = gpio_pin_configure(gpio_dev, pin, gpio_flags);
	if (err) {
		LOG_ERR("GPIO_0 config error: %d", err);
		return err;
	}

	/* Trace gpio list */
	if (sys_slist_peek_head(&sm_gpios) != NULL) {
		SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&sm_gpios, cur, next, node) {
			if (cur->pin == pin) {
				sm_gpio_pin = cur;
			}
		}
	}

	/* Add GPIO node if node does not exist */
	if (sm_gpio_pin == NULL) {
		sm_gpio_pin = (struct sm_gpio_pin_node *)
							k_malloc(sizeof(struct sm_gpio_pin_node));
		if (sm_gpio_pin == NULL) {
			return -ENOBUFS;
		}
		memset(sm_gpio_pin, 0, sizeof(struct sm_gpio_pin_node));
		sys_slist_append(&sm_gpios, &sm_gpio_pin->node);
	}

	sm_gpio_pin->pin = pin;
	sm_gpio_pin->op = op;

	if (op == SM_GPIOC_OP_DISABLE) {
		/* Disable interrupt */
		err = gpio_pin_interrupt_configure(gpio_dev, pin, GPIO_INT_DISABLE);
		if (err) {
			LOG_ERR("Interface pin interrupt config error: %d", err);
			return err;
		}
		/* Remove callback */
		pin_mask &= ~BIT(pin);
		/* Remove node in list */
		sys_slist_find_and_remove(&sm_gpios, &sm_gpio_pin->node);
		k_free(sm_gpio_pin);
	}

	return err;
}

static int do_gpio_pin_configure_read(void)
{
	int err = 0;
	struct sm_gpio_pin_node *cur = NULL, *next = NULL;

	rsp_send("\r\n#XGPIOCFG\r\n");

	if (sys_slist_peek_head(&sm_gpios) != NULL) {
		SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&sm_gpios, cur,
						  next, node) {
			if (cur) {
				LOG_DBG("%hu,%hu", cur->op, cur->pin);
				rsp_send("%hu,%hu\r\n", cur->op, cur->pin);
			}
		}
	}

	return err;
}

static int do_gpio_pin_operate(uint16_t op, gpio_pin_t pin, uint16_t value)
{
	int ret = 0;
	struct sm_gpio_pin_node *cur = NULL, *next = NULL;

	if (sys_slist_peek_head(&sm_gpios) != NULL) {
		SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&sm_gpios, cur, next, node) {
			if (cur) {
				if (cur->pin != pin) {
					continue;
				}
				if (op == SM_GPIO_OP_WRITE) {
					LOG_DBG("Write pin: %d with value: %d", cur->pin, value);
					ret = gpio_pin_set(gpio_dev, pin, value);
					if (ret < 0) {
						LOG_ERR("Cannot write gpio");
						return ret;
					}
				} else if (op == SM_GPIO_OP_READ) {
					ret = gpio_pin_get(gpio_dev, pin);
					if (ret < 0) {
						LOG_ERR("Cannot read gpio high");
						return ret;
					}
					LOG_DBG("Read value: %d", ret);
					rsp_send("\r\n#XGPIO: %d,%d\r\n", pin, ret);
				} else if (op == SM_GPIO_OP_TOGGLE) {
					LOG_DBG("Toggle pin: %d", cur->pin);
					ret = gpio_pin_toggle(gpio_dev, pin);
					if (ret < 0) {
						LOG_ERR("Cannot toggle gpio");
						return ret;
					}
				}
			}
		}
	}

	return 0;
}

SM_AT_CMD_CUSTOM(xgpiocfg, "AT#XGPIOCFG", handle_at_gpio_configure);
static int handle_at_gpio_configure(enum at_parser_cmd_type cmd_type,
				    struct at_parser *parser, uint32_t)
{
	int err = -EINVAL;
	uint16_t pin = 0xff, op = 0xff;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &op);
		if (err < 0) {
			LOG_ERR("Fail to get op: %d", err);
			return err;
		}
		err = at_parser_num_get(parser, 2, &pin);
		if (err < 0) {
			LOG_ERR("Fail to get pin: %d", err);
			return err;
		}
		err = do_gpio_pin_configure_set(op, (gpio_pin_t)pin);
		break;
	case AT_PARSER_CMD_TYPE_READ:
		err = do_gpio_pin_configure_read();
		break;

	default:
		break;
	}

	return err;
}

SM_AT_CMD_CUSTOM(xgpio, "AT#XGPIO", handle_at_gpio_operate);
static int handle_at_gpio_operate(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
				  uint32_t)
{
	int err = -EINVAL;
	uint16_t pin = 0xff, op = 0xff, value = 0xff;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		err = at_parser_num_get(parser, 1, &op);
		if (err < 0) {
			LOG_ERR("Fail to get OP code: %d", err);
			return err;
		}
		if (op > SM_GPIO_OP_TOGGLE) {
			LOG_ERR("GPIO OP code is out of range: %d", op);
			return -EINVAL;
		}
		err = at_parser_num_get(parser, 2, &pin);
		if (err < 0) {
			LOG_ERR("Fail to get pin: %d", err);
			return err;
		}
		if (pin > MAX_GPIO_PIN) {
			LOG_ERR("Incorrect <pin>: %d", pin);
			return -EINVAL;
		}
		if (op == SM_GPIO_OP_WRITE) {
			err = at_parser_num_get(parser, 3, &value);
			if (err < 0) {
				LOG_ERR("Fail to get value: %d", err);
				return err;
			}
			if (value != 1 && value != 0) {
				LOG_ERR("Fail to set gpio value: %d", value);
				return -EINVAL;
			}
		}
		err = do_gpio_pin_operate(op, (gpio_pin_t)pin, value);
		break;

	default:
		break;
	}
	return err;
}

int sm_at_gpio_init(void)
{
	if (!device_is_ready(gpio_dev)) {
		LOG_ERR("GPIO controller not ready");
		return -ENODEV;
	}

	return 0;
}

int sm_at_gpio_uninit(void)
{
	return 0;
}
