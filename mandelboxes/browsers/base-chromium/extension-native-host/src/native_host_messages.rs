use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::io::{Read, Write};

// TODO: Make this automatically in-sync with the typescript src/constants/ipc.ts.
// Until then, we must manually keep these in sync. Also, we could eventually force
// a schema for "value" based on "type".
#[derive(Debug, Serialize, Deserialize, PartialEq)]
pub enum NativeHostMessageType {
    #[serde(rename = "DOWNLOAD_COMPLETE")]
    DownloadComplete,
    // Technically we abuse notation -- if we _send_ to the extension, it will
    // disconnect the native host. But we use this internally to signal various
    // _received_ messages from the extension which mean that we should exit from
    // the host.
    #[serde(rename = "NATIVE_HOST_EXIT")]
    NativeHostExit,
    #[serde(rename = "NATIVE_HOST_UPDATE")]
    NativeHostUpdate,
    #[serde(rename = "POINTER_LOCK")]
    PointerLock,
    #[serde(rename = "NATIVE_HOST_KEEPALIVE")]
    NativeHostKeepalive,
    #[serde(rename = "KEYBOARD_REPEAT_RATE")]
    KeyboardRepeatRate,
    #[serde(rename = "TIMEZONE")]
    Timezone,
}

#[derive(Debug, Serialize, Deserialize, PartialEq)]
pub struct NativeHostMessage {
    #[serde(rename = "type")]
    pub msg_type: NativeHostMessageType,
    pub value: Value,
}

pub fn send_message<W: Write>(mut output: W, msg: &NativeHostMessage) -> Result<(), String> {
    let msg_str = serde_json::to_string(msg).map_err(|e| e.to_string())?;
    let msg_len = msg_str.len();
    // Chromium limits size to 1 MB.
    if msg_len > 1024 * 1024 {
        return Err(format!("Message is too large: {} bytes", msg_len));
    }
    let msg_len = msg_len as u32;

    // [msg_len in native endian (4 bytes), msg_str in UTF-8 (msg_len bytes)]
    let mut msg_buf: Vec<u8> = Vec::new();
    msg_buf.extend_from_slice(&msg_len.to_ne_bytes());
    msg_buf.extend_from_slice(msg_str.as_bytes());

    // Note that by default, stdout should grab a lock, so writing will
    // be thread-safe.
    output.write_all(&msg_buf).map_err(|e| e.to_string())?;
    output.flush().map_err(|e| e.to_string())?;
    Ok(())
}

pub fn read_message<R: Read>(mut input: R) -> Result<NativeHostMessage, String> {
    let mut len_buf = [0; 4];
    match input
        .read_exact(&mut len_buf)
        .map(|()| i32::from_ne_bytes(len_buf))
    {
        Ok(msg_len) => {
            if msg_len == -1 {
                // (Undocumented Chromium behavior) The browser is exiting, so
                // tell the extension to gracefully disconnect this native host.
                return Ok(NativeHostMessage {
                    msg_type: NativeHostMessageType::NativeHostExit,
                    value: json!(null),
                });
            }
            let mut msg_buf = vec![0; msg_len as usize];
            input.read_exact(&mut msg_buf).map_err(|e| e.to_string())?;
            let msg: NativeHostMessage =
                serde_json::from_slice(&msg_buf).map_err(|e| e.to_string())?;
            Ok(msg)
        }
        Err(e) => match e.kind() {
            std::io::ErrorKind::UnexpectedEof => Ok(NativeHostMessage {
                msg_type: NativeHostMessageType::NativeHostExit,
                value: json!(null),
            }),
            _ => Err(e.to_string()),
        },
    }
}

#[cfg(test)]
mod tests {
    // We use this type of macro to parametrize unit tests with different arguments.
    macro_rules! send_message_tests {
        ($($name:ident: $params:expr,)*) => {
        $(
            #[test]
            fn $name() {
                use super::*;
                use std::io::Cursor;
                let mut output = Cursor::new(Vec::new());
                let (input_msg_type, input_value) = $params;
                let msg = NativeHostMessage {
                    msg_type: input_msg_type,
                    value: input_value,
                };
                assert!(send_message(&mut output, &msg).is_ok());
                let msg_str = output.into_inner();
                let msg_len = i32::from_ne_bytes(msg_str[0..4].try_into().unwrap());
                assert_eq!(msg_len + 4, msg_str.len() as i32);
                let msg_str = String::from_utf8(msg_str[4..].to_vec()).unwrap();
                let msg: NativeHostMessage = serde_json::from_str(&msg_str).unwrap();
                let (expected_msg_type, expected_value) = $params;
                assert_eq!(msg.msg_type, expected_msg_type);
                assert_eq!(msg.value, expected_value);
            }
        )*
        }
    }

    send_message_tests! {
        send_message_download_null: (NativeHostMessageType::DownloadComplete, json!(null)),
        send_message_update_string: (NativeHostMessageType::NativeHostUpdate, json!("foo")),
        send_message_keepalive_object: (NativeHostMessageType::NativeHostKeepalive, json!({
            "foo": "bar",
            "hello": "world",
            "number": 42,
            "object": {
                "foo": "bar",
                "hello": "world",
                "number": 42,
            },
        })),
    }

    #[test]
    fn send_message_too_large_error() {
        use super::*;
        use std::io::Cursor;
        let mut output = Cursor::new(Vec::new());
        let msg = NativeHostMessage {
            msg_type: NativeHostMessageType::DownloadComplete,
            value: json!(vec![0; 1024 * 1024 + 1]),
        };
        assert_eq!(
            send_message(&mut output, &msg),
            Err(format!(
                "Message is too large: {} bytes",
                serde_json::to_string(&msg).unwrap().len()
            ))
        );
    }

    macro_rules! send_then_read_message_tests {
        ($($name:ident: $params:expr,)*) => {
        $(
            #[test]
            fn $name() {
                use super::*;
                use std::io::Cursor;
                let mut output = Cursor::new(Vec::new());
                let (input_msg_type, input_value) = $params;
                let msg = NativeHostMessage {
                    msg_type: input_msg_type,
                    value: input_value,
                };
                assert!(send_message(&mut output, &msg).is_ok());
                let mut input = Cursor::new(output.into_inner());
                let msg = read_message(&mut input).unwrap();
                let (expected_msg_type, expected_value) = $params;
                assert_eq!(msg.msg_type, expected_msg_type);
                assert_eq!(msg.value, expected_value);
            }
        )*
        }
    }

    send_then_read_message_tests! {
        send_then_read_message_download_null: (NativeHostMessageType::DownloadComplete, json!(null)),
        send_then_read_message_update_string: (NativeHostMessageType::NativeHostUpdate, json!("foo")),
        send_then_read_message_keepalive_object: (NativeHostMessageType::NativeHostKeepalive, json!({
            "foo": "bar",
            "hello": "world",
            "number": 42,
            "object": {
                "foo": "bar",
                "hello": "world",
                "number": 42,
            },
        })),
    }

    #[test]
    fn read_message_unexpected_eof() {
        use super::*;
        use std::io::Cursor;
        let mut input = Cursor::new(vec![]);
        assert_eq!(
            read_message(&mut input).unwrap(),
            NativeHostMessage {
                msg_type: NativeHostMessageType::NativeHostExit,
                value: json!(null),
            }
        );
    }

    #[test]
    fn read_message_too_short() {
        use super::*;
        use std::io::Cursor;
        let mut input = Cursor::new(vec![10, 0, 0, 0, 0]);
        assert_eq!(
            read_message(&mut input).unwrap_err(),
            "failed to fill whole buffer".to_string()
        );
    }

    #[test]
    fn read_message_browser_exit() {
        // Undocumented behavior: on browser exit, the message length is -1
        use super::*;
        use std::io::Cursor;
        let len: i32 = -1;
        let mut input = Cursor::new(len.to_ne_bytes());
        assert_eq!(
            read_message(&mut input).unwrap(),
            NativeHostMessage {
                msg_type: NativeHostMessageType::NativeHostExit,
                value: json!(null),
            }
        );
    }
}
