// Header and license info goes here
// Copyright 2016 University of Bristol, all rights reserved

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef PLATFORM_WINDOWS
# include <ctime>
//# define DISABLE_WATERLOO_CALLS
# ifdef _DEBUG
#  pragma comment(lib, "../UQDevices/CTimeTag/Win64/CTimeTagLibDebug.lib")
# else
#  pragma comment(lib, "../UQDevices/CTimeTag/Win64/CTimeTagLib.lib")
# endif
#else
# include <unistd.h>   // for usleep
# include <execinfo.h> // for crash handling
# include <signal.h>   // for crash handling
#endif
#include <vector>
#include <map>
#include <string>
#include <time.h>
#include <ctime>
#include "websocket_relay/websocket_relay.h"
#include "mini_parser.h"
#include "cqp_detectotron.h"
#include "./UQDevices/CTimeTag/Include/CTimeTag.h"
#include "./UQDevices/CTimeTag/Include/CLogic.h"

#ifndef MAX_PATH
# define MAX_PATH 4096
#endif

using namespace TimeTag;

#define NUM_CHANNELS 16

bool do_data_relay = true;      // Relay the data to anyone who's listening

uint32_t fake_data_channels = 0;

std::vector<uint32_t> channel_counts(NUM_CHANNELS);
std::vector<float> current_threshold_volts(NUM_CHANNELS);
std::vector<float> current_channel_delay_ns(NUM_CHANNELS);
std::vector<std::string> user_locked_channels(NUM_CHANNELS);
uint32_t    single_filter_channels = 0;      // Bitfield of channels filtered to disallow single counts
uint32_t    edge_inversion_channels = 0;     // 0 bits are rising edge, 1 bits are falling edge
uint32_t    single_filter_minimum_hits = 2;  // (should always be 2) Minimum number of counts for filtering
float       single_filter_window_ns = 10.0f; // Window size for filtering out single counts
uint32_t    unused_channel_mask = ~0;        // Track which channels aren't in use by anyone.

uint32_t startflag = 0;
uint32_t sentlines = 0;

uint32_t counter = 0;
uint32_t px = 0;
uint32_t px_resolution_x = 130;
uint32_t px_resolution_y = 130;
uint32_t countme = 0;

long start = 0.0f;
long duration = 0.0f;


int current_error_number = 0;
double current_error_time = 0;
std::string current_error_string = "";

#ifndef DISABLE_WATERLOO_CALLS
CTimeTag* timetag = NULL;
CLogic* global_logic = NULL;
#endif

double time_resolution_sec = 1.0;

std::vector<ListenerSetupData*> current_listeners;

enum WaterlooDataMode {
    DATA_MODE_NONE,
    DATA_MODE_TIMETAG,
    DATA_MODE_LOGIC,
    DATA_MODE_HOWMANY   // just to count and terminate
};
const char* waterloo_data_mode_names[DATA_MODE_HOWMANY] = {"none", "timetag", "logic"};
WaterlooDataMode current_waterloo_data_mode = DATA_MODE_TIMETAG;

#ifdef PLATFORM_WINDOWS
char log_directory[MAX_PATH] = "./cqp_logs";
#else
char log_directory[MAX_PATH] = "/var/www/html/cqp_logs";
#endif

double last_heartbeat_time = 0;
double heartbeat_interval_seconds = 1.0;
double sample_time = 0;
double last_sample_time = 0;
int raw_incoming_counts = 0;
char local_time_str[1024] = "";

struct WaterlooBuffer {
    std::vector<unsigned char> chan;
    std::vector<long long> time;
};
WaterlooBuffer waterloo_double_buffer[2];
int double_buffer_fill_index = 0;   // Which buffer is being filled by waterloo_data_read_thread
std::mutex waterloo_access_mutex;
std::mutex waterloo_buffer_collect_mutex;


uint64_t nsec_to_timetag_units(double nsec)
{
    return (uint64_t)(nsec / (time_resolution_sec * 1000000000.0));
}

float timetag_units_to_ns(uint64_t tt)
{
    return (float)((double)tt * (time_resolution_sec * 1000000000.0));
}

#ifdef PLATFORM_WINDOWS
static inline double get_time_seconds()
{
	return double(std::clock()) / CLOCKS_PER_SEC;
}
#else
double get_time_ms(void)
{
    static unsigned long long start_ns = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long time_ns = (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    if (!start_ns)
        start_ns = time_ns;
    return ((double)(time_ns - start_ns) * 0.000001);
}
double get_time_seconds(void)
{
    return get_time_ms() * 0.001;
}
#endif

void get_time_string(char* buffer, size_t max_length)
{
#ifdef PLATFORM_WINDOWS
	sprintf(buffer, "(-todo-localtime-win-)");
#else
	time_t rawtime;
	struct tm* timeinfo;

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer, max_length, "%d-%m-%y %H:%M:%S", timeinfo);
#endif
}

void print_time_stamp()
{
    char time_str[256];
    get_time_string(time_str, sizeof(time_str));
    printf("[%s] ", time_str);
}

uint32_t count_one_bits(uint32_t num)
{
    num = (num & 0x55555555) + ((num & 0xaaaaaaaa) >> 1);
    num = (num & 0x33333333) + ((num & 0xcccccccc) >> 2);
    num = (num & 0x0f0f0f0f) + ((num & 0xf0f0f0f0) >> 4);
    num = (num & 0x00ff00ff) + ((num & 0xff00ff00) >> 8);
    num = (num & 0x0000ffff) + ((num & 0xffff0000) >> 16);
    return num;
}

class ListenerSetupData {
public:
    ListenerSetupData();
    ~ListenerSetupData();

    bool from_string(const std::string& in);
    std::string to_string();
    void request_send_setup();
    void request_send_counts();
    void request_disconnect();
    bool send();
    bool receive(const std::string& in);
    void clear_all_counts();
    uint32_t count_active_channels() const;
    void log_output_to_file(const char* output);

    int         listener_slot;              // Which network client is associated with this listener
    bool        send_setup_requested;       // If true, then next time through the loop, send the setup
    bool        send_counts_requested;      // If true, then next time through the loop, send the counts
    bool        disconnect_requested;       // If true, then next time through the loop, disconnect
    uint32_t    active_channels;            // Bitfield of active channels
    float       poll_time;                  // How often user wants data send (0 = only on poll)
    float       poll_started_time;
    unsigned long long span_started_time;
    unsigned long long span_ended_time;
    bool        use_fake_data;
    std::string user_name;
    std::string user_platform;
    std::vector<uint32_t>   single_counts;
    std::vector<int>        coincidence_channels;
    std::vector<float>      coincidence_windows_ns;

    float                           histogram_windows_ns;
    uint32_t                        histogram_channels;
    std::vector<std::vector<int> >  histogram_counts;

    uint32_t                        slide_co_all_mask;
    uint32_t                        slide_co_move_mask;
    uint32_t                        slide_co_width;
    std::vector<uint64_t>           slide_co_counts;

    std::vector<uint32_t>    coincidence_counts;    // output
    FILE* data_log_file;
};

ListenerSetupData::ListenerSetupData()
{
    listener_slot = -1;
    send_setup_requested = true;
    send_counts_requested = true;
    disconnect_requested = false;
    active_channels = 0;
    use_fake_data = false;
    user_name = "(unknown user)";
    user_platform = "(unknown platform)";

    histogram_windows_ns = 50;
    histogram_channels = 0;
    data_log_file = NULL;

    slide_co_all_mask = 0;
    slide_co_move_mask = 0;
    slide_co_width = 0;

    poll_time = 1.0f;
    poll_started_time = 0;
    span_started_time = 0;
    span_ended_time = 0;

    single_counts.resize(NUM_CHANNELS);
    for (int i = 0; i < NUM_CHANNELS; ++i)
        single_counts[i] = 0;
}

ListenerSetupData::~ListenerSetupData()
{
    listener_slot = -1;
    if (data_log_file)
    {
        // Terminate the JSON object
        fwrite("{}]\n", 4, 1, data_log_file);
        fflush(data_log_file);
        fclose(data_log_file);
        data_log_file = NULL;
    }
}

const char* restart_log_filename = "./cqp_restart_log.txt";
void write_to_restart_log(const char* message)
{
    // This is a low-traffic log which is never cleared.
    FILE* fp = fopen(restart_log_filename, "a");
    if (fp)
    {
        // Time stamp
        char time_str[256];
        get_time_string(time_str, sizeof(time_str));
        fprintf(fp, "[%s] ", time_str);

        fprintf(fp, "%s\n", message);
        fclose(fp);
    }
}

void ListenerSetupData::log_output_to_file(const char* output)
{
    // Don't write data for very rapid polling, or it'll fill up the drive.
    if (poll_time > 0 && poll_time < 0.99)
        return;
    if (data_log_file == NULL)
    {
        char user_dir_name_fix[MAX_PATH];
        sprintf(user_dir_name_fix, "cqp_log_%s", user_name.c_str());
        for (char* fix = user_dir_name_fix; *fix; ++fix)
        {
            char ch = *fix;
            if (!(isalnum(ch) || ch == '-' || ch == '_'))
                *fix = '_';
        }

        char file_name_fix[MAX_PATH];
        sprintf(file_name_fix, "cqp_log_%s_%s_%s", user_name.c_str(), user_platform.c_str(), local_time_str);
        for (char* fix = file_name_fix; *fix; ++fix)
        {
            char ch = *fix;
            if (!(isalnum(ch) || ch == '-' || ch == '_'))
                *fix = '_';
        }

        char full_path[MAX_PATH];
#ifdef PLATFORM_WINDOWS
        sprintf(full_path, "%s", log_directory);
#else
        sprintf(full_path, "%s/%s", log_directory, user_dir_name_fix);
        // Create a directory for the logs
        char cmdline[4096];
        sprintf(cmdline, "mkdir -p %s", full_path);
        system(cmdline);

        // Delete any files more than a week old.
        // Note we're being VERY VERY careful to restrict this delete to the
        // exact log files we're looking for.
        int delete_after_days = 8;
        sprintf(cmdline, "find %s -maxdepth 1 -type f -mtime +%d -name cqp_log_*.txt -delete", full_path, delete_after_days);
        system(cmdline);
#endif

        char full_path_name[MAX_PATH];
        sprintf(full_path_name, "%s/%s.txt", full_path, file_name_fix);
        data_log_file = fopen(full_path_name, "wb");
        if (data_log_file)
            fwrite("[\n", 2, 1, data_log_file);
    }
    if (data_log_file)
    {
        fwrite(output, strlen(output), 1, data_log_file);
        fwrite(",\n", 2, 1, data_log_file);
        fflush(data_log_file);
    }
}

std::string ListenerSetupData::to_string()
{
    MiniParser out;
    out.begin();
    out.append("type", "setup");
    out.append("waterloo_data_mode", waterloo_data_mode_names[current_waterloo_data_mode]);
    out.append("user_name", user_name);
    out.append("user_platform", user_platform);
    out.append("poll_time", poll_time);
    out.append("tick_resolution", time_resolution_sec);
    out.append("active_channels", active_channels);
    out.append("input_threshold_volts", current_threshold_volts);
    out.append("channel_delay_ns", current_channel_delay_ns);
    out.append("coincidence_channels", coincidence_channels);
    out.append("coincidence_windows_ns", coincidence_windows_ns);
    out.append("histogram_channels", histogram_channels);
    out.append("histogram_windows_ns", histogram_windows_ns);
    out.append("slide_co_all_mask", slide_co_all_mask);
    out.append("slide_co_move_mask", slide_co_move_mask);
    out.append("slide_co_width", slide_co_width);
    out.append("user_locked_channels", user_locked_channels);
    out.append("single_filter_channels", single_filter_channels);
    out.append("edge_inversion_channels", edge_inversion_channels);
    out.append("single_filter_minimum_hits", single_filter_minimum_hits);
    out.append("single_filter_window_ns", single_filter_window_ns);

    std::vector<const char*> connected_users(current_listeners.size());
    std::vector<const char*> connected_platforms(current_listeners.size());
    std::vector<uint32_t> connected_active_channels(current_listeners.size());

    for (size_t index = 0; index < current_listeners.size(); ++index)
    {
        ListenerSetupData* listener = current_listeners[index];
        connected_users[index] = listener->user_name.c_str();
        connected_platforms[index] = listener->user_platform.c_str();
        connected_active_channels[index] = listener->active_channels;
    }
    out.append("connected_users", connected_users);
    out.append("connected_platforms", connected_platforms);
    out.append("connected_active_channels", connected_active_channels);
    out.terminate();
    return out.std_str();
}

bool ListenerSetupData::from_string(const std::string& in)
{
    MiniParser parser(in.c_str());
    std::string type;
    parser.get("type", type);
    if (type == "setup")
    {
        parser.get("user_name", user_name);
        parser.get("user_platform", user_platform);
        parser.get("poll_time", poll_time);
        parser.get("active_channels", active_channels);
        parser.get("coincidence_channels", coincidence_channels);
        parser.get("coincidence_windows_ns", coincidence_windows_ns);
        parser.get("histogram_channels", histogram_channels);
        parser.get("histogram_windows_ns", histogram_windows_ns);
        parser.get("slide_co_all_mask", slide_co_all_mask);
        parser.get("slide_co_move_mask", slide_co_move_mask);
        parser.get("slide_co_width", slide_co_width);
    }
    return true;
}

uint32_t ListenerSetupData::count_active_channels() const
{
    return count_one_bits(active_channels);
}

void ListenerSetupData::request_send_setup()
{
    send_setup_requested = true;
}

void ListenerSetupData::request_send_counts()
{
    send_counts_requested = true;
}

void ListenerSetupData::request_disconnect()
{
    disconnect_requested = true;
}

void apply_setup_globals(const char* setup_str, uint32_t channel_mask = 0xffffffff)
{
    // Setup based on the requests
    MiniParser parser(setup_str);
    std::string type;
    std::string waterloo_data_mode;
    parser.get("type", type);
    if (type != "setup")
        return;

#if 0
    parser.get("waterloo_data_mode", waterloo_data_mode);

    WaterlooDataMode old_data_mode = current_waterloo_data_mode;
    if (waterloo_data_mode == waterloo_data_mode_names[DATA_MODE_TIMETAG])
        current_waterloo_data_mode = DATA_MODE_TIMETAG;
    else if (waterloo_data_mode == waterloo_data_mode_names[DATA_MODE_LOGIC])
        current_waterloo_data_mode = DATA_MODE_LOGIC;
    else if (waterloo_data_mode == waterloo_data_mode_names[DATA_MODE_NONE])
        current_waterloo_data_mode = DATA_MODE_NONE;
    if (old_data_mode != current_waterloo_data_mode)
    {
        // We need to switch data modes
        if (timetag)
        {
            printf("Switching Waterloo data mode from '%s' to '%s'\n",
                waterloo_data_mode_names[old_data_mode],
                waterloo_data_mode_names[current_waterloo_data_mode]);
            if (old_data_mode == DATA_MODE_TIMETAG)
                timetag->StopTimetags();

            if (current_waterloo_data_mode == DATA_MODE_TIMETAG)
                timetag->StartTimetags();
        }
    }
#endif

    std::vector<float>  input_threshold_volts;
    std::vector<float>  channel_delay_ns;
    std::vector<std::string> requested_user_locked_channels(NUM_CHANNELS);
    uint32_t requested_single_filter_channels = 0;
    uint32_t requested_edge_inversion_channels = 0;
    parser.get("input_threshold_volts", input_threshold_volts);
    parser.get("channel_delay_ns", channel_delay_ns);
    parser.get("user_locked_channels", requested_user_locked_channels);
    parser.get("single_filter_channels", requested_single_filter_channels);
    parser.get("edge_inversion_channels", requested_edge_inversion_channels);

    for (uint32_t chan = 0; chan < input_threshold_volts.size(); ++chan)
    {
        uint32_t mask = 1 << chan;
        if (mask & channel_mask)
        {
            current_threshold_volts[chan] = input_threshold_volts[chan];
#ifndef DISABLE_WATERLOO_CALLS
			if (timetag)
            {
                timetag->SetInputThreshold(chan + 1, current_threshold_volts[chan]);
                print_time_stamp(); printf("chan %u thresh %.3fv\n", chan + 1, current_threshold_volts[chan]);
            }
#endif
		}
    }
    for (uint32_t chan = 0; chan < channel_delay_ns.size(); ++chan)
    {
        uint32_t mask = 1 << chan;
        if (mask & channel_mask)
        {
            float delay_ns = channel_delay_ns[chan];
            // Clamp it to a valid value
            if (delay_ns < 0)
                delay_ns = 0;
            int delay_int = (int)nsec_to_timetag_units(delay_ns);
            int max_value = ((1 << 19) - 1);
            if (delay_int > max_value)
                delay_int = max_value;
            // Convert back, so we can reflect the actual resolution
            delay_ns = timetag_units_to_ns(delay_int);
            current_channel_delay_ns[chan] = delay_ns;
#ifndef DISABLE_WATERLOO_CALLS
			if (timetag)
            {
                timetag->SetDelay(chan + 1, delay_int);
                print_time_stamp(); printf("chan %u delay %.3f ns\n", chan + 1, delay_ns);
            }
#endif
		}
    }

    // If these differ from the current state, set them, but only if the channel is active fr his user.
    for (size_t i = 0; i < requested_user_locked_channels.size(); ++i)
    {
        if (requested_user_locked_channels[i] != user_locked_channels[i])
        {
            uint32_t mask = 1 << i;
            if (mask & channel_mask)
                user_locked_channels[i] = requested_user_locked_channels[i];
        }
    }
    uint32_t filter_change = (single_filter_channels ^ requested_single_filter_channels) & channel_mask;
    single_filter_channels ^= filter_change;
    uint32_t edge_inversion_change = (edge_inversion_channels ^ requested_edge_inversion_channels) & channel_mask;
    edge_inversion_channels ^= edge_inversion_change;

    // Any channels which are not used at all get single-filtering.
    unused_channel_mask = ~0;
    for (size_t index = 0; index < current_listeners.size(); ++index)
    {
        ListenerSetupData* listener = current_listeners[index];
        unused_channel_mask &= ~listener->active_channels;
    }

#ifndef DISABLE_WATERLOO_CALLS
    if (timetag)
    {
        uint32_t filter_exception = ~(unused_channel_mask | single_filter_channels);
        filter_exception &= 0x0000ffff;

        {
            std::lock_guard<std::mutex> locker(waterloo_access_mutex);
//printf("filter_exception: 0x%x\n", filter_exception);
//printf("single_filter_minimum_hits: %x\n", single_filter_minimum_hits);
//printf("(int)nsec_to_timetag_units(single_filter_window_ns): %x\n", (int)nsec_to_timetag_units(single_filter_window_ns));
//printf("edge_inversion_channels: 0x%x\n", edge_inversion_channels);
// Enable this for testing when the detectors are back online
//        timetag->SetFilterException(filter_exception);
//        timetag->SetFilterMinCount(single_filter_minimum_hits);
//        timetag->SetFilterMaxTime((int)nsec_to_timetag_units(single_filter_window_ns));
            timetag->SetInversionMask(edge_inversion_channels);

            timetag->SetFilterException(0x0000ffff);
            timetag->SetFilterMinCount(1);
            timetag->SetFilterMaxTime(40);
//        timetag->SetInversionMask(2);
        }
    }
#endif

}

const char* setup_file_name = "cqp_setup.txt";
void write_setup_file(const char*out_str)
{
    FILE* fp = fopen(setup_file_name, "w");
    if (fp)
    {
        fprintf(fp, "%s", out_str);
        fclose(fp);
    }
}

void read_setup_file()
{
    try {
        FILE* fp = fopen(setup_file_name, "rb");
        if (fp)
        {
            fseek(fp, 0, SEEK_END);
            size_t len = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char* str = new char[len + 1];
            fread(str, len, 1, fp);
            str[len] = '\0';
            fclose(fp);
            apply_setup_globals(str);
            delete[] str;
        }
    } catch (int) {
        print_time_stamp(); printf("Couldn't read the setup file.\n");
    }
}

bool ListenerSetupData::send()
{
    if (listener_slot < 0)
        return false;
    send_setup_requested = false;
    std::string out_str = to_string();
    websocket_relay_send(listener_slot, out_str.c_str(), strlen(out_str.c_str()) + 1, true);
    write_setup_file(out_str.c_str());
    return true;
}

bool ListenerSetupData::receive(const std::string& in)
{
    if (from_string(in))
    {
        //print_time_stamp(); printf("User connected: %s (%s) -------------\n", user_name.c_str(), user_platform.c_str());
        apply_setup_globals(in.c_str(), active_channels);
        return true;
    }
    return false;
}

void ListenerSetupData::clear_all_counts()
{
    poll_started_time = (float)sample_time;
    span_started_time = 0;
    span_ended_time = 0;
    for (int i = 0; i < NUM_CHANNELS; ++i)
        single_counts[i] = 0;
    for (uint32_t channel = 0; channel < histogram_counts.size(); ++channel)
        for (uint32_t i = 0; i < histogram_counts[channel].size(); ++i)
            histogram_counts[channel][i] = 0;
    for (size_t i = 0; i < coincidence_counts.size(); ++i)
        coincidence_counts[i] = 0;
    for (uint32_t slide = 0; slide < slide_co_counts.size(); ++slide)
        slide_co_counts[slide] = 0;
}

ListenerSetupData* new_listener_data()
{
    return new ListenerSetupData();
}

void delete_listener_data(ListenerSetupData* listener)
{
    delete listener;
}

void request_send_setup_to_all_listeners()
{
    for (size_t index = 0; index < current_listeners.size(); index++)
    {
        ListenerSetupData* listener = current_listeners[index];
        listener->request_send_setup();
    }
}

void handle_ascii_receive(const char *data, size_t length, thread_local_data* tld)
{
    //print_time_stamp(); printf("Received ASCII %d bytes: '%s'\n", (int)length, data);

    if (strstr("setup", data))
        tld->listener->request_send_setup();
    else if (strstr("counts", data))
        tld->listener->request_send_counts();
	else if (strstr("disconnect", data)) {
		tld->listener->request_disconnect();
		printf("Disconnect requested \n");
	}
	else if (tld->listener->receive(data)) {
		printf("Request send setup \n");
		request_send_setup_to_all_listeners();

	}
        
}

void handle_binary_receive(const unsigned char *data, size_t length, thread_local_data* tld)
{
    //print_time_stamp(); printf("Received BINARY %d bytes\n", (int)length);
    std::vector<char> terminated_ascii(length + 1);
    memcpy(&terminated_ascii[0], data, length);
    terminated_ascii[length] = '\0';
    handle_ascii_receive(&terminated_ascii[0], terminated_ascii.size(), tld);
}


/*
class FakeTimeTag {
public:
    FakeTimeTag() : next_timetag_value(1000), last_read_time_ms(0) {}
    ~FakeTimeTag() {}
    static const uint64_t max_samples_per_second = 50000;
    static const uint64_t min_samples_per_second =  1000;
    static const uint64_t max_delta_t = 5000;
    static const uint64_t min_delta_t =  100;
    static const uint64_t max_channels = 2;

    uint64_t    next_timetag_value;
    double      last_read_time_ms;

    std::vector<unsigned char> chan_data;
    std::vector<long long> time_data;


    int ReadTags(unsigned char*& chan, long long*& time, uint32_t channel_mask)
    {
        if (channel_mask == 0)
            return 0;
        uint32_t channels[NUM_CHANNELS];
        uint32_t num_channels = 0;
        for (uint32_t i = 0; (1 << i) <= channel_mask; ++i)
        {
            uint32_t mask = (1 << i);
            if (mask & channel_mask)
                channels[num_channels++] = i;
        }

        double now_time_ms = get_timer_ms();
        if (last_read_time_ms == 0)
            last_read_time_ms = now_time_ms - 1000.0;
        double elapsed_time_seconds = 0.001 * (now_time_ms - last_read_time_ms);
        uint64_t min_samples = (uint64_t)(min_samples_per_second * elapsed_time_seconds);
        uint64_t max_samples = (uint64_t)(max_samples_per_second * elapsed_time_seconds);

        uint64_t num_samples = min_samples + ((uint64_t)rand() % (max_samples - min_samples));
        chan_data.resize(num_samples);
        time_data.resize(num_samples);
        for (size_t i = 0; i < num_samples; ++i)
        {
            next_timetag_value += min_delta_t + ((uint64_t)rand() % (max_delta_t - min_delta_t));
            for (uint32_t ch = 0; ch < num_channels; ++ch)
            {
                if (rand() & 1)
                {
                    time_data[i] = next_timetag_value;
                    chan_data[i] = ch + 1;
                }
            }
        }
        time = &time_data[0];
        chan = &chan_data[0];

        last_read_time_ms = now_time_ms;
        return num_samples;
    }
};

FakeTimeTag fake_timetag;
*/

void disconnect_inactive_listeners()
{
    size_t num_listener_slots = websocket_relay_get_num_listener_slots();
    for (size_t slot = 0; slot < num_listener_slots; ++slot)
    {
        ListenerSetupData* listener = websocket_relay_get_listener((int)slot);
        if (listener)
        {
            if (listener->disconnect_requested)
                websocket_relay_request_disconnect((int)slot);
        }
    }
}

void update_current_listeners()
{
    current_listeners.clear();
    size_t num_listener_slots = websocket_relay_get_num_listener_slots();
    for (size_t slot = 0; slot < num_listener_slots; ++slot)
    {
        ListenerSetupData* listener = websocket_relay_get_listener((int)slot);
        if (listener)
        {
            listener->listener_slot = (int)slot;
            current_listeners.push_back(listener);
        }
    }
}

inline void update_histograms(const uint64_t* channel_times,
                                uint32_t channels_found,
                                uint32_t reference_channel,
                                uint32_t histogram_slots,
                                ListenerSetupData* listener)
{
    // If we saw the reference channel
    if (channels_found & (1 << reference_channel))
    {
        uint32_t half_slots = histogram_slots / 2;
        unsigned long long reference_time = channel_times[reference_channel];
        for (uint32_t channel = 0; (channels_found >> channel) != 0; ++channel)
        {
            if (channels_found & (1 << channel))
            {
                if (channel != reference_channel)
                {
                    unsigned long long t = channel_times[channel];
                    int64_t dt = (int64_t)t - (int64_t)reference_time;
                    dt += half_slots;
                    if (dt >= 0 && dt < histogram_slots)
                        listener->histogram_counts[channel][dt]++;
                }
            }
        }
    }
}

void handle_raw_tags(int count, const std::vector<unsigned char>& chan, const std::vector<long long>& time)
{
    raw_incoming_counts = count;
    bool heartbeat = false;
    if (sample_time > last_heartbeat_time + heartbeat_interval_seconds)
    {
//        print_time_stamp(); printf("heartbeat %.2f\n", sample_time);
        heartbeat = true;
        last_heartbeat_time = sample_time;
    }

    disconnect_inactive_listeners();
//printf("]]] lock %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    std::lock_guard<std::mutex> listener_locker(listener_list_mutex);
//printf("]]] lockok %d:%s\n", __LINE__, __FILE__);fflush(stdout);
    update_current_listeners();

    for (size_t index = 0; index < current_listeners.size(); index++)
    {
        ListenerSetupData* listener = current_listeners[index];
        if (listener->send_setup_requested)
        {
            if (listener->disconnect_requested)
                continue;
            listener->send();
        }
    }

    for (int i = 0; i < NUM_CHANNELS; ++i)
        channel_counts[i] = 0;

    for (int i = 0; i < count; ++i)
    {
        unsigned char channel = chan[i] - 1;
        if (channel < NUM_CHANNELS) // This is importnt because higher numbers are used to flag errors.
            channel_counts[channel]++;
    }

    static std::vector<uint32_t> co_groups;

    // Find requested coincidences
    for (size_t index = 0; index < current_listeners.size(); ++index)
    {
        ListenerSetupData* listener = current_listeners[index];
        if (listener->disconnect_requested)
            continue;

        for (int i = 0; i < NUM_CHANNELS; ++i)
            listener->single_counts[i] += channel_counts[i];

        if (count > 0)
        {
            if (listener->span_started_time == 0)
                listener->span_started_time = time[0];
            listener->span_ended_time = time[count - 1];
        }

        size_t num_cos = listener->coincidence_channels.size();
        if (num_cos > listener->coincidence_windows_ns.size())
            num_cos = listener->coincidence_windows_ns.size();
        if (listener->coincidence_counts.size() != num_cos)
        {
            listener->coincidence_counts.resize(num_cos);
            for (size_t i = 0; i < num_cos; ++i)
                listener->coincidence_counts[i] = 0;
        }

        int num_co_groups_found = 0;
        if (num_cos && count)
        {
            // Each listener can request multiple coincidence counts.
            // For each one requested...
            co_groups.resize(count + 1);
            uint32_t full_listener_mask = 0;
            full_listener_mask |= listener->slide_co_all_mask;
            for (size_t co_index = 0; co_index < num_cos; ++co_index)
                full_listener_mask |= listener->coincidence_channels[co_index];

            int window_index = 0;
            uint64_t time_window = nsec_to_timetag_units(listener->coincidence_windows_ns[window_index]);

            uint64_t window_start = time[0];
            uint32_t channels_found = 0;
            // Go through all timetags received...
            for (int index = 0; index < count; ++index)
            {
                unsigned char channel = chan[index] - 1;
                uint32_t channel_mask = 1 << (uint32_t)channel;
                // If the channel for this tag is one of the coincicence channels...
                if (full_listener_mask & channel_mask)
                {
                    uint64_t t = time[index];
                    // If our window has expired, forget what we've seen and reset the window.
                    if (t > window_start + time_window)
                    {
                        window_start = t;
                        co_groups[num_co_groups_found++] = channels_found;
                        channels_found = 0;
                    }
                    // Note which channel we see in this tag.
                    channels_found |= channel_mask;
                }
            }
            if (channels_found)
                co_groups[num_co_groups_found++] = channels_found;
        }

        // Now we've found all of the coincidence groups, so fill in the counts
        for (size_t co_index = 0; co_index < num_cos; ++co_index)
        {
            uint32_t listener_mask = listener->coincidence_channels[co_index];
            if (listener_mask)
            {
                for (int group = 0; group < num_co_groups_found; ++group)
                {
                    uint32_t hits = co_groups[group];
                    if ((hits & listener_mask) == listener_mask)
                        listener->coincidence_counts[co_index]++;
                }
            }
        }

        // Now, if we have sliding coincidences to do, slide them
        if (listener->slide_co_all_mask == 0)
        {
            listener->slide_co_counts.clear();
        }
        else
        {
            uint32_t all_mask = listener->slide_co_all_mask;
            uint32_t move_mask = listener->slide_co_move_mask;
            int32_t w = (int32_t)listener->slide_co_width;
            uint32_t slide_count_slots = w * 2 + 1;
            if (listener->slide_co_counts.size() != slide_count_slots)
            {
                listener->slide_co_counts.resize(slide_count_slots);
                for (uint32_t slide = 0; slide < slide_count_slots; ++slide)
                    listener->slide_co_counts[slide] = 0;
            }
            for (int group = w; group < num_co_groups_found - (int)w; ++group)
            {
                uint32_t hits0 = co_groups[group + w] & ~move_mask;
                for (uint32_t slide = 0; slide < slide_count_slots; ++slide)
                {
                    uint32_t hits1 = co_groups[group + slide] & move_mask;
                    uint32_t hits = (hits0 | hits1) & all_mask;
                    if (hits == all_mask)
                        listener->slide_co_counts[slide]++;
                }
            }
        }

        if (!listener->histogram_channels)
        {
            listener->histogram_counts.clear();
        }
		else
		{
			uint64_t time_window = nsec_to_timetag_units(listener->histogram_windows_ns);
			uint64_t histogram_slots = time_window;

			// Each listener can request a histogram.
			uint32_t histogram_channel_mask = listener->histogram_channels;
			uint32_t reference_channel = 0;
			//while (!(histogram_channel_mask & (1 << reference_channel)))
			//    reference_channel++;

			//listener->histogram_counts.resize(NUM_CHANNELS);
			//for (uint32_t channel = 0; channel < NUM_CHANNELS; ++channel)
			//{
			//    // Make storage space for JUST the channels which will have results.
			//    if ((histogram_channel_mask & (1 << channel))
			//        && channel != reference_channel)
			//    {
			//        if (listener->histogram_counts[channel].size() != time_window)
			//        {
			//            listener->histogram_counts[channel].resize(time_window);
			//            for (uint32_t i = 0; i < histogram_slots; ++i)
			//                listener->histogram_counts[channel][i] = 0;
			//        }
			//    }
			//    else
			//    {
			//        listener->histogram_counts[channel].clear();
			//    }
			//}

			if (startflag == 0) {

			uint32_t histogram_channels = listener->histogram_channels;

			if (listener->user_platform.compare("scanner") == 0) {

				if (histogram_channels < 400 && histogram_channels > 5) {
					px_resolution_x = histogram_channels;
					px_resolution_y = histogram_channels;

				}
			}


			if (listener->user_platform.compare("stabilise") == 0) {
		
				if (histogram_channels < 400 && histogram_channels > 1) {
					px_resolution_x = histogram_channels;
					px_resolution_y = 3;

				}
			}

			listener->histogram_counts.resize(px_resolution_y);

			for (uint32_t index = 0; index < px_resolution_y; index++) {
				listener->histogram_counts[index].resize(px_resolution_x
				);
				for (uint32_t i = 0; i < px_resolution_x; i++)
					listener->histogram_counts[index][i] = 0;

			}


			}

            // Now go through and find our groups of clicks
            if (count)
            {
               
                for (int index = 0; index < count; index++)
                {
					if (startflag == 0 && chan[index] == 15) {
						// Start scan
						printf("start scan \n");
						startflag = 1;
						counter = 0;
						px = 0;
						sentlines = 0;

					}


					if (startflag == 1 && chan[index] == 16) {
						// Start counting photons, gone low
						startflag = 2;
						start = time[index];
						countme = 0;

					}


					if (startflag == 2 && chan[index] == 15) {

						// Channel 15 indicates the pulse train has gone high. This means stop counting photons! Find the duration and get ready to move to the next line in the histogram
						startflag = 1;

						// Calculate the duration of this count
						duration = time[index] - start;

						if (duration > 1000) {
							// prevent glitch if duration ns

							if (counter < px_resolution_y) {
								listener->histogram_counts[counter][px] = (int)((6400000000 / duration)*1000); //  (int)((6400000000 / duration)*countme);
								//printf("line %i ", counter);
								//printf("px %i ", px);
								//printf("counts %i ", countme);
								printf("%i ", duration);
								//printf("duration %i \n", duration);
								countme = 0;
							}


							// New line if at end of line in either direction
							if (px == (px_resolution_x - 1)) {

								counter++;

								px = 0;

								printf("] \n");
								printf("line %i \n", counter);
								printf("sent %i \n", sentlines);
								printf("[");

							}
							else px++;
						}

					}




					if ((chan[index] == 1) && (startflag == 2)) {
						countme++;
					}
                }
            }
        }
    }



    // Send the output to this listener
    {
	bool scannerconnected = 0;
		for (size_t index = 0; index < current_listeners.size(); ++index)
        {
            ListenerSetupData* listener = current_listeners[index];
            if (!listener)
                continue;
            if (listener->disconnect_requested)
                continue;
            if (listener->listener_slot < 0)
                continue;
            if (listener->poll_started_time == 0)
                listener->poll_started_time = (float)last_sample_time;

            float elapsed_time = (float)(sample_time - listener->poll_started_time);


			if (listener->user_platform.compare("scanner") == 0 || listener->user_platform.compare("stabilise") == 0)
				scannerconnected = 1;

            bool do_send = false;
            if (listener->listener_slot >= 0)
            {
                if (listener->poll_time > 0 && elapsed_time >= listener->poll_time)
                    do_send = true;
               // else if (listener->user_platform.compare("scanner") != 0 && listener->poll_time == 0 && listener->send_counts_requested)
               //     do_send = true;
				else if ((listener->user_platform.compare("scanner") == 0) && listener->send_counts_requested && (counter > sentlines))
					do_send = true;
				else if ((listener->user_platform.compare("stabilise") == 0) && (listener->send_counts_requested) && (counter > 2) ) { // taking away listener counts requested as never flags
					do_send = true;
					startflag = 0;
				}
            }

            if (do_send)
            {
                float delta_time = (float)(sample_time - listener->poll_started_time);
                double span_time = (listener->span_ended_time - listener->span_started_time) * time_resolution_sec;

                MiniParser out;
                out.begin();
                out.append("type", "counts");
                out.append("error_id", current_error_number);
                if (current_error_string.size())
                    out.append("error_str", current_error_string);
                out.append("waterloo_data_mode", waterloo_data_mode_names[current_waterloo_data_mode]);
                out.append("time", (float)sample_time);
                out.append("delta_time", (float)delta_time);
                out.append("span_time", (float)span_time);
                out.append("local_time", local_time_str);
                out.append("raw_counts", raw_incoming_counts);
                out.append("slide_co_counts", listener->slide_co_counts);
                out.append("coincidence", listener->coincidence_counts);
				if ((listener->user_platform.compare("scanner") == 0) && size(listener->histogram_counts) > sentlines)
                out.append("histogram_counts", listener->histogram_counts[sentlines]);
				else out.append("histogram_counts", listener->histogram_counts);
                out.append("counts", listener->single_counts);
                out.terminate();

//printf("at line %d slot = %d\n", __LINE__, listener->listener_slot);
//printf("sending: '%s'\n", out.c_str());
                websocket_relay_send(listener->listener_slot, out.c_str(), out.length() + 1, true);
                listener->log_output_to_file(out.c_str());

                listener->send_counts_requested = false;
                // Clear the values for next time
			
					if (listener->user_platform.compare("scanner") == 0) {
						sentlines++;

						if (sentlines > (listener->histogram_channels - 1)) {
							startflag = 0;
							listener->clear_all_counts();
							sentlines = 0;
							counter = 0;
							printf("Sent everything, ready for new stuff \n");
							//listener->request_disconnect();

						}

					}
					else listener->clear_all_counts();
            }
        }
		
		if (scannerconnected == 0) { 
			sentlines = 0;
			startflag = 0;
		}

    }
//printf("at line %d\n", __LINE__);                

    if (0)
    {
        uint64_t total_raw_bytes = count * (sizeof(chan[0]) + sizeof(time[0]));
		//print_time_stamp(); printf("Received %d tags, raw size = %lu kiB. ", count, (unsigned long)(total_raw_bytes / 1024));
        if (last_sample_time)
        {
            double elapsed_time = sample_time - last_sample_time;
            double kb_per_sec = (total_raw_bytes / 1024.0) / elapsed_time;
            printf("   Data rate: %.1f kiB / second. ", kb_per_sec);
        }
        printf("\n");
    }
}


bool check_error()
{
#ifndef DISABLE_WATERLOO_CALLS
    int err = 0;
    try {
        int err = timetag->ReadErrorFlags();
    }
    catch (TimeTag::Exception ex)
    {
        print_time_stamp(); printf ("Exception: %s\n", ex.GetMessageText().c_str());
    }
    
    if (err)
    {
        current_error_time = get_time_seconds();
        current_error_number = err;
        current_error_string = timetag->GetErrorText(err);
        print_time_stamp(); printf(">>>>>> Device reported error %d: %s\n", err, current_error_string.c_str());
    }
    else
    {
        // Clear the old error
        double error_linger_time = 5.0;
        double now_time = get_time_seconds();
        if (now_time > current_error_time + error_linger_time)
        {
            current_error_number = 0;
            current_error_string.clear();
        }
    }
    return err != 0;
#else
    return true;
#endif
}

void* waterloo_data_read_thread(void *arg)
{
    printf("Starting Waterloo access thread...\n");
    unsigned char *chan = NULL;
    long long *time = NULL;
    uint32_t sleep_milliseconds = 10;



    while (timetag)
    {
        int count = 0;
        // Wait for data from the box
        {
            std::lock_guard<std::mutex> locker(waterloo_access_mutex);

#ifndef DISABLE_WATERLOO_CALLS
            try {
    //            if (timetag->TagsPresent())  // This call is in the docs, but doesn't exist?
                    count = timetag->ReadTags(chan, time);
            }
            catch (TimeTag::Exception ex)
            {
                check_error();
                print_time_stamp(); printf ("Exception: %s\n", ex.GetMessageText().c_str());
            }
            check_error();
#endif
        }
        // Hand over any data we got
        if (count)
        {
            std::lock_guard<std::mutex> locker(waterloo_buffer_collect_mutex);
            WaterlooBuffer* buf = &waterloo_double_buffer[double_buffer_fill_index];
            size_t old_len = buf->chan.size();
            size_t new_len = old_len + count;
            buf->chan.resize(new_len);
            buf->time.resize(new_len);
            memcpy(&buf->chan[old_len], chan, count * sizeof(chan[0]));
            memcpy(&buf->time[old_len], time, count * sizeof(time[0]));
        }
#ifdef PLATFORM_WINDOWS
        Sleep(sleep_milliseconds);
#else
        usleep(sleep_milliseconds * 1000);
#endif
    }
	return NULL;
}

bool start_waterloo_data_read_thread()
{
#ifdef PLATFORM_WINDOWS
    static std::thread pth(waterloo_data_read_thread, (void*)NULL);
    return true;
#else
    static pthread_t pth;
    if (pthread_create(&pth, NULL, waterloo_data_read_thread, NULL) != 0)
    {
        fprintf(stderr, "pthread_create() error at %s:%d\n", __FILE__, __LINE__);
        return false;
    }
    return true;
#endif
}

const WaterlooBuffer* get_next_waterloo_buffer()
{
    std::lock_guard<std::mutex> locker(waterloo_buffer_collect_mutex);
    WaterlooBuffer* buf = &waterloo_double_buffer[double_buffer_fill_index];
    double_buffer_fill_index = (double_buffer_fill_index + 1) & 1;
    WaterlooBuffer* new_fill_buf = &waterloo_double_buffer[double_buffer_fill_index];
    new_fill_buf->chan.clear();
    new_fill_buf->time.clear();

    size_t new_length = buf->chan.size();
    static size_t max_length = 0;
    if (new_length > max_length)
    {
        max_length = new_length;
        printf("New max buffer length: %u samples, %uk bytes\n",
                (unsigned int)new_length, (unsigned int)((new_length * 9) / 1024));
    }
    return buf;
}

//#define DONT_CONNECT_USB
//#define DONT_CONNECT_SOCKET

void capture_loop()
{
    unsigned char *chan = NULL;
    long long *time = NULL;
    uint32_t sleep_milliseconds = 10;

    write_to_restart_log("Startup");
#if defined(DONT_CONNECT_USB) || defined(DISABLE_WATERLOO_CALLS)
    print_time_stamp(); printf("///////////// USB DISABLED in %s line %d /////////\n", __FILE__, __LINE__);
#else
	try {
        timetag = new CTimeTag();
		timetag->Open(1);
        check_error();
        time_resolution_sec = timetag->GetResolution();
        print_time_stamp(); printf("\nTime resolution: %lg seconds\n", time_resolution_sec);
        timetag->StartTimetags();
        check_error();
    }
    catch (TimeTag::Exception ex)
    {
        delete timetag;
        timetag = NULL;
        print_time_stamp(); printf("ERROR: -----> Failed to connect to the Waterloo box.\n");
    }
#endif

    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        current_threshold_volts[i] = 2.0f;
        current_channel_delay_ns[i] = 0.0f;
    }
    read_setup_file();

    if (timetag)
    {
        print_time_stamp(); printf(">>> All systems go! \n");
        write_to_restart_log("Connected to Waterloo box ok.");
        start_waterloo_data_read_thread();
    }
    else
    {
        print_time_stamp(); printf(">>> It looks like the time tagger is not connected. \n");
        write_to_restart_log("Failed to connect to the waterloo box.");
    }

    fflush(stdout);
    while (1)
    {
        last_sample_time = sample_time;
        sample_time = get_time_seconds();
        get_time_string(local_time_str, sizeof(local_time_str));

#if 1
        const WaterlooBuffer* buf = get_next_waterloo_buffer();
	    handle_raw_tags((int)buf->chan.size(), buf->chan, buf->time);
#else
		if (timetag)
        {
            int count = 0;
#ifndef DISABLE_WATERLOO_CALLS
			try {
//                if (current_waterloo_data_mode == DATA_MODE_TIMETAG)
//                printf("call read...\n");
                    count = timetag->ReadTags(chan, time);
            }
            catch (TimeTag::Exception ex)
            {
                check_error();
                print_time_stamp(); printf ("Exception: %s\n", ex.GetMessageText().c_str());
            }
            check_error();
#endif
			handle_raw_tags(count, chan, time);
        }
#endif
        fflush(stdout);
#ifdef PLATFORM_WINDOWS
		Sleep(sleep_milliseconds);
#else
		usleep(sleep_milliseconds * 1000);
#endif
	}
//    if (timetag)
//        timetag->StopTimetags();
}

void process_command_line_args(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        if (!strcmp("-norelay", arg))
        {
            do_data_relay = false;
            print_time_stamp(); printf("  -norelay: Disabling network relay.\n");
        }
        if (!strcmp("-rerun", arg))
        {
            while (1)
            {
                print_time_stamp(); printf("========== Server starting =============");
                write_to_restart_log("========== Server starting =============");

                system(argv[0]);

                print_time_stamp(); printf("========== Server stopped (possibly a crash) =============");
                write_to_restart_log("========== Server stopped (possibly a crash) =============");
            }
        }
        else
        {
            fprintf(stderr, "ERROR: Unknown argument '%s'\n", arg);
        }
    }
}

#ifndef PLATFORM_WINDOWS

void crash_handler(int sig)
{
    // We're crashing in malloc, which makes this hang.
    return;

  void *array[50];
  size_t size;
  char line[1024];
  char cmd[1024];

  // get void*'s for all entries on the stack
  size = backtrace(array, 50);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
//  backtrace_symbols_fd(array, size, STDERR_FILENO);
  char** result_strs = backtrace_symbols(array, size);
  print_time_stamp(); printf("Call stack: -------------------------\n");
//  write_to_restart_log("Call stack: -------------------------\n");
  for (int i = 0; i < size; ++i)
  {
    sprintf(line, "  #%d %s\n", i, result_strs[i]);
    printf("%s", line);
//    write_to_restart_log(line);
    sprintf(cmd,"addr2line %p -e run_cqp_detectotron >> %s", array[i], restart_log_filename);
    system(cmd);
    sprintf(cmd,"addr2line %p -e run_cqp_detectotron", array[i]);
    system(cmd);
  }
  exit(1);
}

#endif

int main(int argc, char* argv[])
{
#ifndef PLATFORM_WINDOWS
    signal(SIGSEGV, crash_handler);
#endif
    // Initialize the random generator
    srand((unsigned int)time(NULL));
//print_time_stamp(); printf("about to crash...\n");
//fflush(stdout);
//*(int*)5 = 6;
    process_command_line_args(argc, argv);

    if (do_data_relay)
        websocket_relay_start();

    print_time_stamp(); printf ("Opening timetag device...\n");
    
    while (1)
    {
        current_error_string.clear();
        try
        {
            capture_loop();
        }
        catch (TimeTag::Exception ex)
        {
#ifndef DISABLE_WATERLOO_CALLS
            check_error();
            print_time_stamp(); printf ("Exception: %s\n", ex.GetMessageText().c_str());
#endif
		}
    }
    return 0;
}

