pub struct Config {
    pub refresh_interval: u64,
}

impl Config {
    pub fn default() -> Self {
        Self {
            refresh_interval: 5,
        }
    }
}
