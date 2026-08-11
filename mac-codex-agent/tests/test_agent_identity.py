import dataclasses
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from agent_identity import (  # noqa: E402
    AgentIdentityError,
    AgentIdentityStore,
    IDENTITY_NAME_SERVICE,
    IDENTITY_SERVICE,
    normalize_agent_name,
)


class MemoryKeychain:
    def __init__(self):
        self.items = {}
        self.replacements = []

    def read(self, service, account):
        return self.items.get((service, account))

    def replace(self, service, account, value):
        self.replacements.append((service, account, bytes(value)))
        self.items[(service, account)] = bytes(value)

    def delete(self, service, account):
        self.items.pop((service, account), None)


class FailingKeychain(MemoryKeychain):
    def read(self, service, account):
        raise RuntimeError("secret=00112233445566778899aabbccddeeff")


class AgentIdentityTests(unittest.TestCase):
    def test_identity_is_created_once_and_reused(self):
        keychain = MemoryKeychain()
        first = AgentIdentityStore(keychain, default_name=lambda: "Luke's Mac")
        second = AgentIdentityStore(keychain, default_name=lambda: "Other Mac")

        first_identity = first.load_or_create()
        second_identity = second.load_or_create()

        self.assertEqual(first_identity.public_key, second_identity.public_key)
        self.assertEqual(first_identity.fingerprint, second_identity.fingerprint)
        self.assertEqual(len(first_identity.public_key), 33)
        self.assertEqual(len(first_identity.fingerprint), 16)
        self.assertEqual(
            sum(item[0] == IDENTITY_SERVICE for item in keychain.replacements),
            1,
        )

    def test_snapshot_never_exports_private_key_material(self):
        store = AgentIdentityStore(MemoryKeychain(), default_name=lambda: "Agent")

        identity = store.load_or_create()
        snapshot = store.snapshot()

        self.assertEqual(snapshot.public_key, identity.public_key)
        self.assertEqual(snapshot.fingerprint, identity.fingerprint)
        field_names = {field.name for field in dataclasses.fields(snapshot)}
        self.assertNotIn("private_key", field_names)
        self.assertNotIn("private_bytes", repr(snapshot))

    def test_names_are_nfc_single_line_and_utf8_bounded(self):
        self.assertEqual(normalize_agent_name("  Cafe\u0301\nStudio  "), "Café Studio")
        self.assertEqual(normalize_agent_name("手表" * 10), "手表" * 4)
        self.assertLessEqual(len(normalize_agent_name("手表" * 10).encode("utf-8")), 24)

    def test_empty_or_control_only_name_is_rejected(self):
        for value in ("", "  ", "\x00", "\u200b"):
            with self.subTest(value=repr(value)):
                with self.assertRaises(ValueError):
                    normalize_agent_name(value)

    def test_rename_replaces_only_name_record(self):
        keychain = MemoryKeychain()
        store = AgentIdentityStore(keychain, default_name=lambda: "Original")
        store.load_or_create()
        identity_record = keychain.items[(IDENTITY_SERVICE, "default")]

        renamed = store.rename("  Desk\nAgent  ")

        self.assertEqual(renamed, "Desk Agent")
        self.assertEqual(store.snapshot().display_name, "Desk Agent")
        self.assertEqual(keychain.items[(IDENTITY_SERVICE, "default")], identity_record)
        self.assertIn((IDENTITY_NAME_SERVICE, "default"), keychain.items)

    def test_keychain_failures_are_redacted(self):
        with self.assertRaises(AgentIdentityError) as caught:
            AgentIdentityStore(FailingKeychain()).load_or_create()

        message = str(caught.exception)
        self.assertEqual(message, "Agent identity is unavailable from Keychain")
        self.assertNotIn("001122", message)


if __name__ == "__main__":
    unittest.main()
