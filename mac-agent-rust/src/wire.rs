//! Canonical, bounded encoders and decoders for the watch's BLE protocols.
//!
//! This module is deliberately byte-oriented: field order, endianness, nonce
//! domains, and frame limits mirror the Python Agent and firmware contracts.

use anyhow::{Result, bail};
use base64::{Engine as _, engine::general_purpose::STANDARD as BASE64};
use serde::{Serialize, Serializer};
use serde_json::{Map, Value};
use std::collections::VecDeque;
use unicode_normalization::UnicodeNormalization;

use crate::contracts::{MAX_STATUS_BYTES, StatusSnapshot};
use crate::agent_inventory::{AgentId, AgentInventory};
use crate::security::{
    self, AUDIO_DOMAIN, CCM_NONCE_BYTES, EXCHANGE_NONCE_BYTES, INPUT_FORCE_STEER_DOMAIN,
    INPUT_READY_DOMAIN, INPUT_SUBMIT_DOMAIN, KEY_BYTES, PET_CONTROL_DOMAIN, PET_STATUS_DOMAIN,
    PTT_BEGIN_DOMAIN, PTT_END_DOMAIN, READY_DOMAIN, TAG_BYTES, TASK_DETAIL_DOMAIN,
    UNAVAILABLE_DOMAIN, VOICE_ERROR_DOMAIN,
};

pub const STATUS_PROTOCOL_VERSION: u8 = 1;
pub const STATUS_MESSAGE: u8 = 1;
pub const EVENT_MESSAGE: u8 = 2;
pub const STATUS_HEADER_BYTES: usize = 6;
pub const STATUS_WRITE_SIZE_FALLBACK: usize = 20;
pub const STATUS_WRITE_SIZE_MAX: usize = 256;
pub const STATUS_FRAME_BURST: usize = 8;
pub const STATUS_BURST_PAUSE_MS: u64 = 20;

/// Status encoding is negotiated once and owned by the authenticated BLE session.
/// Public legacy entry points continue to use JSON without negotiation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StatusCodec {
    JsonV1,
    MessagePackV2,
}

impl StatusCodec {
    pub fn frame_version(self) -> u8 {
        match self {
            Self::JsonV1 => STATUS_PROTOCOL_VERSION,
            Self::MessagePackV2 => 2,
        }
    }

    pub fn from_capabilities(descriptor: &[u8]) -> Result<Self> {
        if descriptor.len() != 8 || &descriptor[..3] != b"SWC" {
            bail!("status codec capability descriptor has invalid size or magic");
        }
        if descriptor[3] != 1 || descriptor[5] != 0 {
            bail!("status codec capability descriptor has unsupported version or reserved value");
        }
        if usize::from(u16::from_le_bytes([descriptor[6], descriptor[7]])) != MAX_STATUS_BYTES {
            bail!("status codec capability descriptor has unsupported payload limit");
        }
        match descriptor[4] & 0x03 {
            2 | 3 => Ok(Self::MessagePackV2),
            1 => Ok(Self::JsonV1),
            _ => bail!("watch offers no supported status codec"),
        }
    }

    fn encode<T: Serialize + ?Sized>(self, value: &T) -> Result<Vec<u8>> {
        Ok(match self {
            Self::JsonV1 => serde_json::to_vec(value)?,
            Self::MessagePackV2 => rmp_serde::to_vec_named(value)?,
        })
    }
}

const PAIR_MAGIC: &[u8; 4] = b"SWPR";
const PAIR_VERSION: u8 = 1;
pub const PAIR_MAX_FRAME_BYTES: usize = 256;
pub const PAIR_MAX_AGENT_NAME_BYTES: usize = 24;
const PAIR_HEADER_BYTES: usize = 12;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum PairMessageType {
    DiscoveryRequest = 1,
    DiscoveryResponse = 2,
    PairingRequest = 3,
    PairingOffer = 4,
    PairingConfirm = 5,
    Credential = 6,
    CredentialAck = 7,
    AuthenticationRequest = 8,
    AuthenticationChallenge = 9,
    AuthenticationProof = 10,
    AuthenticationOk = 11,
    Rejected = 12,
    CredentialAckOk = 13,
    Error = 14,
}

impl TryFrom<u8> for PairMessageType {
    type Error = anyhow::Error;

    fn try_from(value: u8) -> Result<Self> {
        Ok(match value {
            1 => Self::DiscoveryRequest,
            2 => Self::DiscoveryResponse,
            3 => Self::PairingRequest,
            4 => Self::PairingOffer,
            5 => Self::PairingConfirm,
            6 => Self::Credential,
            7 => Self::CredentialAck,
            8 => Self::AuthenticationRequest,
            9 => Self::AuthenticationChallenge,
            10 => Self::AuthenticationProof,
            11 => Self::AuthenticationOk,
            12 => Self::Rejected,
            13 => Self::CredentialAckOk,
            14 => Self::Error,
            _ => bail!("pairing frame type is invalid"),
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PairFrame {
    pub kind: PairMessageType,
    pub transaction_id: u32,
    pub payload: Vec<u8>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum CredentialPurpose { Pairing = 1, Rekey = 2 }

impl TryFrom<u8> for CredentialPurpose {
    type Error = anyhow::Error;
    fn try_from(value: u8) -> Result<Self> {
        match value {
            1 => Ok(Self::Pairing),
            2 => Ok(Self::Rekey),
            _ => bail!("credential purpose is invalid"),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PairingRequest {
    pub transaction_id: u32,
    pub agent_nonce: [u8; 16],
    pub agent_ephemeral_public: [u8; 33],
    pub agent_identity_public: [u8; 33],
    pub display_name: String,
    pub identity_signature: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PairingOffer {
    pub transaction_id: u32,
    pub watch_id: [u8; 16],
    pub watch_nonce: [u8; 16],
    pub watch_ephemeral_public: [u8; 33],
    pub watch_identity_public: [u8; 33],
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DiscoveryResponse {
    pub watch_id: [u8; 16],
    pub key_epoch: u32,
    pub watch_identity_public: [u8; 33],
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AuthenticationRequest {
    pub transaction_id: u32,
    pub fingerprint: [u8; 16],
    pub agent_key_epoch: u32,
    pub display_name: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AuthenticationChallenge {
    pub transaction_id: u32,
    pub watch_id: [u8; 16],
    pub connection_nonce: [u8; 16],
    pub key_epoch: u32,
    pub connection_role: u8,
    pub watch_ephemeral_public: [u8; 33],
    pub display_name: String,
    pub watch_signature: Vec<u8>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CredentialEnvelope {
    pub purpose: CredentialPurpose,
    pub transaction_id: u32,
    pub key_epoch: u32,
    pub watch_ephemeral_public: [u8; 33],
    pub nonce: [u8; 13],
    pub ciphertext: Vec<u8>,
    pub ack_nonce: [u8; 13],
}

pub fn encode_pair_frame(kind: PairMessageType, transaction_id: u32, payload: &[u8]) -> Result<Vec<u8>> {
    check_pair_transaction(transaction_id)?;
    if PAIR_HEADER_BYTES + payload.len() > PAIR_MAX_FRAME_BYTES {
        bail!("pairing frame is too large");
    }
    let mut out = Vec::with_capacity(PAIR_HEADER_BYTES + payload.len());
    out.extend_from_slice(PAIR_MAGIC);
    out.push(PAIR_VERSION);
    out.push(kind as u8);
    out.extend_from_slice(&transaction_id.to_be_bytes());
    out.extend_from_slice(&(payload.len() as u16).to_be_bytes());
    out.extend_from_slice(payload);
    Ok(out)
}

pub fn decode_pair_frame(raw: &[u8]) -> Result<PairFrame> {
    if raw.len() < PAIR_HEADER_BYTES { bail!("pairing frame is truncated"); }
    if raw.len() > PAIR_MAX_FRAME_BYTES { bail!("pairing frame is too large"); }
    if &raw[..4] != PAIR_MAGIC || raw[4] != PAIR_VERSION { bail!("pairing frame version is invalid"); }
    let kind = PairMessageType::try_from(raw[5])?;
    let transaction_id = u32::from_be_bytes(raw[6..10].try_into()?);
    check_pair_transaction(transaction_id)?;
    let payload_size = u16::from_be_bytes(raw[10..12].try_into()?) as usize;
    if payload_size != raw.len() - PAIR_HEADER_BYTES { bail!("pairing frame length is invalid"); }
    Ok(PairFrame { kind, transaction_id, payload: raw[PAIR_HEADER_BYTES..].to_vec() })
}

pub fn encode_discovery_request(transaction_id: u32) -> Result<Vec<u8>> {
    encode_pair_frame(PairMessageType::DiscoveryRequest, transaction_id, &[])
}

pub fn encode_discovery_response(transaction_id: u32, response: &DiscoveryResponse) -> Result<Vec<u8>> {
    check_nonzero(&response.watch_id, "watch identifier")?;
    check_epoch(response.key_epoch)?;
    check_nonzero(&response.watch_identity_public, "watch identity key")?;
    let mut payload = Vec::with_capacity(53);
    payload.extend_from_slice(&response.watch_id);
    payload.extend_from_slice(&response.key_epoch.to_be_bytes());
    payload.extend_from_slice(&response.watch_identity_public);
    encode_pair_frame(PairMessageType::DiscoveryResponse, transaction_id, &payload)
}

pub fn decode_discovery_response(payload: &[u8]) -> Result<DiscoveryResponse> {
    if payload.len() != 53 { bail!("discovery response has invalid size"); }
    let result = DiscoveryResponse {
        watch_id: payload[..16].try_into()?,
        key_epoch: u32::from_be_bytes(payload[16..20].try_into()?),
        watch_identity_public: payload[20..53].try_into()?,
    };
    check_nonzero(&result.watch_id, "watch identifier")?;
    check_epoch(result.key_epoch)?;
    check_nonzero(&result.watch_identity_public, "watch identity key")?;
    Ok(result)
}

pub fn encode_pairing_request(request: &PairingRequest) -> Result<Vec<u8>> {
    check_pair_transaction(request.transaction_id)?;
    check_nonzero(&request.agent_nonce, "Agent nonce")?;
    security::parse_public_key(&request.agent_ephemeral_public)?;
    security::parse_public_key(&request.agent_identity_public)?;
    let name = pairing_name(&request.display_name)?;
    if request.identity_signature.is_empty() || request.identity_signature.len() > 80 {
        bail!("Agent identity signature has invalid size");
    }
    let mut payload = Vec::with_capacity(16 + 33 + 33 + 1 + name.len() + 2 + request.identity_signature.len());
    payload.extend_from_slice(&request.agent_nonce);
    payload.extend_from_slice(&request.agent_ephemeral_public);
    payload.extend_from_slice(&request.agent_identity_public);
    payload.push(name.len() as u8);
    payload.extend_from_slice(name);
    push_u16_bytes(&mut payload, &request.identity_signature)?;
    encode_pair_frame(PairMessageType::PairingRequest, request.transaction_id, &payload)
}

pub fn decode_pairing_request(transaction_id: u32, payload: &[u8]) -> Result<PairingRequest> {
    check_pair_transaction(transaction_id)?;
    if payload.len() < 16 + 33 + 33 + 1 { bail!("pairing request is truncated"); }
    let name_len = payload[82] as usize;
    if name_len == 0 || name_len > PAIR_MAX_AGENT_NAME_BYTES || 83 + name_len + 2 > payload.len() {
        bail!("Agent name has invalid size");
    }
    let name_bytes = &payload[83..83 + name_len];
    let display_name = std::str::from_utf8(name_bytes).map_err(|_| anyhow::anyhow!("Agent name is not UTF-8"))?.to_owned();
    pairing_name(&display_name)?;
    let sig_at = 83 + name_len;
    let signature_len = u16::from_be_bytes(payload[sig_at..sig_at + 2].try_into()?) as usize;
    if signature_len == 0 || signature_len > 80 || sig_at + 2 + signature_len != payload.len() {
        bail!("Agent identity signature has invalid size");
    }
    let result = PairingRequest {
        transaction_id,
        agent_nonce: payload[..16].try_into()?,
        agent_ephemeral_public: payload[16..49].try_into()?,
        agent_identity_public: payload[49..82].try_into()?,
        display_name,
        identity_signature: payload[sig_at + 2..].to_vec(),
    };
    check_nonzero(&result.agent_nonce, "Agent nonce")?;
    security::parse_public_key(&result.agent_ephemeral_public)?;
    security::parse_public_key(&result.agent_identity_public)?;
    Ok(result)
}

pub fn encode_pairing_offer(offer: &PairingOffer) -> Result<Vec<u8>> {
    check_pair_transaction(offer.transaction_id)?;
    check_nonzero(&offer.watch_id, "watch identifier")?;
    check_nonzero(&offer.watch_nonce, "watch nonce")?;
    security::parse_public_key(&offer.watch_ephemeral_public)?;
    security::parse_public_key(&offer.watch_identity_public)?;
    let mut payload = Vec::with_capacity(115);
    payload.extend_from_slice(&offer.watch_id);
    payload.extend_from_slice(&offer.watch_nonce);
    payload.extend_from_slice(&offer.watch_ephemeral_public);
    payload.extend_from_slice(&offer.watch_identity_public);
    encode_pair_frame(PairMessageType::PairingOffer, offer.transaction_id, &payload)
}

pub fn decode_pairing_offer(transaction_id: u32, payload: &[u8]) -> Result<PairingOffer> {
    check_pair_transaction(transaction_id)?;
    if payload.len() != 98 { bail!("pairing offer has invalid size"); }
    let offer = PairingOffer {
        transaction_id,
        watch_id: payload[..16].try_into()?,
        watch_nonce: payload[16..32].try_into()?,
        watch_ephemeral_public: payload[32..65].try_into()?,
        watch_identity_public: payload[65..98].try_into()?,
    };
    check_nonzero(&offer.watch_id, "watch identifier")?;
    check_nonzero(&offer.watch_nonce, "watch nonce")?;
    security::parse_public_key(&offer.watch_ephemeral_public)?;
    security::parse_public_key(&offer.watch_identity_public)?;
    Ok(offer)
}

pub fn encode_pairing_confirmation(transaction_id: u32, accepted: bool) -> Result<Vec<u8>> {
    encode_pair_frame(PairMessageType::PairingConfirm, transaction_id, &[u8::from(accepted)])
}

pub fn decode_pairing_confirmation(payload: &[u8]) -> Result<bool> {
    match payload { [0] => Ok(false), [1] => Ok(true), _ => bail!("pairing confirmation is invalid") }
}

pub fn encode_authentication_request(request: &AuthenticationRequest) -> Result<Vec<u8>> {
    check_pair_transaction(request.transaction_id)?;
    check_nonzero(&request.fingerprint, "Agent fingerprint")?;
    check_epoch(request.agent_key_epoch)?;
    let name = pairing_name(&request.display_name)?;
    let mut payload = Vec::with_capacity(21 + name.len());
    payload.extend_from_slice(&request.fingerprint);
    payload.extend_from_slice(&request.agent_key_epoch.to_be_bytes());
    payload.push(name.len() as u8);
    payload.extend_from_slice(name);
    encode_pair_frame(PairMessageType::AuthenticationRequest, request.transaction_id, &payload)
}

pub fn decode_authentication_request(transaction_id: u32, payload: &[u8]) -> Result<AuthenticationRequest> {
    check_pair_transaction(transaction_id)?;
    if payload.len() < 21 { bail!("authentication request has invalid size"); }
    let name_len = payload[20] as usize;
    if name_len == 0 || name_len > PAIR_MAX_AGENT_NAME_BYTES || payload.len() != 21 + name_len { bail!("Agent display name is invalid"); }
    let display_name = std::str::from_utf8(&payload[21..]).map_err(|_| anyhow::anyhow!("Agent display name is invalid"))?.to_owned();
    pairing_name(&display_name)?;
    let request = AuthenticationRequest { transaction_id, fingerprint: payload[..16].try_into()?, agent_key_epoch: u32::from_be_bytes(payload[16..20].try_into()?), display_name };
    check_nonzero(&request.fingerprint, "Agent fingerprint")?;
    check_epoch(request.agent_key_epoch)?;
    Ok(request)
}

pub fn encode_authentication_challenge(challenge: &AuthenticationChallenge) -> Result<Vec<u8>> {
    check_pair_transaction(challenge.transaction_id)?;
    check_nonzero(&challenge.watch_id, "watch identifier")?;
    check_nonzero(&challenge.connection_nonce, "connection nonce")?;
    check_epoch(challenge.key_epoch)?;
    if challenge.connection_role != 1 { bail!("authentication challenge is invalid"); }
    if challenge.watch_ephemeral_public.iter().any(|byte| *byte != 0) { security::parse_public_key(&challenge.watch_ephemeral_public)?; }
    let name = pairing_name(&challenge.display_name)?;
    if challenge.watch_signature.is_empty() || challenge.watch_signature.len() > 80 { bail!("watch challenge signature is invalid"); }
    let mut payload = Vec::with_capacity(71 + name.len() + challenge.watch_signature.len());
    payload.extend_from_slice(&challenge.watch_id);
    payload.extend_from_slice(&challenge.connection_nonce);
    payload.extend_from_slice(&challenge.key_epoch.to_be_bytes());
    payload.push(challenge.connection_role);
    payload.extend_from_slice(&challenge.watch_ephemeral_public);
    payload.push(name.len() as u8);
    payload.extend_from_slice(name);
    push_u16_bytes(&mut payload, &challenge.watch_signature)?;
    encode_pair_frame(PairMessageType::AuthenticationChallenge, challenge.transaction_id, &payload)
}

pub fn decode_authentication_challenge(transaction_id: u32, payload: &[u8]) -> Result<AuthenticationChallenge> {
    check_pair_transaction(transaction_id)?;
    if payload.len() < 71 { bail!("authentication challenge has invalid size"); }
    let name_start = 71;
    let name_len = payload[70] as usize;
    if name_len == 0 || name_len > PAIR_MAX_AGENT_NAME_BYTES || name_start + name_len + 2 > payload.len() { bail!("authentication display name is invalid"); }
    let display_name = std::str::from_utf8(&payload[name_start..name_start + name_len]).map_err(|_| anyhow::anyhow!("authentication display name is invalid"))?.to_owned();
    pairing_name(&display_name)?;
    let signature_at = name_start + name_len;
    let signature_len = u16::from_be_bytes(payload[signature_at..signature_at + 2].try_into()?) as usize;
    if signature_len == 0 || signature_len > 80 || signature_at + 2 + signature_len != payload.len() { bail!("watch challenge signature is invalid"); }
    let challenge = AuthenticationChallenge {
        transaction_id,
        watch_id: payload[..16].try_into()?,
        connection_nonce: payload[16..32].try_into()?,
        key_epoch: u32::from_be_bytes(payload[32..36].try_into()?),
        connection_role: payload[36],
        watch_ephemeral_public: payload[37..70].try_into()?,
        display_name,
        watch_signature: payload[signature_at + 2..].to_vec(),
    };
    check_nonzero(&challenge.watch_id, "watch identifier")?;
    check_nonzero(&challenge.connection_nonce, "connection nonce")?;
    check_epoch(challenge.key_epoch)?;
    if challenge.connection_role != 1 { bail!("authentication challenge is invalid"); }
    if challenge.watch_ephemeral_public.iter().any(|byte| *byte != 0) { security::parse_public_key(&challenge.watch_ephemeral_public)?; }
    Ok(challenge)
}

pub fn encode_authentication_proof(transaction_id: u32, signature: &[u8]) -> Result<Vec<u8>> {
    check_pair_transaction(transaction_id)?;
    if signature.is_empty() || signature.len() > 80 { bail!("authentication signature has invalid size"); }
    let mut payload = Vec::new();
    push_u16_bytes(&mut payload, signature)?;
    encode_pair_frame(PairMessageType::AuthenticationProof, transaction_id, &payload)
}

pub fn decode_authentication_proof(payload: &[u8]) -> Result<Vec<u8>> {
    decode_length_prefixed(payload, 80, "authentication signature")
}

pub fn encode_credential_envelope(envelope: &CredentialEnvelope) -> Result<Vec<u8>> {
    check_pair_transaction(envelope.transaction_id)?;
    check_epoch(envelope.key_epoch)?;
    security::parse_public_key(&envelope.watch_ephemeral_public)?;
    check_nonzero(&envelope.nonce, "credential nonce")?;
    check_nonzero(&envelope.ack_nonce, "acknowledgement nonce")?;
    if envelope.ciphertext.is_empty() { bail!("credential ciphertext is invalid"); }
    let mut payload = Vec::with_capacity(1 + 4 + 33 + 13 + 13 + 2 + envelope.ciphertext.len());
    payload.push(envelope.purpose as u8);
    payload.extend_from_slice(&envelope.key_epoch.to_be_bytes());
    payload.extend_from_slice(&envelope.watch_ephemeral_public);
    payload.extend_from_slice(&envelope.nonce);
    payload.extend_from_slice(&envelope.ack_nonce);
    push_u16_bytes(&mut payload, &envelope.ciphertext)?;
    encode_pair_frame(PairMessageType::Credential, envelope.transaction_id, &payload)
}

pub fn decode_credential_envelope(transaction_id: u32, payload: &[u8]) -> Result<CredentialEnvelope> {
    check_pair_transaction(transaction_id)?;
    if payload.len() < 66 { bail!("credential envelope is truncated"); }
    let ciphertext_len = u16::from_be_bytes(payload[64..66].try_into()?) as usize;
    if ciphertext_len == 0 || payload.len() != 66 + ciphertext_len { bail!("credential envelope is invalid"); }
    let result = CredentialEnvelope {
        purpose: CredentialPurpose::try_from(payload[0])?,
        transaction_id,
        key_epoch: u32::from_be_bytes(payload[1..5].try_into()?),
        watch_ephemeral_public: payload[5..38].try_into()?,
        nonce: payload[38..51].try_into()?,
        ack_nonce: payload[51..64].try_into()?,
        ciphertext: payload[66..].to_vec(),
    };
    check_epoch(result.key_epoch)?;
    security::parse_public_key(&result.watch_ephemeral_public)?;
    check_nonzero(&result.nonce, "credential nonce")?;
    check_nonzero(&result.ack_nonce, "acknowledgement nonce")?;
    Ok(result)
}

pub fn encode_credential_ack(transaction_id: u32, proof: &[u8]) -> Result<Vec<u8>> {
    check_pair_transaction(transaction_id)?;
    if proof.is_empty() || proof.len() > 32 { bail!("credential acknowledgement is invalid"); }
    let mut payload = Vec::new();
    push_u16_bytes(&mut payload, proof)?;
    encode_pair_frame(PairMessageType::CredentialAck, transaction_id, &payload)
}

pub fn decode_credential_ack(payload: &[u8]) -> Result<Vec<u8>> {
    decode_length_prefixed(payload, 32, "credential acknowledgement")
}

pub fn encode_pairing_empty(kind: PairMessageType, transaction_id: u32) -> Result<Vec<u8>> {
    if !matches!(kind, PairMessageType::AuthenticationOk | PairMessageType::Rejected | PairMessageType::CredentialAckOk) { bail!("empty pairing response type is invalid"); }
    encode_pair_frame(kind, transaction_id, &[])
}

fn check_pair_transaction(value: u32) -> Result<()> {
    if value == 0 { bail!("pairing transaction is invalid"); }
    Ok(())
}

fn check_epoch(value: u32) -> Result<()> {
    if value == 0 { bail!("key epoch is invalid"); }
    Ok(())
}

fn check_nonzero(value: &[u8], label: &str) -> Result<()> {
    if value.is_empty() || !value.iter().any(|byte| *byte != 0) { bail!("{label} cannot be zero"); }
    Ok(())
}

fn pairing_name(value: &str) -> Result<&[u8]> {
    if value.is_empty() || value.len() > PAIR_MAX_AGENT_NAME_BYTES || value.chars().any(security::is_category_c) || value.nfc().collect::<String>() != value {
        bail!("Agent name must be nonempty NFC UTF-8 text up to 24 bytes without control characters");
    }
    Ok(value.as_bytes())
}

fn push_u16_bytes(out: &mut Vec<u8>, value: &[u8]) -> Result<()> {
    let size = u16::try_from(value.len()).map_err(|_| anyhow::anyhow!("length-prefixed field is too large"))?;
    out.extend_from_slice(&size.to_be_bytes());
    out.extend_from_slice(value);
    Ok(())
}

fn decode_length_prefixed(raw: &[u8], maximum: usize, label: &str) -> Result<Vec<u8>> {
    if raw.len() < 2 { bail!("{label} is truncated"); }
    let length = u16::from_be_bytes(raw[..2].try_into()?) as usize;
    if length == 0 || length > maximum || raw.len() != 2 + length { bail!("{label} has invalid size"); }
    Ok(raw[2..].to_vec())
}

pub fn encode_status_message(payload: &[u8], message_type: u8, sequence: u16, max_write_size: usize) -> Result<Vec<Vec<u8>>> {
    encode_status_message_with_codec(payload, message_type, sequence, max_write_size, StatusCodec::JsonV1)
}

pub fn encode_status_message_with_codec(payload: &[u8], message_type: u8, sequence: u16, max_write_size: usize, codec: StatusCodec) -> Result<Vec<Vec<u8>>> {
    if payload.is_empty() || payload.len() > MAX_STATUS_BYTES { bail!("invalid BLE message size"); }
    if !matches!(message_type, STATUS_MESSAGE | EVENT_MESSAGE) { bail!("invalid BLE message type"); }
    let chunk_size = max_write_size.checked_sub(STATUS_HEADER_BYTES).filter(|size| *size > 0)
        .ok_or_else(|| anyhow::anyhow!("BLE write capacity is too small"))?;
    let count = payload.len().div_ceil(chunk_size);
    if count > u8::MAX as usize { bail!("too many BLE fragments"); }
    let mut frames = Vec::with_capacity(count);
    for (index, chunk) in payload.chunks(chunk_size).enumerate() {
        let mut frame = Vec::with_capacity(STATUS_HEADER_BYTES + chunk.len());
        frame.extend_from_slice(&[codec.frame_version(), message_type]);
        frame.extend_from_slice(&sequence.to_le_bytes());
        frame.push(index as u8);
        frame.push(count as u8);
        frame.extend_from_slice(chunk);
        frames.push(frame);
    }
    Ok(frames)
}

pub fn status_write_capacity(advertised: Option<usize>) -> usize {
    advertised.filter(|value| *value >= STATUS_WRITE_SIZE_FALLBACK)
        .unwrap_or(STATUS_WRITE_SIZE_FALLBACK)
        .min(STATUS_WRITE_SIZE_MAX)
}

/// Return only the public status projections that are safe to send to the
/// watch or the local status WebSocket. Private task replies, notification IDs,
/// credential material, and collector-private `_ui_*` values never enter this
/// boundary.
pub fn status_messages(snapshot: &StatusSnapshot) -> Result<Vec<(u8, Vec<u8>)>> {
    status_messages_with_codec(snapshot, StatusCodec::JsonV1)
}

pub fn status_messages_with_codec(snapshot: &StatusSnapshot, codec: StatusCodec) -> Result<Vec<(u8, Vec<u8>)>> {
    let inventory = AgentInventory::from_telemetry(&snapshot.telemetry)?;
    let telemetry = telemetry_status(&snapshot.telemetry, codec)?;
    let mut messages = Vec::with_capacity(if inventory.is_some() { 4 } else { 3 });
    if let Some(inventory) = &inventory {
        let manifest = serde_json::json!({
            "source": "agent_inventory",
            "v": crate::agent_inventory::INVENTORY_VERSION,
            "agents": inventory.agents.iter().map(|agent| agent.as_str()).collect::<Vec<_>>(),
        });
        messages.push((
            STATUS_MESSAGE,
            compact_bounded(&manifest, MAX_STATUS_BYTES, "agent inventory", codec)?,
        ));
    }
    if inventory
        .as_ref()
        .is_none_or(|installed| installed.contains(AgentId::Codex))
    {
        let codex = codex_status(&snapshot.codex, codec)?;
        messages.push((
            STATUS_MESSAGE,
            compact_bounded(&codex, MAX_STATUS_BYTES, "Codex status", codec)?,
        ));
    }
    messages.push((
        STATUS_MESSAGE,
        compact_bounded(&telemetry, MAX_STATUS_BYTES, "telemetry status", codec)?,
    ));
    if inventory
        .as_ref()
        .is_none_or(|installed| installed.contains(AgentId::Trae))
    {
        let trae = trae_status(&snapshot.trae, codec)?;
        messages.push((
            STATUS_MESSAGE,
            compact_bounded(&trae, MAX_TRAE_MESSAGE_BYTES, "Trae status", codec)?,
        ));
    }
    Ok(messages)
}

pub(crate) fn is_trae_status_payload(payload: &[u8]) -> bool {
    serde_json::from_slice::<Value>(payload)
        .ok()
        .is_some_and(|value| value.get("source").and_then(Value::as_str) == Some("trae"))
}

pub(crate) fn is_trae_status_payload_with_codec(payload: &[u8], codec: StatusCodec) -> bool {
    if payload.len() > MAX_STATUS_BYTES { return false; }
    let value = match codec {
        StatusCodec::JsonV1 => return is_trae_status_payload(payload),
        StatusCodec::MessagePackV2 => rmp_serde::from_slice::<Value>(payload).ok(),
    };
    value
        .is_some_and(|value| value.get("source").and_then(Value::as_str) == Some("trae"))
}

const MAX_CODEX_ALERTS: usize = 8;
const MAX_CODEX_MODEL_CHARS: usize = 64;
const MAX_CODEX_EFFORT_CHARS: usize = 32;
const MAX_PROJECT_CHARS: usize = 32;
const MAX_TITLE_CHARS: usize = 48;
const MAX_TRAE_TASKS: usize = 4;
const MAX_TRAE_DETAIL_CHARS: usize = 72;
const MAX_TRAE_MESSAGE_BYTES: usize = 1024;
const MAX_TELEMETRY_INTEGER: u64 = 9_007_199_254_740_991;
const MAX_USAGE_MODELS: usize = 6;
const MIN_TELEMETRY_EPOCH: i64 = 946_684_800;
const MAX_TELEMETRY_EPOCH: i64 = 4_102_444_800;

fn codex_status(source: &Value, codec: StatusCodec) -> Result<Value> {
    let Some(source) = source.as_object() else {
        return Ok(Value::Object(Map::new()));
    };
    let mut result = Map::new();
    for (field, maximum) in [
        ("codex_5h_left_pct", 100),
        ("codex_week_left_pct", 100),
        ("codex_alert_pending", u16::MAX as u64),
        ("codex_alert_running", u16::MAX as u64),
        ("codex_alert_completed", u16::MAX as u64),
    ] {
        if let Some(value) = source.get(field) {
            if value.is_null() && matches!(field, "codex_5h_left_pct" | "codex_week_left_pct") {
                result.insert(field.into(), Value::Null);
            } else {
                result.insert(field.into(), Value::from(nonnegative_integer(value).min(maximum)));
            }
        }
    }
    if source.contains_key("codex_model") {
        result.insert("codex_model".into(), Value::String(strict_text(source.get("codex_model"), MAX_CODEX_MODEL_CHARS)));
    }
    if source.contains_key("codex_effort") {
        result.insert("codex_effort".into(), Value::String(strict_text(source.get("codex_effort"), MAX_CODEX_EFFORT_CHARS)));
    }
    if source.contains_key("codex_alert_revision") {
        let revision = source.get("codex_alert_revision").and_then(Value::as_u64)
            .filter(|n| *n <= MAX_TASK_DETAIL_REVISION).unwrap_or(0);
        result.insert("codex_alert_revision".into(), Value::from(revision));
    }

    let has_alerts = source.get("codex_alerts").is_some_and(Value::is_array);
    let mut alerts: Vec<Value> = source.get("codex_alerts").and_then(Value::as_array)
        .into_iter().flatten().filter_map(codex_alert).collect();
    alerts.sort_by_key(|alert| (alert.get("state").and_then(Value::as_str) != Some("pending")) as u8);
    alerts.truncate(MAX_CODEX_ALERTS);
    if has_alerts {
        result.insert("codex_alerts".into(), Value::Array(alerts));
    }

    while compact_len(&result, codec)? > MAX_STATUS_BYTES {
        let Some(Value::Array(alerts)) = result.get_mut("codex_alerts") else { break };
        if alerts.is_empty() { break; }
        alerts.pop();
    }
    if compact_len(&result, codec)? > MAX_STATUS_BYTES {
        result.remove("codex_effort");
        result.remove("codex_model");
    }
    if compact_len(&result, codec)? > MAX_STATUS_BYTES {
        let mut minimal = Map::new();
        for field in [
            "codex_5h_left_pct", "codex_week_left_pct", "codex_alert_pending",
            "codex_alert_running", "codex_alert_completed", "codex_alert_revision",
        ] {
            if let Some(value) = result.get(field) { minimal.insert(field.into(), value.clone()); }
        }
        if has_alerts { minimal.insert("codex_alerts".into(), Value::Array(Vec::new())); }
        result = minimal;
    }
    Ok(Value::Object(result))
}

fn codex_alert(value: &Value) -> Option<Value> {
    let source = value.as_object()?;
    let route = source.get("route")?.as_str()?;
    if route.len() != 16
        || route == "0000000000000000"
        || !route.bytes().all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return None;
    }
    let state = source.get("state")?.as_str()?;
    if !matches!(state, "pending" | "running") { return None; }
    let project = source.get("project")?.as_str()?;
    let title = source.get("title")?.as_str()?;
    let mut result = Map::new();
    result.insert("route".into(), Value::String(route.into()));
    result.insert("project".into(), Value::String(project.chars().take(MAX_PROJECT_CHARS).collect()));
    result.insert("title".into(), Value::String(title.chars().take(MAX_TITLE_CHARS).collect()));
    result.insert("state".into(), Value::String(state.into()));
    let model = source.get("model").and_then(Value::as_str).map(|s| s.chars().take(MAX_CODEX_MODEL_CHARS).collect::<String>()).unwrap_or_default();
    let effort = source.get("effort").and_then(Value::as_str).map(|s| s.chars().take(MAX_CODEX_EFFORT_CHARS).collect::<String>()).unwrap_or_default();
    if !model.is_empty() && !effort.is_empty() {
        result.insert("model".into(), Value::String(model));
        result.insert("effort".into(), Value::String(effort));
    }
    Some(Value::Object(result))
}

fn trae_status(source: &Value, codec: StatusCodec) -> Result<Value> {
    let source = source.as_object();
    let raw_tasks = source.and_then(|o| o.get("trae_tasks")).and_then(Value::as_array);
    let active: Vec<&Value> = raw_tasks.into_iter().flatten().filter(|task| {
        matches!(task.get("state").and_then(Value::as_str), Some("pending" | "running"))
    }).collect();
    let pending_count = source.and_then(|o| o.get("trae_pending_count"))
        .map(nonnegative_integer).unwrap_or_else(|| active.iter().filter(|task| task["state"] == "pending").count() as u64)
        .min(u16::MAX as u64);
    let running_count = source.and_then(|o| o.get("trae_running_count"))
        .map(nonnegative_integer).unwrap_or_else(|| active.iter().filter(|task| task["state"] == "running").count() as u64)
        .min(u16::MAX as u64);
    let process_state = source.and_then(|o| o.get("trae_process_state")).and_then(Value::as_str)
        .filter(|state| matches!(*state, "live" | "off" | "unknown"))
        .unwrap_or_else(|| if source.and_then(|o| o.get("trae_online")).and_then(Value::as_bool) == Some(true) { "live" } else { "off" });
    let mut result = Map::new();
    result.insert("type".into(), Value::String("status".into()));
    result.insert("source".into(), Value::String("trae".into()));
    result.insert("trae_online".into(), Value::Bool(source.and_then(|o| o.get("trae_online")).and_then(Value::as_bool) == Some(true)));
    result.insert("trae_model".into(), Value::String(bounded_text(source.and_then(|o| o.get("trae_model")), 64, "--")));
    result.insert("trae_effort".into(), Value::String(bounded_text(source.and_then(|o| o.get("trae_effort")), 32, "--")));
    result.insert("trae_revision".into(), Value::from(source.and_then(|o| o.get("trae_revision")).map(nonnegative_integer).unwrap_or(0).min(i32::MAX as u64)));
    result.insert("trae_process_state".into(), Value::String(process_state.into()));
    result.insert("trae_pending_count".into(), Value::from(pending_count));
    result.insert("trae_running_count".into(), Value::from(running_count));
    result.insert("trae_tasks_truncated".into(), Value::Bool(
        source.and_then(|o| o.get("trae_tasks_truncated")).and_then(Value::as_bool) == Some(true) || active.len() > MAX_TRAE_TASKS
    ));
    let tasks = active.iter().take(MAX_TRAE_TASKS).map(|task| {
        let mut entry = Map::new();
        for (key, limit) in [("id", 32), ("project", MAX_PROJECT_CHARS), ("title", MAX_TITLE_CHARS), ("detail", MAX_TRAE_DETAIL_CHARS), ("time", 8)] {
            entry.insert(key.into(), Value::String(bounded_text(task.get(key), limit, "")));
        }
        let state = task.get("state").and_then(Value::as_str).unwrap_or("pending");
        entry.insert("state".into(), Value::String(state.into()));
        entry.insert("model".into(), Value::String(bounded_text(task.get("model"), 64, "")));
        entry.insert("effort".into(), Value::String(bounded_text(task.get("effort"), 32, "")));
        Value::Object(entry)
    }).collect::<Vec<_>>();
    result.insert("trae_tasks".into(), Value::Array(tasks));

    while compact_len(&result, codec)? > MAX_TRAE_MESSAGE_BYTES {
        let Some(Value::Array(tasks)) = result.get_mut("trae_tasks") else { break };
        if tasks.len() <= 1 { break; }
        tasks.pop();
        result.insert("trae_tasks_truncated".into(), Value::Bool(true));
    }
    while compact_len(&result, codec)? > MAX_TRAE_MESSAGE_BYTES {
        let mut shortened = false;
        let mut had_tasks = false;
        if let Some(Value::Array(tasks)) = result.get_mut("trae_tasks") {
            for task in tasks.iter_mut().rev() {
                let Some(detail) = task.get("detail").and_then(Value::as_str).map(str::to_owned) else { continue };
                if !detail.is_empty() {
                    let shorter = detail.chars().take(detail.chars().count().saturating_sub(1)).collect();
                    task.as_object_mut().expect("task object").insert("detail".into(), Value::String(shorter));
                    shortened = true;
                    break;
                }
            }
            if !shortened {
                had_tasks = !tasks.is_empty();
                tasks.clear();
            }
        }
        if shortened || had_tasks { result.insert("trae_tasks_truncated".into(), Value::Bool(true)); }
        if !shortened { break; }
    }
    Ok(Value::Object(result))
}

fn telemetry_status(source: &Value, codec: StatusCodec) -> Result<Value> {
    let usage = source.get("usage").and_then(Value::as_object);
    let host = source.get("host").and_then(Value::as_object);
    let fallback_epoch = current_epoch().clamp(MIN_TELEMETRY_EPOCH, MAX_TELEMETRY_EPOCH);
    let usage_at = usage.and_then(|o| o.get("at")).and_then(exact_integer)
        .filter(|v| (MIN_TELEMETRY_EPOCH..=MAX_TELEMETRY_EPOCH).contains(v)).unwrap_or(fallback_epoch);
    let host_at = host.and_then(|o| o.get("at")).and_then(exact_integer)
        .filter(|v| (MIN_TELEMETRY_EPOCH..=MAX_TELEMETRY_EPOCH).contains(v)).unwrap_or(usage_at);
    let mut usage_out = Map::new();
    usage_out.insert("s".into(), Value::String(telemetry_state(usage.and_then(|o| o.get("s")))));
    usage_out.insert("q".into(), Value::String(telemetry_state(usage.and_then(|o| o.get("q")))));
    usage_out.insert("at".into(), Value::from(usage_at));
    usage_out.insert("qa".into(), nullable_telemetry_epoch(usage.and_then(|o| o.get("qa"))));
    for key in ["h", "w"] {
        usage_out.insert(key.into(), nullable_percentage_value(usage.and_then(|o| o.get(key))));
    }
    for key in ["hr", "wr"] {
        usage_out.insert(key.into(), nullable_telemetry_epoch(usage.and_then(|o| o.get(key))));
    }
    let tokens = usage.and_then(|o| o.get("t")).and_then(exact_integer)
        .filter(|v| *v >= 0 && *v as u64 <= MAX_TELEMETRY_INTEGER).map(|v| v as u64);
    usage_out.insert("t".into(), tokens.map_or(Value::Null, Value::from));
    usage_out.insert("c".into(), bounded_number(usage.and_then(|o| o.get("c")), 1_000_000_000.0));
    let cost_status = usage.and_then(|o| o.get("cs")).and_then(Value::as_str)
        .filter(|s| matches!(*s, "ok" | "partial" | "unavailable")).unwrap_or("unavailable");
    usage_out.insert("cs".into(), Value::String(cost_status.into()));
    let day = usage.and_then(|o| o.get("day")).and_then(Value::as_str).filter(|s| valid_telemetry_day(s)).unwrap_or("2000-01-01");
    usage_out.insert("day".into(), Value::String(day.into()));
    if let Some(providers) = usage.and_then(|o| o.get("p")).and_then(Value::as_array) {
        // Only the two explicitly configured provider projections are public.
        // Never forward collector-private fields or arbitrary provider objects.
        let mut visible = Vec::with_capacity(2);
        for name in ["claude", "opencode"] {
            if let Some(provider) = providers.iter().find(|value| value.get("n").and_then(Value::as_str) == Some(name)) {
                let mut output = Map::new();
                output.insert("n".into(), Value::String(name.into()));
                output.insert("s".into(), Value::String(telemetry_state(provider.get("s"))));
                for key in ["h", "w"] {
                    output.insert(key.into(), nullable_percentage_value(provider.get(key)));
                }
                for key in ["hr", "wr"] {
                    output.insert(key.into(), nullable_telemetry_epoch(provider.get(key)));
                }
                visible.push(Value::Object(output));
            }
        }
        if !visible.is_empty() { usage_out.insert("p".into(), Value::Array(visible)); }
    }
    if usage_out.get("s").and_then(Value::as_str) == Some("ok") {
        if let Some(detail) = usage.and_then(|o| o.get("d")).and_then(usage_detail) {
            usage_out.insert("d".into(), detail);
        }
    }

    let mut host_out = Map::new();
    host_out.insert("s".into(), Value::String(telemetry_state(host.and_then(|o| o.get("s")))));
    host_out.insert("at".into(), Value::from(host_at));
    for (key, maximum) in [("cpu", 100.0), ("mem", 100.0), ("disk", 100.0), ("up", 1_000_000_000.0), ("down", 1_000_000_000.0)] {
        host_out.insert(key.into(), bounded_number(host.and_then(|o| o.get(key)), maximum));
    }
    let mut result = Map::new();
    result.insert("telemetry_v".into(), Value::from(1));
    result.insert("usage".into(), Value::Object(usage_out));
    result.insert("host".into(), Value::Object(host_out));
    // Detail must never prevent host, quotas, or task status from being sent.
    // Account for the selected wire encoding, including its string overhead.
    while compact_len(&result, codec)? > MAX_STATUS_BYTES {
        let Some(usage) = result.get_mut("usage").and_then(Value::as_object_mut) else { break };
        let Some(detail) = usage.get_mut("d").and_then(Value::as_object_mut) else { break };
        let removed = detail.get_mut("m").and_then(Value::as_array_mut)
            .is_some_and(|rows| rows.pop().is_some());
        let more = detail.get("more").and_then(Value::as_u64)
            .and_then(|more| more.checked_add(1)).filter(|more| *more <= MAX_TELEMETRY_INTEGER);
        if let Some(more) = more.filter(|_| removed) {
            detail.insert("more".into(), Value::from(more));
        } else {
            usage.remove("d");
        }
    }
    Ok(Value::Object(result))
}

fn usage_detail(value: &Value) -> Option<Value> {
    let source = value.as_object()?;
    if source.get("v")?.as_u64()? != 1 { return None; }
    let origin = source.get("src")?.as_str()?;
    if !matches!(origin, "ccs_session" | "rollout") { return None; }
    let mut partial = source.get("p")?.as_bool()?;
    let models = source.get("m")?.as_array()?;
    let more = usage_integer(source.get("more")?)?
        .checked_add(models.len().saturating_sub(MAX_USAGE_MODELS) as u64)
        .filter(|more| *more <= MAX_TELEMETRY_INTEGER)?;
    let mut rows = models.iter().take(MAX_USAGE_MODELS)
        .map(|row| usage_model(row, origin)).collect::<Option<Vec<_>>>()?;
    partial |= rows.iter().any(|row| row["p"] == true);
    rows.sort_by(|a, b| b["t"].as_u64().cmp(&a["t"].as_u64())
        .then_with(|| a["m"].as_str().cmp(&b["m"].as_str())));
    if rows.iter().enumerate().any(|(index, row)| rows[..index].iter().any(|old| old["m"] == row["m"])) {
        return None;
    }
    Some(serde_json::json!({"v": 1, "src": origin, "p": partial, "m": rows, "more": more}))
}

fn usage_model(value: &Value, origin: &str) -> Option<Value> {
    let source = value.as_object()?;
    let name = source.get("m")?.as_str()?;
    if name.trim().is_empty() || name.len() > 48 || !name.bytes().all(|byte| (b' '..=b'~').contains(&byte)) {
        return None;
    }
    let mut result = Map::new();
    result.insert("m".into(), Value::String(name.into()));
    for key in ["n", "t", "i", "o", "r", "w"] {
        result.insert(key.into(), Value::from(usage_integer(source.get(key)?)?));
    }
    let total = ["i", "o", "r", "w"].iter()
        .try_fold(0_u64, |total, key| total.checked_add(result[*key].as_u64()?))?;
    let requests = result["n"].as_u64()?;
    if total != result["t"].as_u64()? {
        return None;
    }
    result.insert("p".into(), Value::Bool(source.get("p")?.as_bool()?));
    let cost = bounded_number(Some(source.get("c")?), 1_000_000_000.0);
    let cost_status = source.get("cs")?.as_str()?;
    if !matches!(cost_status, "ok" | "partial" | "unavailable") { return None; }
    result.insert("cs".into(), Value::String(if cost.is_null() { "unavailable" } else { cost_status }.into()));
    result.insert("c".into(), cost);
    for (value, count) in [("ts", "tn"), ("ft", "fn")] {
        let samples = if origin == "rollout" { 0 } else {
            source.get(count).and_then(usage_integer).filter(|samples| *samples <= requests).unwrap_or(0)
        };
        let metric = if samples == 0 { Value::Null }
            else { bounded_number(source.get(value), 1_000_000_000.0) };
        let usable = metric.as_f64().is_some_and(|value| value > 0.0);
        result.insert(count.into(), Value::from(if usable { samples } else { 0 }));
        result.insert(value.into(), if usable { metric } else { Value::Null });
    }
    Some(Value::Object(result))
}

fn usage_integer(value: &Value) -> Option<u64> {
    value.as_u64().filter(|value| *value <= MAX_TELEMETRY_INTEGER)
}

fn compact_bounded(value: &Value, maximum: usize, label: &str, codec: StatusCodec) -> Result<Vec<u8>> {
    let encoded = codec.encode(value)?;
    if encoded.is_empty() || encoded.len() > maximum { bail!("{label} exceeds protocol limit"); }
    Ok(encoded)
}

fn compact_len(value: &Map<String, Value>, codec: StatusCodec) -> Result<usize> { Ok(codec.encode(value)?.len()) }

/// Encode a public event/status object in the negotiated representation.
/// Snapshot callers must use `status_messages_with_codec` for whitelist projection.
pub fn encode_status_value(value: &Value, codec: StatusCodec) -> Result<Vec<u8>> {
    if !value.is_object() { bail!("status payload must be an object"); }
    compact_bounded(value, MAX_STATUS_BYTES, "status payload", codec)
}

fn strict_text(value: Option<&Value>, limit: usize) -> String {
    value.and_then(Value::as_str).map(|text| text.chars().take(limit).collect()).unwrap_or_default()
}

fn bounded_text(value: Option<&Value>, limit: usize, fallback: &str) -> String {
    let text = value.and_then(Value::as_str).unwrap_or_default().trim();
    let bounded: String = text.chars().take(limit).collect();
    if bounded.is_empty() { fallback.into() } else { bounded }
}

fn nonnegative_integer(value: &Value) -> u64 {
    if value.is_boolean() || value.is_null() { return 0; }
    if let Some(integer) = value.as_u64() { return integer; }
    if let Some(integer) = value.as_i64() { return integer.max(0) as u64; }
    value.as_f64().filter(|number| number.is_finite() && *number >= 0.0)
        .map(|number| if number >= u64::MAX as f64 { u64::MAX } else { number as u64 }).unwrap_or(0)
}

fn exact_integer(value: &Value) -> Option<i64> {
    if value.is_boolean() || value.is_null() { return None; }
    if let Some(integer) = value.as_i64() { return Some(integer); }
    if let Some(integer) = value.as_u64() { return i64::try_from(integer).ok(); }
    let number = value.as_f64()?;
    (number.is_finite() && number.fract() == 0.0 && number.abs() <= MAX_TELEMETRY_INTEGER as f64)
        .then_some(number as i64)
}

fn telemetry_state(value: Option<&Value>) -> String {
    match value.and_then(Value::as_str) {
        Some("ok") => "ok".into(),
        Some("stale" | "partial") => "stale".into(),
        Some("error") => "error".into(),
        _ => "unavailable".into(),
    }
}

fn nullable_telemetry_epoch(value: Option<&Value>) -> Value {
    value.and_then(exact_integer).filter(|epoch| (MIN_TELEMETRY_EPOCH..=MAX_TELEMETRY_EPOCH).contains(epoch))
        .map_or(Value::Null, Value::from)
}

fn nullable_percentage_value(value: Option<&Value>) -> Value {
    let Some(value) = value else { return Value::Null };
    if value.is_null() { return Value::Null; }
    exact_integer(value).filter(|number| (0..=100).contains(number))
        .map_or(Value::Null, Value::from)
}

fn bounded_number(value: Option<&Value>, maximum: f64) -> Value {
    let Some(number) = value.and_then(Value::as_f64).filter(|n| n.is_finite() && *n >= 0.0 && *n <= maximum) else { return Value::Null };
    serde_json::Number::from_f64(number).map_or(Value::Null, Value::Number)
}

fn valid_telemetry_day(value: &str) -> bool {
    value.len() == 10 && value.as_bytes().iter().enumerate().all(|(index, byte)| {
        if matches!(index, 4 | 7) { *byte == b'-' } else { byte.is_ascii_digit() }
    })
}

fn current_epoch() -> i64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_secs().min(i64::MAX as u64) as i64).unwrap_or(MIN_TELEMETRY_EPOCH)
}

pub const VOICE_VERSION: u8 = 6;
pub const VOICE_READY: u8 = 1;
pub const VOICE_UNAVAILABLE: u8 = 2;
pub const PTT_BEGIN: u8 = 1;
pub const AUDIO: u8 = 2;
pub const PTT_END: u8 = 3;
pub const VOICE_ERROR: u8 = 4;
pub const IMA_ADPCM: u8 = 1;
pub const PUSH_TO_TALK: u8 = 1;
pub const EXTERNAL_MICROPHONE: u8 = 2;
pub const PUSH_TO_TALK_CAPABILITY: u8 = 1;
pub const EXTERNAL_MICROPHONE_CAPABILITY: u8 = 2;
pub const ALL_VOICE_CAPABILITIES: u8 = PUSH_TO_TALK_CAPABILITY | EXTERNAL_MICROPHONE_CAPABILITY;
pub const VOICE_SAMPLE_RATE: u16 = 16_000;
pub const VOICE_BLOCK_SAMPLES: u16 = 320;
pub const VOICE_AUDIO_BYTES: usize = 164;
pub const VOICE_DRIVER_REQUIRED: u8 = 1;
pub const VOICE_RESTART_REQUIRED: u8 = 2;
pub const VOICE_ACCESSIBILITY_REQUIRED: u8 = 3;
pub const VOICE_TYPELESS_NOT_RUNNING: u8 = 4;
pub const VOICE_INITIALIZATION_FAILED: u8 = 5;

const VOICE_CONTROL_AAD_BYTES: usize = 16;
const VOICE_HEADER_BYTES: usize = 18;
const VOICE_BEGIN_BODY_BYTES: usize = 14;
const VOICE_END_BODY_BYTES: usize = 26;
const VOICE_ERROR_BODY_BYTES: usize = 4;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VoiceControl {
    pub kind: u8,
    pub reason: u8,
    pub capabilities: u8,
    pub key_epoch: u32,
    pub exchange_nonce: [u8; 8],
    pub target_bps: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VoiceBegin {
    pub codec: u8,
    pub mode: u8,
    pub sample_rate: u16,
    pub block_samples: u16,
    pub route: u64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VoiceEndSummary {
    pub generated_packets: u32,
    pub accepted_packets: u32,
    pub queue_overflows: u32,
    pub encode_failures: u32,
    pub captured_samples: u32,
    pub elapsed_ms: u32,
    pub negotiated_mtu: u16,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VoiceNotification {
    pub kind: u8,
    pub session: u16,
    pub sequence: u32,
    pub watch_ms: u32,
    pub key_epoch: u32,
    pub payload: Vec<u8>,
    pub session_nonce: Option<[u8; 8]>,
    pub begin: Option<VoiceBegin>,
}

pub fn encode_voice_ready(key: &[u8], exchange_nonce: &[u8; 8], key_epoch: u32, capabilities: u8) -> Result<Vec<u8>> {
    if capabilities == 0 || capabilities & !ALL_VOICE_CAPABILITIES != 0 { bail!("READY capabilities are invalid"); }
    encode_voice_control(key, VOICE_READY, 0, capabilities, key_epoch, exchange_nonce, 8192)
}

pub fn encode_voice_unavailable(key: &[u8], exchange_nonce: &[u8; 8], key_epoch: u32, reason: u8) -> Result<Vec<u8>> {
    if !matches!(reason, VOICE_DRIVER_REQUIRED | VOICE_RESTART_REQUIRED | VOICE_ACCESSIBILITY_REQUIRED | VOICE_TYPELESS_NOT_RUNNING | VOICE_INITIALIZATION_FAILED) {
        bail!("UNAVAILABLE reason is not defined");
    }
    encode_voice_control(key, VOICE_UNAVAILABLE, reason, 0, key_epoch, exchange_nonce, 0)
}

fn encode_voice_control(key: &[u8], kind: u8, reason: u8, capabilities: u8, key_epoch: u32, nonce: &[u8; 8], target_bps: u32) -> Result<Vec<u8>> {
    check_epoch(key_epoch)?;
    security::derive_nonce(if kind == VOICE_READY { READY_DOMAIN } else { UNAVAILABLE_DOMAIN }, nonce, 0)?;
    let domain = if kind == VOICE_READY { READY_DOMAIN } else { UNAVAILABLE_DOMAIN };
    let mut aad = Vec::with_capacity(VOICE_CONTROL_AAD_BYTES);
    aad.extend_from_slice(&[VOICE_VERSION, kind, reason, capabilities]);
    aad.extend_from_slice(&key_epoch.to_le_bytes());
    aad.extend_from_slice(nonce);
    let body = target_bps.to_le_bytes();
    let encrypted = security::encrypt(key, domain, nonce, 0, &body, &aad)?;
    aad.extend_from_slice(&encrypted);
    Ok(aad)
}

pub fn decode_voice_control(key: &[u8], raw: &[u8], expected_epoch: u32) -> Result<VoiceControl> {
    check_epoch(expected_epoch)?;
    if raw.len() != VOICE_CONTROL_AAD_BYTES + 4 + TAG_BYTES { bail!("invalid voice control size"); }
    let kind = raw[1];
    let domain = match kind { VOICE_READY => READY_DOMAIN, VOICE_UNAVAILABLE => UNAVAILABLE_DOMAIN, _ => bail!("unknown voice control kind") };
    let key_epoch = u32::from_le_bytes(raw[4..8].try_into()?);
    if raw[0] != VOICE_VERSION || key_epoch != expected_epoch { bail!("voice key epoch does not match"); }
    let nonce: [u8; 8] = raw[8..16].try_into()?;
    let plaintext = security::decrypt(key, domain, &nonce, 0, &raw[VOICE_CONTROL_AAD_BYTES..], &raw[..VOICE_CONTROL_AAD_BYTES])?;
    if plaintext.len() != 4 { bail!("invalid voice control body size"); }
    let target_bps = u32::from_le_bytes(plaintext[..4].try_into()?);
    let reason = raw[2];
    let capabilities = raw[3];
    if kind == VOICE_READY {
        if reason != 0 || target_bps != 8192 || capabilities == 0 || capabilities & !ALL_VOICE_CAPABILITIES != 0 { bail!("invalid READY control"); }
    } else if !matches!(reason, VOICE_DRIVER_REQUIRED | VOICE_RESTART_REQUIRED | VOICE_ACCESSIBILITY_REQUIRED | VOICE_TYPELESS_NOT_RUNNING | VOICE_INITIALIZATION_FAILED) || capabilities != 0 || target_bps != 0 {
        bail!("invalid UNAVAILABLE control");
    }
    Ok(VoiceControl { kind, reason, capabilities, key_epoch, exchange_nonce: nonce, target_bps })
}

pub fn pack_voice_notification(key: &[u8], kind: u8, session: u16, sequence: u32, watch_ms: u32, payload: &[u8], session_nonce: &[u8; 8], key_epoch: u32) -> Result<Vec<u8>> {
    validate_voice_header(session, sequence, watch_ms)?;
    check_epoch(key_epoch)?;
    validate_voice_payload(kind, sequence, payload)?;
    let domain = voice_notification_domain(kind)?;
    let clear_nonce_size = if kind == PTT_BEGIN { 8 } else { 0 };
    let payload_length = clear_nonce_size + payload.len() + TAG_BYTES;
    let mut header = Vec::with_capacity(VOICE_HEADER_BYTES);
    header.extend_from_slice(&[VOICE_VERSION, kind]);
    header.extend_from_slice(&session.to_le_bytes());
    header.extend_from_slice(&sequence.to_le_bytes());
    header.extend_from_slice(&watch_ms.to_le_bytes());
    header.extend_from_slice(&key_epoch.to_le_bytes());
    header.extend_from_slice(&(payload_length as u16).to_le_bytes());
    let aad = if kind == PTT_BEGIN {
        let mut value = header.clone();
        value.extend_from_slice(session_nonce);
        value
    } else { header.clone() };
    let ciphertext = security::encrypt(key, domain, session_nonce, sequence, payload, &aad)?;
    let mut result = aad;
    result.extend_from_slice(&ciphertext);
    Ok(result)
}

pub fn decode_voice_notification(key: &[u8], raw: &[u8], session_nonce: Option<&[u8; 8]>, expected_epoch: u32) -> Result<VoiceNotification> {
    check_epoch(expected_epoch)?;
    if raw.len() < VOICE_HEADER_BYTES { bail!("notification is shorter than its header"); }
    let kind = raw[1];
    let domain = voice_notification_domain(kind)?;
    let session = u16::from_le_bytes(raw[2..4].try_into()?);
    let sequence = u32::from_le_bytes(raw[4..8].try_into()?);
    let watch_ms = u32::from_le_bytes(raw[8..12].try_into()?);
    let key_epoch = u32::from_le_bytes(raw[12..16].try_into()?);
    let payload_len = u16::from_le_bytes(raw[16..18].try_into()?) as usize;
    if raw[0] != VOICE_VERSION || key_epoch != expected_epoch { bail!("voice key epoch does not match"); }
    validate_voice_header(session, sequence, watch_ms)?;
    if raw.len() != VOICE_HEADER_BYTES + payload_len { bail!("invalid notification payload size"); }
    let clear_nonce = if kind == PTT_BEGIN { 8 } else { 0 };
    let expected = clear_nonce + voice_payload_size(kind)? + TAG_BYTES;
    if payload_len != expected { bail!("invalid encrypted notification payload size"); }
    let (nonce, aad, ciphertext) = if kind == PTT_BEGIN {
        let nonce: [u8; 8] = raw[VOICE_HEADER_BYTES..VOICE_HEADER_BYTES + 8].try_into()?;
        let aad = &raw[..VOICE_HEADER_BYTES + 8];
        (nonce, aad, &raw[VOICE_HEADER_BYTES + 8..])
    } else {
        let nonce = session_nonce.ok_or_else(|| anyhow::anyhow!("session nonce is required for this notification"))?;
        (*nonce, &raw[..VOICE_HEADER_BYTES], &raw[VOICE_HEADER_BYTES..])
    };
    let payload = security::decrypt(key, domain, &nonce, sequence, ciphertext, aad)?;
    validate_voice_payload(kind, sequence, &payload)?;
    let begin = if kind == PTT_BEGIN { Some(decode_voice_begin(&payload)?) } else { None };
    Ok(VoiceNotification { kind, session, sequence, watch_ms, key_epoch, payload: payload.to_vec(), session_nonce: if kind == PTT_BEGIN { Some(nonce) } else { None }, begin })
}

fn voice_notification_domain(kind: u8) -> Result<u8> {
    match kind { PTT_BEGIN => Ok(PTT_BEGIN_DOMAIN), AUDIO => Ok(AUDIO_DOMAIN), PTT_END => Ok(PTT_END_DOMAIN), VOICE_ERROR => Ok(VOICE_ERROR_DOMAIN), _ => bail!("unknown notification kind") }
}

fn voice_payload_size(kind: u8) -> Result<usize> {
    match kind { PTT_BEGIN => Ok(VOICE_BEGIN_BODY_BYTES), AUDIO => Ok(VOICE_AUDIO_BYTES), PTT_END => Ok(VOICE_END_BODY_BYTES), VOICE_ERROR => Ok(VOICE_ERROR_BODY_BYTES), _ => bail!("unknown notification kind") }
}

fn validate_voice_header(session: u16, sequence: u32, _watch_ms: u32) -> Result<()> {
    if session == 0 { bail!("session must be 1..65535"); }
    // u32 inputs already express the complete wire range and are non-boolean.
    let _ = sequence;
    Ok(())
}

fn validate_voice_payload(kind: u8, sequence: u32, payload: &[u8]) -> Result<()> {
    if payload.len() != voice_payload_size(kind)? { bail!("invalid logical notification payload size"); }
    if kind == PTT_BEGIN {
        if sequence != 0 { bail!("PTT_BEGIN sequence must be zero"); }
        decode_voice_begin(payload)?;
    } else if sequence == 0 { bail!("session notification sequence must be nonzero"); }
    if kind == PTT_END { let _ = decode_voice_end(payload)?; }
    Ok(())
}

pub fn encode_voice_begin(begin: VoiceBegin) -> Result<[u8; VOICE_BEGIN_BODY_BYTES]> {
    if !matches!(begin.mode, PUSH_TO_TALK | EXTERNAL_MICROPHONE) { bail!("invalid voice session mode"); }
    if begin.codec != IMA_ADPCM || begin.sample_rate != VOICE_SAMPLE_RATE || begin.block_samples != VOICE_BLOCK_SAMPLES { bail!("invalid production codec tuple"); }
    let mut raw = [0u8; VOICE_BEGIN_BODY_BYTES];
    raw[0] = begin.codec;
    raw[1] = begin.mode;
    raw[2..4].copy_from_slice(&begin.sample_rate.to_le_bytes());
    raw[4..6].copy_from_slice(&begin.block_samples.to_le_bytes());
    raw[6..14].copy_from_slice(&begin.route.to_le_bytes());
    Ok(raw)
}

pub fn decode_voice_begin(raw: &[u8]) -> Result<VoiceBegin> {
    if raw.len() != VOICE_BEGIN_BODY_BYTES { bail!("invalid PTT_BEGIN body size"); }
    let begin = VoiceBegin { codec: raw[0], mode: raw[1], sample_rate: u16::from_le_bytes(raw[2..4].try_into()?), block_samples: u16::from_le_bytes(raw[4..6].try_into()?), route: u64::from_le_bytes(raw[6..14].try_into()?) };
    encode_voice_begin(begin)?;
    Ok(begin)
}

pub fn decode_voice_end(raw: &[u8]) -> Result<VoiceEndSummary> {
    if raw.len() != VOICE_END_BODY_BYTES { bail!("invalid PTT_END body size"); }
    Ok(VoiceEndSummary {
        generated_packets: u32::from_le_bytes(raw[0..4].try_into()?),
        accepted_packets: u32::from_le_bytes(raw[4..8].try_into()?),
        queue_overflows: u32::from_le_bytes(raw[8..12].try_into()?),
        encode_failures: u32::from_le_bytes(raw[12..16].try_into()?),
        captured_samples: u32::from_le_bytes(raw[16..20].try_into()?),
        elapsed_ms: u32::from_le_bytes(raw[20..24].try_into()?),
        negotiated_mtu: u16::from_le_bytes(raw[24..26].try_into()?),
    })
}

pub fn encode_voice_end(summary: VoiceEndSummary) -> [u8; VOICE_END_BODY_BYTES] {
    let mut raw = [0u8; VOICE_END_BODY_BYTES];
    for (index, value) in [summary.generated_packets, summary.accepted_packets, summary.queue_overflows, summary.encode_failures, summary.captured_samples, summary.elapsed_ms].into_iter().enumerate() {
        raw[index * 4..index * 4 + 4].copy_from_slice(&value.to_le_bytes());
    }
    raw[24..26].copy_from_slice(&summary.negotiated_mtu.to_le_bytes());
    raw
}

const IMA_INDEX_TABLE: [i32; 16] = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];
const IMA_STEP_TABLE: [i32; 89] = [
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767,
];

pub fn decode_ima_adpcm_block(payload: &[u8], sample_count: usize) -> Result<Vec<i16>> {
    if sample_count == 0 || sample_count > VOICE_BLOCK_SAMPLES as usize { bail!("sample count is invalid"); }
    if payload.len() != 4 + sample_count / 2 { bail!("invalid IMA ADPCM block size"); }
    let mut predictor = i16::from_le_bytes(payload[..2].try_into()?) as i32;
    let mut index = (payload[2] as usize).min(IMA_STEP_TABLE.len() - 1);
    let mut samples = Vec::with_capacity(sample_count);
    samples.push(predictor as i16);
    for sample_index in 1..sample_count {
        let byte = payload[4 + (sample_index - 1) / 2];
        let nibble = if sample_index & 1 == 1 { byte & 0x0f } else { byte >> 4 } as usize;
        let step = IMA_STEP_TABLE[index];
        let mut delta = step >> 3;
        if nibble & 4 != 0 { delta += step; }
        if nibble & 2 != 0 { delta += step >> 1; }
        if nibble & 1 != 0 { delta += step >> 2; }
        predictor += if nibble & 8 != 0 { -delta } else { delta };
        predictor = predictor.clamp(-32768, 32767);
        index = (index as i32 + IMA_INDEX_TABLE[nibble]).clamp(0, 88) as usize;
        samples.push(predictor as i16);
    }
    Ok(samples)
}

pub const INPUT_VERSION: u8 = 1;
pub const INPUT_READY: u8 = 1;
pub const INPUT_ACTION_SUBMIT: u8 = 1;
pub const INPUT_ACTION_FORCE_STEER: u8 = 2;
const INPUT_CONTROL_BYTES: usize = 12;
const INPUT_ACTION_HEADER_BYTES: usize = 12;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ActionPacket { pub action: u8, pub sequence: u32, pub watch_ms: u32 }

pub fn encode_input_ready(key: &[u8], nonce: &[u8; 8]) -> Result<Vec<u8>> {
    check_nonzero(nonce, "input connection nonce")?;
    let mut aad = Vec::with_capacity(INPUT_CONTROL_BYTES);
    aad.push(INPUT_VERSION);
    aad.push(INPUT_READY);
    aad.extend_from_slice(&0u16.to_le_bytes());
    aad.extend_from_slice(nonce);
    let tag = security::encrypt(key, INPUT_READY_DOMAIN, nonce, 0, &[], &aad)?;
    aad.extend_from_slice(&tag);
    Ok(aad)
}

pub fn decode_input_ready(key: &[u8], raw: &[u8]) -> Result<[u8; 8]> {
    if raw.len() != INPUT_CONTROL_BYTES + TAG_BYTES { bail!("invalid input Ready size"); }
    if raw[0] != INPUT_VERSION || raw[1] != INPUT_READY || raw[2..4] != [0, 0] { bail!("invalid input Ready header"); }
    let nonce: [u8; 8] = raw[4..12].try_into()?;
    check_nonzero(&nonce, "input connection nonce")?;
    let body = security::decrypt(key, INPUT_READY_DOMAIN, &nonce, 0, &raw[INPUT_CONTROL_BYTES..], &raw[..INPUT_CONTROL_BYTES])?;
    if !body.is_empty() { bail!("input Ready body must be empty"); }
    Ok(nonce)
}

pub fn encode_input_action(key: &[u8], action: u8, sequence: u32, watch_ms: u32, nonce: &[u8; 8]) -> Result<Vec<u8>> {
    validate_action(action, sequence, nonce)?;
    let mut header = Vec::with_capacity(INPUT_ACTION_HEADER_BYTES);
    header.push(INPUT_VERSION);
    header.push(action);
    header.extend_from_slice(&sequence.to_le_bytes());
    header.extend_from_slice(&watch_ms.to_le_bytes());
    header.extend_from_slice(&(TAG_BYTES as u16).to_le_bytes());
    let domain = if action == INPUT_ACTION_SUBMIT { INPUT_SUBMIT_DOMAIN } else { INPUT_FORCE_STEER_DOMAIN };
    let tag = security::encrypt(key, domain, nonce, sequence, &[], &header)?;
    header.extend_from_slice(&tag);
    Ok(header)
}

pub fn decode_input_action(key: &[u8], raw: &[u8], nonce: &[u8; 8]) -> Result<ActionPacket> {
    if raw.len() != INPUT_ACTION_HEADER_BYTES + TAG_BYTES { bail!("invalid input action size"); }
    let action = raw[1];
    let sequence = u32::from_le_bytes(raw[2..6].try_into()?);
    let watch_ms = u32::from_le_bytes(raw[6..10].try_into()?);
    if raw[0] != INPUT_VERSION || u16::from_le_bytes(raw[10..12].try_into()?) as usize != TAG_BYTES { bail!("invalid input action header"); }
    validate_action(action, sequence, nonce)?;
    let domain = if action == INPUT_ACTION_SUBMIT { INPUT_SUBMIT_DOMAIN } else { INPUT_FORCE_STEER_DOMAIN };
    let body = security::decrypt(key, domain, nonce, sequence, &raw[INPUT_ACTION_HEADER_BYTES..], &raw[..INPUT_ACTION_HEADER_BYTES])?;
    if !body.is_empty() { bail!("input action body must be empty"); }
    Ok(ActionPacket { action, sequence, watch_ms })
}

fn validate_action(action: u8, sequence: u32, nonce: &[u8; 8]) -> Result<()> {
    if !matches!(action, INPUT_ACTION_SUBMIT | INPUT_ACTION_FORCE_STEER) { bail!("unknown input action"); }
    if sequence == 0 { bail!("input sequence must be 1..4294967295"); }
    check_nonzero(nonce, "input connection nonce")
}

#[derive(Default, Debug)]
pub struct ActionReplayGuard { active_nonce: Option<[u8; 8]>, last_sequence: u32 }

impl ActionReplayGuard {
    pub fn set_active_nonce(&mut self, nonce: [u8; 8]) -> Result<()> {
        check_nonzero(&nonce, "input connection nonce")?;
        if self.active_nonce != Some(nonce) { self.active_nonce = Some(nonce); self.last_sequence = 0; }
        Ok(())
    }

    pub fn accept(&mut self, nonce: &[u8; 8], sequence: u32) -> bool {
        if self.active_nonce != Some(*nonce) || sequence == 0 || sequence <= self.last_sequence { return false; }
        self.last_sequence = sequence;
        true
    }
}

pub const TASK_DETAIL_TYPE: &str = "task_detail";
pub const TASK_DETAIL_VERSION: u64 = 1;
pub const MAX_TASK_DETAIL_REVISION: u64 = 0x7fff_ffff;
pub const MAX_TASK_DETAIL_CHUNKS: u64 = 16;
pub const MAX_TASK_DETAIL_CHUNK_BYTES: usize = 1300;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TaskDetailEnvelope {
    pub route: String,
    pub revision: u64,
    pub index: u64,
    pub count: u64,
    pub key_epoch: u32,
    pub exchange_nonce: [u8; 8],
    pub sequence: u32,
    pub ciphertext: String,
}

pub fn canonical_task_detail_aad(envelope: &TaskDetailEnvelope) -> Result<Vec<u8>> {
    validate_task_detail(envelope)?;
    Ok(format!("{{\"type\":\"task_detail\",\"version\":1,\"route\":\"{}\",\"revision\":{},\"index\":{},\"count\":{},\"epoch\":{},\"nonce\":\"{}\",\"sequence\":{}}}",
        envelope.route, envelope.revision, envelope.index, envelope.count, envelope.key_epoch,
        hex_lower(&envelope.exchange_nonce), envelope.sequence).into_bytes())
}

pub fn encrypt_task_detail(key: &[u8], key_epoch: u32, exchange_nonce: [u8; 8], sequence: u32, route: &str, revision: u64, index: u64, count: u64, text: &str) -> Result<TaskDetailEnvelope> {
    let mut envelope = TaskDetailEnvelope { route: route.to_owned(), revision, index, count, key_epoch, exchange_nonce, sequence, ciphertext: String::new() };
    let aad = canonical_task_detail_aad(&envelope)?;
    if text.len() > MAX_TASK_DETAIL_CHUNK_BYTES { bail!("task detail chunk exceeds protocol limit"); }
    let ciphertext = security::encrypt(key, TASK_DETAIL_DOMAIN, &exchange_nonce, sequence, text.as_bytes(), &aad)?;
    envelope.ciphertext = BASE64.encode(ciphertext);
    Ok(envelope)
}

pub fn decrypt_task_detail(key: &[u8], value: &Value) -> Result<String> {
    if !value.is_object() || value.get("type").and_then(Value::as_str) != Some(TASK_DETAIL_TYPE) { bail!("task detail envelope is invalid"); }
    let route = value.get("route").and_then(Value::as_str).ok_or_else(|| anyhow::anyhow!("task detail envelope is incomplete"))?.to_owned();
    let revision = strict_json_u64(value, "revision")?;
    let index = strict_json_u64(value, "index")?;
    let count = strict_json_u64(value, "count")?;
    let epoch = u32::try_from(strict_json_u64(value, "epoch")?).map_err(|_| anyhow::anyhow!("key epoch is invalid"))?;
    let nonce_hex = value.get("nonce").and_then(Value::as_str).ok_or_else(|| anyhow::anyhow!("task detail nonce is invalid"))?;
    let exchange_nonce = parse_hex8(nonce_hex)?;
    let sequence = u32::try_from(strict_json_u64(value, "sequence")?).map_err(|_| anyhow::anyhow!("sequence is invalid"))?;
    let ciphertext_text = value.get("ciphertext").and_then(Value::as_str).ok_or_else(|| anyhow::anyhow!("task detail ciphertext is invalid"))?;
    let mut envelope = TaskDetailEnvelope { route, revision, index, count, key_epoch: epoch, exchange_nonce, sequence, ciphertext: ciphertext_text.to_owned() };
    let aad = canonical_task_detail_aad(&envelope)?;
    let ciphertext = BASE64.decode(ciphertext_text.as_bytes()).map_err(|_| anyhow::anyhow!("task detail ciphertext is invalid"))?;
    let plaintext = security::decrypt(key, TASK_DETAIL_DOMAIN, &exchange_nonce, sequence, &ciphertext, &aad)?;
    if plaintext.len() > MAX_TASK_DETAIL_CHUNK_BYTES { bail!("task detail plaintext exceeds protocol limit"); }
    std::str::from_utf8(&plaintext).map(str::to_owned).map_err(|_| anyhow::anyhow!("task detail plaintext is not valid UTF-8"))
}

pub fn task_detail_to_value(envelope: &TaskDetailEnvelope) -> Result<Value> {
    canonical_task_detail_aad(envelope)?;
    let mut object = Map::new();
    object.insert("type".into(), Value::String(TASK_DETAIL_TYPE.into()));
    object.insert("route".into(), Value::String(envelope.route.clone()));
    object.insert("revision".into(), Value::from(envelope.revision));
    object.insert("index".into(), Value::from(envelope.index));
    object.insert("count".into(), Value::from(envelope.count));
    object.insert("epoch".into(), Value::from(envelope.key_epoch));
    object.insert("nonce".into(), Value::String(hex_lower(&envelope.exchange_nonce)));
    object.insert("sequence".into(), Value::from(envelope.sequence));
    object.insert("ciphertext".into(), Value::String(envelope.ciphertext.clone()));
    Ok(Value::Object(object))
}

#[derive(Serialize)]
struct BinaryTaskDetail<'a> {
    #[serde(rename = "type")]
    kind: &'static str,
    route: &'a str,
    revision: u64,
    index: u64,
    count: u64,
    epoch: u32,
    #[serde(serialize_with = "serialize_binary")]
    nonce: &'a [u8],
    sequence: u32,
    #[serde(serialize_with = "serialize_binary")]
    ciphertext: &'a [u8],
}

fn serialize_binary<S: Serializer>(bytes: &[u8], serializer: S) -> std::result::Result<S::Ok, S::Error> {
    serializer.serialize_bytes(bytes)
}

pub fn encode_task_detail(envelope: &TaskDetailEnvelope, codec: StatusCodec) -> Result<Vec<u8>> {
    if codec == StatusCodec::JsonV1 {
        return encode_status_value(&task_detail_to_value(envelope)?, codec);
    }
    // Keep the public legacy envelope and canonical AAD unchanged. Only the
    // external MessagePack fields use native bytes instead of hex/Base64 text.
    validate_task_detail(envelope)?;
    let ciphertext = BASE64.decode(envelope.ciphertext.as_bytes())
        .map_err(|_| anyhow::anyhow!("task detail ciphertext is invalid"))?;
    if !(TAG_BYTES..=MAX_TASK_DETAIL_CHUNK_BYTES + TAG_BYTES).contains(&ciphertext.len()) {
        bail!("task detail ciphertext has invalid size");
    }
    let value = BinaryTaskDetail {
        kind: TASK_DETAIL_TYPE,
        route: &envelope.route,
        revision: envelope.revision,
        index: envelope.index,
        count: envelope.count,
        epoch: envelope.key_epoch,
        nonce: &envelope.exchange_nonce,
        sequence: envelope.sequence,
        ciphertext: &ciphertext,
    };
    let payload = codec.encode(&value)?;
    if payload.len() > MAX_STATUS_BYTES { bail!("encrypted task detail exceeds BLE limit"); }
    Ok(payload)
}

fn validate_task_detail(value: &TaskDetailEnvelope) -> Result<()> {
    check_epoch(value.key_epoch)?;
    check_nonzero(&value.exchange_nonce, "exchange nonce")?;
    if value.sequence == 0 { bail!("sequence is outside its valid range"); }
    if value.route.len() != 16 || !value.route.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)) || value.route == "0000000000000000" { bail!("route must be nonzero lowercase 16-digit hex"); }
    if value.revision > MAX_TASK_DETAIL_REVISION { bail!("revision is outside its valid range"); }
    if value.count == 0 || value.count > MAX_TASK_DETAIL_CHUNKS || value.index >= value.count { bail!("task detail chunk index/count is invalid"); }
    Ok(())
}

fn strict_json_u64(value: &Value, key: &str) -> Result<u64> {
    value.get(key).and_then(Value::as_u64).ok_or_else(|| anyhow::anyhow!("task detail {key} is invalid"))
}

fn parse_hex8(text: &str) -> Result<[u8; 8]> {
    if text.len() != 16 || !text.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)) { bail!("task detail nonce is invalid"); }
    let mut bytes = [0u8; 8];
    for (i, pair) in text.as_bytes().chunks_exact(2).enumerate() {
        bytes[i] = (hex_nibble(pair[0])? << 4) | hex_nibble(pair[1])?;
    }
    check_nonzero(&bytes, "task detail nonce")?;
    Ok(bytes)
}

fn hex_nibble(value: u8) -> Result<u8> {
    match value { b'0'..=b'9' => Ok(value - b'0'), b'a'..=b'f' => Ok(value - b'a' + 10), _ => bail!("invalid lowercase hexadecimal") }
}

fn hex_lower(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let mut result = String::with_capacity(bytes.len() * 2);
    for byte in bytes { result.push(DIGITS[(byte >> 4) as usize] as char); result.push(DIGITS[(byte & 0x0f) as usize] as char); }
    result
}

pub const PET_VERSION: u8 = 1;
pub const PET_MAX_CHUNK_BYTES: usize = 480;
pub const PET_MAX_PACKAGE_BYTES: usize = 256 * 1024;
pub const PET_TOMBSTONES: usize = 8;
pub const PET_PREFIX_BYTES: usize = 14;
pub const PET_MAX_FRAME_BYTES: usize = PET_PREFIX_BYTES + 10 + PET_MAX_CHUNK_BYTES + TAG_BYTES;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum PetMessageKind {
    Query = 0x01,
    Begin = 0x02,
    Chunk = 0x03,
    Commit = 0x04,
    Cancel = 0x05,
    Restore = 0x06,
    State = 0x81,
    Progress = 0x82,
    Committed = 0x83,
    Cancelled = 0x84,
    Error = 0xff,
}

impl TryFrom<u8> for PetMessageKind {
    type Error = anyhow::Error;
    fn try_from(value: u8) -> Result<Self> {
        match value {
            0x01 => Ok(Self::Query), 0x02 => Ok(Self::Begin), 0x03 => Ok(Self::Chunk),
            0x04 => Ok(Self::Commit), 0x05 => Ok(Self::Cancel), 0x06 => Ok(Self::Restore),
            0x81 => Ok(Self::State), 0x82 => Ok(Self::Progress), 0x83 => Ok(Self::Committed),
            0x84 => Ok(Self::Cancelled), 0xff => Ok(Self::Error),
            _ => bail!("unknown pet message kind"),
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum PetSource { BuiltIn = 0, Custom = 1 }

impl TryFrom<u8> for PetSource {
    type Error = anyhow::Error;
    fn try_from(value: u8) -> Result<Self> {
        match value { 0 => Ok(Self::BuiltIn), 1 => Ok(Self::Custom), _ => bail!("unknown pet source") }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u16)]
pub enum PetErrorReason {
    InvalidMessage = 1, Unauthorized = 2, Busy = 3, UnsupportedSchema = 4,
    InvalidMetadata = 5, InvalidOffset = 6, Replay = 7, InsufficientStorage = 8,
    HashMismatch = 9, IoFailure = 10, Timeout = 11, UnusablePackage = 12,
    TooLarge = 13, Internal = 14,
}

impl TryFrom<u16> for PetErrorReason {
    type Error = anyhow::Error;
    fn try_from(value: u16) -> Result<Self> {
        match value {
            1 => Ok(Self::InvalidMessage), 2 => Ok(Self::Unauthorized), 3 => Ok(Self::Busy),
            4 => Ok(Self::UnsupportedSchema), 5 => Ok(Self::InvalidMetadata), 6 => Ok(Self::InvalidOffset),
            7 => Ok(Self::Replay), 8 => Ok(Self::InsufficientStorage), 9 => Ok(Self::HashMismatch),
            10 => Ok(Self::IoFailure), 11 => Ok(Self::Timeout), 12 => Ok(Self::UnusablePackage),
            13 => Ok(Self::TooLarge), 14 => Ok(Self::Internal), _ => bail!("unknown pet error reason"),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PetControlPayload {
    Query { transaction_id: u32 },
    Begin { transaction_id: u32, schema: u16, width: u16, height: u16, frame_count: u16, package_length: u32, sha256: [u8; 32] },
    Chunk { transaction_id: u32, offset: u32, data: Vec<u8> },
    Transaction { transaction_id: u32 },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PetControl { pub kind: PetMessageKind, pub sequence: u32, pub exchange_nonce: [u8; 8], pub payload: PetControlPayload }

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum PetStatusPayload {
    State { transaction_id: u32, source: PetSource, usable: bool, schema_min: u16, schema_max: u16, generation: u32, active_sha256: [u8; 32] },
    Progress { transaction_id: u32, acknowledged_bytes: u32, total_bytes: u32 },
    Committed { transaction_id: u32, generation: u32, active_sha256: [u8; 32] },
    Cancelled { transaction_id: u32 },
    Error { transaction_id: u32, reason: PetErrorReason, required_bytes: u32, available_bytes: u32 },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PetStatus { pub kind: PetMessageKind, pub sequence: u32, pub exchange_nonce: [u8; 8], pub payload: PetStatusPayload }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReplayDecision { Rejected, Accepted, Duplicate }

fn pet_control_body(control: &PetControl) -> Result<Vec<u8>> {
    let mut body = Vec::new();
    match (control.kind, &control.payload) {
        (PetMessageKind::Query, PetControlPayload::Query { transaction_id }) => { put_u32(&mut body, *transaction_id)?; }
        (PetMessageKind::Begin, PetControlPayload::Begin { transaction_id, schema, width, height, frame_count, package_length, sha256 }) => {
            put_u32(&mut body, *transaction_id)?;
            if *schema == 0 || *width == 0 || *height == 0 || !(1..=24).contains(frame_count) || *package_length == 0 || *package_length as usize > PET_MAX_PACKAGE_BYTES || !sha256.iter().any(|b| *b != 0) { bail!("pet Begin metadata is invalid"); }
            body.extend_from_slice(&schema.to_le_bytes()); body.extend_from_slice(&width.to_le_bytes()); body.extend_from_slice(&height.to_le_bytes()); body.extend_from_slice(&frame_count.to_le_bytes()); body.extend_from_slice(&package_length.to_le_bytes()); body.extend_from_slice(sha256);
        }
        (PetMessageKind::Chunk, PetControlPayload::Chunk { transaction_id, offset, data }) => {
            put_u32(&mut body, *transaction_id)?;
            if data.is_empty() || data.len() > PET_MAX_CHUNK_BYTES || data.len() > u16::MAX as usize || offset.checked_add(data.len() as u32).is_none() { bail!("pet Chunk is invalid"); }
            body.extend_from_slice(&offset.to_le_bytes()); body.extend_from_slice(&(data.len() as u16).to_le_bytes()); body.extend_from_slice(data);
        }
        (PetMessageKind::Commit | PetMessageKind::Cancel | PetMessageKind::Restore, PetControlPayload::Transaction { transaction_id }) => { put_u32(&mut body, *transaction_id)?; }
        _ => bail!("pet control kind and body do not match"),
    }
    Ok(body)
}

fn pet_status_body(status: &PetStatus) -> Result<Vec<u8>> {
    let mut body = Vec::new();
    match (status.kind, &status.payload) {
        (PetMessageKind::State, PetStatusPayload::State { transaction_id, source, usable, schema_min, schema_max, generation, active_sha256 }) => {
            put_u32(&mut body, *transaction_id)?;
            body.push(*source as u8); body.push(u8::from(*usable));
            if *schema_min == 0 || *schema_max == 0 || schema_min > schema_max { bail!("pet State schema range is invalid"); }
            body.extend_from_slice(&schema_min.to_le_bytes()); body.extend_from_slice(&schema_max.to_le_bytes()); body.extend_from_slice(&0u16.to_le_bytes()); body.extend_from_slice(&generation.to_le_bytes());
            if (*source == PetSource::BuiltIn && active_sha256.iter().any(|b| *b != 0)) || (*source == PetSource::Custom && !active_sha256.iter().any(|b| *b != 0)) { bail!("pet State hash conflicts with source"); }
            body.extend_from_slice(active_sha256);
        }
        (PetMessageKind::Progress, PetStatusPayload::Progress { transaction_id, acknowledged_bytes, total_bytes }) => {
            put_u32(&mut body, *transaction_id)?;
            if *total_bytes == 0 || *total_bytes as usize > PET_MAX_PACKAGE_BYTES || acknowledged_bytes > total_bytes { bail!("pet Progress is invalid"); }
            body.extend_from_slice(&acknowledged_bytes.to_le_bytes()); body.extend_from_slice(&total_bytes.to_le_bytes());
        }
        (PetMessageKind::Committed, PetStatusPayload::Committed { transaction_id, generation, active_sha256 }) => {
            put_u32(&mut body, *transaction_id)?;
            if *generation == 0 || !active_sha256.iter().any(|b| *b != 0) { bail!("pet Committed status is invalid"); }
            body.extend_from_slice(&generation.to_le_bytes()); body.extend_from_slice(active_sha256);
        }
        (PetMessageKind::Cancelled, PetStatusPayload::Cancelled { transaction_id }) => { put_u32(&mut body, *transaction_id)?; }
        (PetMessageKind::Error, PetStatusPayload::Error { transaction_id, reason, required_bytes, available_bytes }) => {
            put_u32(&mut body, *transaction_id)?;
            body.extend_from_slice(&(*reason as u16).to_le_bytes()); body.extend_from_slice(&0u16.to_le_bytes());
            if matches!(reason, PetErrorReason::InsufficientStorage | PetErrorReason::TooLarge) {
                if required_bytes <= available_bytes { bail!("sized pet error byte counts are invalid"); }
            } else if *required_bytes != 0 || *available_bytes != 0 { bail!("unsized pet error byte counts must be zero"); }
            body.extend_from_slice(&required_bytes.to_le_bytes()); body.extend_from_slice(&available_bytes.to_le_bytes());
        }
        _ => bail!("pet status kind and body do not match"),
    }
    Ok(body)
}

fn encode_pet(key: &[u8], nonce: &[u8; 8], sequence: u32, kind: PetMessageKind, body: &[u8], domain: u8) -> Result<Vec<u8>> {
    if sequence == 0 { bail!("pet sequence must be 1..4294967295"); }
    check_nonzero(nonce, "exchange nonce")?;
    let mut prefix = Vec::with_capacity(PET_PREFIX_BYTES);
    prefix.push(kind as u8); prefix.push(PET_VERSION); prefix.extend_from_slice(&sequence.to_le_bytes()); prefix.extend_from_slice(nonce);
    let encrypted = security::encrypt(key, domain, nonce, sequence, body, &prefix)?;
    prefix.extend_from_slice(&encrypted);
    if prefix.len() > PET_MAX_FRAME_BYTES { bail!("pet frame is too large"); }
    Ok(prefix)
}

pub fn encode_pet_control(key: &[u8], nonce: &[u8; 8], sequence: u32, kind: PetMessageKind, payload: PetControlPayload) -> Result<Vec<u8>> {
    if !matches!(kind, PetMessageKind::Query | PetMessageKind::Begin | PetMessageKind::Chunk | PetMessageKind::Commit | PetMessageKind::Cancel | PetMessageKind::Restore) { bail!("pet message kind is on the wrong channel"); }
    let body = pet_control_body(&PetControl { kind, sequence, exchange_nonce: *nonce, payload })?;
    encode_pet(key, nonce, sequence, kind, &body, PET_CONTROL_DOMAIN)
}

pub fn encode_pet_status(key: &[u8], nonce: &[u8; 8], sequence: u32, kind: PetMessageKind, payload: PetStatusPayload) -> Result<Vec<u8>> {
    if !matches!(kind, PetMessageKind::State | PetMessageKind::Progress | PetMessageKind::Committed | PetMessageKind::Cancelled | PetMessageKind::Error) { bail!("pet message kind is on the wrong channel"); }
    let body = pet_status_body(&PetStatus { kind, sequence, exchange_nonce: *nonce, payload })?;
    encode_pet(key, nonce, sequence, kind, &body, PET_STATUS_DOMAIN)
}

pub fn decode_pet_control(key: &[u8], frame: &[u8]) -> Result<PetControl> {
    let (kind, sequence, nonce, body) = decode_pet_prefix(key, frame, true)?;
    let payload = match kind {
        PetMessageKind::Query => { exact_body(&body, 4)?; PetControlPayload::Query { transaction_id: u32::from_le_bytes(body[..4].try_into()?) } }
        PetMessageKind::Begin => {
            exact_body(&body, 48)?;
            PetControlPayload::Begin { transaction_id: u32::from_le_bytes(body[..4].try_into()?), schema: u16::from_le_bytes(body[4..6].try_into()?), width: u16::from_le_bytes(body[6..8].try_into()?), height: u16::from_le_bytes(body[8..10].try_into()?), frame_count: u16::from_le_bytes(body[10..12].try_into()?), package_length: u32::from_le_bytes(body[12..16].try_into()?), sha256: body[16..48].try_into()? }
        }
        PetMessageKind::Chunk => {
            if body.len() < 10 { bail!("invalid pet Chunk body size"); }
            let length = u16::from_le_bytes(body[8..10].try_into()?) as usize;
            if length != body.len() - 10 { bail!("pet Chunk data length mismatch"); }
            PetControlPayload::Chunk { transaction_id: u32::from_le_bytes(body[..4].try_into()?), offset: u32::from_le_bytes(body[4..8].try_into()?), data: body[10..].to_vec() }
        }
        PetMessageKind::Commit | PetMessageKind::Cancel | PetMessageKind::Restore => { exact_body(&body, 4)?; PetControlPayload::Transaction { transaction_id: u32::from_le_bytes(body[..4].try_into()?) } }
        _ => bail!("pet message kind is on the wrong channel"),
    };
    let decoded = PetControl { kind, sequence, exchange_nonce: nonce, payload };
    if pet_control_body(&decoded)? != body { bail!("non-canonical pet control body"); }
    Ok(decoded)
}

pub fn decode_pet_status(key: &[u8], frame: &[u8]) -> Result<PetStatus> {
    let (kind, sequence, nonce, body) = decode_pet_prefix(key, frame, false)?;
    let payload = match kind {
        PetMessageKind::State => {
            exact_body(&body, 48)?;
            if body[10..12] != [0, 0] || !matches!(body[5], 0 | 1) { bail!("invalid pet State reserved or usable field"); }
            PetStatusPayload::State { transaction_id: u32::from_le_bytes(body[..4].try_into()?), source: PetSource::try_from(body[4])?, usable: body[5] == 1, schema_min: u16::from_le_bytes(body[6..8].try_into()?), schema_max: u16::from_le_bytes(body[8..10].try_into()?), generation: u32::from_le_bytes(body[12..16].try_into()?), active_sha256: body[16..48].try_into()? }
        }
        PetMessageKind::Progress => { exact_body(&body, 12)?; PetStatusPayload::Progress { transaction_id: u32::from_le_bytes(body[..4].try_into()?), acknowledged_bytes: u32::from_le_bytes(body[4..8].try_into()?), total_bytes: u32::from_le_bytes(body[8..12].try_into()?) } }
        PetMessageKind::Committed => { exact_body(&body, 40)?; PetStatusPayload::Committed { transaction_id: u32::from_le_bytes(body[..4].try_into()?), generation: u32::from_le_bytes(body[4..8].try_into()?), active_sha256: body[8..40].try_into()? } }
        PetMessageKind::Cancelled => { exact_body(&body, 4)?; PetStatusPayload::Cancelled { transaction_id: u32::from_le_bytes(body[..4].try_into()?) } }
        PetMessageKind::Error => {
            exact_body(&body, 16)?;
            if body[6..8] != [0, 0] { bail!("invalid pet Error reserved field"); }
            PetStatusPayload::Error { transaction_id: u32::from_le_bytes(body[..4].try_into()?), reason: PetErrorReason::try_from(u16::from_le_bytes(body[4..6].try_into()?))?, required_bytes: u32::from_le_bytes(body[8..12].try_into()?), available_bytes: u32::from_le_bytes(body[12..16].try_into()?) }
        }
        _ => bail!("pet message kind is on the wrong channel"),
    };
    let decoded = PetStatus { kind, sequence, exchange_nonce: nonce, payload };
    if pet_status_body(&decoded)? != body { bail!("non-canonical pet status body"); }
    Ok(decoded)
}

fn decode_pet_prefix(key: &[u8], frame: &[u8], control: bool) -> Result<(PetMessageKind, u32, [u8; 8], Vec<u8>)> {
    if frame.len() < PET_PREFIX_BYTES + TAG_BYTES + 4 || frame.len() > PET_MAX_FRAME_BYTES { bail!("invalid pet frame size"); }
    let kind = PetMessageKind::try_from(frame[0])?;
    let right_channel = if control { matches!(kind, PetMessageKind::Query | PetMessageKind::Begin | PetMessageKind::Chunk | PetMessageKind::Commit | PetMessageKind::Cancel | PetMessageKind::Restore) } else { matches!(kind, PetMessageKind::State | PetMessageKind::Progress | PetMessageKind::Committed | PetMessageKind::Cancelled | PetMessageKind::Error) };
    if !right_channel || frame[1] != PET_VERSION { bail!("unsupported pet message channel or version"); }
    let sequence = u32::from_le_bytes(frame[2..6].try_into()?);
    if sequence == 0 { bail!("pet sequence must be 1..4294967295"); }
    let nonce: [u8; 8] = frame[6..14].try_into()?;
    check_nonzero(&nonce, "exchange nonce")?;
    let domain = if control { PET_CONTROL_DOMAIN } else { PET_STATUS_DOMAIN };
    let body = security::decrypt(key, domain, &nonce, sequence, &frame[PET_PREFIX_BYTES..], &frame[..PET_PREFIX_BYTES])?;
    Ok((kind, sequence, nonce, body.to_vec()))
}

fn exact_body(body: &[u8], size: usize) -> Result<()> {
    if body.len() != size { bail!("invalid pet body size"); }
    Ok(())
}

fn put_u32(body: &mut Vec<u8>, value: u32) -> Result<()> {
    if value == 0 { bail!("pet transaction identifier must be nonzero"); }
    body.extend_from_slice(&value.to_le_bytes());
    Ok(())
}

#[derive(Default, Debug)]
pub struct PetControlReplayGuard {
    generation: Option<u32>, active_nonce: Option<[u8; 8]>, transaction_id: Option<u32>,
    last_sequence: u32, next_offset: u32, last_chunk_sequence: u32, last_chunk_offset: u32,
    last_chunk_frame: Option<Vec<u8>>, tombstones: VecDeque<(u32, [u8; 8])>,
}

impl PetControlReplayGuard {
    pub fn set_connection_generation(&mut self, generation: u32) -> Result<()> {
        if generation == 0 { bail!("connection generation must be nonzero"); }
        if self.generation != Some(generation) {
            self.generation = Some(generation); self.active_nonce = None; self.transaction_id = None;
            self.last_sequence = 0; self.next_offset = 0; self.last_chunk_sequence = 0;
            self.last_chunk_offset = 0; self.last_chunk_frame = None; self.tombstones.clear();
        }
        Ok(())
    }

    pub fn begin(&mut self, nonce: [u8; 8], transaction_id: u32) -> Result<bool> {
        check_nonzero(&nonce, "exchange nonce")?; check_pair_transaction(transaction_id)?;
        if self.generation.is_none() || self.active_nonce.is_some() || self.tombstones.iter().any(|(id, old)| *id == transaction_id || *old == nonce) { return Ok(false); }
        self.active_nonce = Some(nonce); self.transaction_id = Some(transaction_id); self.last_sequence = 0;
        self.next_offset = 0; self.last_chunk_sequence = 0; self.last_chunk_offset = 0; self.last_chunk_frame = None;
        Ok(true)
    }

    pub fn accept(&mut self, control: &PetControl, frame: &[u8]) -> Result<ReplayDecision> {
        let transaction_id = pet_control_transaction(&control.payload);
        if self.active_nonce != Some(control.exchange_nonce) || self.transaction_id != Some(transaction_id) { return Ok(ReplayDecision::Rejected); }
        let _ = pet_control_body(control)?;
        if control.sequence == 0 { return Ok(ReplayDecision::Rejected); }
        if self.last_sequence == 0 && control.sequence != 1 { return Ok(ReplayDecision::Rejected); }
        if control.sequence == self.last_sequence {
            if matches!(&control.payload, PetControlPayload::Chunk { .. }) && control.sequence == self.last_chunk_sequence && frame == self.last_chunk_frame.as_deref().unwrap_or_default() { return Ok(ReplayDecision::Duplicate); }
            return Ok(ReplayDecision::Rejected);
        }
        if control.sequence < self.last_sequence { return Ok(ReplayDecision::Rejected); }
        if let PetControlPayload::Chunk { offset, data, .. } = &control.payload {
            if *offset != self.next_offset { return Ok(ReplayDecision::Rejected); }
            self.next_offset = self.next_offset.checked_add(data.len() as u32).ok_or_else(|| anyhow::anyhow!("pet chunk offset overflow"))?;
            self.last_chunk_sequence = control.sequence; self.last_chunk_offset = *offset; self.last_chunk_frame = Some(frame.to_vec());
        } else { self.last_chunk_sequence = 0; self.last_chunk_frame = None; }
        self.last_sequence = control.sequence;
        Ok(ReplayDecision::Accepted)
    }

    pub fn retire(&mut self, completed: bool) {
        if completed { if let (Some(id), Some(nonce)) = (self.transaction_id, self.active_nonce) { if self.tombstones.len() == PET_TOMBSTONES { self.tombstones.pop_front(); } self.tombstones.push_back((id, nonce)); } }
        self.active_nonce = None; self.transaction_id = None; self.last_sequence = 0; self.next_offset = 0;
        self.last_chunk_sequence = 0; self.last_chunk_offset = 0; self.last_chunk_frame = None;
    }
}

fn pet_control_transaction(payload: &PetControlPayload) -> u32 {
    match payload { PetControlPayload::Query { transaction_id } | PetControlPayload::Begin { transaction_id, .. } | PetControlPayload::Chunk { transaction_id, .. } | PetControlPayload::Transaction { transaction_id } => *transaction_id }
}

#[derive(Default, Debug)]
pub struct PetStatusReplayGuard { active_nonce: Option<[u8; 8]>, last_sequence: u32 }

impl PetStatusReplayGuard {
    pub fn set_active_nonce(&mut self, nonce: [u8; 8]) -> Result<()> {
        check_nonzero(&nonce, "exchange nonce")?;
        if self.active_nonce.is_some() { bail!("status replay guard is already active"); }
        self.active_nonce = Some(nonce); self.last_sequence = 0; Ok(())
    }

    pub fn accept(&mut self, status: &PetStatus) -> Result<bool> {
        if self.active_nonce != Some(status.exchange_nonce) { return Ok(false); }
        let _ = pet_status_body(status)?;
        if status.sequence == 0 || (self.last_sequence == 0 && status.sequence != 1) || status.sequence <= self.last_sequence { return Ok(false); }
        self.last_sequence = status.sequence; Ok(true)
    }

    pub fn retire(&mut self) { self.active_nonce = None; self.last_sequence = 0; }
}
