use stopwatch_codex_agent::{
    credentials::{
        AgentIdentityStore, MemoryKeychain, WatchCredential, WatchCredentialStore,
        decode_pairing_credential, normalize_agent_name,
    },
    security::{
        PairingTranscript, derive_pairing_keys, derive_shared_secret,
        encode_pairing_identity_claim, encode_pairing_transcript, sign_challenge,
        unwrap_credential, verification_code, verify_challenge, wrap_credential,
    },
    wire::{
        ActionReplayGuard, INPUT_ACTION_FORCE_STEER, INPUT_ACTION_SUBMIT,
        decode_input_action, decode_input_ready, decrypt_task_detail, encode_input_action,
        encode_input_ready, encrypt_task_detail, task_detail_to_value,
    },
};
use std::sync::Arc;

const WATCH_PUBLIC: &str = concat!(
    "020217e617f0b6443928278f96999e69",
    "a23a4f2c152bdf6d6cdf66e5b80282d4ed"
);
const AGENT_PUBLIC: &str = concat!(
    "03d65a93977caa3d1b081852ff57a79e",
    "465f1660577304baead505dd3a48589cf3"
);
const IDENTITY_PUBLIC: &str = concat!(
    "0351a7580833898ea1b183cbd7350a40",
    "99078c6ef1c1e18e970cd7683035f25e7d"
);
const EXPECTED_TRANSCRIPT: &str = concat!(
    "5357504149520101020304",
    "0010d4c3b2a19080706050403020100f0e0d",
    "001000112233445566778899aabbccddeeff",
    "0010ffeeddccbbaa99887766554433221100",
    "0021020217e617f0b6443928278f96999e69a23a4f2c152bdf6d6cdf66e5b80282d4ed",
    "00210351a7580833898ea1b183cbd7350a4099078c6ef1c1e18e970cd7683035f25e7d",
    "002103d65a93977caa3d1b081852ff57a79e465f1660577304baead505dd3a48589cf3",
    "00210351a7580833898ea1b183cbd7350a4099078c6ef1c1e18e970cd7683035f25e7d",
    "000a53747564696f204d6163"
);
const EXPECTED_IDENTITY_CLAIM: &str = concat!(
    "535750434c4d0101020304",
    "0010ffeeddccbbaa99887766554433221100",
    "002103d65a93977caa3d1b081852ff57a79e465f1660577304baead505dd3a48589cf3",
    "00210351a7580833898ea1b183cbd7350a4099078c6ef1c1e18e970cd7683035f25e7d",
    "000a53747564696f204d6163"
);
const WRAP_NONCE: &str = "0102030405060708090a0b0c0d";
// This older vector intentionally exercises AES-CCM over arbitrary bytes; it
// is not the structured pairing credential payload.
const WRAP_VECTOR_PLAINTEXT: &str = concat!(
    "00112233445566778899aabbccddeeff",
    "00000007",
    "d4c3b2a19080706050403020100f0e0d",
    "2ea06a7bca900cd2410d78584bdc1add"
);
// Output of pairing_client.encode_credential_plaintext for the same key,
// epoch, watch id, and identity used by the Python pairing oracle.
const PAIRING_CREDENTIAL_PLAINTEXT: &str = concat!(
    "53574352454401",
    "00112233445566778899aabbccddeeff",
    "00000007",
    "d4c3b2a19080706050403020100f0e0d",
    "0351a7580833898ea1b183cbd7350a40",
    "99078c6ef1c1e18e970cd7683035f25e7d",
    "2ea06a7bca900cd2410d78584bdc1add"
);
const EXPECTED_WRAPPED: &str = concat!(
    "a932792ba08de46917aa1c7469d8fd51",
    "c1d7d45a785b81d33b381be7fbee8395",
    "2a01d6472584e8f619ce5531c0baf8ed",
    "583758f77d639f494e29bb71"
);

fn bytes(hex_text: &str) -> Vec<u8> { hex::decode(hex_text).unwrap() }

fn pairing_fixture() -> PairingTranscript {
    PairingTranscript {
        transaction_id: 0x0102_0304,
        watch_id: bytes("d4c3b2a19080706050403020100f0e0d").try_into().unwrap(),
        watch_nonce: bytes("00112233445566778899aabbccddeeff").try_into().unwrap(),
        agent_nonce: bytes("ffeeddccbbaa99887766554433221100").try_into().unwrap(),
        watch_ephemeral_public: bytes(WATCH_PUBLIC).try_into().unwrap(),
        watch_identity_public: bytes(IDENTITY_PUBLIC).try_into().unwrap(),
        agent_ephemeral_public: bytes(AGENT_PUBLIC).try_into().unwrap(),
        agent_identity_public: bytes(IDENTITY_PUBLIC).try_into().unwrap(),
        agent_name: "Studio Mac".into(),
    }
}

#[test]
fn pairing_crypto_matches_the_python_wire_and_ccm_vectors() {
    let fixture = pairing_fixture();
    let transcript = encode_pairing_transcript(&fixture).unwrap();
    assert_eq!(hex::encode(&transcript), EXPECTED_TRANSCRIPT);
    assert_eq!(
        hex::encode(encode_pairing_identity_claim(
            fixture.transaction_id,
            &fixture.agent_nonce,
            &fixture.agent_ephemeral_public,
            &fixture.agent_identity_public,
            &fixture.agent_name,
        ).unwrap()),
        EXPECTED_IDENTITY_CLAIM
    );

    let shared = derive_shared_secret(&[0x11; 32], &bytes(AGENT_PUBLIC)).unwrap();
    assert_eq!(hex::encode(shared.as_ref()), "ccfc261f58193c98ca4ad4a53bbac6f0ee29bc4d48438090446908622ca79af6");
    let peer_shared = derive_shared_secret(&[0x22; 32], &bytes(WATCH_PUBLIC)).unwrap();
    assert_eq!(shared.as_ref(), peer_shared.as_ref());

    let keys = derive_pairing_keys(shared.as_ref(), &transcript).unwrap();
    assert_eq!(hex::encode(keys.confirm_key), "b8a80f8e6163543c9f246f966de93072");
    assert_eq!(hex::encode(keys.wrap_key), "9f0df5d9315a44cb61c4fc5f0e23a79e");
    assert_eq!(hex::encode(keys.ack_key), "5887abf88287da67131dfd25085e9b18");
    assert_eq!(verification_code(&keys.confirm_key, &transcript).unwrap(), "782441");

    let wrapped = wrap_credential(
        &keys.wrap_key,
        &bytes(WRAP_NONCE),
        &bytes(WRAP_VECTOR_PLAINTEXT),
        &transcript,
    ).unwrap();
    assert_eq!(hex::encode(&wrapped), EXPECTED_WRAPPED);
    assert_eq!(unwrap_credential(&keys.wrap_key, &bytes(WRAP_NONCE), &wrapped, &transcript).unwrap().as_slice(), bytes(WRAP_VECTOR_PLAINTEXT));
    assert!(unwrap_credential(&keys.wrap_key, &bytes(WRAP_NONCE), &wrapped, b"altered").is_err());
}

#[test]
fn p256_signatures_authenticate_exact_bytes_and_reject_mutation() {
    let transcript = encode_pairing_transcript(&pairing_fixture()).unwrap();
    let signature = sign_challenge(&[0x33; 32], &transcript).unwrap();
    assert!(verify_challenge(&bytes(IDENTITY_PUBLIC), &transcript, &signature));
    let mut changed = transcript.clone();
    *changed.last_mut().unwrap() ^= 1;
    assert!(!verify_challenge(&bytes(IDENTITY_PUBLIC), &changed, &signature));
    assert!(!verify_challenge(&bytes(WATCH_PUBLIC), &transcript, &signature));
}

#[test]
fn production_input_frames_match_firmware_vectors_and_replay_is_monotonic() {
    let key = bytes("000102030405060708090a0b0c0d0e0f");
    let nonce: [u8; 8] = bytes("1011121314151617").try_into().unwrap();
    let ready = bytes("010100001011121314151617fc73d242a022667e");
    let submit = bytes("0101010000000102030408009085a9571d1cff3c");
    let force = bytes("0102ffffffffffffffff08007b4211635bc8f5ca");
    assert_eq!(encode_input_ready(&key, &nonce).unwrap(), ready);
    assert_eq!(decode_input_ready(&key, &ready).unwrap(), nonce);
    assert_eq!(encode_input_action(&key, INPUT_ACTION_SUBMIT, 1, 0x0403_0201, &nonce).unwrap(), submit);
    assert_eq!(encode_input_action(&key, INPUT_ACTION_FORCE_STEER, u32::MAX, u32::MAX, &nonce).unwrap(), force);
    assert_eq!(decode_input_action(&key, &submit, &nonce).unwrap().sequence, 1);

    let mut guard = ActionReplayGuard::default();
    guard.set_active_nonce(nonce).unwrap();
    let packet = decode_input_action(&key, &submit, &nonce).unwrap();
    assert!(guard.accept(&nonce, packet.sequence));
    assert!(!guard.accept(&nonce, packet.sequence));
    assert!(!guard.accept(&[0x20; 8], 2));
    let mut tampered = submit.clone();
    *tampered.last_mut().unwrap() ^= 0x80;
    assert!(decode_input_action(&key, &tampered, &nonce).is_err());
}

#[test]
fn task_detail_aad_domain_and_ciphertext_match_the_legacy_vector() {
    let key = bytes("000102030405060708090a0b0c0d0e0f");
    let nonce: [u8; 8] = bytes("1011121314151617").try_into().unwrap();
    let envelope = encrypt_task_detail(
        &key, 7, nonce, 0x0102_0304, "0123456789abcdef", 12, 1, 3, "fixed 私密",
    ).unwrap();
    assert_eq!(envelope.ciphertext, "MgJpSHfh+JIsO/mNgxI4BI09QBA=");
    let value = task_detail_to_value(&envelope).unwrap();
    assert_eq!(decrypt_task_detail(&key, &value).unwrap(), "fixed 私密");
    let mut altered = value;
    altered["revision"] = 13.into();
    assert!(decrypt_task_detail(&key, &altered).is_err());
}

#[test]
fn memory_credentials_preserve_wire_format_and_reject_epoch_rollback() {
    let keychain = Arc::new(MemoryKeychain::default());
    let store = WatchCredentialStore::new(keychain.clone());
    let parsed = decode_pairing_credential(&bytes(PAIRING_CREDENTIAL_PLAINTEXT)).unwrap();
    assert_eq!(parsed.key_epoch, 7);
    assert_eq!(parsed.watch_id.as_slice(), bytes("d4c3b2a19080706050403020100f0e0d").as_slice());
    store.replace(&parsed).unwrap();
    assert_eq!(store.load(&parsed.watch_id).unwrap(), Some(parsed.clone()));
    let mut newer = parsed.clone();
    newer.key_epoch = 8;
    store.replace(&newer).unwrap();
    assert_eq!(store.load(&newer.watch_id).unwrap().unwrap().key_epoch, 8);
    assert!(store.replace(&parsed).is_err());
    store.forget(&parsed.watch_id).unwrap();
    assert!(store.load(&parsed.watch_id).unwrap().is_none());

    let identity_store = AgentIdentityStore::with_default_name(keychain.clone(), || "Test Mac".into());
    let identity = identity_store.load_or_create().unwrap();
    assert_eq!(identity.display_name, "Test Mac");
    let next = AgentIdentityStore::with_default_name(keychain, || "ignored".into()).load_or_create().unwrap();
    assert_eq!(identity.public_key, next.public_key);
    assert_eq!(identity.fingerprint, next.fingerprint);
}

#[test]
fn agent_names_use_nfc_byte_bounds_and_reject_unicode_category_c() {
    assert_eq!(normalize_agent_name("  e\u{301}   Mac  ").unwrap(), "é Mac");
    assert_eq!(normalize_agent_name(&format!("{}界", "a".repeat(23))).unwrap(), "a".repeat(23));
    assert!(normalize_agent_name("\u{0378}").is_err()); // unassigned (Cn)
    assert!(normalize_agent_name("name\u{e000}").is_err()); // private use (Co)
    assert!(normalize_agent_name("\u{200b}").is_err()); // format (Cf)
    let mut decomposed = pairing_fixture();
    decomposed.agent_name = "e\u{301}".into();
    assert!(encode_pairing_transcript(&decomposed).is_err());
}

#[test]
fn pairing_credential_envelope_keeps_secrets_out_of_debug_output() {
    let parsed = decode_pairing_credential(&bytes(PAIRING_CREDENTIAL_PLAINTEXT)).unwrap();
    let debug = format!("{parsed:?}");
    assert!(debug.contains("REDACTED"));
    assert!(!debug.contains(&hex::encode(parsed.master_key.0)));
    assert!(decode_pairing_credential(&[0u8; 91]).is_err());
}
