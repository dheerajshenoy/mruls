pub struct Config {
    pub refresh_interval: u64,
    pub show_line_numbers: bool,
}

impl Config {
    pub fn default() -> Self {
        Self {
            refresh_interval: 5,
            show_line_numbers: true,
        }
    }
}
