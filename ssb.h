#ifndef SSB_H

#define SND_ALSA

typedef struct dirent dirent;

typedef struct netspec
{
	unsigned long tx;
	unsigned long rx;
} netspec;

#ifdef SND_PULSEAUDIO
typedef struct audio_handles
{
	pa_threaded_mainloop *mainloop;
	pa_mainloop_api *mainloop_api;
	pa_context *context;
} audio_handles;

typedef struct audio_state
{
	int id;
	int volume;
	int ready;
	char str[16];
} audio_state;
#endif

typedef struct block_cfg
{
	int (*func)();
	void (*init)(); // function called once before the program start
	int freq; // how often to refresh in ms
} block_cfg;

typedef struct block
{
	block_cfg *cfg;
	int elapsed; // how much time has passed since last update
	char *str; // where it's going to store it's block string
} block;

#define ARRAY_LEN(x) ((sizeof((x)))/(sizeof((*(x)))))
#define SEC_IN_MS(x) ((x) * 1000)
#define MIN_IN_MS(x) ((x) * 60 * 1000)

#ifdef SND_PULSEAUDIO
static void sinks_callback(pa_context *c, const pa_sink_info *i, int eol, void *uptr);
static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *uptr);
static void state_callback(pa_context *c, void *uptr);
static void ssb_init_audio();
#endif

static int ssb_memory_get_number(char *meminfo, char *field);
static int ssb_mail_get_new(char *dir_name, char *dir_mail);
static void ssb_blocks_init();
static int ssb_get_weather(void *arg);

static int ssb_init();
static int ssb_run();

static int ssb_get_date();
static int ssb_get_memory();
static int ssb_get_disk();
static int ssb_get_battery();
static int ssb_get_network();
static int ssb_get_mail();
#ifdef SND_PULSEAUDIO
static int ssb_get_audio();
#endif
static int ssb_get_weather_wrapper();

static int ssb_xset_root();

/*
 * Config part:
 * 	add a entry to the array if you want to add a block to the statusbar
 * 	the order of blocks is dictated by index in the array, block with index
 * 	0 is the left most and then going right
 *
 * 	if you will refresh the block yourself, the freq should be 0
 */

block_cfg config[] = {
	{ssb_get_mail, 0, SEC_IN_MS(5)},
	{ssb_get_network, 0, SEC_IN_MS(1)},
	{ssb_get_weather_wrapper, 0, MIN_IN_MS(5)},
	{ssb_get_memory, 0, SEC_IN_MS(1)},
	{ssb_get_disk, 0, SEC_IN_MS(5)},
#ifdef SND_PULSEAUDIO
	{ssb_get_audio, ssb_init_audio, 0},
#endif
	{ssb_get_battery, 0, SEC_IN_MS(5)},
	{ssb_get_date, 0, SEC_IN_MS(1)},
};

static const char *rsep = "]";
static const char *lsep = "[";

#define SSB_H
#endif
