import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from size_report import compare, main, measure, parse_size


class SizeReportTests(unittest.TestCase):
    def report(self):
        return {"schema": 1, "configuration": {"profile": "gc"},
                "sizes": {"text": 100, "data": 20, "bss": 30, "text_data": 120}}

    def test_parse(self):
        self.assertEqual(parse_size("text data bss dec hex filename\n100 20 30 150 96 a.elf")
                         ["text_data"], 120)

    def test_reject_multiple_or_invalid_inputs(self):
        for text in ("", "text data bss\n1 2 3\n4 5 6", "text data bss\n-1 2 3"):
            with self.assertRaises(ValueError):
                parse_size(text)

    def test_difference_and_budget(self):
        before = self.report()
        after = copy.deepcopy(before)
        after["sizes"]["text"] += 7
        after["sizes"]["text_data"] += 7
        table, failed = compare(before, after, 6)
        self.assertIn("| text_data | 120 | 127 | +7 |", table)
        self.assertTrue(failed)
        self.assertFalse(compare(before, after, 7)[1])
        self.assertFalse(compare(before, after)[1])
        self.assertFalse(compare(after, before, 0)[1])

    def test_configuration_mismatch(self):
        before = self.report()
        after = copy.deepcopy(before)
        after["configuration"]["profile"] = "lto-gc"
        with self.assertRaises(ValueError):
            compare(before, after)

    def test_build_refuses_to_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("# placeholder")
            marker = root / "keep.json"
            marker.write_text("keep")
            self.assertEqual(main(["build", "--source", directory, "--output", directory]), 2)
            self.assertEqual(marker.read_text(), "keep")

    def test_schema_mismatch(self):
        after = self.report()
        after["schema"] = 2
        with self.assertRaises(ValueError):
            compare(self.report(), after)

    def test_panic_configuration_mismatch(self):
        before, after = self.report(), self.report()
        before["configuration"]["panic_diagnostics"] = "on"
        after["configuration"]["panic_diagnostics"] = "off"
        with self.assertRaises(ValueError):
            compare(before, after)

    def test_old_engine_cannot_claim_diagnostics_disabled(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text("# old engine")
            output = root / "output"

            def configure(*args, **kwargs):
                flags = output / "plain/engine/CMakeFiles/v4engine.dir/flags.make"
                flags.parent.mkdir(parents=True)
                flags.write_text("CXX_DEFINES =\n")

            with patch("size_report.subprocess.run", side_effect=configure) as mocked:
                self.assertEqual(main(["build", "--source", str(root), "--output", str(output),
                                       "--panic-diagnostics", "off"]), 2)
                self.assertEqual(mocked.call_count, 1)

    def test_elf_only(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input"
            for content in (b"!<arch>\n", b"raw binary", b"\x7fELF" + bytes(20)):
                path.write_bytes(content)
                with self.assertRaises(ValueError):
                    measure(path, "size", {})

    def test_external_elf_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.elf"
            header = bytearray(64)
            header[:6] = b"\x7fELF\x01\x01"
            header[16:20] = b"\x02\x00\xf3\x00"  # Executable, RISC-V
            path.write_bytes(header)
            with patch("size_report.run", side_effect=["GNU size 2.40", "text data bss\n10 2 3"]):
                report = measure(path, "riscv-size", {"identity": "board-config"})
            self.assertEqual(report["configuration"]["machine"], 243)
            self.assertEqual(report["sizes"]["text_data"], 12)
            header[16] = 1  # Relocatable object is not a final binary.
            path.write_bytes(header)
            with self.assertRaises(ValueError):
                measure(path, "size", {})

    def test_missing_file_returns_error(self):
        self.assertEqual(main(["compare", "/nonexistent/before.json", "/nonexistent/after.json"]), 2)


if __name__ == "__main__":
    unittest.main()
