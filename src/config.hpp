#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

struct Config
{
    struct JobList
    {
        float refresh_interval{5.0f};
    } job_list;

    struct JobDetail
    {
        float refresh_interval{5.0f};
    } job_detail;

    struct JobOutput
    {
        float refresh_interval{5.0f};
    } job_output;

    struct Slurm
    {
        std::string squeue_cmd{
            "squeue -o '%.8i %.9P %8j %.8u %.2t %.10M %.6D %R'"};
        std::string username{"$(whoami)"};
    } slurm;
};
