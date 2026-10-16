//! Protocol cryptography shared by authenticated BLE services.

use aes::Aes128;
use anyhow::{Result, bail};
use ccm::{Ccm, aead::{Aead, KeyInit, Payload, generic_array::GenericArray}, consts::{U8, U13}};
use hkdf::Hkdf;
use hmac::{Hmac, Mac};
use p256::{PublicKey, SecretKey, ecdh::diffie_hellman, ecdsa::{Signature, SigningKey, VerifyingKey, signature::{Signer, Verifier}}, elliptic_curve::sec1::ToEncodedPoint};
use regex::Regex;
use sha2::{Digest, Sha256};
use std::sync::OnceLock;
use unicode_normalization::UnicodeNormalization;
use zeroize::{Zeroize, ZeroizeOnDrop, Zeroizing};

pub const KEY_BYTES: usize = 16;
pub const EXCHANGE_NONCE_BYTES: usize = 8;
pub const CCM_NONCE_BYTES: usize = 13;
pub const TAG_BYTES: usize = 8;

pub const READY_DOMAIN: u8 = 0x01;
pub const UNAVAILABLE_DOMAIN: u8 = 0x02;
pub const PTT_BEGIN_DOMAIN: u8 = 0x11;
pub const AUDIO_DOMAIN: u8 = 0x12;
pub const PTT_END_DOMAIN: u8 = 0x13;
pub const VOICE_ERROR_DOMAIN: u8 = 0x14;
pub const INPUT_READY_DOMAIN: u8 = 0x21;
pub const INPUT_SUBMIT_DOMAIN: u8 = 0x22;
pub const INPUT_FORCE_STEER_DOMAIN: u8 = 0x23;
pub const TASK_DETAIL_DOMAIN: u8 = 0x24;
pub const PET_CONTROL_DOMAIN: u8 = 0x30;
pub const PET_STATUS_DOMAIN: u8 = 0x31;

const PAIRING_VERSION: u8 = 1;
const LABEL_CONFIRM: &[u8] = b"stopwatch/pair/confirm";
const LABEL_WRAP: &[u8] = b"stopwatch/pair/wrap";
const LABEL_ACK: &[u8] = b"stopwatch/pair/ack";

type AesCcm = Ccm<Aes128, U8, U13>;

#[derive(Clone, Zeroize, ZeroizeOnDrop)]
pub struct PairingKeys {
    pub confirm_key: [u8; KEY_BYTES],
    pub wrap_key: [u8; KEY_BYTES],
    pub ack_key: [u8; KEY_BYTES],
}

impl std::fmt::Debug for PairingKeys {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("PairingKeys([REDACTED])")
    }
}

#[derive(Clone, PartialEq, Eq, Zeroize, ZeroizeOnDrop)]
pub struct Secret16(pub [u8; KEY_BYTES]);

impl std::fmt::Debug for Secret16 {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("Secret16([REDACTED])")
    }
}

pub fn public_key_bytes(public_key: &PublicKey) -> [u8; 33] {
    let encoded = public_key.to_encoded_point(true);
    let mut bytes = [0; 33];
    bytes.copy_from_slice(encoded.as_bytes());
    bytes
}

pub fn parse_public_key(encoded: &[u8]) -> Result<PublicKey> {
    if encoded.len() != 33 || !matches!(encoded.first(), Some(2 | 3)) {
        bail!("public key must be a compressed P-256 point");
    }
    PublicKey::from_sec1_bytes(encoded).map_err(|_| anyhow::anyhow!("public key is not on P-256"))
}

pub fn derive_shared_secret(private_scalar: &[u8], peer_public: &[u8]) -> Result<Zeroizing<[u8; 32]>> {
    let private_key = SecretKey::from_slice(private_scalar)
        .map_err(|_| anyhow::anyhow!("P-256 private key is invalid"))?;
    let peer_key = parse_public_key(peer_public)?;
    let shared = diffie_hellman(private_key.to_nonzero_scalar(), peer_key.as_affine());
    let mut result = Zeroizing::new([0u8; 32]);
    result.copy_from_slice(shared.raw_secret_bytes().as_slice());
    Ok(result)
}

pub fn sign_challenge(private_scalar: &[u8], challenge: &[u8]) -> Result<Vec<u8>> {
    if challenge.is_empty() {
        bail!("challenge cannot be empty");
    }
    let signing_key = SigningKey::from_slice(private_scalar)
        .map_err(|_| anyhow::anyhow!("P-256 identity key is invalid"))?;
    let signature: Signature = signing_key.sign(challenge);
    Ok(signature.to_der().as_bytes().to_vec())
}

pub fn verify_challenge(public_key: &[u8], challenge: &[u8], signature: &[u8]) -> bool {
    if challenge.is_empty() || signature.is_empty() {
        return false;
    }
    let Ok(public_key) = parse_public_key(public_key) else { return false; };
    let Ok(verifying_key) = VerifyingKey::from_sec1_bytes(public_key.to_encoded_point(true).as_bytes()) else { return false; };
    let Ok(signature) = Signature::from_der(signature) else { return false; };
    verifying_key.verify(challenge, &signature).is_ok()
}

pub fn derive_pairing_keys(shared_secret: &[u8], transcript: &[u8]) -> Result<PairingKeys> {
    if shared_secret.len() != 32 || transcript.is_empty() {
        bail!("invalid ECDH secret or transcript");
    }
    let salt = Sha256::digest(transcript);
    let hkdf = Hkdf::<Sha256>::new(Some(&salt), shared_secret);
    let mut confirm_key = [0u8; KEY_BYTES];
    let mut wrap_key = [0u8; KEY_BYTES];
    let mut ack_key = [0u8; KEY_BYTES];
    hkdf.expand(LABEL_CONFIRM, &mut confirm_key).map_err(|_| anyhow::anyhow!("pairing key derivation failed"))?;
    hkdf.expand(LABEL_WRAP, &mut wrap_key).map_err(|_| anyhow::anyhow!("pairing key derivation failed"))?;
    hkdf.expand(LABEL_ACK, &mut ack_key).map_err(|_| anyhow::anyhow!("pairing key derivation failed"))?;
    Ok(PairingKeys { confirm_key, wrap_key, ack_key })
}

pub fn verification_code(confirm_key: &[u8], transcript: &[u8]) -> Result<String> {
    if confirm_key.len() != KEY_BYTES || transcript.is_empty() {
        bail!("invalid confirmation key or transcript");
    }
    let mut mac = <Hmac<Sha256> as Mac>::new_from_slice(confirm_key)
        .map_err(|_| anyhow::anyhow!("confirmation code calculation failed"))?;
    mac.update(transcript);
    let digest = mac.finalize().into_bytes();
    let number = u32::from_be_bytes(digest[..4].try_into()?) % 1_000_000;
    Ok(format!("{number:06}"))
}

/// The exact bytes signed by a watch for production authentication. This is not
/// the SWPR transport frame: it is the canonical SWAUTH transcript used as the
/// ECDSA message and as the HKDF salt source during rekey.
pub fn authentication_challenge_bytes(
    transaction_id: u32,
    watch_id: &[u8],
    connection_nonce: &[u8],
    key_epoch: u32,
    connection_role: u8,
    watch_ephemeral_public: &[u8],
    display_name: &str,
) -> Result<Vec<u8>> {
    if transaction_id == 0 || key_epoch == 0 || connection_role != 1 {
        bail!("authentication challenge fields are invalid");
    }
    validate_nonzero(watch_id, 16, "watch identifier")?;
    validate_nonzero(connection_nonce, 16, "connection nonce")?;
    if watch_ephemeral_public.len() != 33 {
        bail!("watch rekey public key has invalid size");
    }
    if watch_ephemeral_public.iter().any(|byte| *byte != 0) {
        parse_public_key(watch_ephemeral_public)?;
    }
    let name = validate_pairing_name(display_name)?;
    if name.len() > u8::MAX as usize {
        bail!("authentication display name is invalid");
    }
    let mut bytes = Vec::with_capacity(82 + name.len());
    bytes.extend_from_slice(b"SWAUTH");
    bytes.extend_from_slice(&[PAIRING_VERSION, connection_role]);
    bytes.extend_from_slice(&transaction_id.to_be_bytes());
    bytes.extend_from_slice(watch_id);
    bytes.extend_from_slice(connection_nonce);
    bytes.extend_from_slice(&key_epoch.to_be_bytes());
    bytes.extend_from_slice(watch_ephemeral_public);
    bytes.push(name.len() as u8);
    bytes.extend_from_slice(&name);
    Ok(bytes)
}

pub fn credential_envelope_aad(purpose: u8, transaction_id: u32) -> Result<[u8; 11]> {
    if !matches!(purpose, 1 | 2) || transaction_id == 0 {
        bail!("credential envelope fields are invalid");
    }
    let mut aad = [0u8; 11];
    aad[..5].copy_from_slice(b"SWENV");
    aad[5] = PAIRING_VERSION;
    aad[6] = purpose;
    aad[7..].copy_from_slice(&transaction_id.to_be_bytes());
    Ok(aad)
}

pub fn credential_ack_proof(ack_key: &[u8], transaction_id: u32, nonce: &[u8]) -> Result<Vec<u8>> {
    if transaction_id == 0 {
        bail!("credential acknowledgement transaction is invalid");
    }
    let mut aad = [0u8; 10];
    aad[..5].copy_from_slice(b"SWACK");
    aad[5] = PAIRING_VERSION;
    aad[6..].copy_from_slice(&transaction_id.to_be_bytes());
    wrap_credential(ack_key, nonce, &transaction_id.to_be_bytes(), &aad)
}

pub fn wrap_credential(key: &[u8], nonce: &[u8], plaintext: &[u8], aad: &[u8]) -> Result<Vec<u8>> {
    validate_wrap(key, nonce, plaintext, aad)?;
    let cipher = AesCcm::new_from_slice(key).map_err(|_| anyhow::anyhow!("credential wrapping key is invalid"))?;
    cipher.encrypt(GenericArray::from_slice(nonce), Payload { msg: plaintext, aad })
        .map_err(|_| anyhow::anyhow!("credential wrapping failed"))
}

pub fn unwrap_credential(key: &[u8], nonce: &[u8], ciphertext: &[u8], aad: &[u8]) -> Result<Zeroizing<Vec<u8>>> {
    if key.len() != KEY_BYTES || nonce.len() != CCM_NONCE_BYTES || !nonce.iter().any(|byte| *byte != 0) || aad.is_empty() || ciphertext.len() < TAG_BYTES {
        bail!("wrapped credential is invalid");
    }
    let cipher = AesCcm::new_from_slice(key).map_err(|_| anyhow::anyhow!("credential wrapping key is invalid"))?;
    let plaintext = cipher.decrypt(GenericArray::from_slice(nonce), Payload { msg: ciphertext, aad })
        .map_err(|_| anyhow::anyhow!("credential authentication failed"))?;
    if plaintext.is_empty() { bail!("credential cannot be empty"); }
    Ok(Zeroizing::new(plaintext))
}

fn validate_wrap(key: &[u8], nonce: &[u8], plaintext: &[u8], aad: &[u8]) -> Result<()> {
    if key.len() != KEY_BYTES { bail!("credential wrapping key must be 16 bytes"); }
    if nonce.len() != CCM_NONCE_BYTES || !nonce.iter().any(|byte| *byte != 0) { bail!("credential nonce must be 13 nonzero bytes"); }
    if plaintext.is_empty() { bail!("credential cannot be empty"); }
    if aad.is_empty() { bail!("credential AAD cannot be empty"); }
    Ok(())
}

pub fn derive_nonce(domain: u8, exchange_nonce: &[u8], sequence: u32) -> Result<[u8; 13]> {
    if !matches!(domain, READY_DOMAIN | UNAVAILABLE_DOMAIN | PTT_BEGIN_DOMAIN | AUDIO_DOMAIN | PTT_END_DOMAIN | VOICE_ERROR_DOMAIN | INPUT_READY_DOMAIN | INPUT_SUBMIT_DOMAIN | INPUT_FORCE_STEER_DOMAIN | TASK_DETAIL_DOMAIN | PET_CONTROL_DOMAIN | PET_STATUS_DOMAIN) {
        bail!("unknown BLE nonce domain");
    }
    if exchange_nonce.len() != EXCHANGE_NONCE_BYTES || !exchange_nonce.iter().any(|byte| *byte != 0) {
        bail!("exchange nonce must be 8 nonzero bytes");
    }
    if sequence == 0 && matches!(domain, PET_CONTROL_DOMAIN | PET_STATUS_DOMAIN) {
        bail!("pet sequence must be 1..4294967295");
    }
    let mut nonce = [0u8; 13];
    nonce[0] = domain;
    nonce[1..9].copy_from_slice(exchange_nonce);
    nonce[9..13].copy_from_slice(&sequence.to_le_bytes());
    Ok(nonce)
}

pub fn encrypt(key: &[u8], domain: u8, exchange_nonce: &[u8], sequence: u32, plaintext: &[u8], aad: &[u8]) -> Result<Vec<u8>> {
    if key.len() != KEY_BYTES { bail!("BLE encryption key must be 16 bytes"); }
    let nonce = derive_nonce(domain, exchange_nonce, sequence)?;
    let cipher = AesCcm::new_from_slice(key).map_err(|_| anyhow::anyhow!("BLE encryption key is invalid"))?;
    cipher.encrypt(GenericArray::from_slice(&nonce), Payload { msg: plaintext, aad })
        .map_err(|_| anyhow::anyhow!("BLE encryption failed"))
}

pub fn decrypt(key: &[u8], domain: u8, exchange_nonce: &[u8], sequence: u32, ciphertext: &[u8], aad: &[u8]) -> Result<Zeroizing<Vec<u8>>> {
    if key.len() != KEY_BYTES || ciphertext.len() < TAG_BYTES { bail!("BLE encrypted frame is invalid"); }
    let nonce = derive_nonce(domain, exchange_nonce, sequence)?;
    let cipher = AesCcm::new_from_slice(key).map_err(|_| anyhow::anyhow!("BLE encryption key is invalid"))?;
    let plaintext = cipher.decrypt(GenericArray::from_slice(&nonce), Payload { msg: ciphertext, aad })
        .map_err(|_| anyhow::anyhow!("BLE frame authentication failed"))?;
    Ok(Zeroizing::new(plaintext))
}

pub fn encode_pairing_identity_claim(
    transaction_id: u32,
    agent_nonce: &[u8],
    agent_ephemeral_public: &[u8],
    agent_identity_public: &[u8],
    agent_name: &str,
) -> Result<Vec<u8>> {
    if transaction_id == 0 { bail!("transaction identifier is out of range"); }
    validate_nonzero(agent_nonce, 16, "Agent nonce")?;
    parse_public_key(agent_ephemeral_public)?;
    parse_public_key(agent_identity_public)?;
    let name = validate_pairing_name(agent_name)?;
    let mut output = Vec::with_capacity(10 + 4 + 2 * (2 + 33) + 2 + name.len());
    output.extend_from_slice(b"SWPCLM");
    output.push(PAIRING_VERSION);
    output.extend_from_slice(&transaction_id.to_be_bytes());
    for field in [agent_nonce, agent_ephemeral_public, agent_identity_public, name.as_slice()] {
        append_field(&mut output, field)?;
    }
    Ok(output)
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PairingTranscript {
    pub transaction_id: u32,
    pub watch_id: [u8; 16],
    pub watch_nonce: [u8; 16],
    pub agent_nonce: [u8; 16],
    pub watch_ephemeral_public: [u8; 33],
    pub watch_identity_public: [u8; 33],
    pub agent_ephemeral_public: [u8; 33],
    pub agent_identity_public: [u8; 33],
    pub agent_name: String,
}

pub fn encode_pairing_transcript(value: &PairingTranscript) -> Result<Vec<u8>> {
    validate_nonzero(&value.watch_id, 16, "watch identifier")?;
    validate_nonzero(&value.watch_nonce, 16, "watch nonce")?;
    validate_nonzero(&value.agent_nonce, 16, "Agent nonce")?;
    for key in [&value.watch_ephemeral_public, &value.watch_identity_public, &value.agent_ephemeral_public, &value.agent_identity_public] {
        parse_public_key(key)?;
    }
    let name = validate_pairing_name(&value.agent_name)?;
    let mut output = Vec::with_capacity(11 + 4 + 8 * (2 + 33) + name.len());
    output.extend_from_slice(b"SWPAIR");
    output.push(PAIRING_VERSION);
    output.extend_from_slice(&value.transaction_id.to_be_bytes());
    for field in [
        value.watch_id.as_slice(), value.watch_nonce.as_slice(), value.agent_nonce.as_slice(),
        value.watch_ephemeral_public.as_slice(), value.watch_identity_public.as_slice(),
        value.agent_ephemeral_public.as_slice(), value.agent_identity_public.as_slice(), name.as_slice(),
    ] {
        append_field(&mut output, field)?;
    }
    Ok(output)
}

pub fn decode_pairing_transcript(data: &[u8]) -> Result<PairingTranscript> {
    if data.len() < 11 || !data.starts_with(b"SWPAIR") || data[6] != PAIRING_VERSION { bail!("invalid pairing transcript header"); }
    let transaction_id = u32::from_be_bytes(data[7..11].try_into()?);
    let mut offset = 11;
    let mut fields = Vec::with_capacity(8);
    for _ in 0..8 {
        if offset + 2 > data.len() { bail!("truncated pairing transcript"); }
        let size = u16::from_be_bytes(data[offset..offset + 2].try_into()?) as usize;
        offset += 2;
        if offset + size > data.len() { bail!("truncated pairing field"); }
        fields.push(&data[offset..offset + size]);
        offset += size;
    }
    if offset != data.len() { bail!("trailing pairing transcript bytes"); }
    let result = PairingTranscript {
        transaction_id,
        watch_id: fields[0].try_into().map_err(|_| anyhow::anyhow!("watch identifier must be 16 bytes"))?,
        watch_nonce: fields[1].try_into().map_err(|_| anyhow::anyhow!("watch nonce must be 16 bytes"))?,
        agent_nonce: fields[2].try_into().map_err(|_| anyhow::anyhow!("Agent nonce must be 16 bytes"))?,
        watch_ephemeral_public: fields[3].try_into().map_err(|_| anyhow::anyhow!("watch key must be 33 bytes"))?,
        watch_identity_public: fields[4].try_into().map_err(|_| anyhow::anyhow!("watch key must be 33 bytes"))?,
        agent_ephemeral_public: fields[5].try_into().map_err(|_| anyhow::anyhow!("Agent key must be 33 bytes"))?,
        agent_identity_public: fields[6].try_into().map_err(|_| anyhow::anyhow!("Agent key must be 33 bytes"))?,
        agent_name: std::str::from_utf8(fields[7]).map_err(|_| anyhow::anyhow!("Agent name is not UTF-8"))?.to_owned(),
    };
    if encode_pairing_transcript(&result)? != data { bail!("pairing transcript is not canonical"); }
    Ok(result)
}

fn validate_nonzero(value: &[u8], len: usize, label: &str) -> Result<()> {
    if value.len() != len || !value.iter().any(|byte| *byte != 0) { bail!("{label} must be {len} nonzero bytes"); }
    Ok(())
}

fn validate_pairing_name(value: &str) -> Result<Vec<u8>> {
    if value.is_empty()
        || value.nfc().collect::<String>() != value
        || value.chars().any(is_disallowed_name_character)
    {
        bail!("Agent name is invalid");
    }
    let encoded = value.as_bytes();
    if encoded.len() > 24 { bail!("Agent name exceeds 24 UTF-8 bytes"); }
    Ok(encoded.to_vec())
}

fn is_disallowed_name_character(character: char) -> bool {
    static CATEGORY_C: OnceLock<Regex> = OnceLock::new();
    CATEGORY_C
        .get_or_init(|| Regex::new(r"\p{C}").expect("Unicode category C is supported"))
        .is_match(&character.to_string())
}

/// Whether a character is in Unicode General Category C (Other). Python's
/// previous Agent rejected the complete category, including unassigned and
/// private-use code points; protocol-name validation must do the same.
pub fn is_category_c(character: char) -> bool {
    is_disallowed_name_character(character)
}

fn append_field(output: &mut Vec<u8>, field: &[u8]) -> Result<()> {
    let size = u16::try_from(field.len()).map_err(|_| anyhow::anyhow!("pairing field is too large"))?;
    output.extend_from_slice(&size.to_be_bytes());
    output.extend_from_slice(field);
    Ok(())
}

