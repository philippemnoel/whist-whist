# Debugging Tools

This folder contains a variety of tools we use to debug and analyze the Whist `/protocol` project.

## Debug Console

### Overview

When enabled, the `Debug Console` provides a telnet style console on port 9090 at client side, allows you to send commands to the client while it is running. You can change parameters of the client interactively/dynamically (e.g. change `bitrate` `audio_fec_ratio` `video_fec_ratio`), or let the program run some commands for you (e.g. get report from `protocol_analyzer`).

You can find images illustrating this process inside this [PR](https://github.com/whisthq/whist/pull/5352).

### How to enable

Debug Console can only be enabled with a `DEBUG` build.

To enable the debug console, add `--debug-console PORT` to the options of client, for example:

If without debug console, the command is:

```
./wclient 54.219.45.195 -p32262:30420.32263:35760.32273:4473 -k 1ef9aec50197a20aa35b68b85fb7ace0
```

Then the following command will run the client with debug console enable on port 9090:

```
./wclient 54.219.45.195 -p32262:30420.32263:35760.32273:4473 -k 1ef9aec50197a20aa35b68b85fb7ace0 --debug-console 9090
```

### How to access

the easiest way to access debug console is to use the tool `netcat`:

```
nc 127.0.0.1 9090 -u
```

Then you get a telnet style console. You can run commands (inside the console) and get outputs, in a way similar to a shell (e.g. `bash`).

the `rlwrap` tool can make your use of `netcat` easier, e.g. you can use the arrows to move cursor and recall previous command, when used with rlwrap, the full commands is:

```
rlwrap nc 127.0.0.1 9090 -u
```

(you can get both `netcat` and `rlwrap` easily from `apt` or `brew`)

### Support Commands

#### `report_XXX`

use `report_video` or `report_audio` to get a report of the last `report_num` (default:2000) of records of video or audio.

`report_video_moreformat` or `report_audio_moreformat` are similar, increase the use of format string at the cost of a longer output.

examples:

```
report_video  video1.txt                    #get a report of video, save to video1.txt
report_audio  /home/yancey/audio1.txt       #get a report of audio, save to /home/yancey/audio1.txt
report_video_moreformat  video2.txt         #get a report of video, with more format attempt
report_audio_moreformat  audio2.txt.cpp     #get a report of audio, save the file as cpp, so that you can cheat your editor to highlight it
```

#### `set`

Set allows you to change parameters inside the client dynamically.
Support parameters:

- `video_fec_ratio`, change the video fec ratio
- `audio_fec_ratio`, change the audio fec ratio
- `bitrate`, force the bitrate outputted by bitrate algorithm
- `burst_bitrate`, force the burst bitrate
- `no_minimize`, disable the MESSAGE_STOP_STREAMING sent by client when there is a window minimize, which might be annoying for debugging (e.g. when switching to other window and look at logs)
- `verbose_log`, toggle the verbose_log flag, enable more logs inside the client.
- `report_num`, change the number of records/frames included inside a report, default: 2000
- `skip_last`, skip the last n frames recorded by the analyzer, default:60. (the last few records might not have not arrived yet, including them will make metrics inaccurate)

Examples:

```
set video_fec_ratio 0.15             #set video fec ratio to 0.15
set verbose_log 1                    #enable verbose log
set bitrate 2000000                  #force bitrate to 2Mbps
set report_num 5000                  #include 5000 records in following reports
```

#### `info`

Take a peek of the values of parameters supported by set

#### `insert_atexit_handler` or `insert`

Insert a atexit handler that turns exit() to abort, so that we can get the stack calling exit(). It's helpful for debugging problems, when you don't know where in the code called exit(), especially the exit() is called in some lib.

#### `plot_xxx`

Use `plot_start` to start or restart (the sampling of) plotting.

Use `plot_export <filename>` to export the plot data to a file, which can be future turned into plot graphs by the `plotter.py`. Examples:

```
#inside debug console
plot_export /Users/yancey/demo.json

#inside shell
./plotter.py ~/demo.json
```

Use `plot_stop` to stop (the sampling) of plotting, e.g avoid waste of CPU and memory used on sampling.

## Protocol Analyzer

### Overview

This is a protocol analyzer in the side-way style, that you can talk to interactively via the debug console. Allows you to analyze the protocol dynamically, get advanced metrics.

Features/Advantages:

- It only gives output if you ask to, instead of spamming the log. As a result you can put as many infos as you like.
- It tracks the life of frames/segments, organize info in a compact structured way. Reduce eye parsing and brain processing.
- Centralized way to put meta infos, allows advanced metric to be implemented easily.
- It hooks into the protocol to track the protocol's running, instead of inserting attributes into the data structures of protocol. Almost doesn't increase the complexity of code of protocol

Disadvantages:

- when you make serious changes to the protocol, you need to update the protocol analyzer to track it correctly, increases maintain burden of codebase. But (1) this usually won't happen unless something essential is changed in protocol (2) when it happens it should be easy to update the analyzer

### How to run

When debug console is enabled, the protocol analyzer is automatically enabled. You don't need to explicitly start it, all you need to do is talk to it via `debug_console`. To get report from it, or change some parameters (e.g `report_num`, `skip_last`)

### Report format

The report of protocol analyzer consists two sections: (1) high level information/statistics (2) a breakdown of all frames inside the report

#### High-level information/statistic

This section is a list of information/statistic, consist of:

- `type`, whether the report is for audio or video. Possible values: `VIDEO` or `AUDIO`
- `frame_count`, number of frames includes in the report
- `frame_seen_count`, number of frames that are seen, i.e, we received at least one segment of those frames.
- `first_seen_to_ready_time`, the average time that frames become ready to render from first seen. Unit: ms
- `first_seen_to_decode_time`, the average time that frames are fed to decode from first seen. Unit: ms
- `recover_by_nack`, the percentage of frames recovered by nack. "Recovered by nack" means, when the frame becomes ready, at least one retransmission of packet arrives
- `recover_by_fec`, the percentage of frames recovered by fec.
- `recover_by_fec_after_nack`, the percentage of frames that is recovered by fec followed after nack. Or you think those frames are recovered by both fec and nack.
- `false_drop`, the percentage of frames that becomes ready but never feed into decoder
- `recoverable_drop`, the percentage of frames that only lost a small proportion (<10%), should be recovered easily but actually never become ready.
- `not_seen`, the percentage of frames that we never saw a single segment for.
- `not_ready`, the percentage of frames that never become ready.
- `not_decode`, the percentage of frames that was never feed into decoder
- `frame_skip_times`, the times of frames skips happened
- `num_frames_skiped`, the number of total frame skipped
- `ringbuffer_reset_times`, the times of ringbuffer reset happened
- `estimated_network_loss`, the estimated network packet loss. (only exist in `VIDEO` report)

All time are the relative time since client start. This also applies to the sections below, in the whole report.

#### Breakdown of frames

This section is a list of frame infos, each one consist of:

Main Attributes(always showing):

- `id`, id of the frame
- `type`, type of frame. Possible values: `VIDEO` or `AUDIO`
- `num`, the number of all packets inside the frame, including fec packets.
- `fec`, the number of fec packets inside the frame.
- `frame_type`, the type of frame, e.g. intra frame or normal frame. (Only for `Video`)
- `first_seen`, the time we first seen the frame, i.e. the first segment of the frame arrives
- `ready_time`, the time when the frame becomes ready to render, correspond to the `is_ready_to_render()` insider `ringbuffer.c`
- `pending_time`, the time when the frame becomes pending, i.e. in a state that is waiting to be taken by the decoder thread
- `decode_time`, the time when the frame is fed into the decoder.
- `segments`, a break down of segments info

(In addition to the possible values, the value can be `-1` or `null`, indicate there is no value available)

Other attributes(onlys shows when there are something interesting happens):

- `nack_used`, when this shows, it means nack is used for recovering this frame
- `fec_used`, when this shows, it means fec is used fro recovering this frame
- `skip_to`, when this shows, it means there is a frame skip from the current frame to the frame `skip_to` points to
- `reset_ringbuffer_to`, when this shows, it means there is a ringbuffer reset from the current frame to the frame `reset_ringbuffer_to` points to
- `reset_ringbuffer_from`, when this shows, it means there is a ringbuffer reset from the frame `reset_ringbuffer_from` points to, to the current frame. (kind of redundant with above)
- `overwrite`, when this shows, it means when the current frame becomes "current rendering" inside ringbuffer, it overwrites a frame that never becomes pending
- `queue full`, when this shows, it means when the current frame tries to become pending, it triggers an "Audio queue full". (Only for audio)

##### Breakdown of segments

The breakdown of segments is a list of items. Each items correspond of a segment of the frame, contains:

- `idx`, the index of segment. this `idx` label is omitted by default output
- a list contains 3 sections:
  - `arrival`, a list of all times of packets which arrive without an `is_a_nack` flag, i.e. the non-retransmit packets. The label is omitted by default output.
  - `retrans_arrival`, a list all times of packets which arrive with an `is_a_nack` flag, i.e. the retransmitted packets. The label is shown as `retr` by default output, for short.
  - `nack_sent` , a list of all times of an NACK is sent for this segment. The label is shown as `nack` by default output, for short.

Note about the terminology: A `segment` is a segment of `frame`, a `segment` is sent via a `packet`. There might be multiple `packets` contains the same `segment`.

An example that helps understand, with default format:

```
segments=[
{0[4542.4,4543.0]},
{1[4545.0]},
{2[4545.1; retr:4630.0; nack:4545.0]},
{3[retr:4640.3; nack:4560.0]},
{4[retr:4680.0; nack:4561.0, 4610.0]},
{5[4545.2]}]
```

or with the `moreformat` format:

```
segments=[
{idx=0,[arrival:4542.4,4543.0]},
{idx=1,[arrival:4545.0]},
{idx=2,[arrival:4546.1;retrans_arrival:4630.0; nack_sent:4545.0]},
{idx=3,[retrans_arrival:4640.3; nack_sent:4560.0]},
{idx=4,[retrans_arrival:4680.0; nack_send:4561.0, 4610.0]},
{idx=5,[arrivale:4545.2]}]
```

(this is an artificial example for help understanding, it should be rare to see such segments in real use )

(the outputs are in one line in the real output, instead of multiple lines, to reduce page space. an editor plug-in such as `rainbow_parentheses`, significantly increase the readability, it's very suggested to install one)

In the example, the frame consist of 6 segments:

1. The 0th segment arrives twice without nack, there might be a network duplication.
2. the 1st segment arrives as normal
3. for the 2nd segment, the non-retransmit packet successfully arrives, but there is still an NACK sent, and we got the retransmitted segment
4. for the 3rd segment, the non-retransmit packet got lost, and NACK was sent, we got the retransmitted segment.
5. for the 4th segment, the non-retransmit packet got lost, and NACK was sent, the retransmit segment got lost, we sent and NACK again. And finally got the segment.
6. the 5th segment arrives as normal.

### Implementation

This section talks about the implementation of protocol analyzer, hopefully you can get an idea on how to extend it.

The protocol analyzer mainly consist of:

1. data structures, that store the recorded info
2. hooks, that hook into the protocol to record info
3. statistics generator, for the high-level info/stat in report
4. pretty printer for the frame breakdown in report

#### Data structures

Below are the data structures used for protocol analyzer.

```
//info at segment level
Struct SegmentLevelInfo
{
... //attributes
}

//info at frame level
struct FrameLevelInfo {
map<int, SegmentLevelInfo> segments;
... //attributes
}

//a map from id to frame
typedef map<int, FrameLevelInfo> FrameMap;

// type level info.  By "type" I mean VIDEO or AUDIO
struct TypeLevelInfo {
    FrameMap frames;
    ... //attributes
};

//the protocol analyzer class
struct ProtocolAnalyzer {
    map<int, TypeLevelInfo> type_level_infos;   //a map from type to type level info.
}

```

If you want to record new info, you will need to put it as an attribute of the data structures.

#### Hooks

Hooks are the public functions with name `whist_analyzer_record_XXXXX()`, they hook into the protocol and save info into the data structure, for example:

```
// record info related to the arrival of a segment
void whist_analyzer_record_segment(WhistSegment *packet)
{
  FUNC_WRAPPER(record_segment, packet);
}
```

It's a wrapper of the corresponding method of ProtocolAnalyzer class:

```
void ProtocolAnalyzer::record_segment()
{
//real implementation is here
}

```

If you want to record new info, you might need to implement new hooks.

#### Statistics generator

The Statistics generator is just the function:

```
string ProtocolAnalyzer::get_stat(）
{
...
}
```

If you want to implement new metric/stat, add code to this function

#### Pretty printer for frame breakdown

The pretty printer for frame breakdown is just the function:

```
string FrameLevelInfo::to_string()
{
...
}
```

If you want new attributes of frames to be printed out, add code to this function.

## Plotter

### Overiew

the Plotter consist of the C++ APIs part (`plotter.h` and `plotter.cpp`) and the python helper `plotter.py` part.

#### C++ API part

##### Controlling APIs:

```
void whist_plotter_init(void);  // init the plotter, usually called inside
void whist_plotter_start_sampling(void);  // start sampling,
void whist_plotter_stop_sampling(void);   // stop sampling
```

They are usually expected to be called inside `debug_console`. But you can also call it manually at other places, e.g. in the unit-test to manually draw plot graphs.

##### Sampling API:

```
void whist_plotter_insert_sample(const char* label, double x, double y);
```

This function can be called anywhere inside the `WhistClient`. The first parameter `label` is the label of data point `{x,y}`, data point with same label are considered to belong to a same data collection and has the same color in plotting. The `x` and `y`, are self-explained values to be plotted.

#### Data Exporting API:

```
std::string whist_plotter_export();
```

This function exports the sampled values to a string of json, which can be turned into graphs by the `plotter.py`.

It's usually expected to be called inside `debug_console`. But you can also call it manually at other places.

#### Python Helper Part

The python helper is the `plotter.py`, which turns the exported data into graphs. Full options:

```
Usage: plotter.py [options] filename1 [filename2 ...]

Options:
  -h, --help            show this help message and exit
  -s, --scatter         draw scatter instead of line
  -d, --distribution    draw distribution of y, instead of the x-y plot
  -f FILTER_PATTERN, --filter=FILTER_PATTERN
                        regex expression to filter label names
  -v, --inverse-match   when used together with -f, do inverse match
  -r RANGE_X, --range=RANGE_X
                        range of x, e.g. "10.0~15.0"
  -w WEIGHT, --weight=WEIGHT
                        weight of the line or point, default: 0.5
```

### Introduce to the use of plotter

#### Practices for using the Sampling API

To easily control the plotting of data, and to avoid wasting CPU and memory to sample the data you are not interested.

It's suggested to organize related datasets as a group, and enable/disable the group as needed.

This can be done in two ways:

1. use MACRO
2. use the dynamic value change mechanism of `debug_console`, AKA the `set` command.

##### MACRO Method

Define PLOT_AUDIO_ALGO inside `debug_flags.h`:

```
#define PLOT_AUDIO_ALGO false                // audio algo
```

Guard the group of datasets with macro:

```
    if (PLOT_AUDIO_ALGO) {
        double current_time = get_timestamp_sec();
        whist_plotter_insert_sample("audio_device_queue", current_time, device_queue_len);
        whist_plotter_insert_sample("audio_total_queue", current_time, total_queue_len);
        whist_plotter_insert_sample("audio_scale_factor", current_time,
                                    adaptive_parameter_controller.get_scale_factor());
    }
```

Then you can enable/disable the group at compile time with MACRO.

##### dynamic Method

If a group of data need to be looked very frequently, you might want to enable/disable the group without re-compile.

First, you define `plot_audio_algo` inside the `DebugConsoleOverrideValues struct` of `debug_consol.h`. And make the relevant modification to `handle_set()` `handle_info()` to support that variable.

Guard the group with the `plot_audio_algo` variable:

```
    if (get_debug_console_override_values()->plot_audio_algo) {
        double current_time = get_timestamp_sec();
        whist_plotter_insert_sample("audio_device_queue", current_time, device_queue_len);
        whist_plotter_insert_sample("audio_total_queue", current_time, total_queue_len);
        whist_plotter_insert_sample("audio_scale_factor", current_time,
                                    adaptive_parameter_controller.get_scale_factor());
    }
```

Then you can enable/disable the group with:

```
#inside debug console
set plot_audio_algo 1           #to enable
set plot_audio_algo 0           #to disable
```

#### Examples of using the `plotter.py`

#### Basic usage

```
./plotter.py ~/sdl_present_cpu_copy.json
```

<img src="https://user-images.githubusercontent.com/4922024/176613266-80a41f03-69dc-4ec9-bac5-19143f66668e.png" width="400">

#### Change style

Sometimes the lines are too dense and hides each other, you can change style to scatter for a better view:

```
./plotter.py ~/saved_plots/audio_algo_new_piece.json -s
```

default style vs scatter style:

<img src="https://user-images.githubusercontent.com/4922024/176612467-b9561388-e27c-4c09-ba56-cce24fbc341d.png" width="400"> <img src="https://user-images.githubusercontent.com/4922024/176612641-ebed72cf-d2d4-48c7-906f-e8af6694e882.png" width="400">

#### Use distribution graph

Sometimes the plot is filled by spikes, looking at the distribution of y value might give you a better understanding of data:

```
./plotter.py ~/socket_queue_len.json -d
```

default graph vs distribution graph:

<img src="https://user-images.githubusercontent.com/4922024/176611074-6849c5bd-8af4-42a4-8c82-adce80dd5297.png" width="400"> <img src="https://user-images.githubusercontent.com/4922024/176613048-ad841793-2370-4500-ad71-bb2e6a196632.png" width="400">

#### Change Weight

You can also change the weight of line or scatter with the `-w` option:

```
./plotter.py ~/udp_dedicated_thread.json -d -w 2
```

default weight vs changed weight:

<img src="https://user-images.githubusercontent.com/4922024/176620204-c7575168-0ad5-413c-a278-86aa02c6cd63.png" width="400"> <img src="https://user-images.githubusercontent.com/4922024/176620131-9980ce33-aaf3-43da-b41f-0e1465222ec3.png" width="400">

#### User Label filter

Sometimes the exported data contains too many labels than you are interested, you can filter out the interested lablel with regular expression:

```
./plotter.py ~/saved_plots/audio_algo_new.json -f '.*(scale|device).*'
```

without filter vs with filter:

<img src="https://user-images.githubusercontent.com/4922024/176617848-6af9c004-2c1f-46c7-a13e-f5c0c3fe6049.png" width="400"> <img src="https://user-images.githubusercontent.com/4922024/176618049-d4702445-063b-4c58-beb7-cd9f94363d0f.png" width="400">

#### Plot only specific range

Sometimes the graph contains a super lage range of data. You can use the zoom function of the UI to zoom, but huge amount of data might make your UI very slow.

So you might want to pre-specify a range of x data to plot, with the `-r` option:

```
./plotter.py ~/saved_plots/audio_algo_new.json -r 35~60
```

without range vs with range:

<img src="https://user-images.githubusercontent.com/4922024/176618908-bab9bc36-3727-4368-89a0-873c09ee6d67.png" width="400"> <img src="https://user-images.githubusercontent.com/4922024/176619182-f9e75493-db2d-4cec-acfd-fd6d584ce01f.png" width="400">

#### Compare two plots

For easier comparsion, you can specific multiple files to `./plotter`, and compare the graphs on the same picture:

```
./plotter.py ~/my_pr.json ~/dev.json

```

<img src="https://user-images.githubusercontent.com/4922024/176622283-2109e8da-c432-41df-a4a1-ab4671a18762.png" width="400">

Note that when you specific more than one input files, the labels are auto prefixed by the file name.
