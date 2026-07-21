#ifndef BMS_COMMAND_H
#define BMS_COMMAND_H

#include <stdint.h>

/*
 * 外部命令(CAN RX -> cmd_q -> 各任务)。
 *
 * vendor CAN 协议只定义了 TX 广播,没有 RX 命令;
 * 这里是项目自定义扩展,帧格式见 bms_protocol_codec.h。
 */
typedef enum BmsCommandType {
	BMS_CMD_NONE = 0,
	BMS_CMD_CLEAR_FAULT = 1,
	BMS_CMD_ENTER_OTA = 2, /* 进 CAN OTA:先关双 FET 确认,再置 BKP 标志复位到 Bootloader */
} BmsCommandType;

typedef struct BmsCommand {
	BmsCommandType type;
	uint32_t arg;
} BmsCommand;

#endif
