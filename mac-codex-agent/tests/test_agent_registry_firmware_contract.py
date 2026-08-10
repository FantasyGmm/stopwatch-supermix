import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class AgentRegistryFirmwareContractTests(unittest.TestCase):
    def test_registry_has_bounded_identity_and_rotation_contract(self):
        header = (
            ROOT / "main/apps/app_codex_status/agent_registry.h"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "main/apps/app_codex_status/agent_registry.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("constexpr size_t kMaxAgents = 8", header)
        self.assertIn("constexpr size_t kAgentNameBytes = 24", header)
        self.assertIn("IdentityPublicKey public_key", header)
        self.assertIn("AgentFingerprint fingerprint", header)
        self.assertIn("uint32_t key_epoch", header)
        self.assertIn("remove_and_rotate", header)
        self.assertIn("RegistryResult::EntropyFailure", source)
        self.assertIn("RegistryResult::Duplicate", source)
        self.assertNotIn("std::vector", header)

    def test_store_uses_two_verified_generations_and_selector(self):
        header = (
            ROOT / "main/apps/app_codex_status/agent_registry_store.h"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "main/apps/app_codex_status/agent_registry_store.cpp"
        ).read_text(encoding="utf-8")

        for value in (
            '"codex_agents"',
            '"registry_a"',
            '"registry_b"',
            '"registry_sel"',
            "nvs_get_blob",
            "nvs_set_blob",
            "nvs_commit",
        ):
            self.assertIn(value, source)
        self.assertIn("RegistryStorageBackend", header)
        self.assertIn("kRegistryVersion", source)
        self.assertIn("mbedtls_sha256", source)
        self.assertLess(
            source.index("verify_slot"),
            source.index("write_selector"),
        )

    def test_unified_runner_executes_registry_host_test(self):
        runner = (
            ROOT / "scripts/run_unified_offline_validation.sh"
        ).read_text(encoding="utf-8")
        self.assertIn('stage "Authorized Agent registry host test"', runner)
        self.assertIn("tests/agent_registry_test.cpp", runner)
        self.assertIn(
            "main/apps/app_codex_status/agent_registry_store.cpp", runner
        )


if __name__ == "__main__":
    unittest.main()
