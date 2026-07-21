import unittest

from virtual_bms_upper import parse_frames


class VirtualBmsUpperTest(unittest.TestCase):
    def test_parses_current_can_tx_record(self):
        output = 'BMS_CAN_TX {"id":1,"dlc":8,"data":"aa 01 00 01 00 02 00 03","result":0}'

        frames = parse_frames(output)

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["id"], "0x0001")
        self.assertEqual(frames[0]["data_bytes"], [0xAA, 0x01, 0, 1, 0, 2, 0, 3])

    def test_parses_legacy_can_frame_record(self):
        output = 'BMS_CAN_FRAME {"id":"0x0007","dlc":8,"data":"aa 07 19 01 01 00 00 00"}'

        frames = parse_frames(output)

        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]["id"], "0x0007")

    def test_ignores_failed_or_interleaved_records(self):
        output = "\n".join(
            [
                'BMS_CAN_TX {"id":1,"dlc":8,"data":"aa 01 00 01 00 02 00 03","result":-5}',
                'BMS_CAN_TX {"id":2,"dlc":8,"data":"aa 02 BMS_WDT_STATE',
            ]
        )

        self.assertEqual(parse_frames(output), [])


if __name__ == "__main__":
    unittest.main()
