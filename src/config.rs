use std::fs;
use std::process::Command;
use toml::Value;

pub struct Config {
    pub refresh_interval: u64,
    pub show_line_numbers: bool,
    pub relative_line_numbers: bool,
    pub confirm_on_quit: bool,
    pub max_lines: Option<i64>,
    pub username: String,
    pub slurm_command: String,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            refresh_interval: 5,
            show_line_numbers: true,
            confirm_on_quit: true,
            relative_line_numbers: true,
            max_lines: Some(-1),
            username: String::new(),
            slurm_command: "squeue -o %i %u %j %t %M %D %R".to_string(),
        }
    }
}

impl Config {
    pub fn load(path: &str) -> Self {
        let content = fs::read_to_string(path).unwrap_or_else(|_| {
            eprintln!("Failed to read config file at '{}', using defaults.", path);
            String::new()
        });

        let value = content.parse::<Value>().unwrap_or_else(|_| {
            eprintln!("Failed to parse config file, using defaults.");
            Value::Table(Default::default())
        });

        let refresh_interval = value
            .get("general")
            .and_then(|g| g.get("refresh_interval"))
            .and_then(|v| v.as_integer())
            .unwrap_or(5) as u64;

        let show_line_numbers = value
            .get("general")
            .and_then(|g| g.get("show_line_numbers"))
            .and_then(|v| v.as_bool())
            .unwrap_or(true);

        let confirm_on_quit = value
            .get("general")
            .and_then(|g| g.get("confirm_on_quit"))
            .and_then(|v| v.as_bool())
            .unwrap_or(true);

        let relative_line_numbers = value
            .get("general")
            .and_then(|g| g.get("relative_line_numbers"))
            .and_then(|v| v.as_bool())
            .unwrap_or(true);

        let max_lines = value
            .get("general")
            .and_then(|g| g.get("max_lines"))
            .and_then(|v| v.as_integer())
            .map(|i| if i < 0 { None } else { Some(i) })
            .unwrap_or(Some(-1));

        let username = value
            .get("slurm")
            .and_then(|g| g.get("username"))
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();

        let slurm_command = value
            .get("slurm")
            .and_then(|g| g.get("command"))
            .and_then(|v| v.as_str())
            .unwrap_or("squeue -o %i %u %t %M %D %R")
            .to_string();

        Self {
            refresh_interval,
            show_line_numbers,
            confirm_on_quit,
            relative_line_numbers,
            max_lines,
            username,
            slurm_command,
        }
    }
}
