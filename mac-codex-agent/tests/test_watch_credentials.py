import dataclasses
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from pairing_client import WatchCredential  # noqa: E402
from watch_credentials import (  # noqa: E402
    StaleCredentialError,
    WatchCredentialError,
    WatchCredentialStore,
)


class MemoryKeychain:
    def __init__(self):
        self.items = {}
        self.replacements = []

    def read(self, service, account):
        return self.items.get((service, account))

    def replace(self, service, account, value):
        encoded = bytes(value)
        self.replacements.append((service, account, encoded))
        self.items[(service, account)] = encoded

    def delete(self, service, account):
        self.items.pop((service, account), None)


def credential(seed, epoch):
    return WatchCredential(
        watch_id=bytes([seed]) * 16,
        master_key=bytes([seed + 1]) * 16,
        key_epoch=epoch,
        fingerprint=bytes([seed + 2]) * 16,
    )


class WatchCredentialTests(unittest.TestCase):
    def test_records_are_isolated_by_exact_watch_identifier(self):
        keychain = MemoryKeychain()
        store = WatchCredentialStore(keychain)
        first = credential(1, 3)
        second = credential(9, 7)

        store.replace(first)
        store.replace(second)

        self.assertEqual(store.load(first.watch_id), first)
        self.assertEqual(store.load(second.watch_id), second)
        self.assertNotEqual(
            keychain.replacements[0][0], keychain.replacements[1][0]
        )

    def test_key_and_epoch_are_replaced_in_one_keychain_write(self):
        keychain = MemoryKeychain()
        store = WatchCredentialStore(keychain)
        original = credential(2, 1)
        rotated = dataclasses.replace(
            original, master_key=b"R" * 16, key_epoch=2
        )
        store.replace(original)
        writes_before = len(keychain.replacements)

        store.replace(rotated)

        self.assertEqual(len(keychain.replacements), writes_before + 1)
        self.assertEqual(store.load(original.watch_id), rotated)

    def test_stale_epoch_and_same_epoch_key_change_are_rejected(self):
        store = WatchCredentialStore(MemoryKeychain())
        current = credential(3, 5)
        store.replace(current)

        with self.assertRaises(StaleCredentialError):
            store.replace(dataclasses.replace(current, key_epoch=4))
        with self.assertRaises(StaleCredentialError):
            store.replace(dataclasses.replace(current, master_key=b"X" * 16))

        self.assertEqual(store.load(current.watch_id), current)

    def test_snapshot_contains_metadata_but_no_master_key(self):
        store = WatchCredentialStore(MemoryKeychain())
        saved = credential(4, 11)
        store.replace(saved)

        snapshot = store.snapshot(saved.watch_id)

        self.assertEqual(snapshot.key_epoch, 11)
        self.assertEqual(snapshot.watch_id_suffix, saved.watch_id.hex()[-4:].upper())
        self.assertEqual(snapshot.fingerprint_suffix, saved.fingerprint.hex()[-4:].upper())
        self.assertNotIn("master_key", {f.name for f in dataclasses.fields(snapshot)})
        self.assertNotIn(saved.master_key.hex(), repr(snapshot))

    def test_corrupt_or_mismatched_record_uses_redacted_error(self):
        keychain = MemoryKeychain()
        store = WatchCredentialStore(keychain)
        saved = credential(5, 2)
        store.replace(saved)
        service, account, payload = keychain.replacements[-1]
        keychain.items[(service, account)] = payload[:-1] + b"\xff"

        with self.assertRaises(WatchCredentialError) as caught:
            store.load(saved.watch_id)

        self.assertEqual(
            str(caught.exception), "watch credential is unavailable from Keychain"
        )
        self.assertNotIn(saved.master_key.hex(), str(caught.exception))

    def test_invalid_values_are_rejected_before_keychain_access(self):
        store = WatchCredentialStore(MemoryKeychain())
        valid = credential(6, 1)
        invalid = (
            dataclasses.replace(valid, watch_id=b"short"),
            dataclasses.replace(valid, master_key=b"short"),
            dataclasses.replace(valid, fingerprint=b"short"),
            dataclasses.replace(valid, key_epoch=0),
        )
        for item in invalid:
            with self.subTest(item=item):
                with self.assertRaises(ValueError):
                    store.replace(item)


if __name__ == "__main__":
    unittest.main()
