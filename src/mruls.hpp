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

private:
    void initUI();
    void render();

    ftxui::Element renderDetail();
    ftxui::Element renderTable(const std::string &output);
    void fetchJobDetail(std::string id);
    ftxui::Element formatJobDetail(const std::string &raw_content);

    std::vector<std::pair<std::string, std::string>>
    parseJobDetail(const std::string &raw);
    std::vector<ftxui::Element> jobDetailLines(const std::string &raw_content);

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

private:
    std::mutex m_mutex;
    ftxui::ScreenInteractive m_screen;
    ftxui::Component m_main_view;
    std::string m_output;
    std::thread m_job_thread;
    std::thread m_detail_thread;
    std::atomic<bool> m_running{false};

    int m_detail_scroll{-1};
    std::string m_job_detail;
    bool m_detail{false};
    int m_detail_selected{-1};

    int m_selected_row{-1};
    std::vector<std::vector<std::string>> m_current_rows;
    std::vector<std::pair<std::string, std::string>> m_detail_rows;

    std::string m_key_buf;
};
