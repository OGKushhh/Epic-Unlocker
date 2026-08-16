// build.rs — Embed MANIFEST_API_KEY.txt as a plain string in the binary.
//
// Reads the key file at compile time and emits it as a rustc-env variable.
// manifest.rs uses env!() to embed it. No obfuscation, no runtime file read.
// The key file is gitignored — it never enters source control.

use std::fs;
use std::path::Path;

fn main() {
    let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR not set");
    let project_root = Path::new(&manifest_dir)
        .parent()
        .expect("src-tauri has no parent directory");
    let key_file = project_root.join("MANIFEST_API_KEY.txt");

    if key_file.exists() {
        let raw = fs::read_to_string(&key_file)
            .expect("Failed to read MANIFEST_API_KEY.txt");
        let key = raw.trim();

        if key.is_empty() {
            panic!("MANIFEST_API_KEY.txt exists but is empty — add your API key on a single line");
        }

        println!("cargo:rustc-env=MANIFEST_API_KEY={}", key);
        println!("cargo:rerun-if-changed={}", key_file.display());
    } else {
        // No key file — embed empty string, uploads will be disabled gracefully
        println!("cargo:rustc-env=MANIFEST_API_KEY=");
        println!(
            "cargo:warning=MANIFEST_API_KEY.txt not found at {} — manifest uploads disabled",
            key_file.display()
        );
    }

    tauri_build::build()
}
