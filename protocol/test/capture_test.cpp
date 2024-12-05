
#include <gtest/gtest.h>
#include "fixtures.hpp"
#include <cstdio>

extern "C" {

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <X11/Xlib.h>

#include <whist/core/whist.h>
#include <whist/utils/threads.h>
#include <whist/video/codec/decode.h>
#include <whist/video/capture/capture.h>
#include <server/state.h>
}

using namespace std;

/** @brief context for the X11 server + WM */
typedef struct {
    bool debug;
    char displayStr[30];
    uint32_t initTimeout;
    bool Xvfb;
    char *Xconfig;
    bool ci_mode;

    WhistSemaphore envReadyEvent;
    WhistSemaphore stopEvent;
    WhistThread thread;
    pid_t xserver_pid;
    pid_t wm_pid;
    bool envRunning;
} X11EnvContext;

static bool fork_and_exec(const char *tag, const char *prog, char *const *args, pid_t *ppid) {
    pid_t pid = fork();
    if (pid < 0) return false;

    if (!pid) {
        int ret, err;
        ret = execvp(prog, args);
        err = errno;
        if (ret < 0) cerr << tag << ", error during execvp, error=" << strerror(err) << endl;
    }

    *ppid = pid;
    return true;
}

static bool test_wm_atom(const char *display_name) {
    Display *display = XOpenDisplay(display_name);
    if (!display) return false;

    bool ret = (XInternAtom(display, "_NET_WM_STATE", True) != None);
    XCloseDisplay(display);
    return ret;
}

static int32_t run_x11_env(void *opaque) {
    X11EnvContext *context = (X11EnvContext *)opaque;
    bool have_pipes = false;
    int fds[2];
    char display_fd[5];
    char close_from[40];
    int ret;
    WhistTimer context_ready_timer;
    bool have_x_server = false;
    char *xargs[] = {(char *)"sudo",   close_from,
                     (char *)"X",      (char *)"-displayfd",
                     display_fd,       (char *)"-config",
                     context->Xconfig, (char *)"-novtswitch",
                     (char *)"vt4",    NULL};
    char *xvfbargs[] = {(char *)"Xvfb", (char *)"-displayfd", display_fd, NULL};

#define WM_EXE "awesome"
#define WM_PATH "/usr/bin/awesome"
    char *const wmargs[] = {(char *)WM_EXE, NULL};
    char *display_ptr = &context->displayStr[1];
    size_t display_str_sz = sizeof(context->displayStr) - 2;
    const char *to_run = "sudo";
    char **args = xargs;

    if (access(WM_PATH, R_OK | X_OK) != 0) {
        cerr << "windowManager " << WM_PATH << " can't be reached" << endl;
        goto out;
    }

    /* create pipe fds for displayFd */
    if (pipe(fds) < 0) {
        if (context->debug) cerr << "unable to create pipes" << endl;
        goto out;
    }
    have_pipes = true;

    if (context->Xvfb) {
        to_run = "Xvfb";
        args = xvfbargs;
    } else {
        snprintf(close_from, sizeof(close_from), "--close-from=%d", fds[1] + 1);
    }
    snprintf(display_fd, sizeof(display_fd), "%d", fds[1]);

    if (!fork_and_exec("Xserver", to_run, args, &context->xserver_pid)) {
        if (context->debug) cerr << "error during fork/exec" << endl;
        goto out;
    }

    /* we wait until we can read the display from the X server displayFd, or the timeout is
     * reached
     */
    start_timer(&context_ready_timer);

    do {
        double d = get_timer(&context_ready_timer) * 1000;

        if (d >= context->initTimeout) goto out;

        struct pollfd pfd = {
            .fd = fds[0],
            .events = POLLIN,
        };

        do {
            ret = poll(&pfd, 1, context->initTimeout - d);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            if (context->debug) cerr << "error when polling displayFd" << endl;
            goto out;
        } else if (ret == 0) {
            if (context->debug) cerr << "X server not ready in time" << endl;
            goto out;
        }

        ret = read(fds[0], display_ptr, display_str_sz);
        if (ret <= 0) {
            if (context->debug) cerr << "error when reading displayFd" << endl;
            goto out;
        }
        display_str_sz -= ret;

        char *eol =
            (char *)memchr(context->displayStr, '\n', sizeof(context->displayStr) - display_str_sz);
        if (eol != NULL) {
            *eol = '\0';
            if (context->debug) cerr << "X server is running on " << context->displayStr << endl;

            /* TODO: for now we force the DISPLAY env variable, but later we shall exec the WM
             * with execvpe and so ship env variables
             */
            setenv("DISPLAY", context->displayStr, 1);
            break;
        }
    } while (true);

    /* then run the WM */
    if (!fork_and_exec("WindowManager", WM_PATH, wmargs, &context->wm_pid)) goto out;

    // let the awesome start and initialize, as it launches the DBUS session instance,
    // it can take a while. And we need awesome to run because otherwise the X11
    // backend will fail to retrieve atoms set by awesome.
    //

    do {
        if (test_wm_atom(context->displayStr)) break;

        double d = get_timer(&context_ready_timer) * 1000;
        if (d >= context->initTimeout) goto out;

        whist_sleep(100);
    } while (true);

    context->envRunning = true;

out:
    if (have_pipes) {
        close(fds[0]);
        close(fds[1]);
    }

    whist_post_semaphore(context->envReadyEvent);

    int status;
    if (context->envRunning) {
        /* if the environment is ok and running, let's wait until we're instructed
         * to do the cleanup
         */
        whist_wait_semaphore(context->stopEvent);
    }

    if (context->wm_pid != 0) {
        if (context->debug) cerr << "killing WM" << endl;
        kill(context->wm_pid, SIGTERM);

        if (context->debug) cerr << "waiting for WM to die" << endl;
        waitpid(context->wm_pid, &status, 0);
    }

    if (context->xserver_pid != 0) {
        if (strcmp(to_run, "sudo") == 0) {
            char kill_cmd[200];

            /* if the X server has been run with sudo we can only kill it with a sudo kill */
            snprintf(kill_cmd, sizeof(kill_cmd), "sudo -- kill -9 %d", context->xserver_pid);
            if (context->debug) cerr << "sudo killing Xserver(" << kill_cmd << ")" << endl;

            ret = system(kill_cmd);
            if (ret < 0) {
                if (context->debug) cerr << "sudo kill retCode=" << strerror(errno) << endl;
            }
        } else {
            if (context->debug) cerr << "killing Xserver" << endl;
            kill(context->xserver_pid, SIGTERM);
        }

        if (context->debug) cerr << "waiting X server to die" << endl;
        waitpid(context->xserver_pid, &status, 0);
    }

    if (context->debug) cerr << "end of thread" << endl;

    return 0;
}

static bool x_server_init(X11EnvContext *context, const char *config_file) {
    memset(context, 0, sizeof(*context));
    context->displayStr[0] = ':';
    context->displayStr[1] = '\0';
    context->initTimeout = 5 * 1000;
    context->debug = false;
    context->Xvfb = true;
    if (config_file) {
        context->Xconfig = strdup(config_file);
        context->Xvfb = false;
    }

    context->envReadyEvent = whist_create_semaphore(0);
    context->stopEvent = whist_create_semaphore(0);
    context->envRunning = false;
    context->xserver_pid = 0;
    context->wm_pid = 0;
    context->thread = whist_create_thread(run_x11_env, "X_env_runner", context);

    whist_wait_semaphore(context->envReadyEvent);
    return context->envRunning;
}

static void x_server_uninit(X11EnvContext *context) {
    int ret;

    whist_post_semaphore(context->stopEvent);
    whist_wait_thread(context->thread, &ret);

    whist_destroy_semaphore(context->envReadyEvent);
    whist_destroy_semaphore(context->stopEvent);
    free(context->Xconfig);
}

class CaptureDeviceTest : public CaptureStdoutFixture {
   protected:
    static bool is_black_screen(void *data, int width, int height, int stride) {
        int x, y;
        uint32_t *ptr;

        for (y = 0; y < height; y++) {
            ptr = (uint32_t *)((char *)data + (stride * y));

            for (x = 0; x < width; x++, ptr++) {
                if (*ptr != 0x00000000) return false;
            }
        }

        return true;
    }

    static bool get_first_nvidia_gpu(char *dest, size_t sz) {
        char line[1000] = {0};
        FILE *f = popen("nvidia-xconfig --query-gpu-info", "r");
        int ret = fread(line, 1, sizeof(line) - 1, f);
        pclose(f);

#define PCI_KEY "PCI BusID : "
        char *pos = strstr(line, PCI_KEY);
        if (!pos) return false;

        pos += strlen(PCI_KEY);
        char *pos2 = strchr(pos, '\n');
        if (!pos2) return false;

        size_t to_copy = (pos2 - pos);
        if (to_copy + 1 > sz) {
            cerr << "target PCI id buffer is too small" << endl;
            return false;
        }

        strncpy(dest, pos, pos2 - pos);
        dest[to_copy] = '\x00';
        return true;
    }

    static char *setup_nvidia_server(void) {
        char pci_id[100];

        if (!get_first_nvidia_gpu(pci_id, sizeof(pci_id))) return nullptr;

        /* build minimal X configuration file */
        char filename[] = "/tmp/xconf-XXXXXX";
        int fd = mkstemp(filename);
        if (fd < 0) return nullptr;

        FILE *f = fdopen(fd, "w");
        if (!f) return nullptr;
        fputs(
            "Section \"Monitor\"\n"
            "\tIdentifier \"Monitor0\"\n"
            "\tVendorName \"WHIST TECHNOLOGIES, INC.\"\n"
            "\tModelName \"WHIST NVIDIA VIRTUAL DISPLAY\"\n"
            "\tOption \"DPMS\"\n"
            "EndSection\n"

            "Section \"Device\"\n"
            "\tIdentifier \"Device0\"\n"
            "\tDriver \"nvidia\"\n"
            "\tVendorName \"NVIDIA Corporation\"\n"
            "\tBoardName  \"Tesla T4\"\n"
            "\tBusID \"",
            f);
        fputs(pci_id, f);
        fputs(
            "\"\n\tOption \"AllowEmptyInitialConfiguration\" \"True\"\n"
            "\tOption \"NoLogo\" \"True\"\n"
            "\tOption \"NoBandWidthTest\" \"True\"\n"
            "EndSection\n"

            "Section \"Screen\"\n"
            "\tIdentifier     \"Screen0\"\n"
            "\tDevice         \"Device0\"\n"
            "\tMonitor        \"Monitor0\"\n"
            "\tDefaultDepth    24\n"
            "EndSection\n"

            "Section \"ServerFlags\"\n"
            "\tOption \"AutoAddDevices\" \"true\"\n"
            "\tOption \"AutoEnableDevices\" \"true\"\n"
            "EndSection\n"

            "Section \"ServerLayout\"\n"
            "\tIdentifier \"Layout0\"\n"
            "\tScreen \"Screen0\"\n"
            "EndSection\n",
            f);

        fclose(f);
        return strdup(filename);
    }

    bool run_capture_test(CaptureDevice *capture, int width, int height, int *black_screens) {
        for (int i = 0; i < 20; i++) {
            void *data;
            int stride;

            int ret = capture_screen(capture);
            EXPECT_GE(ret, 0);
            if (ret < 0) return false;

            if (ret > 0) {
                CaptureEncoderHints hints;
                ret = transfer_screen(capture, &hints);
                EXPECT_GE(ret, 0);

                ret = capture_get_data(capture, &data, &stride);
                EXPECT_GE(ret, 0);

                if (is_black_screen(data, width, height, stride)) ++*black_screens;
            }
        }

        return true;
    }
};

TEST_F(CaptureDeviceTest, CaptureX11) {
    X11EnvContext xcontext;
    CaptureDevice cap;
    int width = 1024, height = 800;
    int i, ret;
    int black_screens = 0;
    bool status;

    status = x_server_init(&xcontext, NULL);
    EXPECT_TRUE(status);
    if (!status) goto out;

    ret = create_capture_device(&cap, X11_DEVICE, (void *)xcontext.displayStr, width, height, 81);
    EXPECT_GE(ret, 0);
    if (ret < 0) goto out;

    /* do a few screen captures */
    EXPECT_TRUE(run_capture_test(&cap, width, height, &black_screens));

    destroy_capture_device(&cap);
    EXPECT_LT(black_screens, 15);
out:
    x_server_uninit(&xcontext);
}

TEST_F(CaptureDeviceTest, CaptureNvX11) {
    X11EnvContext xcontext;
    CaptureDevice cap;
    int width = 1024, height = 800;
    int i, ret;
    int black_screens = 0;
    bool status;
    char *config = setup_nvidia_server();
    if (!config) {
        cerr << "no NVIDIA setup, skipping test" << endl;
        return;
    }

    status = x_server_init(&xcontext, config);
    if (!status) {
        cerr << "unable to setup NVIDIA server, skipping test" << endl;
        free(config);
        return;
    }

    ret = create_capture_device(&cap, NVX11_DEVICE, (void *)xcontext.displayStr, width, height, 81);
    EXPECT_GE(ret, 0);
    if (ret < 0) goto out;

    /* do a few screen captures */
    EXPECT_TRUE(run_capture_test(&cap, width, height, &black_screens));

    destroy_capture_device(&cap);
    EXPECT_LT(black_screens, 15);
out:
    x_server_uninit(&xcontext);
    free(config);
}

static bool store_picture(const char *target, uint32_t id, int width, int height, void *frame_data,
                          int stride) {
    char path[256];
    bool ret = false;

    snprintf(path, sizeof(path), target, id);

    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_ERROR("unable to open file %s", path);
        return false;
    }

    for (int y = 0; y < height; y++) {
        uint8_t *data = (uint8_t *)frame_data + y * stride;

        size_t r = fwrite(data, 1, width * 4, f);
        if (r != (size_t)(width * 4)) {
            LOG_ERROR("unable to write record to %s at y=%d stride=%d data=%p width=%d r=%zu", path,
                      y, stride, data, width, r);
            goto out;
        }
    }

    ret = true;
out:
    fclose(f);
    return ret;
}

static bool test_decode_frame(VideoEncoder *encoder, VideoDecoder *decoder, int width, int height,
                              bool dump) {
    static size_t frame_id = 0;
    int ret;

    if (encoder->num_packets < 1) return false;

    for (int i = 0; i < encoder->num_packets; i++) {
        AVPacket *packet = encoder->packets[i];

        ret = avcodec_send_packet(decoder->context, packet);
        if (ret < 0) return false;
    }
    decoder->received_a_frame = true;

    ret = video_decoder_decode_frame(decoder);
    if (ret < 0) return false;

    DecodedFrameData decoded = video_decoder_get_last_decoded_frame(decoder);
    if (dump) {
        AVFrame *frame = decoded.decoded_frame;
        return store_picture("/tmp/picture_%d.yuv", frame_id++, frame->width, frame->height,
                             frame->data[0], frame->linesize[0]);
    }
    return true;
}

static bool capture_encode_decode_frame(CaptureDevice *capture, uint32_t width, uint32_t height,
                                        VideoEncoder *encoder, VideoDecoder *decoder) {
    int ret;
    bool iframe;

    ret = capture_screen(capture);
    EXPECT_GE(ret, 0);

    ret = transfer_capture(capture, encoder, &iframe);
    EXPECT_GE(ret, 0);

    ret = video_encoder_encode(encoder);
    EXPECT_GE(ret, 0);

    if (0) EXPECT_TRUE(test_decode_frame(encoder, decoder, width, height, true));
    return true;
}

/** @brief test data */
typedef struct {
    int width;
    int height;
    int dpi;
    int bitrate;
    int vbv;
    CaptureDevice capture_device;
    VideoEncoder *encoder;
    VideoDecoder *decoder;
    bool test_result;
} CaptureAndEncodeData;

static int32_t capture_and_encode_thread(void *opaque) {
    CaptureAndEncodeData *data = (CaptureAndEncodeData *)opaque;
    bool res;

    /* get a first frame (X11) */
    data->test_result = true;
    data->test_result &= capture_encode_decode_frame(&data->capture_device, data->width,
                                                     data->height, data->encoder, data->decoder);

    sleep(1); /* sleep for 1s so to give the time to switch to the NVidia encoder */

    /* get a second frame (should be on NVidia) */
    data->test_result &= capture_encode_decode_frame(&data->capture_device, data->width,
                                                     data->height, data->encoder, data->decoder);

    /* get a third frame (should be on NVidia too, just for check) */
    data->test_result &= capture_encode_decode_frame(&data->capture_device, data->width,
                                                     data->height, data->encoder, data->decoder);

    /* do a reconfigure to 1920x1080 */
    data->width = 1920;
    data->height = 1080;
    res = reconfigure_capture_device(&data->capture_device, data->width, data->height, data->dpi);
    EXPECT_TRUE(res);
    if (!res) {
        data->test_result = false;
        goto out;
    }

    res = reconfigure_encoder(data->encoder, data->width, data->height, data->bitrate, data->vbv,
                              CODEC_TYPE_H264);
    EXPECT_TRUE(res);
    if (!res) {
        data->test_result = false;
        goto out;
    }

    data->test_result &= capture_encode_decode_frame(&data->capture_device, data->width,
                                                     data->height, data->encoder, data->decoder);

    /* do a reconfigure to 1910x1020 */
    data->width = 1910;
    data->height = 1020;
    res = reconfigure_capture_device(&data->capture_device, data->width, data->height, data->dpi);
    EXPECT_TRUE(res);
    if (!res) {
        data->test_result = false;
        goto out;
    }

    res = reconfigure_encoder(data->encoder, data->width, data->height, data->bitrate, data->vbv,
                              CODEC_TYPE_H264);
    EXPECT_TRUE(res);
    if (!res) {
        data->test_result = false;
        goto out;
    }

    data->test_result &= capture_encode_decode_frame(&data->capture_device, data->width,
                                                     data->height, data->encoder, data->decoder);

out:
    return 0;
}

TEST_F(CaptureDeviceTest, CaptureAndEncodeNvX11) {
    X11EnvContext xcontext;
    bool status, iframe;
    char *config = NULL;
    CaptureDeviceType capture_type = NVX11_DEVICE;
    int i, ret;
    WhistThread test_thread;
    CaptureAndEncodeData data = {0};
    int32_t ret_code;

    data.bitrate = 18857939;
    data.vbv = 7543175;
    data.width = 1024;
    data.height = 800;
    data.dpi = 81;
    if (capture_type == NVX11_DEVICE) {
        config = setup_nvidia_server();
        if (!config) {
            cerr << "no NVIDIA setup, skipping test" << endl;
            return;
        }
    }

    status = x_server_init(&xcontext, config);
    if (!status) {
        cerr << "unable to setup X server, skipping test" << endl;
        free(config);
        return;
    }

    /* prepare the setup */
    ret = create_capture_device(&data.capture_device, capture_type, (void *)xcontext.displayStr,
                                data.width, data.height, data.dpi);
    EXPECT_GE(ret, 0);
    if (ret < 0) goto out;

    data.encoder =
        create_video_encoder(data.width, data.height, data.bitrate, data.vbv, CODEC_TYPE_H264);
    if (!data.encoder) goto out;

    data.decoder = create_video_decoder(data.width, data.height, true, CODEC_TYPE_H264);
    if (!data.decoder) goto out;

    /* let the test run in the thread */
    test_thread =
        whist_create_thread(capture_and_encode_thread, "CaptureAndEncodeNvX11 thread", &data);
    whist_wait_thread(test_thread, &ret_code);

    EXPECT_TRUE(data.test_result);

    destroy_capture_device(&data.capture_device);
out:
    if (data.encoder) destroy_video_encoder(data.encoder);

    if (data.decoder) destroy_video_decoder(data.decoder);
    x_server_uninit(&xcontext);
    free(config);
}
