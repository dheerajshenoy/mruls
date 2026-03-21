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
}
