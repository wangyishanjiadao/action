/**
 * mp3player — Android 命令行 MP3 播放器
 *
 * 功能：
 *   - 扫描当前目录下的 .mp3 文件，编号选择播放
 *   - 终端进度条实时显示
 *   - 双击音量下键：暂停 / 继续
 *   - 双击音量上键：退出程序
 *   - 键盘回退：Space/P=暂停，Q/ESC=退出
 *
 * 依赖：
 *   - miniaudio.h (单文件音频库，构建时自动下载)
 *   - Android NDK 交叉编译为 arm64-v8a
 *   - Root 权限读取 /dev/input/event*
 *
 * 构建：见 CMakeLists.txt 和 .github/workflows/build.yml
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <time.h>
#include <termios.h>
#include <errno.h>

/* ====================================================================
 * miniaudio — 单文件引入
 * 构建时 CMake 会自动从 GitHub 下载 miniaudio.h
 * ==================================================================== */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

/* ====================================================================
 * 常量 / 配置
 * ==================================================================== */
#define MAX_MP3_FILES           1024
#define MAX_FILENAME_LEN        512
#define DOUBLE_TAP_THRESHOLD_MS 400
#define PROGRESS_BAR_WIDTH      40
#define INPUT_DEVICE_MAX        32
#define PROGRESS_REFRESH_MS     100

/* ----- 位操作辅助（用于解析 input 设备能力位）----- */
#define MY_BITS_PER_LONG  (sizeof(unsigned long) * 8)
#define MY_NBITS(x)       ((((x) - 1) / MY_BITS_PER_LONG) + 1)
#define MY_TEST_BIT(b, a) \
    (((a)[(b) / MY_BITS_PER_LONG] >> ((b) % MY_BITS_PER_LONG)) & 1)

/* ====================================================================
 * 全局状态
 * ==================================================================== */
static atomic_bool g_pause_requested = false;
static atomic_bool g_exit_requested   = false;
static atomic_bool g_is_paused        = false;
static atomic_bool g_sound_loaded     = false;
static volatile sig_atomic_t g_terminate = 0;

static ma_engine      g_engine;
static ma_sound       g_sound;
static float          g_paused_position = 0.0f;
static int            g_input_fd = -1;
static struct termios g_orig_termios;

/* ====================================================================
 * 前向声明
 * ==================================================================== */
static void  cleanup(void);
static void  signal_handler(int sig);
static void  terminal_raw_enable(void);
static void  terminal_raw_disable(void);
static int   find_input_device(char *path, size_t len);
static void *input_thread(void *arg);
static void  toggle_playback(void);
static void  format_time(int seconds, char *buf, size_t bufsz);
static void  draw_progress(float cur, float total);
static int   play_file(const char *filepath);
static int   scan_directory(char files[][MAX_FILENAME_LEN], int max);

/* ====================================================================
 * 信号处理
 * ==================================================================== */
static void signal_handler(int sig) {
    (void)sig;
    g_terminate = 1;
    atomic_store(&g_exit_requested, true);
}

/* ====================================================================
 * 资源清理
 * ==================================================================== */
static void cleanup(void) {
    /* 释放 input 设备独占 */
    if (g_input_fd >= 0) {
        ioctl(g_input_fd, EVIOCGRAB, 0);
        close(g_input_fd);
        g_input_fd = -1;
    }

    /* 停止并销毁音频 */
    if (atomic_load(&g_sound_loaded)) {
        ma_sound_stop(&g_sound);
        ma_sound_uninit(&g_sound);
        atomic_store(&g_sound_loaded, false);
    }

    /* 销毁音频引擎 */
    ma_engine_uninit(&g_engine);

    /* 恢复终端 */
    terminal_raw_disable();
    printf("\033[?25h");   /* 显示光标 */
    printf("\n");
}

/* ====================================================================
 * 终端 raw 模式
 * ==================================================================== */
static void terminal_raw_enable(void) {
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);   /* 关回显、关行缓冲 */
    raw.c_cc[VMIN]  = 0;               /* 非阻塞读 */
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void terminal_raw_disable(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
}

/* ====================================================================
 * 查找音量键所在的 input 设备
 *
 * 遍历 /dev/input/event0..eventN，找到同时支持
 * KEY_VOLUMEUP 和 KEY_VOLUMEDOWN 的设备。
 * 返回已打开的 fd，失败返回 -1。
 * ==================================================================== */
static int find_input_device(char *path, size_t len) {
    for (int i = 0; i < INPUT_DEVICE_MAX; i++) {
        snprintf(path, len, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        /* 必须支持 EV_KEY 事件类型 */
        unsigned long evbits[MY_NBITS(EV_MAX)];
        memset(evbits, 0, sizeof(evbits));
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) {
            close(fd); continue;
        }
        if (!MY_TEST_BIT(EV_KEY, evbits)) {
            close(fd); continue;
        }

        /* 必须支持音量键 */
        unsigned long keybits[MY_NBITS(KEY_MAX)];
        memset(keybits, 0, sizeof(keybits));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0) {
            close(fd); continue;
        }

        if (MY_TEST_BIT(KEY_VOLUMEUP, keybits) &&
            MY_TEST_BIT(KEY_VOLUMEDOWN, keybits)) {
            char name[256] = {0};
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            fprintf(stderr, "[INFO] 音量键设备: %s (%s)\n", path, name);
            return fd;   /* 返回已打开的 fd */
        }

        close(fd);
    }
    return -1;
}

/* ====================================================================
 * 输入线程 — 监听音量键双击
 *
 * 双击判定：
 *   同一按键两次按下的间隔 ≤ DOUBLE_TAP_THRESHOLD_MS (400ms)
 *   只处理按下事件 (value == 1)，忽略释放和重复
 * ==================================================================== */
static void *input_thread(void *arg) {
    (void)arg;

    char dev_path[256];
    int fd = find_input_device(dev_path, sizeof(dev_path));

    if (fd < 0) {
        fprintf(stderr, "[WARN] 未找到音量键设备，硬件按键不可用。\n");
        fprintf(stderr, "[INFO] 回退键盘: Space=暂停  Q=退出\n");
        return NULL;
    }

    g_input_fd = fd;

    /* 尝试独占设备（阻止 Android 系统响应音量键） */
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        fprintf(stderr, "[WARN] 无法独占输入设备 (SELinux 可能拦截)\n");
        fprintf(stderr, "[TIP]  先执行: su -c setenforce 0\n");
    }

    struct timespec last_down = {0};
    struct timespec last_up   = {0};
    struct input_event ev;

    while (!g_terminate && !atomic_load(&g_exit_requested)) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < (ssize_t)sizeof(ev)) {
            if (errno == EINTR) continue;
            usleep(10000);
            continue;
        }

        /* 只响应"按下"事件 */
        if (ev.type != EV_KEY || ev.value != 1) continue;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (ev.code == KEY_VOLUMEDOWN) {
            long diff = (now.tv_sec - last_down.tv_sec) * 1000 +
                        (now.tv_nsec - last_down.tv_nsec) / 1000000;

            if (last_down.tv_sec != 0 && diff > 0 &&
                diff <= DOUBLE_TAP_THRESHOLD_MS) {
                /* 双击！ */
                atomic_store(&g_pause_requested, true);
                last_down.tv_sec = 0;
            } else {
                last_down = now;
            }
        }
        else if (ev.code == KEY_VOLUMEUP) {
            long diff = (now.tv_sec - last_up.tv_sec) * 1000 +
                        (now.tv_nsec - last_up.tv_nsec) / 1000000;

            if (last_up.tv_sec != 0 && diff > 0 &&
                diff <= DOUBLE_TAP_THRESHOLD_MS) {
                /* 双击！ */
                atomic_store(&g_exit_requested, true);
                last_up.tv_sec = 0;
            } else {
                last_up = now;
            }
        }
    }

    return NULL;
}

/* ====================================================================
 * 播放暂停 / 恢复
 * ==================================================================== */
static void toggle_playback(void) {
    if (atomic_load(&g_is_paused)) {
        /* 恢复播放 — seek 到暂停位置再 start */
        ma_uint32 sample_rate = 44100;
        ma_sound_get_data_format(&g_sound, NULL, NULL, &sample_rate,
                                 NULL, 0);
        if (sample_rate == 0) sample_rate = 44100;

        ma_uint64 frame = (ma_uint64)(g_paused_position * sample_rate);
        ma_sound_seek_to_pcm_frame(&g_sound, frame);
        ma_sound_start(&g_sound);
        atomic_store(&g_is_paused, false);
    } else {
        /* 暂停 — 记下当前位置再 stop */
        ma_sound_get_cursor_in_seconds(&g_sound, &g_paused_position);
        ma_sound_stop(&g_sound);
        atomic_store(&g_is_paused, true);
    }
}

/* ====================================================================
 * 时间格式化 (秒 -> MM:SS)
 * ==================================================================== */
static void format_time(int seconds, char *buf, size_t bufsz) {
    int m = seconds / 60;
    int s = seconds % 60;
    snprintf(buf, bufsz, "%02d:%02d", m, s);
}

/* ====================================================================
 * 绘制进度条
 *
 * 使用 \r 回车符原地刷新同一行。
 * 末尾追加空格以覆盖上一帧更长的残留字符。
 * 使用 '#' / '-' 替代 UTF-8 方块字符，确保跨平台兼容。
 * ==================================================================== */
static void draw_progress(float cur, float total) {
    if (total <= 0.0f) total = 1.0f;
    if (cur < 0.0f)   cur  = 0.0f;
    if (cur > total)  cur  = total;

    int pct    = (int)((cur / total) * 100.0f);
    int filled = (int)((cur / total) * (float)PROGRESS_BAR_WIDTH);
    if (filled > PROGRESS_BAR_WIDTH) filled = PROGRESS_BAR_WIDTH;
    if (filled < 0) filled = 0;

    char bar[PROGRESS_BAR_WIDTH + 1];
    int i;
    for (i = 0; i < filled; i++)
        bar[i] = '#';
    for (; i < PROGRESS_BAR_WIDTH; i++)
        bar[i] = '-';
    bar[PROGRESS_BAR_WIDTH] = '\0';

    char t_cur[16], t_tot[16];
    format_time((int)cur,    t_cur, sizeof(t_cur));
    format_time((int)total,  t_tot, sizeof(t_tot));

    printf("\r  [%s] %3d%%  %s / %s    ", bar, pct, t_cur, t_tot);
    fflush(stdout);
}

/* ====================================================================
 * 播放单个 MP3 文件
 *
 * 返回: 0=正常播完, 1=错误, 2=用户中断
 * ==================================================================== */
static int play_file(const char *filepath) {
    const char *fname = strrchr(filepath, '/');
    fname = fname ? fname + 1 : filepath;

    /* 初始化音频引擎 */
    ma_result r = ma_engine_init(NULL, &g_engine);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "[ERR] 音频引擎初始化失败: %s\n",
                ma_result_description(r));
        return 1;
    }

    /* 加载音频文件 */
    r = ma_sound_init_from_file(&g_engine, filepath,
                                 MA_SOUND_FLAG_NO_SPATIALIZATION,
                                 NULL, NULL, &g_sound);
    if (r != MA_SUCCESS) {
        fprintf(stderr, "[ERR] 无法加载 '%s': %s\n",
                fname, ma_result_description(r));
        ma_engine_uninit(&g_engine);
        return 1;
    }
    atomic_store(&g_sound_loaded, true);

    /* 获取总时长 */
    float duration = 0.0f;
    ma_sound_get_length_in_seconds(&g_sound, &duration);

    /* 清屏并显示头部信息 */
    printf("\033[2J\033[H");
    printf("> %s\n", fname);
    printf("  [Vol- x2: Pause/Play]  [Vol+ x2: Quit]"
           "  [Space: Pause]  [Q: Quit]\n");
    printf("\033[?25l");   /* 隐藏光标 */
    fflush(stdout);

    /* 开始播放 */
    atomic_store(&g_is_paused, false);
    ma_sound_start(&g_sound);

    /* 主循环 */
    int finished = 0;
    while (!g_terminate && !atomic_load(&g_exit_requested)) {
        /* 处理来自输入线程的暂停请求 */
        if (atomic_exchange(&g_pause_requested, false)) {
            toggle_playback();
        }

        /* 获取当前播放位置 */
        float cursor;
        if (atomic_load(&g_is_paused)) {
            cursor = g_paused_position;
        } else {
            ma_sound_get_cursor_in_seconds(&g_sound, &cursor);
        }
        draw_progress(cursor, duration);

        /* 检查是否播完 */
        if (!atomic_load(&g_is_paused) && ma_sound_at_end(&g_sound)) {
            finished = 1;
            break;
        }

        /* 键盘回退控制 */
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == ' ' || c == 'p' || c == 'P') {
                toggle_playback();
            } else if (c == 'q' || c == 'Q' || c == 27) {
                break;
            }
        }

        usleep(PROGRESS_REFRESH_MS * 1000);
    }

    /* 最终状态 */
    float final_cursor;
    if (atomic_load(&g_is_paused)) {
        final_cursor = g_paused_position;
    } else {
        ma_sound_get_cursor_in_seconds(&g_sound, &final_cursor);
    }
    draw_progress(final_cursor, duration);
    printf("\n\n");

    if (finished) {
        printf("Finished.\n");
    } else {
        printf("Stopped.\n");
    }

    printf("Press Enter to continue...\n");
    fflush(stdout);

    /* 等待 Enter（临时切回规范模式） */
    terminal_raw_disable();
    while (getchar() != '\n' && !g_terminate);
    terminal_raw_enable();

    cleanup();
    return finished ? 0 : 2;
}

/* ====================================================================
 * 扫描当前目录下的 .mp3 文件
 * ==================================================================== */
static int scan_directory(char files[][MAX_FILENAME_LEN], int max) {
    DIR *d = opendir(".");
    if (!d) {
        fprintf(stderr, "[ERR] Cannot open current directory\n");
        return 0;
    }

    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) && count < max) {
        size_t len = strlen(e->d_name);
        if (len <= 4) continue;
        const char *ext = e->d_name + len - 4;
        if (strcasecmp(ext, ".mp3") != 0) continue;

        strncpy(files[count], e->d_name, MAX_FILENAME_LEN - 1);
        files[count][MAX_FILENAME_LEN - 1] = '\0';
        count++;
    }
    closedir(d);
    return count;
}

/* ====================================================================
 * 入口
 * ==================================================================== */
int main(int argc, char *argv[]) {
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    char filelist[MAX_MP3_FILES][MAX_FILENAME_LEN];
    char *target = NULL;

    if (argc > 1) {
        /* 直接播放参数指定的文件 */
        target = argv[1];
    } else {
        /* 扫描当前目录 */
        int n = scan_directory(filelist, MAX_MP3_FILES);

        if (n == 0) {
            printf("No .mp3 files in current directory.\n");
            printf("Usage: %s [file.mp3]\n", argv[0]);
            return 1;
        }

        printf("Found %d MP3 file(s):\n\n", n);
        for (int i = 0; i < n; i++) {
            struct stat st;
            char sz[32] = "?";
            if (stat(filelist[i], &st) == 0) {
                double mb = st.st_size / (1024.0 * 1024.0);
                if (mb >= 1.0)
                    snprintf(sz, sizeof(sz), "%.1f MB", mb);
                else
                    snprintf(sz, sizeof(sz), "%.1f KB",
                             st.st_size / 1024.0);
            }
            printf("  %2d. %s  (%s)\n", i + 1, filelist[i], sz);
        }
        printf("\n   0. Play all\n");
        printf("   q. Quit\n\n");
        printf("Select: ");
        fflush(stdout);

        char input[32];
        if (!fgets(input, sizeof(input), stdin))
            return 0;
        input[strcspn(input, "\n")] = 0;

        if (input[0] == 'q' || input[0] == 'Q')
            return 0;

        int sel = atoi(input);

        if (sel == 0) {
            /* 全部播放 */
            printf("\nPlaying all %d files...\n\n", n);
            for (int i = 0;
                 i < n && !g_terminate && !atomic_load(&g_exit_requested);
                 i++) {
                printf("-- %d/%d --\n", i + 1, n);
                terminal_raw_enable();
                pthread_t tid;
                pthread_create(&tid, NULL, input_thread, NULL);
                pthread_detach(tid);
                play_file(filelist[i]);
            }
            printf("Done!\n");
            return 0;
        }

        if (sel < 1 || sel > n) {
            printf("Invalid selection.\n");
            return 1;
        }
        target = filelist[sel - 1];
    }

    if (!target) return 0;

    if (access(target, F_OK) != 0) {
        fprintf(stderr, "[ERR] File not found: %s\n", target);
        return 1;
    }

    /* 进入播放模式 */
    terminal_raw_enable();

    pthread_t tid;
    pthread_create(&tid, NULL, input_thread, NULL);
    pthread_detach(tid);

    int ret = play_file(target);

    /* 确保线程退出 */
    atomic_store(&g_exit_requested, true);
    cleanup();

    return ret;
}
