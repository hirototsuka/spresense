/****************************************************************************
 * Adapted from various Nuttx and Sony sources
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>

#include <audiolite/audiolite.h>

#include "event_str.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NUM_CHANNELS        (8)
#define SAMPLING_RATE_HZ    (48000)
#define SAMPLE_BW_BYTES     (2)
#define SAMPLE_BW_BITS      (8 * SAMPLE_BW_BYTES)
#define MEM_BLK_NUM_SAMPLES (1024)
#define MEM_BLK_SIZE        (MEM_BLK_NUM_SAMPLES * NUM_CHANNELS * SAMPLE_BW_BYTES)
#define MEM_BLK_NUM_BYTES   (4096 * 32) /* from original Spresense code */
#define NUM_MEM_BLOCKS      (MEM_BLK_NUM_BYTES / MEM_BLK_SIZE)

#define MAX_NUM_FILES       (100)
#define FILENAME_FORMAT_WAV "%s_%02d.wav"
#define FILENAME_FORMAT_MD  "%s_md.txt"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_stopflag = false;

/****************************************************************************
 * Private Classes
 ****************************************************************************/

/* For Event receiving */

class wave_encoder_listener : public audiolite_eventlistener {
public:
    void
    on_event(int evt, audiolite_component *cmp,
             unsigned long arg)
    {
        printf("\nEvent %s has occurred: %d\n", convert_evtid(evt), (int)arg);
        if (evt == AL_EVENT_STOPINPUT) {
            g_stopflag = true;
            printf("Stop recording event detected\n");
        }
    }
};

class interceptor : public audiolite_component {
public:
    uint64_t timestamp = 0x1111DEADBEEF1111;
    void
    on_data()
    {
        audiolite_memapbuf *mem = (audiolite_memapbuf *)pop_data();

        if (mem) {
            /* int16_t *data    = (int16_t *)mem->get_data(); */
            /* int      samples = mem->get_storedsize() / mem->get_channels() / sizeof(int16_t); */

            /* printf("DL %d : %6d %6d %6d %6d\n", samples, */
            /*        data[0], data[1], data[2], data[3]); */

            push_data(mem);
            mem->release();
        }
    }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int
remove_existing_files(const char *prefix)
{
    int  ret = 0;
    char fname[AL_WAVENC_FNAMELEN];

    for (int i = 0; i < MAX_NUM_FILES; i++) {
        snprintf(fname, AL_WAVENC_FNAMELEN, FILENAME_FORMAT_WAV, prefix, i);
        if (access(fname, F_OK) == OK) {
            printf("Removing existing file: %s\n", fname);
            ret = remove(fname);
            if (ret != OK) {
                printf("Failed to remove file: %s\n", fname);
                return ret;
            }
        }
    }

    snprintf(fname, AL_WAVENC_FNAMELEN, FILENAME_FORMAT_MD, prefix);
    if (access(fname, F_OK) == OK) {
        printf("Removing existing file: %s\n", fname);
        ret = remove(fname);
        if (ret != OK) {
            printf("Failed to remove file: %s\n", fname);
            return ret;
        }
    }

    return ret;
}

static int
write_metadata_file(const char *prefix, uint64_t timestamp)
{
    int   ret = 0;
    char  fname_metadata[AL_WAVENC_FNAMELEN];
    char  fname_collection[AL_WAVENC_FNAMELEN];
    FILE *fp;

    snprintf(fname_metadata, AL_WAVENC_FNAMELEN, FILENAME_FORMAT_MD, prefix);

    fp = fopen(fname_metadata, "w");
    if (fp == NULL) {
        printf("Failed to open file: %s\n", fname_metadata);
        return -1;
    }

    fprintf(fp, "Timestamp: %" PRIu64 "\n", timestamp);
    fprintf(fp, "Sampling Rate: %dHz\n", SAMPLING_RATE_HZ);
    fprintf(fp, "Sample Bitwidth: %d bits\n", SAMPLE_BW_BITS);
    fprintf(fp, "Channels: %d\n", NUM_CHANNELS);
    fprintf(fp, "File list:\n");

    fprintf(fp, "%s\n", fname_metadata);

    for (int i = 0; i < MAX_NUM_FILES; i++) {
        snprintf(fname_collection, AL_WAVENC_FNAMELEN, FILENAME_FORMAT_WAV, prefix, i);

        ret = access(fname_collection, F_OK);
        if (ret == OK) {
            fprintf(fp, "%s\n", fname_collection);
        }
    }

    ret = fclose(fp);

    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

extern "C" int
main(int argc, FAR char *argv[])
{
    int                   ret = 0;
    int                   record_time_sec;
    wave_encoder_listener lsn;

#define FILE_PREFIX_INDEX (1)
#define RECORD_TIME_INDEX (2)

    if (argc != 3) {
        printf("Usage nsh> %s"
               " <Recording wav file prefix>"
               " <Recording time in seconds>"
               "\n"
               "Example: %s"
               " /mnt/sd0/rec/X"
               " 10"
               "\n",
               argv[0], argv[0]);
        return -1;
    }

    /* To Create below structure.
     *
     * +------------------------------+
     * | wave_encoder_listener events |
     * +------------------------------+
     *     ^            ^           ^
     *     |            |           |
     * +-------+    +-------+    +-----+    +--------+
     * | Audio |    | Data  |    | WAV |    | File   |
     * | Input | -> |Monitor| -> | Enc | -> | Stream |
     * +-------+    +-------+    +-----+    ---------+
     *       ^
     *       |
     *      +------+
     *      |Memory|
     *      | Pool |
     *      +------+
     */

    g_stopflag = false;

    audiolite_filestream   *fstream   = new audiolite_filestream;
    audiolite_mempoolapbuf *mempool   = new audiolite_mempoolapbuf;
    audiolite_inputcomp    *aindev    = new audiolite_inputcomp;
    audiolite_wavenc       *wavenc    = new audiolite_wavenc;
    interceptor            *intercept = new interceptor;

    ret = remove_existing_files(argv[FILE_PREFIX_INDEX]);
    if (ret != OK) {
        printf("Failed to remove existing files\n");
        goto app_error;
    }

    ret = audiolite_set_systemparam(SAMPLING_RATE_HZ, SAMPLE_BW_BITS, NUM_CHANNELS);
    if (ret != 0) {
        printf("Failed to set system parameters: %d\n", ret);
        goto app_error;
    }
    audiolite_set_evtlistener(&lsn);

    if (!mempool->create_instance(MEM_BLK_SIZE, NUM_MEM_BLOCKS)) {
        printf("Failed to create memory pool\n");
        goto app_error;
    }

    wavenc->set_stream(fstream);
    ret = wavenc->set_fileprefix(argv[FILE_PREFIX_INDEX]);
    if (ret != 0) {
        printf("Failed to set file prefix: %d\n", ret);
        goto app_error;
    }

    aindev->set_mempool(mempool);

    aindev->bind(intercept);
    intercept->bind(wavenc);

    record_time_sec = atoi(argv[RECORD_TIME_INDEX]);

    printf("Recording for %d seconds\n", record_time_sec);

    ret = aindev->start();
    if (ret != OK) {
        printf("Start error..: %d\n", ret);
        goto app_error;
    }

    for (int i = 0; i < record_time_sec; i++) {
        printf("\r%d/%d", i, record_time_sec);
        fflush(stdout);
        if (g_stopflag != false) {
            printf("Stopping recording early\n");
            break;
        }
        sleep(1);
    }

    printf("\n");

    printf("Stopping recording\n");
    aindev->stop();

    write_metadata_file(argv[FILE_PREFIX_INDEX], intercept->timestamp);

app_error:

    /* Clean up */

    aindev->unbindall();
    intercept->unbindall();

    printf("Delete instances\n");

    delete aindev;
    delete wavenc;
    delete mempool;
    delete intercept;
    delete fstream;

    printf("Delete system event handler\n");

    audiolite_eventdestroy();

    return 0;
}
