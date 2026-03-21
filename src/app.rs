use crate::Args;

use crate::config::Config;
use ratatui::widgets::TableState;

pub enum View {
    JobList,
    JobDetails,
    JobOutput,
}

#[derive(Clone)]
pub enum Dialog {
    None,
    ConfirmQuit,
    ConfirmCancel { job_id: String },
}

pub struct App {
    pub view: View,
    pub table_state: TableState,
    pub num_rows: usize,
    pub should_quit: bool,
    pub output_rows: Vec<String>,
    pub config: Config,
    pub selected_job_id: Option<String>,
    pub count_buffer: String,
    pub pending_key: Option<char>,
    pub should_refresh: bool,
    pub dialog: Dialog,
}

// in app.rs
fn build_config(args: &Args) -> Config {
    let mut config = match &args.config_path {
        Some(path) => Config::load(path),
        None => Config::default(),
    };

    // CLI overrides
    if let Some(ref username) = args.username {
        config.username = username.clone();
    }
    if let Some(refresh) = args.refresh_interval {
        config.refresh_interval = refresh;
    }

    config
}

impl App {
    pub fn new(args: Args) -> Self {
        let config = build_config(&args);
        let mut table_state = TableState::default();
        table_state.select(Some(0));

        Self {
            view: View::JobList,
            table_state,
            num_rows: 0,
            should_quit: false,
            output_rows: Vec::new(),
            config: config,
            selected_job_id: None,
            count_buffer: String::new(),
            pending_key: None,
            should_refresh: false,
            dialog: Dialog::None,
        }
    }

    pub fn load_config(&mut self, path: &str) {
        self.config = Config::load(path);
    }

    pub fn quit(&mut self) -> bool {
        self.should_quit = true;
        return true;
    }

    pub fn next_row(&mut self) {
        let next = self.table_state.selected().map(|i| i + 1).unwrap_or(0);
        if next < self.num_rows {
            self.table_state.select(Some(next));
        }
    }

    pub fn prev_row(&mut self) {
        let prev = self
            .table_state
            .selected()
            .map(|i| i.saturating_sub(1))
            .unwrap_or(0);
        if prev < self.num_rows {
            self.table_state.select(Some(prev));
        }
    }

    fn select_job(&mut self) {
        let index = self.table_state.selected().unwrap_or(0);
        let data_index = index + 1; // Skip header row
        if data_index < self.output_rows.len() {
            let line = &self.output_rows[data_index];
            let job_id = line.split_whitespace().next().unwrap_or("");
            self.selected_job_id = Some(job_id.to_string());
            self.output_rows.clear();
            self.view = View::JobDetails;
            self.table_state.select(Some(0));
        }
    }

    pub fn select_current_row(&mut self) {
        match self.view {
            View::JobList => self.select_job(),
            View::JobDetails => {}
            View::JobOutput => {}
        }
    }

    pub fn go_back(&mut self) {
        match self.view {
            View::JobList => {}
            View::JobDetails | View::JobOutput => {
                self.view = View::JobList;
                self.selected_job_id = None;
                self.table_state.select(Some(0));
            }
        }
    }

    pub fn first_row(&mut self) {
        self.table_state.select(Some(0));
    }

    pub fn last_row(&mut self) {
        if self.num_rows > 0 {
            self.table_state.select(Some(self.num_rows - 1));
        }
    }

    pub fn get_count(&mut self) -> usize {
        let count = self.count_buffer.parse::<usize>().unwrap_or(1);
        self.count_buffer.clear();
        count
    }

    pub fn toggle_line_numbers(&mut self) {
        self.config.show_line_numbers = !self.config.show_line_numbers;
    }
}
