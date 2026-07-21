cmake_minimum_required(VERSION 3.20.0)

# 项目自带 devicetree binding(dts/bindings/afe/*.yaml)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(FlexLoVoBMS)

# 头文件与源文件同住各层文件夹(模块=一个文件夹)。include 根 = src + drivers,
# 于是每行 #include 都带层前缀(如 "afe/bms_afe.h"、"sensor/bms_ntc.h"、"algo/...")
# ——依赖关系写在脸上:打开任一 .c 的 include 列表就是它"依赖哪些层"的地图。
target_include_directories(app PRIVATE src drivers)

target_sources(app PRIVATE
    src/main.c
    # 应用层 - 纯函数算法/策略(可主机侧单测)
    src/algo/bms_protection_policy.c
    src/algo/bms_charge_policy.c
    src/algo/bms_balance_policy.c
    src/algo/bms_energy_estimator.c
    src/algo/bms_protocol_codec.c
    src/algo/bms_diag_format.c
    # 应用层 - 任务编排
    src/tasks/sample_task.c
    src/tasks/fault_protection_task.c
    src/tasks/charge_state_task.c
    src/tasks/balance_task.c
    src/tasks/soc_energy_task.c
    src/tasks/can_comm_task.c
    src/tasks/comm_diag_task.c
    src/tasks/auxiliary_task.c
    # 应用层 - 共享设施(状态仓库/心跳/装配根共享对象)
    src/core/bms_state_store.c
    src/core/bms_heartbeat.c
    src/core/app_objects.c
    src/core/bms_ota_boot.c
    # 设备驱动层 - AFE 接口(dispatch,vtable 派发,自身不碰硬件)+ 具体设备,同住 drivers/afe/
    drivers/afe/bms_afe.c
    # 设备驱动层 - BQ769x0 家族核 + 每芯片薄驱动(认领 compatible)
    drivers/afe/bq769x0_core.c
    drivers/afe/bms_bq76940.c
    drivers/afe/bms_bq76930.c
    # 设备驱动层 - BQ79616-Q1 车规局链骨架(未写完,见 TODO(bq79616))
    drivers/afe/bms_bq79616.c
    # 传感器层 - NTC 热敏电阻 mV->温度换算(与 AFE 芯片解耦)
    drivers/sensor/bms_ntc.c
    # 指示层 - 状态灯(GPIO 闪烁 / PWM 呼吸,编译期二选一)
    drivers/indicator/bms_status_led.c
)

if(CONFIG_BMS_TEST_TELEMETRY)
    target_sources(app PRIVATE src/core/bms_test_telemetry.c)
endif()

# ==========================================================================
# 依赖注入机制——第 0 层（构建系统侧）：登记自定义 linker 段
# ==========================================================================
#
# zephyr_linker_sources 将 app_task_iterable.ld 合并到最终链接脚本中，
# 从而登记 app_task_def 这个 iterable section。
#
# 少了这一行，链接器不认识 app_task_def 段，STRUCT_SECTION_FOREACH 的
# _start / _end 符号未定义，直接链接失败。
#
# app_task_iterable.ld 内部用 ITERABLE_SECTION_RAM 宏（Zephyr 提供），
# 效果等价于手写：
#     .app_task_def : {
#         _app_task_def_list_start = .;
#         KEEP(*(SORT_BY_NAME(.app_task_def.*)));
#         _app_task_def_list_end = .;
#     } > RAM
#
# 而 APP_TASK_REGISTER 宏中的 STRUCT_SECTION_ITERABLE 将每个任务条目
# 放入 `.app_task_def.<name>` 段中，链接器自动收集并排序。
zephyr_linker_sources(DATA_SECTIONS app_task_iterable.ld)
