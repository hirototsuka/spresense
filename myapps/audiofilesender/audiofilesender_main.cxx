/****************************************************************************
 * Adapted from various Nuttx and Sony sources
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <audiolite/al_wavenc.h>
#include <nuttx/crc32.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_NUM_FILES       (100)
#define FILENAME_FORMAT_WAV "%s_%02d.wav"
#define FILENAME_FORMAT_MD  "%s_md.txt"

#define FRAME_FLAG  (0x7E)
#define ESCAPE_BYTE (0x7D)
#define ESCAPE_XOR  (0x20)

#define MIN(a, b)     (((a) < (b)) ? (a) : (b))
#define MAX(a, b)     (((a) > (b)) ? (a) : (b))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define CRC32_INIT_VALUE (0xFFFFFFFF)

#define COLOUR_R "\033[31m"
#define COLOUR_G "\033[32m"
#define COLOUR_Y "\033[33m"
#define COLOUR_B "\033[34m"
#define COLOUR_M "\033[35m"
#define COLOUR_C "\033[36m"
#define COLOUR_W "\033[37m"
#define COLOUR_X "\033[0m"

/****************************************************************************
 * Frame Data Structure
 ****************************************************************************/

enum frame_type {
    FRAME_TYPE_CONTROL = 0,
    FRAME_TYPE_TRANSMISSION_START,
    FRAME_TYPE_TRANSMISSION_END,
    FRAME_TYPE_FILE_DATA,
    FRAME_TYPE_FILE_START,
    FRAME_TYPE_FILE_END,
};

typedef struct __attribute__((__packed__)) META_FRAME_DATA {
    uint32_t crc;          // calculated for the data
    uint32_t frame_number; // increments every time
    uint32_t payload_size; // size of the payload
    uint8_t  device_id;    // unique for device
    uint8_t  version;      // software version
    uint8_t  frame_type;   // data, control, etc.
    uint8_t  rsv_byte_1;   // to be used for later implementations
} meta_frame_data_t;

#define METADATA_SIZE                 sizeof(meta_frame_data_t)
#define FRAME_HEADER_AND_TRAILER_SIZE (2)
#define FRAME_BUFFER_PAYLOAD_SIZE     (256)
#define UNPROCESSED_FRAME_SIZE        (METADATA_SIZE + FRAME_BUFFER_PAYLOAD_SIZE)
#define PROCESSED_FRAME_SIZE          (FRAME_HEADER_AND_TRAILER_SIZE + 2 * (UNPROCESSED_FRAME_SIZE))

/****************************************************************************
 * Progress Tracker Structure
 ****************************************************************************/

typedef struct {
    clock_t start_time;        // Start time of the process
    clock_t last_update_time;  // Time of the last update
    size_t  total_steps;       // Total number of steps
    size_t  current_step;      // Current step
    size_t  next_update_step;  // Next step at which to print progress
    double  update_interval_s; // Desired update interval (seconds)
} progress_tracker_t;

#define PROGRESS_BAR_WIDTH (50)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint8_t unprocessed_frame_buffer[UNPROCESSED_FRAME_SIZE];
static uint8_t processed_frame_buffer[PROCESSED_FRAME_SIZE];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void
format_steps(size_t value, char *buffer, size_t buffer_size)
{
    const char  *units[]      = { "", "K", "M", "G", "T", "P", "E", "Z", "Y" };
    double       scaled_value = (double)value;
    unsigned int unit_index   = 0;

    while (scaled_value >= 1000.0 && unit_index < (ARRAY_SIZE(units)) - 1) {
        scaled_value /= 1000.0;
        unit_index++;
    }

    snprintf(buffer, buffer_size, "%6.2f%s", scaled_value, units[unit_index]);
}

static void
initialise_progress_tracker(progress_tracker_t *tracker, size_t total_steps, double update_interval_s, size_t next_update_step)
{
    tracker->start_time        = clock();
    tracker->last_update_time  = tracker->start_time;
    tracker->total_steps       = total_steps;
    tracker->current_step      = 0;
    tracker->next_update_step  = next_update_step;
    tracker->update_interval_s = update_interval_s;
}

static void
print_progress_tracker(progress_tracker_t *tracker, const char *step_name)
{
    clock_t now          = clock();
    double  elapsed_time = (double)(now - tracker->start_time) / CLOCKS_PER_SEC;

    double avg_time_per_step = elapsed_time / tracker->current_step;

    tracker->next_update_step = tracker->current_step + (size_t)(tracker->update_interval_s / avg_time_per_step);

    double percentage           = ((double)tracker->current_step / tracker->total_steps) * 100.0;
    double estimated_total_time = avg_time_per_step * tracker->total_steps;
    double time_remaining       = estimated_total_time - elapsed_time;

    int filled_width = (int)(PROGRESS_BAR_WIDTH * percentage / 100.0);
    int bar_width    = PROGRESS_BAR_WIDTH;

    printf("\r");

    printf("%s[", COLOUR_X);
    for (int i = 0; i < bar_width; i++) {
        char symbol = (i < filled_width) ? '#' : '.';
        printf("%s%c", COLOUR_G, symbol);
    }
    printf("%s]", COLOUR_X);

    char current_step_buffer[10];
    char total_steps_buffer[10];
    format_steps(tracker->current_step, current_step_buffer, sizeof(current_step_buffer));
    format_steps(tracker->total_steps, total_steps_buffer, sizeof(total_steps_buffer));

    printf("[%10s][%s/%s][%5.1f%%][%6.1fs][%6.1fs]", step_name, current_step_buffer, total_steps_buffer, percentage, elapsed_time, time_remaining);
    fflush(stdout);

    tracker->last_update_time = now;
}

static void
finalise_progress_tracker(progress_tracker_t *tracker, const char *step_name)
{
    print_progress_tracker(tracker, step_name);
    printf("\n");
    if (tracker->current_step < tracker->total_steps) {
        printf("Progress tracker did not reach the end\n");
        printf("Progress tracker likely set up incorrectly\n");
    }
    printf("[%s] Finished\n", step_name);
}

static int
send_data(uint8_t *data, size_t len, int sock)
{
    size_t  sent_sz = 0;
    ssize_t sz;
    int     retry_rem = 0;

    if (sock >= 0) {
        while (len > sent_sz) {
            sz = send(sock, (void *)(data + sent_sz), len - sent_sz, 0);
            if (sz < 0) {
                printf("Error sending data: %d/%d\n", errno, sz);
                return sz;
            } else if (sz == 0 && retry_rem-- <= 0) {
                printf("Error sending data: 0 bytes sent\n");
                return -1;
            }
            sent_sz += sz;
        }
    }

    return sent_sz;
}

static int
connect_to_server(const char *ip, int server_port)
{
    int sock;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return -errno;
    }

    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = inet_addr(ip);
    sin.sin_port        = htons(server_port);

    if (connect(sock, (const struct sockaddr *)&sin, sizeof(sin)) < 0) {
        close(sock);
        return -errno;
    }

    return sock;
}

static int
disconnect_from_server(int sock)
{
    char end_mark[] = "12345678";
    if (sock >= 0) {
        sleep(1);
        send_data((uint8_t *)end_mark, strlen(end_mark), sock);
        sleep(1);
        return close(sock);
    }

    return 0;
}

static size_t
frame_data(uint8_t *frame_buffer, const uint8_t *unbuffered, size_t unbuffered_length)
{
    size_t length_target = MIN(UNPROCESSED_FRAME_SIZE, unbuffered_length);
    size_t frame_length  = 0;

    frame_buffer[frame_length++] = FRAME_FLAG;

    for (size_t i = 0; i < length_target; i++) {
        uint8_t byte = unbuffered[i];
        if (byte == FRAME_FLAG || byte == ESCAPE_BYTE) {
            frame_buffer[frame_length++] = ESCAPE_BYTE;
            frame_buffer[frame_length++] = byte ^ ESCAPE_XOR;
        } else {
            frame_buffer[frame_length++] = byte;
        }
    }

    frame_buffer[frame_length++] = FRAME_FLAG;

    if (frame_length > PROCESSED_FRAME_SIZE) {
        printf("Error: Frame buffer too small: %zu/%zu\n", frame_length, PROCESSED_FRAME_SIZE);
        return 0;
    }

    return frame_length;
}

static void
send_file_over_network(char *fname, int sock, meta_frame_data_t *meta_frame_data)
{
    FILE  *src_fp;
    int    dst_fp;
    size_t frame_length;
    size_t read_length;
    char   temp_filename[AL_WAVENC_FNAMELEN + 1];
    size_t iter_bytes = 1 << 19;

    snprintf(temp_filename, sizeof(temp_filename), "%sT", fname);

    printf("Attempting to send file: %s\n", fname);

    src_fp = fopen(fname, "rb");
    if (src_fp == NULL) {
        printf("Failed to open file: %s\n", fname);
        return;
    }

    dst_fp = open(temp_filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (dst_fp == -1) {
        printf("Failed to create temporary file: %s\n", temp_filename);
        fclose(src_fp);
        return;
    }

    meta_frame_data->frame_number++;
    meta_frame_data->frame_type   = FRAME_TYPE_FILE_START;
    meta_frame_data->payload_size = 0;

    meta_frame_data->crc = crc32part((uint8_t *)(meta_frame_data) + sizeof(meta_frame_data->crc),
                                     sizeof(*meta_frame_data) - sizeof(meta_frame_data->crc),
                                     CRC32_INIT_VALUE);

    frame_length = frame_data(processed_frame_buffer, (uint8_t *)meta_frame_data, METADATA_SIZE);
    send_data(processed_frame_buffer, frame_length, sock);

    printf("[%" PRIu32 "]Sent file start frame\n", meta_frame_data->frame_number);

    progress_tracker_t framing_tracker;

    fseek(src_fp, 0, SEEK_END);
    size_t total_bytes = ftell(src_fp);
    fseek(src_fp, 0, SEEK_SET);

    initialise_progress_tracker(&framing_tracker, total_bytes, 1.0, iter_bytes / 2);

    while ((read_length = fread(unprocessed_frame_buffer + METADATA_SIZE, 1, FRAME_BUFFER_PAYLOAD_SIZE, src_fp)) > 0) {
        meta_frame_data->frame_number++;
        meta_frame_data->frame_type   = FRAME_TYPE_FILE_DATA;
        meta_frame_data->payload_size = read_length;

        meta_frame_data->crc = crc32part((uint8_t *)(meta_frame_data) + sizeof(meta_frame_data->crc),
                                         sizeof(*meta_frame_data) - sizeof(meta_frame_data->crc),
                                         CRC32_INIT_VALUE);
        meta_frame_data->crc = crc32part(unprocessed_frame_buffer + sizeof(*meta_frame_data),
                                         meta_frame_data->payload_size,
                                         meta_frame_data->crc);

        memcpy((void *)unprocessed_frame_buffer, (void *)meta_frame_data, METADATA_SIZE);

        frame_length = frame_data(processed_frame_buffer, (uint8_t *)unprocessed_frame_buffer, METADATA_SIZE + read_length);

        size_t  total_written = 0;
        ssize_t written;

        while (total_written < frame_length) {
            written = write(dst_fp, processed_frame_buffer + total_written, frame_length - total_written);
            if (written <= 0) {
                printf("Error writing to temporary file: %s\n", temp_filename);
                fclose(src_fp);
                close(dst_fp);
                unlink(temp_filename);
                return;
            }
            total_written += written;
        }

        framing_tracker.current_step += read_length;

        if (framing_tracker.current_step >= framing_tracker.next_update_step) {
            print_progress_tracker(&framing_tracker, "Framing");
        }
    }

    finalise_progress_tracker(&framing_tracker, "Framing");

    fclose(src_fp);
    close(dst_fp);

    dst_fp = open(temp_filename, O_RDONLY);
    if (dst_fp == -1) {
        printf("Failed to open temporary file for reading: %s\n", temp_filename);
        unlink(temp_filename);
        return;
    }

    off_t       offset = 0;
    struct stat stat_buf;
    if (fstat(dst_fp, &stat_buf) == -1) {
        printf("Failed to stat temporary file: %s\n", temp_filename);
        close(dst_fp);
        unlink(temp_filename);
        return;
    }

    progress_tracker_t sending_tracker;
    initialise_progress_tracker(&sending_tracker, stat_buf.st_size, 1.0, iter_bytes / 2);
    printf("Sending file data frames\n");
    ssize_t sent_bytes;
    while (offset < stat_buf.st_size) {
        size_t count = MIN(iter_bytes, (size_t)(stat_buf.st_size - offset));
        sent_bytes   = sendfile(sock, dst_fp, &offset, count);
        if (sent_bytes <= 0) {
            printf("Error sending file using sendfile: %s\n", temp_filename);
            close(dst_fp);
            unlink(temp_filename);
            return;
        }

        sending_tracker.current_step += sent_bytes;
        if (sending_tracker.current_step >= sending_tracker.next_update_step) {
            print_progress_tracker(&sending_tracker, "Sending");
        }
    }

    finalise_progress_tracker(&sending_tracker, "Sending");

    printf("Sent file data frames\n");

    meta_frame_data->frame_type   = FRAME_TYPE_FILE_END;
    meta_frame_data->payload_size = 0;
    meta_frame_data->frame_number++;

    meta_frame_data->crc = crc32part((uint8_t *)(meta_frame_data) + sizeof(meta_frame_data->crc),
                                     sizeof(*meta_frame_data) - sizeof(meta_frame_data->crc),
                                     CRC32_INIT_VALUE);

    frame_length = frame_data(processed_frame_buffer, (uint8_t *)meta_frame_data, METADATA_SIZE);
    send_data(processed_frame_buffer, frame_length, sock);

    printf("[%" PRIu32 "]Sent file end frame\n", meta_frame_data->frame_number);

    close(dst_fp);
    unlink(temp_filename);
}

static void
send_files_over_network(const char *prefix, int sock)
{
    char   fname[AL_WAVENC_FNAMELEN];
    size_t frame_length;

    meta_frame_data_t meta_frame_data = {
        .crc          = 0xBEEFDEAD,
        .frame_number = 0,
        .payload_size = 0,
        .device_id    = 0x1D,
        .version      = 0,
        .frame_type   = 0,
        .rsv_byte_1   = 0,
    };

    printf("Starting transmission\n");

    meta_frame_data.frame_type   = FRAME_TYPE_TRANSMISSION_START;
    meta_frame_data.payload_size = 0;
    meta_frame_data.frame_number++;
    meta_frame_data.crc = crc32part((uint8_t *)(&meta_frame_data) + sizeof(meta_frame_data.crc),
                                    sizeof(meta_frame_data) - sizeof(meta_frame_data.crc),
                                    CRC32_INIT_VALUE);

    memcpy((void *)unprocessed_frame_buffer, (void *)&meta_frame_data, METADATA_SIZE);
    frame_length = frame_data(processed_frame_buffer, (uint8_t *)&meta_frame_data, METADATA_SIZE);
    send_data(processed_frame_buffer, frame_length, sock);

    printf("[%" PRIu32 "]Sent transmission start frame\n", meta_frame_data.frame_number);

    snprintf(fname, AL_WAVENC_FNAMELEN, FILENAME_FORMAT_MD, prefix);
    send_file_over_network(fname, sock, &meta_frame_data);

    for (int i = 0; i < MAX_NUM_FILES; i++) {
        snprintf(fname, AL_WAVENC_FNAMELEN, FILENAME_FORMAT_WAV, prefix, i);
        if (access(fname, F_OK) == OK) {
            send_file_over_network(fname, sock, &meta_frame_data);
        }
    }

    meta_frame_data.frame_type   = FRAME_TYPE_TRANSMISSION_END;
    meta_frame_data.payload_size = 0;
    meta_frame_data.frame_number++;
    meta_frame_data.crc = crc32part((uint8_t *)(&meta_frame_data) + sizeof(meta_frame_data.crc),
                                    sizeof(meta_frame_data) - sizeof(meta_frame_data.crc),
                                    CRC32_INIT_VALUE);

    memcpy((void *)unprocessed_frame_buffer, (void *)&meta_frame_data, METADATA_SIZE);
    frame_length = frame_data(processed_frame_buffer, (uint8_t *)&meta_frame_data, METADATA_SIZE);
    send_data(processed_frame_buffer, frame_length, sock);

    printf("[%" PRIu32 "]Sent transmission end frame\n", meta_frame_data.frame_number);
}

static bool
parse_arguments(int argc, char *argv[], const char **file_prefix, const char **ip_address, int *port)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--prefix") == 0 && i + 1 < argc) {
            *file_prefix = argv[++i];
        } else if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
            *ip_address = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            *port = atoi(argv[++i]);
        } else {
            printf("Unknown or incomplete flag: %s\n", argv[i]);
            return false;
        }
    }

    if (!(*file_prefix) || !(*ip_address) || *port <= 0) {
        printf("Missing required arguments.\n");
        return false;
    }

    return true;
}

static void
print_usage(const char *program_name)
{
    printf("Usage: nsh> %s"
           " --prefix <Recording wav file prefix>"
           " --ip <IP address of the server>"
           " --port <Port number of the server>"
           "\n"
           "Example: nsh> %s"
           " --prefix /mnt/sd0/rec/X"
           " --ip 192.168.X.X"
           " --port 12345"
           "\n",
           program_name, program_name);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

extern "C" int
main(int argc, FAR char *argv[])
{
    int         ret = 0;
    int         sock;
    int         port        = -1;
    const char *file_prefix = NULL;
    const char *ip_address  = NULL;

    if (!parse_arguments(argc, argv, &file_prefix, &ip_address, &port)) {
        print_usage(argv[0]);
        return -1;
    }

    sock = connect_to_server(ip_address, port);
    if (sock < 0) {
        printf("Failed to connect to server: %d\n", sock);
        return sock;
    }

    send_files_over_network(file_prefix, sock);

    ret = disconnect_from_server(sock);
    if (ret != OK) {
        printf("Failed to disconnect from server: %d\n", ret);
    }

    return 0;
}
