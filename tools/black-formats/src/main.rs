use serde_json::json;
use sha2::{Digest, Sha256};
use std::{
    env, fs,
    io::{self, Write},
    path::{Path, PathBuf},
    process::ExitCode,
};

fn collect(path: &Path, files: &mut Vec<PathBuf>) -> Result<(), String> {
    let metadata = fs::symlink_metadata(path).map_err(|e| format!("{}: {e}", path.display()))?;
    if metadata.file_type().is_symlink() {
        return Err(format!("refusing symlink: {}", path.display()));
    }
    if metadata.is_dir() {
        for entry in fs::read_dir(path).map_err(|e| e.to_string())? {
            let entry = entry.map_err(|e| e.to_string())?;
            if entry.file_type().map_err(|e| e.to_string())?.is_symlink() {
                continue;
            }
            collect(&entry.path(), files)?;
        }
    } else if metadata.is_file()
        && path
            .extension()
            .is_some_and(|e| matches!(e.to_ascii_lowercase().to_str(), Some("bin" | "db" | "awd")))
    {
        files.push(path.to_owned());
    }
    Ok(())
}

fn run() -> Result<bool, String> {
    let args: Vec<_> = env::args_os().skip(1).collect();
    if args.is_empty() || args.iter().any(|a| a == "--help" || a == "-h") {
        eprintln!("Usage: black-formats FILE_OR_DIRECTORY [...]\nWrites one JSON object per BIN/DB/AWD file to stdout. Does not modify assets.\nMalformed supported layouts cause exit 1; unsupported layouts are reported explicitly.");
        return Ok(!args.is_empty());
    }
    let mut files = Vec::new();
    for arg in args {
        collect(Path::new(&arg), &mut files)?;
    }
    files.sort();
    files.dedup();
    if files.is_empty() {
        return Err("no .bin, .db or .awd files found".into());
    }
    let mut out = io::BufWriter::new(io::stdout().lock());
    let mut ok = 0;
    let mut bad = 0;
    let mut unsupported = 0;
    for path in files {
        let size = fs::metadata(&path).map_err(|e| e.to_string())?.len();
        if size > 512 * 1024 * 1024 {
            return Err(format!("{} exceeds 512 MiB reader limit", path.display()));
        }
        let bytes = fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        let sha256 = format!("{:x}", Sha256::digest(&bytes));
        let mut report = json!({"path":path.to_string_lossy(),"bytes":bytes.len(),"sha256":sha256});
        match black_formats::inspect(
            &bytes,
            &path.file_name().unwrap_or_default().to_string_lossy(),
        ) {
            Ok(v) => {
                if v["format"] == "unsupported" {
                    unsupported += 1;
                    report["status"] = json!("unsupported");
                } else {
                    ok += 1;
                    report["status"] = json!("ok");
                }
                report["layout"] = v;
            }
            Err(e) => {
                bad += 1;
                report["status"] = json!("error");
                report["error"] = json!(e);
            }
        }
        serde_json::to_writer(&mut out, &report).map_err(|e| e.to_string())?;
        writeln!(out).map_err(|e| e.to_string())?;
    }
    out.flush().map_err(|e| e.to_string())?;
    eprintln!("{ok} parsed, {unsupported} unsupported, {bad} errors");
    Ok(bad == 0)
}
fn main() -> ExitCode {
    match run() {
        Ok(true) => ExitCode::SUCCESS,
        Ok(false) => ExitCode::FAILURE,
        Err(e) => {
            eprintln!("{e}");
            ExitCode::FAILURE
        }
    }
}
