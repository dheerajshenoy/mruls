#pragma once

#include "argparse.hpp"

#include <atomic>
#include <condition_variable>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class mruls
{
public:
    mruls(const argparse::ArgumentParser &args);
    ~mruls();
    void loop();

    // Non-copyable, non-movable (owns threads)
    mruls(const mruls &)            = delete;
    mruls &operator=(const mruls &) = delete;
    mruls(mruls &&)                 = delete;
    mruls &operator=(mruls &&)      = delete;

private:
    enum class ViewType
    {
        JOB_LIST,
        JOB_DETAIL,
        JOB_OUTPUT,
    };

    enum class OutputType
    {
        STDOUT,
        STDERR,
    };

    // Initialization
    void initUI();
    void readArgs(const argparse::ArgumentParser &args);

    // Rendering
    ftxui::Element renderJobList();
    ftxui::Element renderDetail();
    ftxui::Element renderOutput();

    // Data fetching
    void fetchJobDetail(const std::string &job_id);
    void refreshJobList();
    void loadJobOutput(const std::string &job_id);
    void toggleOutputType();
    std::string getJobOutputPath(const std::string &job_id, OutputType type);

    // Parsing
    static std::vector<std::string> splitLine(const std::string &line);
    static std::vector<std::vector<std::string>>
    parseOutput(const std::string &output);
    static std::vector<std::pair<std::string, std::string>>
    parseKeyValue(const std::string &raw);
    static std::string execCommand(const std::string &cmd);

    // Navigation
    void navUp();
    void navDown();
    void navBegin();
    void navEnd();
    void selectJob();
    void goBack();
    void quit();

    // Event handling
    bool handleEvent(ftxui::Event event);
    bool handleKeySequence(const std::string &ch);

    // Thread-safe state access
    int getRowCount() const;

    // Configuration
    void initConfig();

private:
    static constexpr int FOOTER_HEIGHT    = 4;
    static constexpr int REFRESH_INTERVAL = 5;
    static constexpr const char *SQUEUE_CMD
        = "squeue -o '%.8i %.9P %.8j %.8u %.2t %.10M %.6D %R'";

    // Screen (must be first - other members may depend on it)
    ftxui::ScreenInteractive m_screen;
    ftxui::Component m_main_view;

    // Threading
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_refresh_thread;
    std::thread m_detail_thread;
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_detail_pending{false};

    // View state
    ViewType m_view_type{ViewType::JOB_LIST};
    std::string m_key_buffer;

    // Job list state (protected by m_mutex)
    std::string m_raw_output;
    std::vector<std::vector<std::string>> m_job_rows;
    std::vector<int> m_col_widths;
    int m_selected{1}; // Start at first data row (0 is header)
    bool m_dirty{true};

    // Detail view state (protected by m_mutex)
    std::vector<std::pair<std::string, std::string>> m_detail_rows;
    int m_detail_selected{0};
    int m_scroll_y{0};

    // Output view state
    std::string m_current_job_id;
    std::string m_output_path;
    OutputType m_output_type{OutputType::STDOUT};

    // Configuration
    std::string m_config_file_path;
};
