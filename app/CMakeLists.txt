#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(serial_modem)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/sm_util.c)
target_sources(app PRIVATE src/sm_ctrl_pin.c)
target_sources(app PRIVATE src/sm_settings.c)
target_sources(app PRIVATE src/sm_at_host.c)
target_sources(app PRIVATE src/sm_at_commands.c)
target_sources(app PRIVATE src/sm_at_socket.c)
target_sources(app PRIVATE src/sm_at_tcp_proxy.c)
target_sources(app PRIVATE src/sm_at_udp_proxy.c)
target_sources(app PRIVATE src/sm_at_icmp.c)
target_sources(app PRIVATE src/sm_at_fota.c)
target_sources(app PRIVATE src/sm_uart_handler.c)
# NORDIC SDK APP END
target_sources_ifdef(CONFIG_SM_SMS app PRIVATE src/sm_at_sms.c)
target_sources_ifdef(CONFIG_SM_PPP app PRIVATE src/sm_ppp.c)
target_sources_ifdef(CONFIG_SM_CMUX app PRIVATE src/sm_cmux.c)

add_subdirectory_ifdef(CONFIG_SM_GNSS src/gnss)
add_subdirectory_ifdef(CONFIG_SM_NRF_CLOUD src/nrfcloud)
add_subdirectory_ifdef(CONFIG_SM_MQTTC src/mqtt_c)
add_subdirectory_ifdef(CONFIG_SM_HTTPC src/http_c)
add_subdirectory_ifdef(CONFIG_SM_TWI src/twi)
add_subdirectory_ifdef(CONFIG_SM_GPIO src/gpio)
add_subdirectory_ifdef(CONFIG_SM_CARRIER src/lwm2m_carrier)
add_subdirectory_ifdef(CONFIG_SM_NATIVE_TLS src/nativetls)
if (CONFIG_SM_FTPC OR CONFIG_SM_TFTPC)
        add_subdirectory(src/ftp_c)
endif()

zephyr_include_directories(src)
