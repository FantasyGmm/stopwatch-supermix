import importlib.util
import json
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / "scripts" / "report_firmware_metrics.py"


def load_metrics_module():
    spec = importlib.util.spec_from_file_location("report_firmware_metrics", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReportFirmwareMetricsTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp_dir.name)
        self.build = self.root / "build"
        self.build.mkdir()

        (self.root / "partitions.csv").write_text(
            "# Name, Type, SubType, Offset, Size, Flags\n"
            "nvs,data,nvs,0x9000,0x4000,\n"
            "ota_0,app,ota_0,0x20000,0x4f0000,\n"
            "ota_1,app,ota_1,,0x4f0000,\n",
            encoding="utf-8",
        )
        (self.build / "project_description.json").write_text(
            json.dumps(
                {
                    "project_name": "StopWatch-UserDemo",
                    "project_version": "test-sha",
                    "target": "esp32s3",
                    "build_dir": str(self.build),
                    "app_elf": "StopWatch-UserDemo.elf",
                    "app_bin": "StopWatch-UserDemo.bin",
                }
            ),
            encoding="utf-8",
        )
        (self.build / "StopWatch-UserDemo.bin").write_bytes(b"a" * 100)
        (self.build / "StopWatch-UserDemo.map").write_text(
            "                0x40378000 _diram_i_start = 0x40378000\n"
            ".iram0.text     0x40374404    0x1c52f\n"
            ".dram0.data     0x3fca0a00     0x5b7c\n"
            ".dram0.bss      0x3fca6580     0x8f18\n",
            encoding="utf-8",
        )
        self.assistant_bin = self.root / "xiaozhi.bin"
        self.assistant_bin.write_bytes(b"b" * 40)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_collects_binary_partition_and_diram_metrics(self):
        metrics_module = load_metrics_module()

        metrics = metrics_module.collect_metrics(
            repo_root=self.root,
            build_dir=self.build,
            partition_table=self.root / "partitions.csv",
            assistant_bin=self.assistant_bin,
            source="launcher-baseline",
        )

        self.assertEqual(metrics["app_bytes"], 100)
        self.assertEqual(metrics["app_partition_bytes"], 0x4F0000)
        self.assertEqual(metrics["app_headroom_bytes"], 0x4F0000 - 100)
        self.assertEqual(metrics["diram_bytes"], 160711)
        self.assertEqual(metrics["assistant_app_bytes"], 40)
        self.assertEqual(metrics["combined_app_bytes"], 140)
        self.assertEqual(metrics["build_project_version"], "test-sha")
        self.assertEqual(metrics["source_git_commit"], "test-sha")
        self.assertEqual(metrics["source"], "launcher-baseline")

    def test_missing_required_artifact_is_an_error(self):
        metrics_module = load_metrics_module()
        (self.build / "StopWatch-UserDemo.map").unlink()

        with self.assertRaisesRegex(
            metrics_module.MetricsError, "required build artifact is missing"
        ):
            metrics_module.collect_metrics(
                repo_root=self.root,
                build_dir=self.build,
                partition_table=self.root / "partitions.csv",
                assistant_bin=self.assistant_bin,
                source="launcher-baseline",
            )


if __name__ == "__main__":
    unittest.main()
