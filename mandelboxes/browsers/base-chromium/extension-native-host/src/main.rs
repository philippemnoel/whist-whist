mod native_host_messages;
mod triggers;

use native_host_messages::{read_message, send_message, NativeHostMessage, NativeHostMessageType};
use serde_json::json;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::process::Command;
use triggers::{trigger_path, write_trigger, write_trigger_sequential, Trigger};

// If the extension hasn't tried updating yet, then tell the
// extension to update and use the trigger to keep track of this
// fact. Else, just tell the extension that we're ready to go.
fn check_for_updates() -> Result<(), String> {
    let needs_update = !trigger_path(Trigger::DidUpdateExtension).exists();
    if needs_update {
        write_trigger(Trigger::DidUpdateExtension, "")?;
    }
    send_message(
        std::io::stdout(),
        &NativeHostMessage {
            msg_type: NativeHostMessageType::NativeHostUpdate,
            value: json!(needs_update),
        },
    )
}

fn send_keepalive_messages(keep_running: &AtomicBool) -> Result<(), String> {
    const KEEPALIVE: NativeHostMessage = NativeHostMessage {
        msg_type: NativeHostMessageType::NativeHostKeepalive,
        value: json!(null),
    };
    let stdout = std::io::stdout();
    while keep_running.load(Ordering::Relaxed) {
        send_message(&stdout, &KEEPALIVE)?;
        thread::sleep(std::time::Duration::from_secs(5));
    }
    Ok(())
}

fn handle_download_complete(msg: NativeHostMessage) -> Result<(), String> {
    let relative_filename = match msg.value.as_str() {
        Some(filename) => match filename.split("/").last() {
            Some(filename) => filename.to_string(),
            None => {
                eprintln!("Invalid filename: {}", filename);
                return Ok(());
            }
        },
        None => {
            eprintln!("DownloadComplete message did not contain a filename");
            return Ok(());
        }
    };
    write_trigger_sequential(Trigger::FileDownload, &relative_filename)?;
    Ok(())
}

fn handle_pointer_lock(msg: NativeHostMessage) -> Result<(), String> {
    // These technically don't need to be sequential since we only care about
    // latest state. But for now we do this to avoid any potential race conditions
    // between the protocol reading and deleting the trigger file.
    write_trigger_sequential(
        Trigger::PointerLock,
        match msg.value.as_bool() {
            Some(true) => "1",
            Some(false) => "0",
            None => {
                eprintln!("PointerLock message did not contain a boolean");
                return Ok(());
            }
        },
    )?;
    Ok(())
}

fn handle_keyboard_repeat_rate_change(msg: NativeHostMessage) -> Result<(), String> {
    let repeatDelay = msg.value["repeatDelay"].as_i64();
    let repeatRate = msg.value["repeatRate"].as_i64();

    if repeatDelay.is_none() || repeatRate.is_none() {
        eprintln!("KeyboardRepeatRate message did not contain repeatDelay and repeatRate strings");
        return Ok(());
    }

    let xset_cmd = Command::new("xset").args(
        ["r", "rate", repeatDelay.unwrap().to_string().as_str(), repeatRate.unwrap().to_string().as_str(), 
        "-display", ":10"]).spawn();
    Ok(())
}

fn handle_timezone_change(msg: NativeHostMessage) -> Result<(), String> {
    match msg.value.as_str() {
        Some(timezone) => {
            let set_timezone_cmd = Command::new("/usr/bin/sudo-set-timezone.sh")
                .arg(timezone)
                .spawn();
        },
        None => {
            eprintln!("Timezone message did not contain timezone");
            return Ok(());
        }
    }
    Ok(())
}

fn main() -> Result<(), String> {
    check_for_updates()?;

    let keep_running = Arc::new(AtomicBool::new(true));

    let keep_running_keepalive = keep_running.clone();
    let keepalive_thread = thread::spawn(move || -> Result<(), String> {
        send_keepalive_messages(&keep_running_keepalive)
    });

    loop {
        match read_message(std::io::stdin()) {
            Ok(msg) => match msg.msg_type {
                NativeHostMessageType::NativeHostExit => break,
                NativeHostMessageType::DownloadComplete => handle_download_complete(msg)?,
                NativeHostMessageType::PointerLock => handle_pointer_lock(msg)?,
                NativeHostMessageType::KeyboardRepeatRate => handle_keyboard_repeat_rate_change(msg)?,
                NativeHostMessageType::Timezone => handle_timezone_change(msg)?,
                _ => {}
            },
            Err(e) => eprintln!("{}", e),
        }
    }

    keep_running.store(false, Ordering::Relaxed);
    match keepalive_thread.join().unwrap() {
        Ok(_) => Ok(()),
        Err(e) => {
            eprintln!("Error in keepalive thread: {}", e);
            std::process::exit(1);
        }
    }
}
