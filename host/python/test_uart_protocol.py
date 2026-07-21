import unittest

from uart_protocol import cell_map, parse_afe_reg_line, parse_snap_line
from uart_upper import EventRingBuffer


SAMPLE = (
    "SNAP version=2 pack_mv=32855 "
    "cells_mv=[3650,3651,3651,3648,3653,3650,3651,3651,3650] "
    "current_ma=-1200 temp_cdeg=[2503,2503] temp_valid=3 flags=0 "
    "soc_0p1=348 soh_0p1=1000 conf_0p1=600 sop_chg_ma=10000 "
    "sop_dsg_ma=20000 fault_bits=1 afe_status=0 bal_mask=0 "
    "pwr_mode=3 chg=0 dsg=1 hb_stale=0"
)


class UartProtocolTests(unittest.TestCase):
    def test_event_ring_buffer_drops_oldest_item(self):
        events = EventRingBuffer(maxlen=2)
        events.put(("line", "one"))
        events.put(("line", "two"))
        events.put(("line", "three"))
        self.assertEqual(events.drain(10), [("line", "two"), ("line", "three")])
        self.assertEqual(events.take_dropped(), 1)

    def test_snapshot_fields_and_units(self):
        snapshot = parse_snap_line(SAMPLE)
        self.assertIsNotNone(snapshot)
        assert snapshot is not None
        self.assertEqual(snapshot["pack_mv"], 32855)
        self.assertEqual(snapshot["pack_v"], 32.855)
        self.assertEqual(snapshot["current_a"], -1.2)
        self.assertEqual(snapshot["temp_c"], [25.03, 25.03])
        self.assertEqual(snapshot["soc_percent"], 34.8)
        self.assertEqual(snapshot["fault_names"], ["单体过压"])
        self.assertEqual(snapshot["power_name"], "故障")

    def test_non_snapshot_is_ignored(self):
        self.assertIsNone(parse_snap_line("BMS UART diag ready. Send S for snapshot."))

    def test_nine_cell_slots_are_placed_on_shipping_channels(self):
        mapped = cell_map([3650, 3651, 3651, 3648, 3653, 3650, 3651, 3651, 3650])
        self.assertEqual(mapped[1], 3650)
        self.assertEqual(mapped[5], 3651)
        self.assertEqual(mapped[15], 3650)
        self.assertIsNone(mapped[3])

    def test_afe_register_line_is_decoded_as_hex(self):
        self.assertEqual(parse_afe_reg_line("AFE_REG addr=05 value=43"), (0x05, 0x43))
        self.assertIsNone(parse_afe_reg_line("SNAP version=2"))


if __name__ == "__main__":
    unittest.main()
