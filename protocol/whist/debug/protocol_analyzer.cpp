/*
============================
Includes
============================
*/

#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>

#include <assert.h>
extern "C" {
#include "protocol_analyzer.h"
#include <whist/network/network.h>
#include <whist/network/udp.h>
#include <whist/utils/threads.h>
#include <whist/network/ringbuffer.h>
#include <whist/fec/fec_controller.h>
};

/*
============================
Defines
============================
*/

using namespace std;

#ifndef NDEBUG
// make sure protocol analyzer is only enabled for DEBUG build
// with this macro, we remove codes at compile, to avoid competitors enable the function in a hacky
// way and get too much info of our product
#define USE_PROTOCOL_ANALYZER
#endif

// a helper macro for wrapping C++ into C
#ifdef USE_PROTOCOL_ANALYZER
#define FUNC_WRAPPER(func, ...)                  \
    do {                                         \
        if (!g_analyzer) return;                 \
        whist_lock_mutex(g_analyzer->m_mutex);   \
        g_analyzer->func(__VA_ARGS__);           \
        whist_unlock_mutex(g_analyzer->m_mutex); \
    } while (0);
#else
#define FUNC_WRAPPER(func, ...)
#endif

// the internal timestamp used by protocol_analyzer, it's a signed value
typedef int64_t Timestamp;

// the analyzer only maintains this many records, stale ones will be kicked out
static const int max_maintained_records = 100000;

/*
struct TimedID
{
    int id;
    Timestamp time;
};*/

// segment level info
struct SegmentLevelInfo {
    vector<Timestamp> time_us;  // the arrival time of segment, it's a vector since a segment can
                                // arrive multiple times
    vector<Timestamp> retrans_time_us;

    // the time we send a nack to server for this segment, also a vector
    vector<Timestamp> nack_time_us;

    int size;  // the size of segment
};

// info related to congest control
struct CCInfo {
    int bitrate = -1;
    int incoming_bitrate = -1;
    double packet_loss = -1;
    double latency = -1;
};

// info related to FEC
typedef struct {
    // base fec depending on packet loss measuring
    double base_fec_ratio;
    // extra fec for protect bandwitdh probing
    double extra_fec_ratio;
    // the orignal total_fec_ratio calculated by base_fec_ratio and extra_fec_ratio without adjust
    double total_fec_ratio_original;
    // the final total_fec_ratio, this is the value actually used for fec. All values above are
    // just for easier debuging
    double total_fec_ratio;
} FECInfo;

// FrameLevelInfo
struct FrameLevelInfo {
    int id = -1;                     // id of frame
    int type = -1;                   // whether it's audio or video
    int max_segment_size = -1;       // size of max segment of frame
    int min_segment_size = 9999;     // size of min segment of frame
    Timestamp ready_time = -1;       // the time when the frame becomes ready
    Timestamp decode_time = -1;      // the time then the frame is feeded into decoder
    Timestamp first_seen_time = -1;  // the first time we see the packet
    int frame_type = -1;             // the type of frame, e.g. iframe, only used for video
    int is_empty = -1;               // whehter empty frame or not, only used for audio
    int num_of_packets = -1;         // num of packets including fec
    int num_of_fec_packets = 0;      // num of fec packets
    int skip_to = -1;                // we got a frame skip from the current frame to this frame
    int reset_ringbuffer_to = -1;  // we got a ringbutter reset from the current frame to this from

    // we got a ringbuffer reset from this frame to the current frame
    int reset_ringbuffer_from = -1;

    // vector<TimedID> reset_ringbuffer_vec;

    int nack_cnt = 0;    // how many received segments comes with is_nack flag, count duplicate
    bool nack_used = 0;  // nack is used for recovering the frame

    // fec is used for recovering the frame. fec_used and nack_used can be both true
    bool fec_used = 0;
    bool fec_used_after_nack = 0;  // fec is used after nack

    int num_received = 0;  // number of segments arrived in the frames, without counting duplicate

    // number of segments arrived without the is_nack flag, without counting duplicate
    int num_received_nonack = 0;
    // int num_fec_received = 0;  // number of fec segments arrived, without counting duplicate

    // the time this frame becomes the current_rendering inside ringbuffer
    Timestamp become_current_rending_time = -1;
    // the time this frame become pending (waiting for decoder to take away)
    Timestamp become_pending_time = -1;

    int overwrite_id = -1;  // while this frame becomes the current_rending frame inside
                            // ringbuffer, it overwrites a frame that never becomes pending
    Timestamp stream_reset_time = -1;  // there is a stream reset sending to server, with the
                                       // current frame as greatest_failed_id, at the time

    int queue_full = -1;  // when we try to make the current frame pending, there is a
                          // decoder-queue-full stops us from doing this, only for audio

    map<int, SegmentLevelInfo> segments;  // stores segment level info of the frame

    string to_string(bool more_format);  // turn the info of frame to a json-like format

    FECInfo current_fec_info;  // FEC info when the frame is first seen
    CCInfo current_cc_info;    // congestion control info when the frame is first seen
};

typedef map<int, FrameLevelInfo> FrameMap;

// type level info
struct TypeLevelInfo {
    FrameMap frames;
    int current_rending_id;  // currrent_rendering_id inside running buffer
    int pending_rending_id;  // current pending frame waiting to be take out by the decoder

    // temply stores the 2 info at TypeLevel, and pass through to frame level
    FECInfo current_fec_info;  // current FEC info
    CCInfo current_cc_info;    // current congestion control info
};

// we put a duplicate forward declaration before ProtocolAnalyzer, so that we don't need
// to move a lot of methods implementation out, this keeps code more readable
static Timestamp get_timestamp(void);

// the Protocol Analyzer class
struct ProtocolAnalyzer {
    WhistMutex m_mutex;  // an advisory mutex, that can be use for multithread

    map<int, TypeLevelInfo>
        type_level_infos;  // stores type level info, currently for PACKET_ADUIO and PACKET_VIDEO

    // get the begin and end iterator, for the range of records required
    pair<FrameMap::iterator, FrameMap::iterator> get_range_it(int type, int num_of_records,
                                                              int skip_last);

    ProtocolAnalyzer() { m_mutex = whist_create_mutex(); }
    ~ProtocolAnalyzer() { whist_destroy_mutex(m_mutex); }

    // get high level stats
    string get_stat(int type, int num_of_records, int skip_last);

    // get a list of serialized frame level info
    string get_frames_info(int type, int num_of_records, int skip_last, bool more_format) {
        stringstream ss;
        auto range = get_range_it(type, num_of_records, skip_last);
        for (auto it = range.first; it != range.second; it++) {
            ss << it->second.to_string(more_format) << endl;
            if (more_format) ss << endl;
        }
        return ss.str();
    }

    // kicks old records out so that the total num doesn't exceed MAX_MAINTAINEd_RECORDS
    void clear_old_records(int type) {
        FrameMap &frames = type_level_infos[type].frames;
        while ((int)frames.size() > max_maintained_records) {
            frames.erase(frames.begin());
        }
    }

    // record the arrival of a segment
    void record_segment(WhistSegment *segment0) {
        WhistSegment &segment = *segment0;
        int type = segment.whist_type;
        int id = segment.id;
        int index = segment.index;

        clear_old_records(type);

        bool is_first_segment = true;
        if (type_level_infos[type].frames.find(id) != type_level_infos[type].frames.end()) {
            is_first_segment = false;
        }

        FrameLevelInfo &info = type_level_infos[type].frames[id];
        info.segments[index].size = segment.segment_size;
        info.max_segment_size = max(info.max_segment_size, segment.segment_size);
        info.min_segment_size = min(info.min_segment_size, segment.segment_size);
        if (is_first_segment) {
            info.current_fec_info = type_level_infos[type].current_fec_info;
            info.current_cc_info = type_level_infos[type].current_cc_info;
        }

        if (segment.is_a_nack) {
            info.nack_cnt++;
        }
        if (info.num_of_packets == -1) {
            info.num_of_packets = segment.num_indices;
            info.num_of_fec_packets = segment.num_fec_indices;
        }
        info.type = type;
        info.id = id;

        if (info.segments[index].time_us.size() == 0 &&
            info.segments[index].retrans_time_us.size() == 0) {
            info.num_received++;
        }

        if ((int)info.segments[index].time_us.size() == 0 && !segment.is_a_nack) {
            info.num_received_nonack++;
        }

        auto time_stamp = get_timestamp();
        if (!segment.is_a_nack) {
            info.segments[index].time_us.push_back(time_stamp);
        } else {
            info.segments[index].retrans_time_us.push_back(time_stamp);
        }

        if (info.first_seen_time == -1) info.first_seen_time = time_stamp;
    }

    void record_fec_used(int type, int id) {
        FrameLevelInfo &info = type_level_infos[type].frames[id];
        info.fec_used = true;
        if (info.nack_cnt > 0) {
            info.fec_used_after_nack = true;
        }
    }

    void record_ready_to_render(int type, int id, char *frame_buffer) {
        if (frame_buffer == NULL) return;
        FrameLevelInfo &info = type_level_infos[type].frames[id];

        // this has to be comment out, since on extreme case a frame can become ready multiple times
        // bc of ringbuffer reset

        // assert(info.ready_time==-1);

        if (info.ready_time != -1) return;

        info.ready_time = get_timestamp();
        if (info.nack_cnt > 0) info.nack_used = 1;

        WhistPacket *whist_packet = (WhistPacket *)frame_buffer;
        if (type == PACKET_VIDEO) {
            VideoFrame *frame = (VideoFrame *)whist_packet->data;
            info.frame_type = frame->frame_type;
            info.is_empty = frame->is_empty_frame;
        } else if (type == PACKET_AUDIO) {
            // AudioFrame *frame = (AudioFrame *)whist_packet->data;
            // add code to track more info of audio here
        }
    }

    void record_nack(int type, int id, int index) {
        FrameLevelInfo &info = type_level_infos[type].frames[id];
        info.segments[index].nack_time_us.push_back(get_timestamp());
    }

    void record_skip(int type, int from_id, int to_id) {
        FrameLevelInfo &info = type_level_infos[type].frames[from_id];
        if (to_id == from_id + 1)
            return;  // this shouldn't count as a skip, since no frame is dropped
        assert(info.skip_to == -1);
        info.skip_to = to_id;
    }

    void record_decode_inner(int type) {
        int id = type_level_infos[type].pending_rending_id;
        assert(type_level_infos[type].frames.find(id) != type_level_infos[type].frames.end());
        FrameLevelInfo &info = type_level_infos[type].frames[id];
        assert(info.decode_time == -1);
        info.decode_time = get_timestamp();
    }
    void record_decode_video() {
        int type = PACKET_VIDEO;
        record_decode_inner(type);
    }

    void record_decode_audio() {
        int type = PACKET_AUDIO;
        record_decode_inner(type);
    }

    void record_stream_reset(int type, int id) {
        FrameLevelInfo &info = type_level_infos[type].frames[id];
        if (info.stream_reset_time == -1) info.stream_reset_time = get_timestamp();
    }

    void record_current_rendering(int type, int id, int reset_id) {
        type_level_infos[type].current_rending_id = id;
        FrameLevelInfo &info = type_level_infos[type].frames[id];
        info.become_current_rending_time = get_timestamp();

        if (reset_id != -1) {
            if (type_level_infos[type].frames.find(reset_id) !=
                type_level_infos[type].frames.end()) {
                if (type_level_infos[type].frames[reset_id].become_pending_time == -1) {
                    info.overwrite_id = reset_id;  // record the overwrite, if the previous fame
                                                   // never became pending
                }
            }
        }
    }

    void record_pending_rendering(int type) {
        int id = type_level_infos[type].current_rending_id;
        type_level_infos[type].pending_rending_id = id;
        FrameLevelInfo &info = type_level_infos[type].frames[id];
        info.become_pending_time = get_timestamp();
    }

    void record_audio_queue_full() {
        int type = PACKET_AUDIO;
        int id = type_level_infos[type].current_rending_id;
        FrameLevelInfo &info = type_level_infos[type].frames[id];
        info.queue_full = get_timestamp();
    }

    void record_reset_ringbuffer(int type, int from_id, int to_id) {
        FrameLevelInfo &info1 = type_level_infos[type].frames[from_id];
        FrameLevelInfo &info2 = type_level_infos[type].frames[to_id];
        info1.reset_ringbuffer_to = to_id;
        info2.reset_ringbuffer_from = from_id;
    }

    void record_current_fec_info(int type, double base_fec_ratio, double extra_fec_ratio,
                                 double total_fec_ratio_original, double total_fec_ratio) {
        type_level_infos[type].current_fec_info.base_fec_ratio = base_fec_ratio;
        type_level_infos[type].current_fec_info.extra_fec_ratio = extra_fec_ratio;
        type_level_infos[type].current_fec_info.total_fec_ratio_original = total_fec_ratio_original;
        type_level_infos[type].current_fec_info.total_fec_ratio = total_fec_ratio;
    }
    void record_current_cc_info(int type, double packet_loss, double latency, int bitrate,
                                int incoming_bitrate) {
        type_level_infos[type].current_cc_info.packet_loss = packet_loss;
        type_level_infos[type].current_cc_info.latency = latency * MS_IN_SECOND;
        type_level_infos[type].current_cc_info.bitrate = bitrate;
        type_level_infos[type].current_cc_info.incoming_bitrate = incoming_bitrate;
    }
};

// generate distribution info of samples
struct DistributionStat {
    const int range_max = 1000;  // max range of value
    vector<int> cnt_array;       // array to cnt of samples at each value
    int exceed_range_cnt = 0;    // numer of samples exceeding range

    DistributionStat() { cnt_array = vector<int>(range_max, 0); }

    // insert the sample to it's slot
    void insert(int value) {
        if (value < range_max) {
            cnt_array[value]++;
        } else {
            exceed_range_cnt++;
        }
    }

    // generate distribution statistic info
    string get_stat() {
        stringstream ss;
        // generate stat for [0,5) [5,10) ... [25,30)
        for (int i = 0; i < 30; i += 5) {
            int cnt = 0;
            for (int j = 0; j < 5; j++) {
                cnt += cnt_array[i + j];
            }
            // use "[" and "]" so that they can be highlight easily by text editor
            ss << "[" << i + 0 << "," << i + 5 << "]~" << cnt << "  ";
        }
        ss << endl;
        // generate stat for [30,40).....[90,100)
        for (int i = 30; i < 100; i += 10) {
            int cnt = 0;
            for (int j = 0; j < 10; j++) {
                cnt += cnt_array[i + j];
            }
            ss << "[" << i + 0 << "," << i + 10 << "]~" << cnt << "  ";
        }
        ss << endl;
        // [100,150).......[350,400)
        for (int i = 100; i < 400; i += 50) {
            int cnt = 0;
            for (int j = 0; j < 50; j++) {
                cnt += cnt_array[i + j];
            }
            ss << "[" << i + 0 << "," << i + 50 << "]~" << cnt << "  ";
        }
        ss << endl;
        // [400,500) .... [900,1000)
        for (int i = 400; i < range_max; i += 100) {
            int cnt = 0;
            for (int j = 0; j < 100; j++) {
                cnt += cnt_array[i + j];
            }
            ss << "[" << i + 0 << "," << i + 100 << "]~" << cnt << "  ";
        }
        ss << endl;
        // [1000,inf)
        ss << "[" << range_max << ",inf]~" << exceed_range_cnt << endl;
        return ss.str();
    }
};
/*
============================
Globals
============================
*/

static ProtocolAnalyzer *g_analyzer;

/*
============================
Private Functions
============================
*/

static Timestamp get_timestamp(void);
static string time_to_str(Timestamp t, bool more_format);
static string video_frame_type_to_str(int frame_type);

/*
============================
Public Function Implementations
============================
*/

void whist_analyzer_init(void) {
#ifdef USE_PROTOCOL_ANALYZER
    get_timestamp();
    g_analyzer = new ProtocolAnalyzer;
#endif
}

void whist_analyzer_record_current_rendering(int type, int id, int reset_id) {
    FUNC_WRAPPER(record_current_rendering, type, id, reset_id);
}

void whist_analyzer_record_pending_rendering(int type) {
    FUNC_WRAPPER(record_pending_rendering, type);
}

void whist_analyzer_record_segment(WhistSegment *packet) { FUNC_WRAPPER(record_segment, packet); }

void whist_analyzer_record_ready_to_render(int type, int id, char *packet) {
    FUNC_WRAPPER(record_ready_to_render, type, id, packet);
}

void whist_analyzer_record_nack(int type, int id, int index) {
    FUNC_WRAPPER(record_nack, type, id, index);
}

void whist_analyzer_record_decode_video(void) { FUNC_WRAPPER(record_decode_video); }

void whist_analyzer_record_decode_audio(void) { FUNC_WRAPPER(record_decode_audio); }

void whist_analyzer_record_skip(int type, int from_id, int to_id) {
    FUNC_WRAPPER(record_skip, type, from_id, to_id);
}

void whist_analyzer_record_reset_ringbuffer(int type, int from_id, int to_id) {
    FUNC_WRAPPER(record_reset_ringbuffer, type, from_id, to_id);
}

void whist_analyzer_record_fec_used(int type, int id) { FUNC_WRAPPER(record_fec_used, type, id); }

void whist_analyzer_record_stream_reset(int type, int id) {
    FUNC_WRAPPER(record_stream_reset, type, id);
}

void whist_analyzer_record_audio_queue_full(void) { FUNC_WRAPPER(record_audio_queue_full); }

void whist_analyzer_record_current_fec_info(int type, double base_fec_ratio, double extra_fec_ratio,
                                            double total_fec_ratio_original,
                                            double total_fec_ratio) {
    FUNC_WRAPPER(record_current_fec_info, type, base_fec_ratio, extra_fec_ratio,
                 total_fec_ratio_original, total_fec_ratio);
}

void whist_analyzer_record_current_cc_info(int type, double packet_loss, double latency,
                                           int bitrate, int incoming_bitrate) {
    FUNC_WRAPPER(record_current_cc_info, type, packet_loss, latency, bitrate, incoming_bitrate);
}

string whist_analyzer_get_report(int type, int num, int skip, bool more_format) {
    assert(g_analyzer != NULL);
    whist_lock_mutex(g_analyzer->m_mutex);
    string s;
    s += g_analyzer->get_stat(type, num, skip);
    s += "\n";
    s += "frame_breakdown:\n";
    s += g_analyzer->get_frames_info(type, num, skip, more_format);
    whist_unlock_mutex(g_analyzer->m_mutex);
    return s;
}

/*
============================
Private Function Implementations
============================
*/

// get time in us since start
static Timestamp get_timestamp(void) {
    static Timestamp g_start_time = current_time_us();
    return current_time_us() - g_start_time;
}

// covert time from Timestamp to string
static string time_to_str(Timestamp t, bool more_format) {
    if (t == -1)
        return "null";
    else {
        stringstream ss;
        ss.setf(ios::fixed, ios::floatfield);
        ss.precision(1);
        ss << t / 1000.0;
        if (more_format) ss << "ms";
        return ss.str();
    }
}

// turn the info of frame to a json-like format
// TODO add an option for full json format
// TODO we can also output html code
string FrameLevelInfo::to_string(bool more_format) {
    stringstream ss;
#ifdef USE_PROTOCOL_ANALYZER
    ss << "{"
       << "id=" << id << ",";
    if (type == PACKET_VIDEO) {
        ss << "type=VIDEO,";
    } else if (type == PACKET_AUDIO) {
        ss << "type=AUDIO,";
    } else {
        ss << "type=" << type << ",";
    }
    ss << "num=" << num_of_packets << ",";
    ss << "fec=" << num_of_fec_packets << ",";
    if (type == PACKET_VIDEO) {
        ss << "frame_type=" << video_frame_type_to_str(frame_type) << ",";
        // ss << "is_empty=" << is_empty << ",";
    }
    ss << "segment_size=" << max_segment_size << ",";
    ss << "first_seen=" << time_to_str(first_seen_time, more_format) << ",";

    ss << "ready_time=" << time_to_str(ready_time, more_format) << ",";

    // ss << "current_rendering_time=" <<time_to_str(become_current_rending_time)<<",";

    ss << "pending_time=" << time_to_str(become_pending_time, more_format) << ",";
    ss << "decode_time=" << time_to_str(decode_time, more_format) << ",";
    if (nack_used) {
        ss << "nack_used"
           << ",";
    }
    if (fec_used) {
        ss << "fec_used"
           << ",";
    }
    if (skip_to != -1) ss << "skip_to=" << skip_to << ",";
    if (reset_ringbuffer_from != -1) ss << "reset_ringbuffer_from=" << reset_ringbuffer_from << ",";
    if (reset_ringbuffer_to != -1) ss << "reset_ringbuffer_to=" << reset_ringbuffer_to << ",";

    /*
    ss<<"[";
    for(int i=0;i<(int)reset_ringbuffer_vec.size();i++)
    {
        ss<<"<"<<reset_ringbuffer_vec[i].id<<","<<reset_ringbuffer_vec[i].time/1000.0<<">";
    }
    ss<<"]";*/

    if (overwrite_id != -1) ss << "overwrite=" << overwrite_id << ",";
    if (stream_reset_time != -1)
        ss << "stream_reset_time=" << time_to_str(stream_reset_time, more_format) << ",";
    if (type == PACKET_AUDIO && queue_full != -1) {
        ss << "queue_full"
           << ",";
    }
    if (more_format) ss << "}" << endl << "{";
    ss << "segments=[";
    for (auto it = segments.begin(); it != segments.end(); it++) {
        if (it != segments.begin()) ss << ",";
        ss << "{";
        if (more_format) ss << "idx=";
        ss << it->first;
        if (more_format) ss << ",";
        auto &segment_info = it->second;
        int item_cnt = 0;
        ss << "[";
        if (more_format) ss << "arrival:";
        for (int i = 0; i < (int)segment_info.time_us.size(); i++) {
            if (i) ss << ",";
            ss << time_to_str(segment_info.time_us[i], more_format);
            item_cnt++;
        }

        if (segment_info.retrans_time_us.size()) {
            if (item_cnt) ss << "; ";
            if (more_format)
                ss << "retrans_arrival:";
            else
                ss << "retr:";
            for (int i = 0; i < (int)segment_info.retrans_time_us.size(); i++) {
                if (i) ss << ",";
                ss << time_to_str(segment_info.retrans_time_us[i], more_format);
                item_cnt++;
            }
        }

        if (segment_info.nack_time_us.size()) {
            if (item_cnt) ss << "; ";
            if (more_format)
                ss << "nack_sent:";
            else
                ss << "nack:";
            for (int i = 0; i < (int)segment_info.nack_time_us.size(); i++) {
                if (i) ss << ",";
                ss << time_to_str(segment_info.nack_time_us[i], more_format);
                item_cnt++;
            }
        }
        ss << "]";

        ss << "}";
    }
    ss << "]}";
#else
    UNUSED(&time_to_str);
    UNUSED(&video_frame_type_to_str);
#endif
    return ss.str();
}

// get the begin and end iterator, for the range of records required
pair<FrameMap::iterator, FrameMap::iterator> ProtocolAnalyzer::get_range_it(int type,
                                                                            int num_of_records,
                                                                            int skip_last) {
    FrameMap::iterator begin_it, end_it;
    auto &info = type_level_infos[type].frames;
    int cnt = 0;
    int key = -1;

    // look up backward from the end of map, for the record we should begin with
    for (auto it = info.rbegin(); it != info.rend(); it++) {
        if (it->second.id == -1)
            it->second.id = it->first;  // fixed some records with incomplete info by the way
        cnt++;
        if (cnt == num_of_records + skip_last) {
            key = it->first;
            break;
        }
    }

    // set the begin iterator to the record found, or the begin of the map if there is no enough
    // record
    begin_it = info.begin();
    if (key != -1) {
        begin_it = info.find(key);
    } else {
        begin_it = info.begin();
    }

    // set the end it
    if (cnt <= skip_last)  // boundary case where there is less record than need to skip
    {
        end_it = begin_it;  // in this case it's an empty range.
    } else {
        int actual_number_of_records = cnt - skip_last;  // adjust the actual number of record to
                                                         // report, according to the record we have
        cnt = 0;
        for (auto it = begin_it; it != info.end(); it++) {
            cnt++;
            end_it = it;
            if (cnt > actual_number_of_records) break;
        }
    }

    return make_pair(begin_it, end_it);
}

// get high level stats
string ProtocolAnalyzer::get_stat(int type, int num_of_records, int skip_last) {
    stringstream ss;
#ifdef USE_PROTOCOL_ANALYZER
    auto range = get_range_it(type, num_of_records, skip_last);

    if (range.first == range.second) {
        return "no record\n";
    }

    double first_seen_to_ready = 0.0;  // the sum of time of frame become ready from first seen
    int first_seen_to_ready_cnt = 0;   // for how many frames, the data is valid

    double first_seen_to_decode = 0.0;  // the sum of time of frame feed into decode from first_seen
    int first_seen_to_decode_cnt = 0;   // for how many frames, the data is valid

    int recovery_by_nack_cnt = 0;  // num of frames recovered by nack
    int recovery_by_fec_cnt = 0;   // num of frames recoverd by fec

    // num of frames recovered by fec after ack (or think it as by both fec and ack)
    int recovery_by_fec_after_nack_cnt = 0;

    int total_seen_cnt = 0;   // num of all frames that was seen
    int total_ready_cnt = 0;  // num of frames become ready
    int not_ready_cnt = 0;    // num of frames not ready
    int not_decode_cnt = 0;   // num of frames not decoded

    int fasle_drop_cnt = 0;        // false drop defined by ming
    int recoverable_drop_cnt = 0;  // recoverable drop defined by ming

    int frame_skip_times = 0;        // num of frame skip times
    int frame_skip_cnt = 0;          // the total num of frames skipped
    int ringbuffer_reset_times = 0;  // num of ringbuffer reset times

    int received_segments_nonack = 0;
    int total_segments = 0;  // total

    int begin_id = range.first->first;  // the id of first frame
    int end_id = -1;                    // the id last frame

    DistributionStat first_seen_to_decode_stat;

    Timestamp last_decode_time = -1;   // help calculate gap
    DistributionStat decode_gap_stat;  // help gen distribution of gap

    double fec_info_cnt = 0;                          // num of samples with fec info
    double rough_base_fec_ratio_sum = 0.0;            // for calculate rough avg of base_fec_ratio
    double rough_extra_fec_ratio_sum = 0.0;           // for calculate rough avg extra_fec_ratio
    double rough_total_fec_ratio_sum = 0.0;           // for calculate rough avg of total_fec_raito
    double rough_total_fec_ratio_original_sum = 0.0;  // for cal rough avg of total_fec_raito_origin

    double max_packet_loss = 0.0;
    double min_packet_loss = 9999;

    double min_latency = 9999;
    double max_latency = 0;
    double rough_latency_sum = 0.0;  // for cal rough ave of latency
    double cc_info_cnt = 0;          // num of frame with congestion control info

    long long total_segments_size = 0;      // accurate value of sum of all segments
    long long total_fec_segments_size = 0;  // sum of all fec sements

    long long rough_bitrate_sum = 0;  // for cal rough avg of bitrate

    Timestamp begin_ts = -1;  // time of first seen frame
    Timestamp end_ts = 0;     // time of last seen frame

    for (auto it = range.first; it != range.second; it++) {
        end_id = it->first;
        total_seen_cnt++;

        if (it->second.first_seen_time != -1) {
            if (begin_ts == -1) {
                begin_ts = it->second.first_seen_time;
            }
            end_ts = it->second.first_seen_time;
        }

        for (auto it2 = it->second.segments.begin(); it2 != it->second.segments.end(); it2++) {
            total_segments_size += it2->second.size;
            if (it2->first >= it->second.num_of_packets - it->second.num_of_fec_packets) {
                total_fec_segments_size += it2->second.size;
            }
        }

        if (it->second.current_fec_info.total_fec_ratio != -1) {
            rough_base_fec_ratio_sum += it->second.current_fec_info.base_fec_ratio;
            rough_extra_fec_ratio_sum += it->second.current_fec_info.extra_fec_ratio;
            rough_total_fec_ratio_sum += it->second.current_fec_info.total_fec_ratio;
            rough_total_fec_ratio_original_sum +=
                it->second.current_fec_info.total_fec_ratio_original;
            fec_info_cnt++;
        }

        if (it->second.current_cc_info.latency != -1) {
            min_latency = min(min_latency, it->second.current_cc_info.latency);
            max_latency = max(max_latency, it->second.current_cc_info.latency);
            max_packet_loss = max(max_packet_loss, it->second.current_cc_info.packet_loss);
            min_packet_loss = min(min_packet_loss, it->second.current_cc_info.packet_loss);
            rough_latency_sum += it->second.current_cc_info.latency;
            FATAL_ASSERT(it->second.current_cc_info.bitrate >= 0);
            rough_bitrate_sum += it->second.current_cc_info.bitrate;
            cc_info_cnt++;
        }

        if (it->second.decode_time != -1) {
            if (last_decode_time != -1) {
                Timestamp decode_gap = it->second.decode_time - last_decode_time;
                int decode_gap_ms = decode_gap / US_IN_MS;
                decode_gap_stat.insert(decode_gap_ms);
            }
            last_decode_time = it->second.decode_time;
        }

        // for estimate packet loss
        if (it->second.type != -1 && it->second.num_of_packets != 1) {
            total_segments += it->second.num_of_packets;
            received_segments_nonack += it->second.num_received_nonack;
        }

        // for frame skip
        if (it->second.skip_to != -1) frame_skip_times++;
        if (it->second.skip_to != -1) {
            frame_skip_cnt += it->second.skip_to - it->second.id - 1;
        }

        // for ring buffer reset
        if (it->second.reset_ringbuffer_from != -1) ringbuffer_reset_times++;

        // for metric related to ready frames
        if (it->second.ready_time != -1) {
            first_seen_to_ready += it->second.ready_time - it->second.first_seen_time;
            first_seen_to_ready_cnt++;
            total_ready_cnt++;
            if (it->second.nack_used) recovery_by_nack_cnt++;
            if (it->second.fec_used) recovery_by_fec_cnt++;
            if (it->second.fec_used_after_nack) recovery_by_fec_after_nack_cnt++;

            if (it->second.decode_time == -1) {
                fasle_drop_cnt++;
            }
        } else {  // for metric related to non-ready frames
            not_ready_cnt++;
            const double recoverable_drop_threshold =
                0.1;  // if a frame loss <10% of segments, we consider it (easily) recoverable
            if (1 - it->second.num_received_nonack / (double)it->second.num_of_packets <
                recoverable_drop_threshold) {
                recoverable_drop_cnt++;
            }
        }

        if (it->second.decode_time != -1) {
            first_seen_to_decode += it->second.decode_time - it->second.first_seen_time;
            first_seen_to_decode_stat.insert((it->second.decode_time - it->second.first_seen_time) /
                                             US_IN_MS);
            first_seen_to_decode_cnt++;
        } else {
            not_decode_cnt++;
        }
    }

    if (type == PACKET_VIDEO) {
        ss << "type=VIDEO" << endl;
    } else if (type == PACKET_AUDIO) {
        ss << "type=AUDIO" << endl;
    } else {
        ss << "type=" << type << endl;
    }

    int total_cnt = end_id - begin_id + 1;  // total num of frames

    ss << "frame_count=" << total_cnt << endl;
    ss << "frame_seen_count=" << total_seen_cnt << endl;

    ss << "first_seen_to_ready_time=" << first_seen_to_ready / 1000.0 / first_seen_to_ready_cnt
       << "ms" << endl;
    ss << "first_seen_to_decode_time=" << first_seen_to_decode / 1000.0 / first_seen_to_decode_cnt
       << "ms" << endl;

    ss << "recover_by_nack=" << recovery_by_nack_cnt * 100.0 / total_ready_cnt << "%" << endl;
    ss << "recover_by_fec=" << recovery_by_fec_cnt * 100.0 / total_ready_cnt << "%" << endl;
    ss << "recover_by_fec_after_nack=" << recovery_by_fec_after_nack_cnt * 100.0 / total_ready_cnt
       << "%" << endl;

    ss << "false_drop=" << fasle_drop_cnt * 100.0 / total_cnt << "%" << endl;
    ss << "recoverable_drop=" << recoverable_drop_cnt * 100.0 / total_cnt << "%" << endl;

    ss << "not_seen=" << (total_cnt - total_seen_cnt) * 100.0 / total_cnt << "%" << endl;
    ss << "not_ready=" << not_ready_cnt * 100.0 / total_cnt << "%" << endl;
    ss << "not_decode=" << not_decode_cnt * 100.0 / total_cnt << "%" << endl;

    ss << "frame_skip_times=" << frame_skip_times << endl;
    ss << "num_frames_skiped=" << frame_skip_cnt << endl;
    ss << "ringbuffer_reset_times=" << ringbuffer_reset_times << endl;
    ss << "fps=" << first_seen_to_decode_cnt / ((end_ts - begin_ts) * 1.0 / US_IN_SECOND) << endl;

    if (type == PACKET_VIDEO)  // below info are only for video
    {
        ss << "estimated_network_loss=" << 100.0 - received_segments_nonack * 100.0 / total_segments
           << "%" << endl;

        ss << endl;

        ss << "min_packet_loss=" << min_packet_loss * 100.0 << "%" << endl;
        ss << "max_packet_loss=" << max_packet_loss * 100.0 << "%" << endl;

        ss << "min_latency=" << min_latency << "ms" << endl;
        ss << "max_latency=" << max_latency << "ms" << endl;
        ss << "rough_latency_avg=" << rough_latency_sum / cc_info_cnt << "ms" << endl;

        ss << "rough_expected_bitrate_avg=" << rough_bitrate_sum * 1.0 / cc_info_cnt / 1024
           << " kbps" << endl;
        ss << "incoming_bitrate_avg="
           << total_segments_size * 8.0 / ((end_ts - begin_ts) / US_IN_SECOND) / 1024 << " kbps"
           << endl;

        ss << endl;

        ss << "rough_base_fec_ratio_avg=" << rough_base_fec_ratio_sum / fec_info_cnt * 100.0 << "%"
           << endl;
        ss << "rough_extra_fec_ratio_avg=" << rough_extra_fec_ratio_sum / fec_info_cnt * 100.0
           << "%" << endl;
        ss << "rough_toal_fec_ratio_original_avg="
           << rough_total_fec_ratio_original_sum / fec_info_cnt * 100.0 << "%" << endl;
        ss << "rough_toal_fec_ratio_avg=" << rough_total_fec_ratio_sum / fec_info_cnt * 100.0 << "%"
           << endl;

        ss << endl;
        ss << "actual_fec_overhead_ratio="
           << (double)total_fec_segments_size / total_segments_size * 100.0 << "%" << endl;
    }

    ss << endl;

    ss << "first_seen_to_decode_distribution_in_ms:" << endl;
    ss << first_seen_to_decode_stat.get_stat() << endl;
    // ss << endl;

    ss << "decode_gap_distribution_in_ms:" << endl;
    ss << decode_gap_stat.get_stat() << endl;

#endif

    return ss.str();
}

// a pretty print helper for video frame type, used only inside analyzer
string video_frame_type_to_str(int frame_type) {
    switch (frame_type) {
        case VIDEO_FRAME_TYPE_NORMAL: {
            return "Normal";
        }
        case VIDEO_FRAME_TYPE_INTRA: {
            return "Intra";
        }
        case VIDEO_FRAME_TYPE_CREATE_LONG_TERM: {
            return "Create_LT";
        }
        case VIDEO_FRAME_TYPE_REFER_LONG_TERM: {
            return "Refer_LT";
        }
        default: {
            stringstream ss;
            ss << frame_type;
            return ss.str();
        }
    }
}
