import hashlib
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSET_SCRIPT_SHA256 = (
    "b0fd24a9f2be39e94ecfaf25cac8a5f53f77a5dcc0d9ea58167c193357905338"
)
FONT_SHA256 = "0684c8dbe08fe5ab374037b222fcc1376c28290146828baa67cf353ac465eac1"


def read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def sha256(relative_path):
    return hashlib.sha256((ROOT / relative_path).read_bytes()).hexdigest()


class ReproducibleDependencyFirmwareContractTests(unittest.TestCase):
    def test_tracked_xiaozhi_inputs_match_verified_sources(self):
        self.assertEqual(
            sha256("tools/xiaozhi/build_default_assets.py"), ASSET_SCRIPT_SHA256
        )
        self.assertEqual(
            sha256("main/assets/fonts/font_puhui_14_1.c"), FONT_SHA256
        )

    def test_cmake_uses_only_tracked_xiaozhi_inputs(self):
        main_cmake = read("main/CMakeLists.txt")
        assets_cmake = read("main/cmake/unified_assets.cmake")

        self.assertIn("assets/fonts/font_puhui_14_1.c", main_cmake)
        self.assertIn("tools/xiaozhi/build_default_assets.py", assets_cmake)
        self.assertNotIn("third_party/xiaozhi-esp32", main_cmake)
        self.assertNotIn("third_party/xiaozhi-esp32", assets_cmake)

    def test_xiaozhi_input_notice_records_cryptographic_provenance(self):
        notice = read("third_party_notices/xiaozhi-build-inputs.md")

        for value in (
            "v2.2.6",
            "49ac8a6da399f27a9546d4f73640b7f86c24bac6",
            ASSET_SCRIPT_SHA256,
            "78/xiaozhi-fonts",
            "1.6.0",
            "19ee829a7439702e18084a313b7b86017d3b8d17a094684575edc76bb6d11023",
            FONT_SHA256,
            "MIT",
        ):
            self.assertIn(value, notice)

    def test_obsolete_standalone_xiaozhi_patch_is_removed(self):
        self.assertFalse((ROOT / "patches/xiaozhi-stopwatch-size.patch").exists())

    def test_supported_build_has_no_full_xiaozhi_tree_dependency(self):
        for path in (
            "main/CMakeLists.txt",
            "main/cmake/unified_assets.cmake",
            "scripts/build_unified_candidate.sh",
            "scripts/report_firmware_metrics.py",
            "README.md",
        ):
            self.assertNotIn("third_party/xiaozhi-esp32", read(path), path)

    def test_candidate_build_checks_source_dependencies_before_idf(self):
        build_script = read("scripts/build_unified_candidate.sh")
        preflight = 'python3 "$ROOT/fetch_repos.py" --check'

        self.assertIn(preflight, build_script)
        self.assertIn("Source dependencies are missing or invalid.", build_script)
        self.assertIn("Run: python3 ./fetch_repos.py", build_script)
        self.assertLess(build_script.index(preflight), build_script.index('"$IDF_PY"'))

    def test_clean_checkout_gate_rebuilds_without_local_generated_dependencies(self):
        script = read("scripts/validate_reproducible_checkout.sh")

        for required in (
            "git clone --no-local",
            "components managed_components third_party build build-unified",
            "python3 ./fetch_repos.py",
            "./scripts/build_unified_candidate.sh --clean",
            "git rev-parse HEAD",
            "shasum -a 256",
            "StopWatch-UserDemo.bin",
            "generated_assets.bin",
        ):
            self.assertIn(required, script)
        self.assertNotIn("ln -s", script)
        self.assertNotIn("ln -sf", script)


if __name__ == "__main__":
    unittest.main()
