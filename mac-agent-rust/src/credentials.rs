//! Compatibility-preserving Keychain storage for Agent identity and watch keys.
//!
//! Secrets are never serialized to the Agent state directory or exposed in
//! `Debug` output. Tests inject an in-memory Keychain adapter.

use anyhow::{Result, bail};
use p256::{ecdsa::SigningKey, pkcs8::{DecodePrivateKey, EncodePrivateKey}};
use rand::rngs::OsRng;
use sha2::{Digest, Sha256};
use std::{collections::HashMap, sync::{Arc, Mutex}};
use unicode_normalization::UnicodeNormalization;
use zeroize::{ZeroizeOnDrop, Zeroizing};

use crate::security::{self, Secret16};

pub const IDENTITY_SERVICE: &str = "com.stopwatch.codex-agent.identity";
pub const IDENTITY_NAME_SERVICE: &str = "com.stopwatch.codex-agent.identity-name";
pub const KEYCHAIN_ACCOUNT: &str = "default";
pub const WATCH_SERVICE_PREFIX: &str = "com.stopwatch.codex-agent.watch.";
pub const WATCH_ACCOUNT: &str = "credential";
pub const VOICE_KEY_SERVICE: &str = "com.stopwatch.codex-agent.voice";
pub const VOICE_KEY_ACCOUNT: &str = "StopWatch-Codex";
pub const MAX_AGENT_NAME_BYTES: usize = 24;

const IDENTITY_MAGIC: &[u8; 4] = b"SWID";
const IDENTITY_VERSION: u8 = 1;
const NAME_MAGIC: &[u8; 4] = b"SWNM";
const NAME_VERSION: u8 = 1;
const WATCH_MAGIC: &[u8; 6] = b"SWCRED";
const WATCH_VERSION: u8 = 2;
const WATCH_RECORD_BYTES: usize = 6 + 1 + 16 + 4 + 16 + 16 + 33;
const CHECKSUM_BYTES: usize = 16;

pub trait KeychainAdapter: Send + Sync {
    fn read(&self, service: &str, account: &str) -> Result<Option<Vec<u8>>>;
    fn replace(&self, service: &str, account: &str, value: &[u8]) -> Result<()>;
    fn delete(&self, service: &str, account: &str) -> Result<()>;
}

/// Real-product adapter backed by Security.framework generic-password items.
/// The API is cfg-gated so synthetic non-macOS tests never open a Keychain.
#[derive(Default)]
pub struct MacOSKeychain;

impl KeychainAdapter for MacOSKeychain {
    fn read(&self, service: &str, account: &str) -> Result<Option<Vec<u8>>> {
        #[cfg(target_os = "macos")]
        {
            use security_framework::passwords::{generic_password, PasswordOptions};
            return match generic_password(PasswordOptions::new_generic_password(service, account)) {
                Ok(value) => Ok(Some(value)),
                Err(error) if error.code() == -25300 => Ok(None),
                Err(_) => bail!("macOS Keychain read failed"),
            };
        }
        #[cfg(not(target_os = "macos"))]
        {
            let _ = (service, account);
            bail!("macOS Keychain framework is unavailable")
        }
    }

    fn replace(&self, service: &str, account: &str, value: &[u8]) -> Result<()> {
        #[cfg(target_os = "macos")]
        {
            use core_foundation::{
                base::TCFType,
                data::CFData,
                dictionary::CFDictionary,
                string::CFString,
            };

            #[link(name = "Security", kind = "framework")]
            unsafe extern "C" {
                static kSecClass: *const std::ffi::c_void;
                static kSecClassGenericPassword: *const std::ffi::c_void;
                static kSecAttrService: *const std::ffi::c_void;
                static kSecAttrAccount: *const std::ffi::c_void;
                static kSecAttrAccessible: *const std::ffi::c_void;
                static kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly: *const std::ffi::c_void;
                static kSecValueData: *const std::ffi::c_void;
                fn SecItemUpdate(
                    query: *const std::ffi::c_void,
                    attributes_to_update: *const std::ffi::c_void,
                ) -> i32;
                fn SecItemAdd(
                    attributes: *const std::ffi::c_void,
                    result: *mut *const std::ffi::c_void,
                ) -> i32;
            }

            const ITEM_NOT_FOUND: i32 = -25300;
            const DUPLICATE_ITEM: i32 = -25299;

            let query = CFDictionary::from_CFType_pairs(&[
                (
                    unsafe { CFString::wrap_under_get_rule(kSecClass as _) },
                    unsafe { CFString::wrap_under_get_rule(kSecClassGenericPassword as _) }
                        .into_CFType(),
                ),
                (
                    unsafe { CFString::wrap_under_get_rule(kSecAttrService as _) },
                    CFString::from(service).into_CFType(),
                ),
                (
                    unsafe { CFString::wrap_under_get_rule(kSecAttrAccount as _) },
                    CFString::from(account).into_CFType(),
                ),
            ]);
            let update = CFDictionary::from_CFType_pairs(&[(
                unsafe { CFString::wrap_under_get_rule(kSecValueData as _) },
                CFData::from_buffer(value).into_CFType(),
            )]);

            // Match the Agent's Python adapter: update by service/account first,
            // and only attach the device-only accessibility policy on creation.
            // This avoids making an existing item match on a non-identity attribute.
            let mut status = unsafe {
                SecItemUpdate(
                    query.as_concrete_TypeRef() as *const _,
                    update.as_concrete_TypeRef() as *const _,
                )
            };
            if status == ITEM_NOT_FOUND {
                let attributes = vec![
                    (
                        unsafe { CFString::wrap_under_get_rule(kSecClass as _) },
                        unsafe {
                            CFString::wrap_under_get_rule(kSecClassGenericPassword as _)
                        }
                        .into_CFType(),
                    ),
                    (
                        unsafe { CFString::wrap_under_get_rule(kSecAttrService as _) },
                        CFString::from(service).into_CFType(),
                    ),
                    (
                        unsafe { CFString::wrap_under_get_rule(kSecAttrAccount as _) },
                        CFString::from(account).into_CFType(),
                    ),
                    (
                        unsafe { CFString::wrap_under_get_rule(kSecAttrAccessible as _) },
                        unsafe {
                            CFString::wrap_under_get_rule(
                                kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly as _,
                            )
                        }
                        .into_CFType(),
                    ),
                    (
                        unsafe { CFString::wrap_under_get_rule(kSecValueData as _) },
                        CFData::from_buffer(value).into_CFType(),
                    ),
                ];
                let add = CFDictionary::from_CFType_pairs(&attributes);
                status = unsafe {
                    SecItemAdd(add.as_concrete_TypeRef() as *const _, std::ptr::null_mut())
                };
                if status == DUPLICATE_ITEM {
                    status = unsafe {
                        SecItemUpdate(
                            query.as_concrete_TypeRef() as *const _,
                            update.as_concrete_TypeRef() as *const _,
                        )
                    };
                }
            }
            if status != 0 {
                bail!("macOS Keychain update failed");
            }
            return Ok(());
        }
        #[cfg(not(target_os = "macos"))]
        {
            let _ = (service, account, value);
            bail!("macOS Keychain framework is unavailable")
        }
    }

    fn delete(&self, service: &str, account: &str) -> Result<()> {
        #[cfg(target_os = "macos")]
        {
            use security_framework::passwords::{delete_generic_password_options, PasswordOptions};
            match delete_generic_password_options(PasswordOptions::new_generic_password(service, account)) {
                Ok(()) => return Ok(()),
                Err(error) if error.code() == -25300 => return Ok(()),
                Err(_) => bail!("macOS Keychain delete failed"),
            }
        }
        #[cfg(not(target_os = "macos"))]
        {
            let _ = (service, account);
            bail!("macOS Keychain framework is unavailable")
        }
    }
}

#[derive(Clone)]
pub struct AgentIdentity {
    private_scalar: Zeroizing<[u8; 32]>,
    pub public_key: [u8; 33],
    pub fingerprint: [u8; 16],
    pub display_name: String,
}

impl std::fmt::Debug for AgentIdentity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("AgentIdentity")
            .field("public_key", &self.public_key)
            .field("fingerprint", &self.fingerprint)
            .field("display_name", &self.display_name)
            .field("private_scalar", &"[REDACTED]")
            .finish()
    }
}

impl AgentIdentity {
    pub fn private_scalar(&self) -> &[u8; 32] { &self.private_scalar }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AgentIdentitySnapshot {
    pub public_key: [u8; 33],
    pub fingerprint: [u8; 16],
    pub display_name: String,
}

pub struct AgentIdentityStore {
    keychain: Arc<dyn KeychainAdapter>,
    default_name: Box<dyn Fn() -> String + Send + Sync>,
    identity: Mutex<Option<AgentIdentity>>,
}

impl AgentIdentityStore {
    pub fn new(keychain: Arc<dyn KeychainAdapter>) -> Self {
        Self::with_default_name(keychain, default_agent_name)
    }

    pub fn with_default_name<F>(keychain: Arc<dyn KeychainAdapter>, default_name: F) -> Self
    where F: Fn() -> String + Send + Sync + 'static {
        Self { keychain, default_name: Box::new(default_name), identity: Mutex::new(None) }
    }

    pub fn macos() -> Self { Self::new(Arc::new(MacOSKeychain)) }

    pub fn load_or_create(&self) -> Result<AgentIdentity> {
        let mut cached = self.identity.lock().map_err(|_| anyhow::anyhow!("Agent identity state is unavailable"))?;
        if let Some(identity) = cached.as_ref() { return Ok(identity.clone()); }
        let private_scalar = match self.keychain.read(IDENTITY_SERVICE, KEYCHAIN_ACCOUNT) {
            Ok(Some(payload)) => {
                let payload = Zeroizing::new(payload);
                decode_identity(payload.as_slice())?
            }
            Ok(None) => {
                let signing = SigningKey::random(&mut OsRng);
                let scalar = Zeroizing::new(signing.to_bytes().into());
                let payload = Zeroizing::new(encode_identity(&signing)?);
                self.keychain.replace(IDENTITY_SERVICE, KEYCHAIN_ACCOUNT, payload.as_slice())
                    .map_err(|_| anyhow::anyhow!("Agent identity is unavailable from Keychain"))?;
                scalar
            }
            Err(_) => bail!("Agent identity is unavailable from Keychain"),
        };
        let signing = signing_key(&private_scalar)?;
        let encoded_public = signing.verifying_key().to_encoded_point(true);
        let public_key: [u8; 33] = encoded_public.as_bytes().try_into()?;
        let digest = Sha256::digest(public_key);
        let fingerprint: [u8; 16] = digest[..16].try_into()?;
        let display_name = self.load_or_create_name()?;
        let identity = AgentIdentity { private_scalar, public_key, fingerprint, display_name };
        *cached = Some(identity.clone());
        Ok(identity)
    }

    fn load_or_create_name(&self) -> Result<String> {
        match self.keychain.read(IDENTITY_NAME_SERVICE, KEYCHAIN_ACCOUNT) {
            Ok(Some(payload)) => decode_name(&payload),
            Ok(None) => {
                let display_name = normalize_agent_name(&(self.default_name)())?;
                let record = encode_name(&display_name)?;
                self.keychain.replace(IDENTITY_NAME_SERVICE, KEYCHAIN_ACCOUNT, &record)
                    .map_err(|_| anyhow::anyhow!("Agent identity is unavailable from Keychain"))?;
                Ok(display_name)
            }
            Err(_) => bail!("Agent identity is unavailable from Keychain"),
        }
    }

    pub fn rename(&self, value: &str) -> Result<String> {
        let display_name = normalize_agent_name(value)?;
        let identity = self.load_or_create()?;
        let record = encode_name(&display_name)?;
        self.keychain.replace(IDENTITY_NAME_SERVICE, KEYCHAIN_ACCOUNT, &record)
            .map_err(|_| anyhow::anyhow!("Agent name is unavailable from Keychain"))?;
        let mut cached = self.identity.lock().map_err(|_| anyhow::anyhow!("Agent identity state is unavailable"))?;
        *cached = Some(AgentIdentity { display_name: display_name.clone(), ..identity });
        Ok(display_name)
    }

    pub fn snapshot(&self) -> Result<AgentIdentitySnapshot> {
        let identity = self.load_or_create()?;
        Ok(AgentIdentitySnapshot { public_key: identity.public_key, fingerprint: identity.fingerprint, display_name: identity.display_name })
    }
}

fn default_agent_name() -> String {
    let host = std::env::var("HOSTNAME").unwrap_or_else(|_| "Mac Agent".into());
    normalize_agent_name(host.split('.').next().unwrap_or("Mac Agent")).unwrap_or_else(|_| "Mac Agent".into())
}

pub fn normalize_agent_name(value: &str) -> Result<String> {
    let normalized: String = value.nfc().collect();
    let mut candidate = String::new();
    let mut pending_space = false;
    for character in normalized.chars() {
        if character.is_whitespace() {
            pending_space = !candidate.is_empty();
            continue;
        }
        if security::is_category_c(character) { bail!("Agent name contains unsupported characters"); }
        if pending_space { candidate.push(' '); pending_space = false; }
        candidate.push(character);
    }
    let mut bounded = String::new();
    for character in candidate.chars() {
        if bounded.len() + character.len_utf8() > MAX_AGENT_NAME_BYTES { break; }
        bounded.push(character);
    }
    let bounded = bounded.trim_end().to_owned();
    if bounded.is_empty() { bail!("Agent name cannot be empty"); }
    Ok(bounded)
}

fn encode_identity(signing: &SigningKey) -> Result<Vec<u8>> {
    let der = signing.to_pkcs8_der().map_err(|_| anyhow::anyhow!("Agent identity cannot be encoded"))?;
    let der = der.as_bytes();
    let length = u16::try_from(der.len()).map_err(|_| anyhow::anyhow!("Agent identity cannot be encoded"))?;
    let mut record = Vec::with_capacity(7 + der.len());
    record.extend_from_slice(IDENTITY_MAGIC); record.push(IDENTITY_VERSION); record.extend_from_slice(&length.to_be_bytes()); record.extend_from_slice(der);
    Ok(record)
}

fn decode_identity(payload: &[u8]) -> Result<Zeroizing<[u8; 32]>> {
    if payload.len() < 7 || &payload[..4] != IDENTITY_MAGIC || payload[4] != IDENTITY_VERSION || u16::from_be_bytes(payload[5..7].try_into()?) as usize != payload.len() - 7 {
        bail!("invalid Agent identity record");
    }
    let signing = SigningKey::from_pkcs8_der(&payload[7..]).map_err(|_| anyhow::anyhow!("invalid Agent identity record"))?;
    Ok(Zeroizing::new(signing.to_bytes().into()))
}

fn signing_key(scalar: &[u8; 32]) -> Result<SigningKey> {
    SigningKey::from_slice(scalar).map_err(|_| anyhow::anyhow!("invalid Agent identity record"))
}

fn encode_name(name: &str) -> Result<Vec<u8>> {
    let normalized = normalize_agent_name(name)?;
    let bytes = normalized.as_bytes();
    let mut output = Vec::with_capacity(6 + bytes.len());
    output.extend_from_slice(NAME_MAGIC); output.push(NAME_VERSION); output.push(bytes.len() as u8); output.extend_from_slice(bytes);
    Ok(output)
}

fn decode_name(payload: &[u8]) -> Result<String> {
    if payload.len() < 6 || &payload[..4] != NAME_MAGIC || payload[4] != NAME_VERSION || payload[5] as usize != payload.len() - 6 {
        bail!("invalid Agent name record");
    }
    let name = std::str::from_utf8(&payload[6..]).map_err(|_| anyhow::anyhow!("invalid Agent name record"))?;
    let normalized = normalize_agent_name(name)?;
    if normalized.as_bytes() != &payload[6..] { bail!("invalid Agent name record"); }
    Ok(normalized)
}

#[derive(Clone, PartialEq, Eq)]
pub struct WatchCredential {
    pub watch_id: [u8; 16],
    pub master_key: Secret16,
    pub key_epoch: u32,
    pub fingerprint: [u8; 16],
    pub watch_identity_public: [u8; 33],
}

impl std::fmt::Debug for WatchCredential {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("WatchCredential")
            .field("watch_id", &self.watch_id)
            .field("master_key", &"[REDACTED]")
            .field("key_epoch", &self.key_epoch)
            .field("fingerprint", &self.fingerprint)
            .field("watch_identity_public", &self.watch_identity_public)
            .finish()
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WatchCredentialSnapshot {
    pub watch_id_suffix: String,
    pub key_epoch: u32,
    pub fingerprint_suffix: String,
}

pub struct WatchCredentialStore { keychain: Arc<dyn KeychainAdapter> }

impl WatchCredentialStore {
    pub fn new(keychain: Arc<dyn KeychainAdapter>) -> Self { Self { keychain } }
    pub fn macos() -> Self { Self::new(Arc::new(MacOSKeychain)) }

    pub fn load(&self, watch_id: &[u8; 16]) -> Result<Option<WatchCredential>> {
        let service = watch_service(watch_id);
        match self.keychain.read(&service, WATCH_ACCOUNT) {
            Ok(Some(payload)) => {
                let payload = Zeroizing::new(payload);
                decode_watch_credential(payload.as_slice(), watch_id).map(Some)
                    .map_err(|_| anyhow::anyhow!("watch credential is unavailable from Keychain"))
            }
            Ok(None) => Ok(None),
            Err(_) => bail!("watch credential is unavailable from Keychain"),
        }
    }

    pub fn replace(&self, credential: &WatchCredential) -> Result<()> {
        validate_watch_credential(credential)?;
        if let Some(current) = self.load(&credential.watch_id)? {
            if credential.key_epoch < current.key_epoch { bail!("watch credential epoch is stale"); }
            if credential.key_epoch == current.key_epoch && credential != &current { bail!("watch credential epoch conflicts"); }
            if credential == &current { return Ok(()); }
        }
        let payload = Zeroizing::new(encode_watch_credential(credential)?);
        let service = watch_service(&credential.watch_id);
        self.keychain.replace(&service, WATCH_ACCOUNT, payload.as_slice())
            .map_err(|_| anyhow::anyhow!("watch credential is unavailable from Keychain"))
    }

    pub fn save(&self, credential: &WatchCredential) -> Result<()> { self.replace(credential) }

    pub fn forget(&self, watch_id: &[u8; 16]) -> Result<()> {
        let service = watch_service(watch_id);
        self.keychain.delete(&service, WATCH_ACCOUNT)
            .map_err(|_| anyhow::anyhow!("watch credential is unavailable from Keychain"))
    }

    pub fn snapshot(&self, watch_id: &[u8; 16]) -> Result<Option<WatchCredentialSnapshot>> {
        let Some(credential) = self.load(watch_id)? else { return Ok(None); };
        Ok(Some(WatchCredentialSnapshot {
            watch_id_suffix: uppercase_hex(&credential.watch_id[14..]),
            key_epoch: credential.key_epoch,
            fingerprint_suffix: uppercase_hex(&credential.fingerprint[14..]),
        }))
    }
}

fn watch_service(watch_id: &[u8; 16]) -> String { format!("{WATCH_SERVICE_PREFIX}{}", lower_hex(watch_id)) }

fn validate_watch_credential(value: &WatchCredential) -> Result<()> {
    check_credential_bytes(&value.watch_id, "watch identifier")?;
    check_credential_bytes(&value.master_key.0, "watch master key")?;
    check_credential_bytes(&value.fingerprint, "Agent fingerprint")?;
    check_credential_bytes(&value.watch_identity_public, "watch identity public key")?;
    if value.key_epoch == 0 { bail!("watch key epoch is invalid"); }
    security::parse_public_key(&value.watch_identity_public)?;
    Ok(())
}

fn check_credential_bytes(value: &[u8], label: &str) -> Result<()> {
    if value.is_empty() || !value.iter().any(|byte| *byte != 0) { bail!("{label} is invalid"); }
    Ok(())
}

fn encode_watch_credential(value: &WatchCredential) -> Result<Vec<u8>> {
    validate_watch_credential(value)?;
    let mut record = Vec::with_capacity(WATCH_RECORD_BYTES + CHECKSUM_BYTES);
    record.extend_from_slice(WATCH_MAGIC); record.push(WATCH_VERSION); record.extend_from_slice(&value.watch_id);
    record.extend_from_slice(&value.key_epoch.to_be_bytes()); record.extend_from_slice(&value.master_key.0);
    record.extend_from_slice(&value.fingerprint); record.extend_from_slice(&value.watch_identity_public);
    let checksum = Sha256::digest(&record);
    record.extend_from_slice(&checksum[..CHECKSUM_BYTES]);
    Ok(record)
}

fn decode_watch_credential(payload: &[u8], expected_watch_id: &[u8; 16]) -> Result<WatchCredential> {
    if payload.len() != WATCH_RECORD_BYTES + CHECKSUM_BYTES { bail!("invalid watch credential record"); }
    let record = &payload[..WATCH_RECORD_BYTES];
    let checksum = Sha256::digest(record);
    if checksum[..CHECKSUM_BYTES] != payload[WATCH_RECORD_BYTES..] { bail!("invalid watch credential record"); }
    if &record[..6] != WATCH_MAGIC || record[6] != WATCH_VERSION { bail!("invalid watch credential record"); }
    let credential = WatchCredential {
        watch_id: record[7..23].try_into()?,
        key_epoch: u32::from_be_bytes(record[23..27].try_into()?),
        master_key: Secret16(record[27..43].try_into()?),
        fingerprint: record[43..59].try_into()?,
        watch_identity_public: record[59..92].try_into()?,
    };
    validate_watch_credential(&credential)?;
    if &credential.watch_id != expected_watch_id { bail!("invalid watch credential record"); }
    Ok(credential)
}

/// Decode the version-1 plaintext carried only inside an authenticated
/// SWENV/AES-CCM envelope during first pairing and rekey. The returned secret
/// remains zeroized on drop and callers must persist it only through
/// `WatchCredentialStore`.
pub fn decode_pairing_credential(payload: &[u8]) -> Result<WatchCredential> {
    const PLAINTEXT_BYTES: usize = 6 + 1 + 16 + 4 + 16 + 33 + 16;
    if payload.len() != PLAINTEXT_BYTES || &payload[..6] != b"SWCRED" || payload[6] != 1 {
        bail!("wrapped watch credential has invalid format");
    }
    let credential = WatchCredential {
        master_key: Secret16(payload[7..23].try_into()?),
        key_epoch: u32::from_be_bytes(payload[23..27].try_into()?),
        watch_id: payload[27..43].try_into()?,
        watch_identity_public: payload[43..76].try_into()?,
        fingerprint: payload[76..92].try_into()?,
    };
    validate_watch_credential(&credential)?;
    Ok(credential)
}

pub fn load_voice_key(keychain: &dyn KeychainAdapter) -> Result<Secret16> {
    let payload = Zeroizing::new(
        keychain.read(VOICE_KEY_SERVICE, VOICE_KEY_ACCOUNT)
            .map_err(|_| anyhow::anyhow!("voice key is unavailable from Keychain"))?
            .ok_or_else(|| anyhow::anyhow!("voice key is unavailable from Keychain"))?,
    );
    let text = std::str::from_utf8(payload.as_slice()).map_err(|_| anyhow::anyhow!("voice key stored in Keychain is malformed"))?.trim_end_matches(['\r', '\n']);
    if text.len() != 32 || !text.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)) { bail!("voice key stored in Keychain is malformed"); }
    let mut key = [0u8; 16];
    for (index, pair) in text.as_bytes().chunks_exact(2).enumerate() {
        key[index] = (nibble(pair[0])? << 4) | nibble(pair[1])?;
    }
    Ok(Secret16(key))
}

fn nibble(byte: u8) -> Result<u8> { match byte { b'0'..=b'9' => Ok(byte - b'0'), b'a'..=b'f' => Ok(byte - b'a' + 10), _ => bail!("voice key stored in Keychain is malformed") } }

fn lower_hex(bytes: &[u8]) -> String { hex_string(bytes, false) }
fn uppercase_hex(bytes: &[u8]) -> String { hex_string(bytes, true) }
fn hex_string(bytes: &[u8], uppercase: bool) -> String {
    let digits = if uppercase { b"0123456789ABCDEF" } else { b"0123456789abcdef" };
    let mut text = String::with_capacity(bytes.len() * 2);
    for byte in bytes { text.push(digits[(byte >> 4) as usize] as char); text.push(digits[(byte & 0x0f) as usize] as char); }
    text
}

/// Explicit in-memory adapter for protocol tests. It is never selected by a
/// production constructor and performs no filesystem or Keychain access.
#[derive(Default)]
pub struct MemoryKeychain { entries: Mutex<HashMap<(String, String), Vec<u8>>> }

impl KeychainAdapter for MemoryKeychain {
    fn read(&self, service: &str, account: &str) -> Result<Option<Vec<u8>>> {
        Ok(self.entries.lock().map_err(|_| anyhow::anyhow!("memory keychain is unavailable"))?.get(&(service.into(), account.into())).cloned())
    }
    fn replace(&self, service: &str, account: &str, value: &[u8]) -> Result<()> {
        self.entries.lock().map_err(|_| anyhow::anyhow!("memory keychain is unavailable"))?.insert((service.into(), account.into()), value.to_vec());
        Ok(())
    }
    fn delete(&self, service: &str, account: &str) -> Result<()> {
        self.entries.lock().map_err(|_| anyhow::anyhow!("memory keychain is unavailable"))?.remove(&(service.into(), account.into()));
        Ok(())
    }
}
