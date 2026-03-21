use crate::config::Config;
use ratatui::widgets::TableState;

pub enum View {
    JobList,
    JobDetails,
    JobOutput,
}

pub struct App {
    pub view: View,
    pub table_state: TableState,
    pub num_rows: usize,
    pub should_quit: bool,
    pub output_rows: Vec<String>,
    pub config: Config,
    pub selected_job_id: Option<String>,
}

impl App {
    pub fn new() -> Self {
        let mut table_state = TableState::default();
        table_state.select(Some(0));
        Self {
            view: View::JobList,
            table_state,
            num_rows: 0,
            should_quit: false,
            output_rows: Vec::new(),
            config: Config::default(),
            selected_job_id: None,
        }
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
        let data_index = index + 1;
        if data_index < self.output_rows.len() {
            let line = &self.output_rows[data_index];
            let job_id = line.split_whitespace().next().unwrap_or("");
            self.selected_job_id = Some(job_id.to_string());
            self.output_rows.clear(); // clear immediately so old rows don't flash
            self.view = View::JobDetails;
            self.table_state.select(Some(0)); // reset highlight for new view
        }
    }

    pub fn select_current_row(&mut self) {
        match self.view {
            View::JobList => self.select_job(),
            View::JobDetails => {
                // Handle job details selection if needed
            }
            View::JobOutput => {
                // Handle job output selection if needed
            }
        }
    }

    pub fn go_back(&mut self) {
        match self.view {
            View::JobList => {
                // Already at the top level, do nothing
            }
            View::JobDetails | View::JobOutput => {
                self.view = View::JobList;
                self.selected_job_id = None;
                self.table_state.select(Some(0)); // Reset selection to the first row
            }
        }
    }
}
