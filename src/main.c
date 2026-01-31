/*
 * Copyright (c) 2019 Tobias Svehagen
 * Modified for Fixed Keys, Auto-Provisioning mode, and LED Status
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/drivers/gpio.h>

#define SW0_NODE    DT_ALIAS(sw0)
#define LED0_NODE   DT_ALIAS(led0) // Định nghĩa LED 1

/* --- ĐỊNH NGHĨA KHÓA CỐ ĐỊNH (FIXED KEYS) --- */
static const uint8_t fixed_net_key[16] = {
    0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
    0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef
};

static const uint8_t fixed_app_key[16] = {
    0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21,
    0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21
};

static const uint8_t fixed_dev_key[16] = {
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99
};

static const uint16_t net_idx = 0;
static const uint16_t app_idx = 0;
static uint16_t self_addr = 1, node_addr;
static const uint8_t dev_uuid[16] = { 0xdd, 0xdd };
static uint8_t node_uuid[16];

static bool provisioning_auto_mode = false;

/* Khai báo cấu trúc LED */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

K_SEM_DEFINE(sem_unprov_beacon, 0, 1);
K_SEM_DEFINE(sem_node_added, 0, 1);
#ifdef CONFIG_MESH_PROVISIONER_USE_SW0
K_SEM_DEFINE(sem_button_pressed, 0, 1);
#endif

static struct bt_mesh_cfg_cli cfg_cli = {};

static void health_current_status(struct bt_mesh_health_cli *cli, uint16_t addr,
				  uint8_t test_id, uint16_t cid, uint8_t *faults,
				  size_t fault_count)
{
	printk("Health Status from 0x%04x: %zu faults\n", addr, fault_count);
}

static struct bt_mesh_health_cli health_cli = {
	.current_status = health_current_status,
};

static const struct bt_mesh_model root_models[] = {
	BT_MESH_MODEL_CFG_SRV,
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
	BT_MESH_MODEL_HEALTH_CLI(&health_cli),
};

static const struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, root_models, BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp mesh_comp = {
	.cid = BT_COMP_ID_LF,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static void setup_cdb(void)
{
	struct bt_mesh_cdb_app_key *key;

	key = bt_mesh_cdb_app_key_get(app_idx);
	if (key) return;

	key = bt_mesh_cdb_app_key_alloc(net_idx, app_idx);
	if (key == NULL) return;

	bt_mesh_cdb_app_key_import(key, 0, fixed_app_key);
	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		bt_mesh_cdb_app_key_store(key);
	}
	printk("CDB initialized with FIXED AppKey.\n");
}

static void configure_self(struct bt_mesh_cdb_node *self)
{
	struct bt_mesh_cdb_app_key *key;
	uint8_t app_key[16], status = 0;

	printk("Configuring self...\n");
	key = bt_mesh_cdb_app_key_get(app_idx);
	bt_mesh_cdb_app_key_export(key, 0, app_key);

	bt_mesh_cfg_cli_app_key_add(self->net_idx, self->addr, self->net_idx, app_idx, app_key, &status);
	if (status) {
		printk("Self: AppKey add failed (0x%02x)\n", status);
	} else {
		printk("Self: AppKey added\n");
	}

	bt_mesh_cfg_cli_mod_app_bind(self->net_idx, self->addr, self->addr, app_idx, BT_MESH_MODEL_ID_HEALTH_CLI, &status);
	if (status) {
		printk("Self: Health Bind failed (0x%02x)\n", status);
	} else {
		printk("Self: Health Bound\n");
	}

	atomic_set_bit(self->flags, BT_MESH_CDB_NODE_CONFIGURED);
	if (IS_ENABLED(CONFIG_BT_SETTINGS)) bt_mesh_cdb_node_store(self);
	printk("Self Configuration Complete\n");
}

static void configure_node(struct bt_mesh_cdb_node *node)
{
	NET_BUF_SIMPLE_DEFINE(buf, BT_MESH_RX_SDU_MAX);
	struct bt_mesh_comp_p0_elem elem;
	struct bt_mesh_cdb_app_key *key;
	uint8_t app_key[16], status;
	struct bt_mesh_comp_p0 comp;
	int elem_addr;

	printk("Configuring node 0x%04x...\n", node->addr);
	key = bt_mesh_cdb_app_key_get(app_idx);
	bt_mesh_cdb_app_key_export(key, 0, app_key);

	bt_mesh_cfg_cli_app_key_add(net_idx, node->addr, net_idx, app_idx, app_key, &status);
	if (status) {
		printk("Failed to add AppKey to 0x%04x (0x%02x)\n", node->addr, status);
	} else {
		printk("AppKey added to 0x%04x\n", node->addr);
	}

	bt_mesh_cfg_cli_comp_data_get(net_idx, node->addr, 0, &status, &buf);
	bt_mesh_comp_p0_get(&comp, &buf);

	elem_addr = node->addr;
	while (bt_mesh_comp_p0_elem_pull(&comp, &elem)) {
		for (int i = 0; i < elem.nsig; i++) {
			uint16_t id = bt_mesh_comp_p0_elem_mod(&elem, i);
			if (id == BT_MESH_MODEL_ID_CFG_CLI || id == BT_MESH_MODEL_ID_CFG_SRV) continue;
			bt_mesh_cfg_cli_mod_app_bind(net_idx, node->addr, elem_addr, app_idx, id, &status);
			if (status) {
				printk("Failed to bind model 0x%04x at 0x%04x (0x%02x)\n", id, elem_addr, status);
			} else {
				printk("Bound AppKey to model 0x%04x at 0x%04x\n", id, elem_addr);
			}
		}
		elem_addr++;
	}
	atomic_set_bit(node->flags, BT_MESH_CDB_NODE_CONFIGURED);
	if (IS_ENABLED(CONFIG_BT_SETTINGS)) bt_mesh_cdb_node_store(node);
	printk("Configuration complete for 0x%04x\n", node->addr);
}

static void unprovisioned_beacon(uint8_t uuid[16], bt_mesh_prov_oob_info_t oob_info, uint32_t *uri_hash)
{
	memcpy(node_uuid, uuid, 16);
	k_sem_give(&sem_unprov_beacon);
}

static void node_added(uint16_t idx, uint8_t uuid[16], uint16_t addr, uint8_t num_elem)
{
	node_addr = addr;
	k_sem_give(&sem_node_added);
}

static const struct bt_mesh_prov prov = {
	.uuid = dev_uuid,
	.unprovisioned_beacon = unprovisioned_beacon,
	.node_added = node_added,
};

static int bt_ready(void)
{
	int err;
	err = bt_mesh_init(&prov, &mesh_comp);
	if (IS_ENABLED(CONFIG_BT_SETTINGS)) settings_load();

	err = bt_mesh_cdb_create(fixed_net_key);
	if (err != -EALREADY) setup_cdb();

	bt_mesh_provision(fixed_net_key, BT_MESH_NET_PRIMARY, 0, 0, self_addr, fixed_dev_key);
	return 0;
}

static uint8_t check_unconfigured(struct bt_mesh_cdb_node *node, void *data)
{
	if (!atomic_test_bit(node->flags, BT_MESH_CDB_NODE_CONFIGURED)) {
		if (node->addr == self_addr) configure_self(node);
		else configure_node(node);
	}
	return BT_MESH_CDB_ITER_CONTINUE;
}

static void led_init(void)
{
	if (!gpio_is_ready_dt(&led)) return;
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

#ifdef CONFIG_MESH_PROVISIONER_USE_SW0
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static struct gpio_callback button_cb_data;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	provisioning_auto_mode = !provisioning_auto_mode;
	
	/* LED sáng khi bật Auto Mode, tắt khi OFF */
	gpio_pin_set_dt(&led, (int)provisioning_auto_mode);
	
	printk("\n--- AUTO MODE: %s ---\n\n", provisioning_auto_mode ? "ON" : "OFF");
	k_sem_give(&sem_button_pressed);
}

static void button_init(void)
{
	gpio_pin_configure_dt(&button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
}
#endif

int main(void)
{
	char uuid_hex_str[32 + 1];
	int err;

	led_init();
	printk("--- STARTING PROVISIONER (FIXED KEYS) ---\n");

	bt_enable(NULL);
	bt_ready();

#ifdef CONFIG_MESH_PROVISIONER_USE_SW0
	button_init();
#endif

	while (1) {
		k_sem_reset(&sem_unprov_beacon);
		k_sem_reset(&sem_node_added);
		bt_mesh_cdb_node_foreach(check_unconfigured, NULL);

		err = k_sem_take(&sem_unprov_beacon, K_SECONDS(5));
		if (err == -EAGAIN) continue;

		bin2hex(node_uuid, 16, uuid_hex_str, sizeof(uuid_hex_str));

		if (!provisioning_auto_mode) {
			printk("Detected %s. PRESS SW0 to provision.\n", uuid_hex_str);
			k_sem_reset(&sem_button_pressed);
			if (k_sem_take(&sem_button_pressed, K_SECONDS(30)) == -EAGAIN) continue;
		}

		printk("Provisioning %s...\n", uuid_hex_str);
		if (bt_mesh_provision_adv(node_uuid, net_idx, 0, 0) < 0) continue;

		k_sem_take(&sem_node_added, K_SECONDS(10));
		printk("Added 0x%04x success.\n", node_addr);
	}
	return 0;
}