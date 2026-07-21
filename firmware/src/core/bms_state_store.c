#include "core/bms_state_store.h"

#include <string.h>

int bms_state_store_construct(BmsStateStore *store)
{
	if (store == NULL) {
		return -1;
	}

	memset(store, 0, sizeof(*store));
	return k_mutex_init(&store->lock);
}

/*
 * 六类状态的读写完全同构:写 = 拷贝 + 版本自增,读 = 拷贝。
 * 用宏生成,保证任何一类都不会漏掉加锁或版本处理。
 */
#define BMS_STATE_STORE_DEFINE_PAIR(name, type, field, version_field)              \
	int bms_state_store_write_##name(BmsStateStore *store, const type *value)  \
	{                                                                           \
		if ((store == NULL) || (value == NULL)) {                           \
			return -1;                                                  \
		}                                                                   \
		if (k_mutex_lock(&store->lock, K_FOREVER) != 0) {                   \
			return -1;                                                  \
		}                                                                   \
		store->field = *value;                                              \
		store->version_field++;                                             \
		store->field.version = store->version_field;                        \
		k_mutex_unlock(&store->lock);                                       \
		return 0;                                                           \
	}                                                                           \
	int bms_state_store_read_##name(BmsStateStore *store, type *out)           \
	{                                                                           \
		if ((store == NULL) || (out == NULL)) {                             \
			return -1;                                                  \
		}                                                                   \
		if (k_mutex_lock(&store->lock, K_FOREVER) != 0) {                   \
			return -1;                                                  \
		}                                                                   \
		*out = store->field;                                                \
		k_mutex_unlock(&store->lock);                                       \
		return 0;                                                           \
	}

BMS_STATE_STORE_DEFINE_PAIR(sample, BmsSampleSnapshot, latest_sample, sample_version)
BMS_STATE_STORE_DEFINE_PAIR(fault, BmsFaultState, latest_fault, fault_version)
BMS_STATE_STORE_DEFINE_PAIR(power, BmsPowerState, latest_power, power_version)
BMS_STATE_STORE_DEFINE_PAIR(energy, BmsEnergyState, latest_energy, energy_version)
BMS_STATE_STORE_DEFINE_PAIR(balance, BmsBalanceState, latest_balance, balance_version)
BMS_STATE_STORE_DEFINE_PAIR(selftest, BmsSelftestState, latest_selftest, selftest_version)
