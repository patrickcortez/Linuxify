/*
Compile: cargo build --release
Run: ./target/release/atomizer <script.ln>
*/
use std::env;
use std::fs;
use std::io;
use std::path::Path;
use std::process::Command;

fn main() -> io::Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: atomizer <input.ln>");
        std::process::exit(1);
    }

    let input_path = &args[1];
    let content = fs::read_to_string(input_path).map_err(|e| {
        io::Error::new(
            io::ErrorKind::NotFound,
            format!("Failed to read input file '{}': {}", input_path, e),
        )
    })?;

    let rust_code = transpile(&content).map_err(|e| {
        io::Error::new(io::ErrorKind::InvalidData, format!("Transpilation error: {}", e))
    })?;

    let output_rs = format!("{}.rs", Path::new(input_path).file_stem().unwrap().to_string_lossy());
    fs::write(&output_rs, &rust_code)?;

    let mut output_exe = Path::new(input_path).file_stem().unwrap().to_string_lossy().to_string();
    #[cfg(windows)]
    {
        if !output_exe.to_lowercase().ends_with(".exe") {
            output_exe.push_str(".exe");
        }
    }

    let status = Command::new("rustc")
        .arg(&output_rs)
        .arg("-o")
        .arg(&output_exe)
        .status();

    // Clean up temporary Rust file
    let _ = fs::remove_file(&output_rs);

    match status {
        Ok(s) if s.success() => {
            println!("Successfully compiled to {}", output_exe);
            Ok(())
        }
        Ok(_) => {
            eprintln!("Compilation failed during rustc execution.");
            std::process::exit(1);
        }
        Err(e) => {
            eprintln!("Failed to execute rustc: {}", e);
            std::process::exit(1);
        }
    }
}

fn transpile(source: &str) -> Result<String, String> {
    let mut rust_code = String::new();
    rust_code.push_str("fn main() {\n");

    for (i, line) in source.lines().enumerate() {
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed.starts_with("//") {
            continue;
        }

        match parse_line(trimmed) {
            Ok(code) => rust_code.push_str(&format!("    {}\n", code)),
            Err(e) => return Err(format!("Line {}: {}", i + 1, e)),
        }
    }

    rust_code.push_str("}\n");
    Ok(rust_code)
}

fn parse_line(line: &str) -> Result<String, String> {
    if let Some(content) = parse_out(line) {
        return Ok(format!("println!(\"{}\");", content));
    }
    
    // Add future syntax parsers here (e.g., if let Some(...) = parse_variable(line) ...)

    Err(format!("Unknown syntax: '{}'", line))
}

fn parse_out(line: &str) -> Option<String> {
    if line.starts_with("out(\"") && line.ends_with("\")") {
        // Extract content between out(" and ")
        // Length of 'out("' is 5, '")' is 2
        let content = &line[5..line.len() - 2];
        return Some(content.to_string());
    }
    None
}
