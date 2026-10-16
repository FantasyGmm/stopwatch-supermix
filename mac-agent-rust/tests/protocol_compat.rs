use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use stopwatch_codex_agent::{
    contracts::StatusSnapshot,
    wire::{
        ALL_VOICE_CAPABILITIES, AUDIO, CredentialEnvelope, CredentialPurpose, EVENT_MESSAGE,
        PTT_BEGIN, PTT_END, PairMessageType, PairingRequest, PetControlPayload,
        PetControlReplayGuard, PetErrorReason, PetMessageKind, PetSource, PetStatusPayload,
        ReplayDecision, STATUS_MESSAGE, VOICE_ERROR, VoiceBegin, VoiceEndSummary,
        canonical_task_detail_aad, decode_credential_envelope, decode_ima_adpcm_block,
        decode_pair_frame, decode_pairing_request, decode_pet_control, decode_pet_status,
        decode_voice_control, decode_voice_notification, decrypt_task_detail,
        encode_credential_envelope, encode_pairing_request, encode_pet_control, encode_pet_status,
        encode_status_message, encode_voice_begin, encode_voice_end, encode_voice_ready,
        encrypt_task_detail, pack_voice_notification, status_messages, status_write_capacity,
        task_detail_to_value,
    },
};

const PYTHON_FIXTURE: &str = include_str!("../../tests/fixtures/protocol-compat.json");
const CXP1_HEX: &str = include_str!("../../tests/fixtures/codex_pet/cxp1_minimal.hex");

fn fixture() -> Value {
    serde_json::from_str(PYTHON_FIXTURE).expect("legacy Python fixture is valid JSON")
}

fn hex(value: &Value) -> Vec<u8> {
    hex::decode(value.as_str().expect("fixture field is a hex string"))
        .expect("fixture hex is valid")
}

fn hex_field(value: &Value, field: &str) -> Vec<u8> {
    hex(&value[field])
}

fn hex_array(value: &Value) -> Vec<Vec<u8>> {
    value
        .as_array()
        .expect("fixture field is a hex array")
        .iter()
        .map(hex)
        .collect()
}

fn le_u16(data: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(data[offset..offset + 2].try_into().unwrap())
}

fn le_u32(data: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(data[offset..offset + 4].try_into().unwrap())
}

#[test]
fn status_framing_matches_python_bytes_at_payload_and_mtu_boundaries() {
    let fixture = fixture();
    let status = &fixture["status"];
    let payload = hex_field(status, "payload_utf8_hex");
    let constructed = format!("{{\"s\":\"{}abc\"}}", "界".repeat(679));
    assert_eq!(payload.as_slice(), constructed.as_bytes());
    assert_eq!(payload.len(), 2048);
    assert_eq!(String::from_utf8(payload.clone()).unwrap(), constructed);

    let wire_sequence = status["wire_sequence"].as_u64().unwrap() as u16;
    assert_eq!(status["requested_sequence"].as_u64(), Some(0x12345));
    assert_eq!(wire_sequence, 0x2345);

    let mtu20 = encode_status_message(&payload, STATUS_MESSAGE, wire_sequence, 20).unwrap();
    let expected20 = hex_array(&status["mtu20_frames_hex"]);
    assert_eq!(mtu20, expected20);
    assert_eq!(mtu20.len(), 147);
    assert!(mtu20.iter().all(|frame| frame.len() <= 20));
    assert_eq!(&mtu20[0][..6], &[1, STATUS_MESSAGE, 0x45, 0x23, 0, 147]);
    assert_eq!(
        &mtu20.last().unwrap()[..6],
        &[1, STATUS_MESSAGE, 0x45, 0x23, 146, 147]
    );
    assert_eq!(
        mtu20
            .iter()
            .flat_map(|frame| frame[6..].iter().copied())
            .collect::<Vec<_>>(),
        payload
    );

    let mtu256 = encode_status_message(&payload, STATUS_MESSAGE, wire_sequence, 256).unwrap();
    assert_eq!(mtu256, hex_array(&status["mtu256_frames_hex"]));
    assert_eq!(mtu256.len(), 9);
    assert!(mtu256.iter().all(|frame| frame.len() <= 256));
    assert_eq!(status_write_capacity(None), 20);
    assert_eq!(status_write_capacity(Some(19)), 20);
    assert_eq!(status_write_capacity(Some(256)), 256);
    assert_eq!(status_write_capacity(Some(300)), 256);

    let payload255 = hex_field(status, "max_255_fragment_payload_hex");
    let fragments255 = encode_status_message(&payload255, EVENT_MESSAGE, u16::MAX, 7).unwrap();
    assert_eq!(
        fragments255,
        hex_array(&status["max_255_fragment_frames_hex"])
    );
    assert_eq!(fragments255.len(), 255);
    assert_eq!(
        fragments255[0].as_slice(),
        &[1, EVENT_MESSAGE, 0xff, 0xff, 0, 255, b'x']
    );
    assert_eq!(
        fragments255.last().unwrap().as_slice(),
        &[1, EVENT_MESSAGE, 0xff, 0xff, 254, 255, b'x']
    );

    let payload256 = hex_field(status, "max_256_fragment_payload_hex");
    assert!(encode_status_message(&payload256, EVENT_MESSAGE, 0, 7).is_err());
    assert_eq!(status["max_256_fragment_error"], "too many BLE fragments");
    assert!(encode_status_message(&[b'x'; 2049], STATUS_MESSAGE, 1, 20).is_err());
}

#[test]
fn secure_pairing_request_and_credential_frames_match_python_vectors() {
    let fixture = fixture();
    let vectors = &fixture["pairing_wire"];
    let request = PairingRequest {
        transaction_id: 7,
        agent_nonce: [b'N'; 16],
        agent_ephemeral_public: hex_field(vectors, "agent_ephemeral_public_hex")
            .try_into()
            .unwrap(),
        agent_identity_public: hex_field(vectors, "agent_identity_public_hex")
            .try_into()
            .unwrap(),
        display_name: "桌面 Agent".into(),
        identity_signature: vec![b'S'; 70],
    };
    let raw = encode_pairing_request(&request).unwrap();
    assert_eq!(raw, hex_field(vectors, "request_frame_hex"));
    assert!(raw.len() <= 256);
    let frame = decode_pair_frame(&raw).unwrap();
    assert_eq!(frame.kind, PairMessageType::PairingRequest);
    assert_eq!(frame.transaction_id, request.transaction_id);
    assert_eq!(
        decode_pairing_request(frame.transaction_id, &frame.payload).unwrap(),
        request
    );

    let max_name = vectors["max_name"].as_str().unwrap();
    assert_eq!(max_name.as_bytes().len(), 24);
    assert_eq!(vectors["max_name_utf8_bytes"].as_u64(), Some(24));
    let max_name_request = PairingRequest {
        transaction_id: 11,
        agent_nonce: [b'M'; 16],
        agent_ephemeral_public: hex_field(vectors, "agent_ephemeral_public_hex")
            .try_into()
            .unwrap(),
        agent_identity_public: hex_field(vectors, "agent_identity_public_hex")
            .try_into()
            .unwrap(),
        display_name: max_name.into(),
        identity_signature: vec![b'S'; 70],
    };
    assert_eq!(
        encode_pairing_request(&max_name_request).unwrap(),
        hex_field(vectors, "max_name_request_frame_hex")
    );

    let credential = CredentialEnvelope {
        purpose: CredentialPurpose::Rekey,
        transaction_id: 10,
        key_epoch: 6,
        watch_ephemeral_public: hex_field(vectors, "watch_ephemeral_public_hex")
            .try_into()
            .unwrap(),
        nonce: [b'N'; 13],
        ciphertext: b"ciphertext-and-tag".to_vec(),
        ack_nonce: [b'A'; 13],
    };
    let raw = encode_credential_envelope(&credential).unwrap();
    assert_eq!(raw, hex_field(vectors, "credential_envelope_frame_hex"));
    let frame = decode_pair_frame(&raw).unwrap();
    assert_eq!(frame.kind, PairMessageType::Credential);
    assert_eq!(
        decode_credential_envelope(frame.transaction_id, &frame.payload).unwrap(),
        credential
    );

    let mut invalid_credential = credential.clone();
    let mut invalid_watch_public = [b'P'; 33];
    invalid_watch_public[0] = 0x03;
    invalid_credential.watch_ephemeral_public = invalid_watch_public;
    assert!(encode_credential_envelope(&invalid_credential).is_err());
}

#[test]
fn voice_ready_begin_audio_adpcm_end_and_error_match_python_vectors() {
    let fixture = fixture();
    let vectors = &fixture["voice"];
    let key = hex_field(vectors, "key_hex");
    let nonce: [u8; 8] = hex_field(vectors, "exchange_nonce_hex").try_into().unwrap();
    let epoch = vectors["key_epoch"].as_u64().unwrap() as u32;

    let ready = encode_voice_ready(&key, &nonce, epoch, ALL_VOICE_CAPABILITIES).unwrap();
    assert_eq!(ready, hex_field(vectors, "ready_hex"));
    let ready_decoded = decode_voice_control(&key, &ready, epoch).unwrap();
    assert_eq!(ready_decoded.exchange_nonce, nonce);
    assert_eq!(ready_decoded.capabilities, ALL_VOICE_CAPABILITIES);
    assert_eq!(ready_decoded.target_bps, 8192);

    let begin = VoiceBegin {
        codec: 1,
        mode: 1,
        sample_rate: 16_000,
        block_samples: 320,
        route: 0x0123_4567_89ab_cdef,
    };
    let begin_body = encode_voice_begin(begin).unwrap();
    assert_eq!(begin_body.as_slice(), hex_field(vectors, "begin_body_hex"));
    let begin_raw =
        pack_voice_notification(&key, PTT_BEGIN, 7, 0, 100, &begin_body, &nonce, epoch).unwrap();
    assert_eq!(begin_raw, hex_field(vectors, "begin_hex"));
    let begin_decoded = decode_voice_notification(&key, &begin_raw, None, epoch).unwrap();
    assert_eq!(begin_decoded.begin, Some(begin));
    assert_eq!(begin_decoded.session_nonce, Some(nonce));

    let audio_body = hex_field(vectors, "audio_body_hex");
    let audio_raw =
        pack_voice_notification(&key, AUDIO, 7, 1, 101, &audio_body, &nonce, epoch).unwrap();
    assert_eq!(audio_raw, hex_field(vectors, "audio_hex"));
    let audio_decoded = decode_voice_notification(&key, &audio_raw, Some(&nonce), epoch).unwrap();
    assert_eq!(audio_decoded.kind, AUDIO);
    assert_eq!(audio_decoded.payload, audio_body);
    assert_eq!(
        decode_ima_adpcm_block(&audio_decoded.payload, 320)
            .unwrap()
            .len(),
        vectors["audio_decoded_sample_count"].as_u64().unwrap() as usize
    );

    let end = VoiceEndSummary {
        generated_packets: 10,
        accepted_packets: 9,
        queue_overflows: 1,
        encode_failures: 2,
        captured_samples: 3200,
        elapsed_ms: 200,
        negotiated_mtu: 256,
    };
    let end_body = encode_voice_end(end);
    assert_eq!(end_body.as_slice(), hex_field(vectors, "end_body_hex"));
    let end_raw =
        pack_voice_notification(&key, PTT_END, 7, 2, 102, &end_body, &nonce, epoch).unwrap();
    assert_eq!(end_raw, hex_field(vectors, "end_hex"));
    let end_decoded = decode_voice_notification(&key, &end_raw, Some(&nonce), epoch).unwrap();
    assert_eq!(end_decoded.payload, end_body);

    let error_body = hex_field(vectors, "error_body_hex");
    let error_raw =
        pack_voice_notification(&key, VOICE_ERROR, 7, 3, 103, &error_body, &nonce, epoch).unwrap();
    assert_eq!(error_raw, hex_field(vectors, "error_hex"));
    assert_eq!(
        decode_voice_notification(&key, &error_raw, Some(&nonce), epoch)
            .unwrap()
            .payload,
        error_body
    );
    assert!(decode_voice_control(&key, &ready, epoch + 1).is_err());
    let mut tampered = audio_raw;
    *tampered.last_mut().unwrap() ^= 1;
    assert!(decode_voice_notification(&key, &tampered, Some(&nonce), epoch).is_err());
}

#[test]
fn task_detail_python_ciphertexts_round_trip_and_large_reply_stays_out_of_status() {
    let fixture = fixture();
    let vectors = &fixture["task_detail"];
    let key = hex_field(vectors, "key_hex");
    let nonce: [u8; 8] = hex_field(vectors, "exchange_nonce_hex").try_into().unwrap();
    let epoch = vectors["key_epoch"].as_u64().unwrap() as u32;
    let route = vectors["route"].as_str().unwrap();
    let revision = vectors["revision"].as_u64().unwrap();
    let count = vectors["envelopes"].as_array().unwrap().len() as u64;
    let full_reply = vectors["reply_pattern"]
        .as_str()
        .unwrap()
        .repeat(vectors["reply_repetitions"].as_u64().unwrap() as usize);
    assert_eq!(
        full_reply.len(),
        vectors["reply_utf8_bytes"].as_u64().unwrap() as usize
    );
    assert_eq!(full_reply.len(), 12 * 1024);

    let expected_aad = vectors["first_chunk_aad"].as_str().unwrap();
    let mut joined = String::new();
    let characters = full_reply.chars().collect::<Vec<_>>();
    for (index, chunk_chars) in characters
        .chunks(vectors["characters_per_chunk"].as_u64().unwrap() as usize)
        .enumerate()
    {
        let chunk = chunk_chars.iter().collect::<String>();
        assert_eq!(
            chunk.len(),
            vectors["chunk_utf8_bytes"][index].as_u64().unwrap() as usize
        );
        let sequence = index as u32 + 1;
        let envelope = encrypt_task_detail(
            &key,
            epoch,
            nonce,
            sequence,
            route,
            revision,
            index as u64,
            count,
            &chunk,
        )
        .unwrap();
        let python_envelope = &vectors["envelopes"][index];
        let aad = canonical_task_detail_aad(&envelope).unwrap();
        if index == 0 {
            assert_eq!(aad, expected_aad.as_bytes());
        }
        assert_eq!(
            envelope.ciphertext,
            python_envelope["ciphertext"].as_str().unwrap()
        );
        let value = task_detail_to_value(&envelope).unwrap();
        assert_eq!(value["type"], "task_detail");
        assert_eq!(value["route"], route);
        assert_eq!(value["revision"], revision);
        assert_eq!(value["index"], index as u64);
        assert_eq!(value["count"], count);
        assert_eq!(value["epoch"], epoch);
        assert_eq!(
            value["nonce"],
            nonce
                .iter()
                .map(|byte| format!("{byte:02x}"))
                .collect::<String>()
        );
        assert_eq!(value["sequence"], sequence);
        assert_eq!(decrypt_task_detail(&key, &value).unwrap(), chunk);
        joined.push_str(&decrypt_task_detail(&key, &value).unwrap());
    }
    assert_eq!(joined, full_reply);

    let status = StatusSnapshot {
        codex: json!({"codex_model":"fixture", "task_detail":full_reply, "_ui_private":"private"}),
        ..StatusSnapshot::default()
    };
    let messages = status_messages(&status).unwrap();
    assert!(!messages.is_empty());
    for (_, payload) in messages {
        let text = String::from_utf8(payload).unwrap();
        assert!(!text.contains("界界界界"));
        assert!(!text.contains("private"));
    }
}

#[test]
fn pet_upload_vectors_and_replay_guard_match_python_and_firmware_contracts() {
    let fixture = fixture();
    let vectors = &fixture["pet"];
    let key = hex_field(vectors, "key_hex");
    let nonce: [u8; 8] = hex_field(vectors, "exchange_nonce_hex").try_into().unwrap();
    let transaction_id = vectors["transaction_id"].as_u64().unwrap() as u32;
    let begin_hash: [u8; 32] = (0..32).collect::<Vec<_>>().try_into().unwrap();
    let active_hash: [u8; 32] = (0x20..0x40).collect::<Vec<_>>().try_into().unwrap();

    let control_vectors = [
        (
            PetMessageKind::Query,
            PetControlPayload::Query { transaction_id },
            "query_hex",
        ),
        (
            PetMessageKind::Begin,
            PetControlPayload::Begin {
                transaction_id,
                schema: 1,
                width: 144,
                height: 156,
                frame_count: 2,
                package_length: 0x1234,
                sha256: begin_hash,
            },
            "begin_hex",
        ),
        (
            PetMessageKind::Chunk,
            PetControlPayload::Chunk {
                transaction_id,
                offset: 0x20,
                data: b"abc".to_vec(),
            },
            "chunk_hex",
        ),
        (
            PetMessageKind::Commit,
            PetControlPayload::Transaction { transaction_id },
            "commit_hex",
        ),
        (
            PetMessageKind::Cancel,
            PetControlPayload::Transaction { transaction_id },
            "cancel_hex",
        ),
        (
            PetMessageKind::Restore,
            PetControlPayload::Transaction {
                transaction_id: 0x0a0b0c0d,
            },
            "restore_hex",
        ),
    ];
    for (kind, payload, field) in control_vectors {
        let sequence = match kind {
            PetMessageKind::Query => 1,
            PetMessageKind::Begin => 2,
            PetMessageKind::Chunk => 3,
            PetMessageKind::Commit => 4,
            PetMessageKind::Cancel => 5,
            PetMessageKind::Restore => 6,
            _ => unreachable!(),
        };
        let frame = encode_pet_control(&key, &nonce, sequence, kind, payload).unwrap();
        assert_eq!(frame, hex_field(vectors, field), "{field}");
        assert_eq!(decode_pet_control(&key, &frame).unwrap().sequence, sequence);
    }

    let status_vectors = [
        (
            PetMessageKind::State,
            PetStatusPayload::State {
                transaction_id,
                source: PetSource::Custom,
                usable: true,
                schema_min: 1,
                schema_max: 1,
                generation: 7,
                active_sha256: active_hash,
            },
            1,
            "state_hex",
        ),
        (
            PetMessageKind::Progress,
            PetStatusPayload::Progress {
                transaction_id,
                acknowledged_bytes: 3,
                total_bytes: 0x1234,
            },
            2,
            "progress_hex",
        ),
        (
            PetMessageKind::Committed,
            PetStatusPayload::Committed {
                transaction_id,
                generation: 8,
                active_sha256: active_hash,
            },
            3,
            "committed_hex",
        ),
        (
            PetMessageKind::Cancelled,
            PetStatusPayload::Cancelled { transaction_id },
            4,
            "cancelled_hex",
        ),
        (
            PetMessageKind::Error,
            PetStatusPayload::Error {
                transaction_id,
                reason: PetErrorReason::UnsupportedSchema,
                required_bytes: 0,
                available_bytes: 0,
            },
            5,
            "error_hex",
        ),
    ];
    for (kind, payload, sequence, field) in status_vectors {
        let frame = encode_pet_status(&key, &nonce, sequence, kind, payload).unwrap();
        assert_eq!(frame, hex_field(vectors, field), "{field}");
        assert_eq!(decode_pet_status(&key, &frame).unwrap().sequence, sequence);
    }

    let mut replay = PetControlReplayGuard::default();
    replay.set_connection_generation(4).unwrap();
    assert!(replay.begin(nonce, transaction_id).unwrap());
    let first = encode_pet_control(
        &key,
        &nonce,
        1,
        PetMessageKind::Chunk,
        PetControlPayload::Chunk {
            transaction_id,
            offset: 0,
            data: b"abc".to_vec(),
        },
    )
    .unwrap();
    let decoded_first = decode_pet_control(&key, &first).unwrap();
    assert_eq!(
        replay.accept(&decoded_first, &first).unwrap(),
        ReplayDecision::Accepted
    );
    assert_eq!(
        replay.accept(&decoded_first, &first).unwrap(),
        ReplayDecision::Duplicate
    );
    let conflict = encode_pet_control(
        &key,
        &nonce,
        1,
        PetMessageKind::Chunk,
        PetControlPayload::Chunk {
            transaction_id,
            offset: 0,
            data: b"abd".to_vec(),
        },
    )
    .unwrap();
    assert_eq!(
        replay
            .accept(&decode_pet_control(&key, &conflict).unwrap(), &conflict)
            .unwrap(),
        ReplayDecision::Rejected
    );
    let gap = encode_pet_control(
        &key,
        &nonce,
        2,
        PetMessageKind::Chunk,
        PetControlPayload::Chunk {
            transaction_id,
            offset: 4,
            data: b"x".to_vec(),
        },
    )
    .unwrap();
    assert_eq!(
        replay
            .accept(&decode_pet_control(&key, &gap).unwrap(), &gap)
            .unwrap(),
        ReplayDecision::Rejected
    );
    let next = encode_pet_control(
        &key,
        &nonce,
        2,
        PetMessageKind::Chunk,
        PetControlPayload::Chunk {
            transaction_id,
            offset: 3,
            data: b"x".to_vec(),
        },
    )
    .unwrap();
    assert_eq!(
        replay
            .accept(&decode_pet_control(&key, &next).unwrap(), &next)
            .unwrap(),
        ReplayDecision::Accepted
    );
    replay.retire(true);
    assert!(!replay.begin(nonce, transaction_id).unwrap());
}

#[test]
fn python_cxp1_fixture_digest_packbits_and_upload_metadata_stay_in_sync() {
    let fixture = fixture();
    let pet = &fixture["pet"];
    let expected = &pet["cxp1"];
    let package = hex::decode(CXP1_HEX.split_whitespace().collect::<String>()).unwrap();
    assert_eq!(&package[..4], b"CXP1");
    assert_eq!(
        le_u16(&package, 4),
        expected["schema"].as_u64().unwrap() as u16
    );
    assert_eq!(le_u16(&package, 6), 72);
    assert_eq!(
        le_u16(&package, 6),
        expected["header_size"].as_u64().unwrap() as u16
    );
    assert_eq!(
        le_u16(&package, 8),
        expected["width"].as_u64().unwrap() as u16
    );
    assert_eq!(
        le_u16(&package, 10),
        expected["height"].as_u64().unwrap() as u16
    );
    assert_eq!(
        le_u16(&package, 12),
        expected["frame_count"].as_u64().unwrap() as u16
    );
    assert_eq!(
        le_u16(&package, 14),
        expected["palette_count"].as_u64().unwrap() as u16
    );
    assert_eq!(
        le_u16(&package, 16),
        expected["transparent_index"].as_u64().unwrap() as u16
    );
    assert_eq!(le_u32(&package, 32) as usize, package.len());
    assert_eq!(
        package.len(),
        expected["package_length"].as_u64().unwrap() as usize
    );
    assert_eq!(
        hex::encode(&package[40..72]),
        expected["sha256_hex"].as_str().unwrap()
    );

    let mut canonical = package.clone();
    canonical[40..72].fill(0);
    assert_eq!(Sha256::digest(canonical).as_slice(), &package[40..72]);

    let entries = expected["frame_entries"].as_array().unwrap();
    for (index, entry) in entries.iter().enumerate() {
        let offset = 72 + index * 12;
        assert_eq!(
            le_u16(&package, offset),
            entry["duration_ms"].as_u64().unwrap() as u16
        );
        assert_eq!(
            le_u16(&package, offset + 2),
            entry["reserved"].as_u64().unwrap() as u16
        );
        let payload_offset = le_u32(&package, offset + 4) as usize;
        let payload_len = le_u32(&package, offset + 8) as usize;
        assert_eq!(payload_offset, entry["offset"].as_u64().unwrap() as usize);
        assert_eq!(
            payload_len,
            entry["encoded_length"].as_u64().unwrap() as usize
        );
        assert_eq!(
            hex::encode(&package[payload_offset..payload_offset + payload_len]),
            entry["packbits_hex"].as_str().unwrap()
        );
    }

    let key = hex_field(pet, "key_hex");
    let nonce: [u8; 8] = hex_field(pet, "exchange_nonce_hex").try_into().unwrap();
    let metadata_hash: [u8; 32] = package[40..72].try_into().unwrap();
    let upload_transaction_id = expected["upload_transaction_id"].as_u64().unwrap() as u32;
    let upload = encode_pet_control(
        &key,
        &nonce,
        1,
        PetMessageKind::Begin,
        PetControlPayload::Begin {
            transaction_id: upload_transaction_id,
            schema: 1,
            width: 144,
            height: 156,
            frame_count: 2,
            package_length: package.len() as u32,
            sha256: metadata_hash,
        },
    )
    .unwrap();
    assert_eq!(upload, hex_field(pet, "cxp1_upload_begin_hex"));
    assert_eq!(
        decode_pet_control(&key, &upload).unwrap().payload,
        PetControlPayload::Begin {
            transaction_id: upload_transaction_id,
            schema: 1,
            width: 144,
            height: 156,
            frame_count: 2,
            package_length: package.len() as u32,
            sha256: metadata_hash,
        }
    );
}
