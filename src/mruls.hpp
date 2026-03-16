#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/screen.hpp>
#include <iostream>
#include <thread>

class mruls
{

public:
    mruls();
    ~mruls();
    void loop();

    enum class DialogType
    {
        None = 0,
        Quit,
        KillJob,
    };

private:
    void initUI();
    void render();

    ftxui::Element renderDetail();
    ftxui::Element renderOutput();
    ftxui::Element renderTable(const std::string &output);
    void fetchJobDetail(std::string id);
    ftxui::Element formatJobDetail(const std::string &raw_content);

    std::vector<std::pair<std::string, std::string>>
    parseJobDetail(const std::string &raw);
    std::vector<ftxui::Element> jobDetailLines(const std::string &raw_content);
    ftxui::Element renderDialog();

    void detail_beginning();
    void detail_end();
    void detail_next_line();
    void detail_prev_line();

    void job_beginning();
    void job_end();
    void job_next_line();
    void job_prev_line();
    void job_select();

    void quit();
    void refresh_job_list();
    bool handle_key_sequence(const std::string &ch);
    std::string getStdOutPathFromJob(const std::string &job_id) noexcept;

private:
    enum class ViewType
    {
        JOB_LIST = 0,
        JOB_DETAIL,
        JOB_OUTPUT,
    };

    std::mutex m_mutex;
    ftxui::ScreenInteractive m_screen;
    ftxui::Component m_main_view;
    std::string m_output;
    std::thread m_job_thread;
    std::thread m_detail_thread;
    std::atomic<bool> m_running{false};

    int m_scroll_y{-1};
    std::string m_job_detail;
    int m_detail_selected{-1};

    int m_selected_row{-1};
    std::vector<std::vector<std::string>> m_current_rows;
    std::vector<std::pair<std::string, std::string>> m_detail_rows;
    std::string m_key_buf;
    std::string m_stdout_file_path;

    // Cached parsed table state
    std::vector<std::vector<std::string>>
        m_display_rows; // header + numbered data rows
    std::vector<int> m_col_widths;
    bool m_output_dirty{true};
    std::atomic<bool> m_detail_cancel;
    std::condition_variable m_cv;
    ViewType m_view_type{ViewType::JOB_LIST};

    DialogType m_dialog_type{DialogType::None};
};
