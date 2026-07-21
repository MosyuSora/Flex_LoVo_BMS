#ifndef BMS_STATE_STORE_H
#define BMS_STATE_STORE_H

#include "core/bms_balance_state.h"
#include "core/bms_energy_state.h"
#include "core/bms_fault_state.h"
#include "core/bms_power_state.h"
#include "core/bms_sample_snapshot.h"
#include "core/bms_selftest_state.h"

#include <stdint.h>
#include <zephyr/kernel.h>

/*
 * 应用任务共享的中心状态仓库:每类状态保留最新快照,任务之间不直接互调。
 *
 * 每类状态有独立的版本计数器,写入时自动 +1;消费者比较版本号判断
 * 是否已处理过这份数据。仓库只保证一致性,不做任何业务判断。
 */
typedef struct BmsStateStore {
	struct k_mutex lock;
	BmsSampleSnapshot latest_sample;
	BmsFaultState latest_fault;
	BmsPowerState latest_power;
	BmsEnergyState latest_energy;
	BmsBalanceState latest_balance;
	BmsSelftestState latest_selftest;
	uint32_t sample_version;
	uint32_t fault_version;
	uint32_t power_version;
	uint32_t energy_version;
	uint32_t balance_version;
	uint32_t selftest_version;
} BmsStateStore;

int bms_state_store_construct(BmsStateStore *store);

int bms_state_store_write_sample(BmsStateStore *store, const BmsSampleSnapshot *sample);
int bms_state_store_read_sample(BmsStateStore *store, BmsSampleSnapshot *out);
int bms_state_store_write_fault(BmsStateStore *store, const BmsFaultState *fault);
int bms_state_store_read_fault(BmsStateStore *store, BmsFaultState *out);
int bms_state_store_write_power(BmsStateStore *store, const BmsPowerState *power);
int bms_state_store_read_power(BmsStateStore *store, BmsPowerState *out);
int bms_state_store_write_energy(BmsStateStore *store, const BmsEnergyState *energy);
int bms_state_store_read_energy(BmsStateStore *store, BmsEnergyState *out);
int bms_state_store_write_balance(BmsStateStore *store, const BmsBalanceState *balance);
int bms_state_store_read_balance(BmsStateStore *store, BmsBalanceState *out);
int bms_state_store_write_selftest(BmsStateStore *store, const BmsSelftestState *selftest);
int bms_state_store_read_selftest(BmsStateStore *store, BmsSelftestState *out);

#endif
