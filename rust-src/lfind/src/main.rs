use std::env;
use std::fs;
use std::path::Path;
use std::process;
use std::io;

struct Config {
    paths: Vec<String>,
    name_pattern: Option<String>,
    file_type: Option<FileType>,
    help: bool,
}

enum FileType {
    File,
    Directory,
}

impl Config {
    fn new(args: &[String]) -> Result<Config, String> {
        let mut paths = Vec::new();
        let mut name_pattern = None;
        let mut file_type = None;
        let mut help = false;

        let mut i = 1;
        while i < args.len() {
            match args[i].as_str() {
                "-name" | "-n" => {
                    if i + 1 < args.len() {
                        name_pattern = Some(args[i + 1].clone());
                        i += 1;
                    } else {
                        return Err("Missing argument for -name".to_string());
                    }
                }
                "-type" | "-t" => {
                    if i + 1 < args.len() {
                        let t = &args[i + 1];
                        if t == "f" || t == "file" {
                            file_type = Some(FileType::File);
                        } else if t == "d" || t == "dir" || t == "directory" {
                            file_type = Some(FileType::Directory);
                        } else {
                            return Err(format!("Unknown type '{}'. Use 'f' for file or 'd' for directory.", t));
                        }
                        i += 1;
                    } else {
                        return Err("Missing argument for -type".to_string());
                    }
                }
                "-help" | "--help" | "-h" | "/?" => {
                    help = true;
                }
                arg => {
                    if !arg.starts_with("-") {
                        paths.push(arg.to_string());
                    } else {
                        return Err(format!("Unknown option: {}", arg));
                    }
                }
            }
            i += 1;
        }

        if paths.is_empty() {
            paths.push(".".to_string());
        }

        Ok(Config {
            paths,
            name_pattern,
            file_type,
            help,
        })
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();

    let config = match Config::new(&args) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("lfind: {}", e);
            process::exit(1);
        }
    };

    if config.help {
        print_usage();
        return;
    }

    let mut found_any = false;
    let mut has_error = false;

    for path_str in &config.paths {
        let path = Path::new(path_str);
        
        if !path.exists() {
            eprintln!("lfind: {}: No such file or directory", path_str);
            if config.name_pattern.is_none() && !path_str.starts_with("-") {
                eprintln!("(Hint: To search for a file named '{}', use: lfind -name {})", path_str, path_str);
            }
            has_error = true;
            continue;
        }

        if let Err(e) = visit_dirs(path, &config, &mut found_any) {
             // Top level error (e.g. starting at a protected root)
             eprintln!("lfind: {}: {}", path_str, e);
             has_error = true;
        }
    }
    
    if has_error {
        process::exit(1);
    }
}

fn print_usage() {
    println!("Usage: lfind [path...] [options]");
    println!("Options:");
    println!("  -name <pattern>   Search for files matching glob pattern (e.g., *.rs, test_*)");
    println!("  -type <t>         Filter by type: 'f' (file) or 'd' (directory)");
    println!("  -help             Show this help message");
}

fn visit_dirs(dir: &Path, config: &Config, _found_any: &mut bool) -> io::Result<()> {
    if dir.is_dir() {
        check_path(dir, config);

        match fs::read_dir(dir) {
            Ok(entries) => {
                for entry in entries {
                    match entry {
                        Ok(entry) => {
                            let path = entry.path();
                            if path.is_dir() {
                                // Recursively visit directory
                                // FIX: Don't propagate error immediately. Catch it and continue siblings.
                                if let Err(e) = visit_dirs(&path, config, _found_any) {
                                    eprintln!("lfind: {}: {}", path.display(), e);
                                    // We continue loop despite error in subdirectory
                                }
                            } else {
                                check_path(&path, config);
                            }
                        }
                        Err(e) => {
                             if e.kind() != io::ErrorKind::PermissionDenied {
                                 eprintln!("lfind: error reading entry: {}", e);
                             }
                        },
                    }
                }
            }
            Err(e) => {
                 if e.kind() == io::ErrorKind::PermissionDenied {
                     return Ok(());
                 }
                 return Err(e);
            }
        }
    } else {
         check_path(dir, config);
    }
    Ok(())
}

fn check_path(path: &Path, config: &Config) {
    if let Some(ref ft) = config.file_type {
        match ft {
            FileType::File => if !path.is_file() { return; },
            FileType::Directory => if !path.is_dir() { return; },
        }
    }

    if let Some(ref pattern) = config.name_pattern {
        if let Some(file_name) = path.file_name().and_then(|n| n.to_str()) {
            if !matches_pattern(file_name, pattern) {
                return;
            }
        } else {
            return;
        }
    }

    println!("{}", path.display());
}

fn matches_pattern(s: &str, pattern: &str) -> bool {
    let s_chars: Vec<char> = s.chars().collect();
    let p_chars: Vec<char> = pattern.chars().collect();
    let mut s_idx = 0;
    let mut p_idx = 0;
    let mut star_idx = None;
    let mut s_star_match_idx = 0;

    while s_idx < s_chars.len() {
        if p_idx < p_chars.len() && (p_chars[p_idx] == '?' || p_chars[p_idx] == s_chars[s_idx]) {
            s_idx += 1;
            p_idx += 1;
        } else if p_idx < p_chars.len() && p_chars[p_idx] == '*' {
            star_idx = Some(p_idx);
            s_star_match_idx = s_idx;
            p_idx += 1;
        } else if let Some(si) = star_idx {
            p_idx = si + 1;
            s_star_match_idx += 1;
            s_idx = s_star_match_idx;
        } else {
            return false;
        }
    }

    while p_idx < p_chars.len() && p_chars[p_idx] == '*' {
        p_idx += 1;
    }

    p_idx == p_chars.len()
}
