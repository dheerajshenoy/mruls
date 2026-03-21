mod app;
mod config;
mod tui;

const APP_NAME: &str = "MRULS";

struct Args {
    slurm_command: Option<String>,
    username: Option<String>,
    refresh_interval: Option<u64>,
    config_path: Option<String>,
    help: bool,
    version: bool,
}

impl Args {
    fn parse() -> Self {
        let mut args = std::env::args().skip(1);
        let mut slurm_command = None;
        let mut username = None;
        let mut refresh_interval = None;
        let mut config_path = None;
        let mut help = false;
        let mut version = false;

        while let Some(arg) = args.next() {
            match arg.as_str() {
                "--slurm_command" => slurm_command = args.next(),
                "-u" | "--username" => username = args.next(),
                "--refresh-interval" => refresh_interval = args.next().and_then(|s| s.parse().ok()),
                "--config" => config_path = args.next(),
                "-h" | "--help" => help = true,
                "--version" => version = true,
                _ => {
                    eprintln!("Unknown argument: {}", arg);
                    help = true;
                    break;
                }
            }
        }

        Self {
            slurm_command,
            username,
            refresh_interval,
            config_path,
            help,
            version,
        }
    }

    fn print_help() {
        println!("Usage: slurm-tui [OPTIONS]");
        println!();
        println!("Options:");
        println!("  --slurm-command <CMD>       Custom command to fetch Slurm jobs");
        println!("  -u, --username <USERNAME>  Username to filter jobs");
        println!("  --refresh-interval <SEC>   Refresh interval in seconds (default: 5)");
        println!("  --config <PATH>            Path to configuration file");
        println!("  -h, --help                 Print help information");
        println!("  --version                  Print version information");
    }
}

fn main() -> Result<(), std::io::Error> {
    let args = Args::parse();

    if args.help {
        Args::print_help();
        return Ok(());
    }

    if args.version {
        println!("{APP_NAME} version 0.1.0");
        return Ok(());
    }

    if let Err(err) = tui::run(args) {
        eprintln!("{:?}", err);
    }
    Ok(())
}
