#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <netdb.h>
#include <threads.h>
#include <errno.h>
#include <assert.h>

#include <fcntl.h>
#include <dirent.h>
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <X11/Xlib.h>
#ifdef SND_PULSEAUDIO
#include <pulse/pulseaudio.h>
#endif

#include "ssb.h"

Display *display = 0;

#define POWER_PATH "/sys/class/power_supply/"
#define INTERFACE_PATH "/sys/class/net/"
#define BUFFER_SIZE 128
#define SIPREFIX "BKMGP"
#define DEBUG 0

// programs globals
block *blocks = 0;

#ifdef SND_PULSEAUDIO
// users globals
audio_state astate;
audio_handles pulse;

static void
sinks_callback(pa_context *c, const pa_sink_info *info, int eol, void *userdata)
{
	if(eol > 0)
		return;

	float vol = (float)info->volume.values[0] / (float)info->base_volume;
	vol = (vol * 100.0f) + 0.5f;

	astate.id = info->index;
	astate.volume = (int)vol;

	ssb_get_audio();
	ssb_xset_root();
}

static void
subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
	pa_context_get_sink_info_by_index(c, idx, sinks_callback, 0);
	fflush(stdin);
}

static void 
state_callback(pa_context *c, void *userdata)
{ }

static void
ssb_init_audio()
{
	pulse.mainloop = pa_threaded_mainloop_new();
	pulse.mainloop_api = pa_threaded_mainloop_get_api(pulse.mainloop);
	pulse.context = pa_context_new(pulse.mainloop_api, "ssb");

	pa_context_flags_t flags = PA_CONTEXT_NOFLAGS;
	pa_context_set_state_callback(pulse.context, state_callback, 0);
	pa_context_set_subscribe_callback(pulse.context, subscribe_cb, 0);

	pa_threaded_mainloop_lock(pulse.mainloop);
	if(pa_threaded_mainloop_start(pulse.mainloop))
		printf("Failed to start the pulseaudio mainloop");

	if(pa_context_connect(pulse.context, 0, flags, 0))
		printf("Failed to connect to pulseaudio");
	pa_threaded_mainloop_unlock(pulse.mainloop);

	//TODO(matt): make it so that when no pulseaudio is working the bar is not cucked
	pa_context_state_t state = 0;

	while(state != PA_CONTEXT_READY)
		state = pa_context_get_state(pulse.context);

	pa_threaded_mainloop_lock(pulse.mainloop);
	pa_context_subscribe(pulse.context, (pa_subscription_mask_t)PA_SUBSCRIPTION_MASK_SINK, 0, 0);
	pa_threaded_mainloop_unlock(pulse.mainloop);

	pa_context_get_sink_info_list(pulse.context, sinks_callback, 0);
}
#endif

static block *
ssb_get_block_by_func(int (*func)())
{
	for(unsigned int i = 0; i < ARRAY_LEN(config); i++)
		if(func == blocks[i].cfg->func)
			return blocks + i;

	return 0;
}

static void
ssb_blocks_init()
{
	int count = ARRAY_LEN(config);
	blocks = calloc(count, sizeof(*blocks));

	for(int i = 0; i < count; i++)
	{
		block *blk = blocks + i;

		blk->cfg = config + i;
		blk->elapsed = 0;
		blk->str = calloc(BUFFER_SIZE, sizeof(*blk->str));

		// If a initialazation function was provided call it
		if(blk->cfg->init != 0)
			blk->cfg->init();
	}

	for(int i = 0; i < count; i++)
		blocks[i].cfg->func();
}

static int
ssb_xset_root()
{
	char current_status[128] = { 0 };

	int count = ARRAY_LEN(config);
	for(int i = 0; i < count; i++)
	{
		block *blk = blocks + i;

		if(strlen(blk->str) != 0)
		{
			strcat(current_status, lsep);
			strcat(current_status, blk->str);
			strcat(current_status, rsep);
		}
	}
	
	int screen_id = XDefaultScreen(display);
	Window root_window = XRootWindow(display, screen_id);
	XStoreName(display, root_window, current_status);
	XFlush(display);

	return 0;
}

static int
ssb_get_date()
{
	time_t date = time(0);
	struct tm *datef = localtime(&date);
	block *blk = ssb_get_block_by_func(ssb_get_date);

	sprintf(blk->str, "%.02d:%.02d:%.02d %.02d-%.02d-%d",
			  datef->tm_hour, datef->tm_min, datef->tm_sec,
			  datef->tm_mday, datef->tm_mon + 1, datef->tm_year + 1900);

	return 0;
}

static int
ssb_memory_get_number(char *meminfo, char *field)
{
	char *line = 0;
	char *endl = 0;
	int result = 0;

	line = strstr(meminfo, field);
	while(!isdigit(line[0]))
		++line;
	endl = strchr(line, '\n');
	*endl = '\0';
	
	result = atoi(line);

	*endl = '\n';

	return result;
}

static int
ssb_get_memory()
{
	int fd = open("/proc/meminfo", O_RDONLY);

	int content_len = 0;
	char *content = malloc(sizeof(*content) * BUFFER_SIZE);

	while(BUFFER_SIZE == read(fd, content + content_len, BUFFER_SIZE))
	{
		content_len += BUFFER_SIZE;
		content = realloc(content, content_len + BUFFER_SIZE);
	}

	close(fd);

	int total = ssb_memory_get_number(content, "MemTotal");
	int unused = ssb_memory_get_number(content, "MemFree");
	int cache = ssb_memory_get_number(content, "Cached");
	int buffer = ssb_memory_get_number(content, "Buffers");

	free(content);

	float ram_used = (float)((total - unused - cache - buffer)) / (float)(1 << 10);
	float ram_total = (float)(total) / (float)(1 << 20);
	block *blk = ssb_get_block_by_func(ssb_get_memory);
	if(ram_used >= 1024.0)
		sprintf(blk->str, "%.01fGB/%.01fGB", ram_used / (float)(1 << 10), ram_total);
	else
		sprintf(blk->str, "%.01fMB/%.01fGB", ram_used, ram_total);

	return 0;
}

static int
ssb_get_disk()
{
	struct statfs statistics = { 0 };
	statfs("/", &statistics);

	float disk_free = (float)(statistics.f_bavail * statistics.f_bsize);
	disk_free = disk_free / (float)(1 << 30);
	block *blk = ssb_get_block_by_func(ssb_get_disk);

	sprintf(blk->str, "%.01fGB", disk_free);

	return 0;
}

static int
ssb_get_battery()
{
	int dir_fd = open(POWER_PATH, O_RDONLY);
	if(dir_fd == -1)
		return 0;

	DIR *dir = fdopendir(dir_fd);
	if(!dir)
		return 0;

	dirent *file;
	int working_bat = 0;
	char working_state = 'S';
	int capacity = 0;
	int battery_count = 0;

	while((file = readdir(dir)) != 0)
	{
		char *bat_path = strstr(file->d_name, "BAT");

		if(bat_path)
		{
			++battery_count;

			char buffer[BUFFER_SIZE] = POWER_PATH;
			strcat(buffer, file->d_name);
			strcat(buffer, "/capacity");

			int cap_file = open(buffer, O_RDONLY);
			if(cap_file == -1)
				return 0;

			memset(buffer, 0, BUFFER_SIZE);
			read(cap_file, buffer, 8);

			capacity +=  atoi(buffer);

			memset(buffer, 0, BUFFER_SIZE);
			strcpy(buffer, POWER_PATH);

			strcat(buffer, file->d_name);
			strcat(buffer, "/status");

			close(cap_file);

			int state_file = open(buffer, O_RDONLY);
			if(state_file == -1)
				return 0;

			memset(buffer, 0, BUFFER_SIZE);
			read(state_file, buffer, BUFFER_SIZE / 4);

			if(strstr(buffer, "Charging") ||
				strstr(buffer, "Discharging"))
			{
				char *line = file->d_name;

				while(!isdigit(line[0]))
					++line;

				working_bat = atoi(line) + 1;
				working_state = buffer[0];
			}

			close(state_file);
		}
	}

	closedir(dir);
	close(dir_fd);

	block *blk = ssb_get_block_by_func(ssb_get_battery);

	sprintf(blk->str, "%d: %c@%d%%",
			  working_bat, working_state, capacity / battery_count);

	return 0;
}

static int
ssb_get_network()
{
	static netspec stat = { 0 };

	int dirfd = open(INTERFACE_PATH, O_RDONLY);
	if(dirfd == -1)
	{
		printf("%s", strerror(errno));
		return -1;
	}

	DIR *dir;
	dir = fdopendir(dirfd);

	if(!dir)
	{
		close(dirfd);

		printf("%s", strerror(errno));
		return -1;
	}

	dirent *file = 0;
	errno = 0; // To see if errors occured on readdir()
	// Truncating "." and ".." directories
	for(int i = 0; i < 2; i++)
	{
		file = readdir(dir);

		if(!file && errno != 0)
		{
			printf("%s", strerror(errno));
			return -1;
		}
	}

	int tx = 0;
	int rx = 0;
	int int_count = 0;

	while((file = readdir(dir)) != 0)
	{
		if(!file && errno != 0)
		{
			printf("%s", strerror(errno));
			return -1;
		}

		char path[128] = INTERFACE_PATH;
		strcat(path, file->d_name);
		strcat(path, "/operstate");

		int fd = open(path, O_RDONLY);
		if(fd == -1)
		{
			printf("%s: %s", file->d_name, strerror(errno));
			continue;
		}

		char content[16] = { 0 };
		if(read(fd, content, sizeof(content)) == -1)
		{
			close(fd);
			printf("%s: %s", file->d_name, strerror(errno));
			continue;
		}

		close(fd);

		if(strncmp(content, "up", 2) == 0)
		{
			int_count++;

			char path[128] = INTERFACE_PATH;
			strcat(path, file->d_name);
			strcat(path, "/statistics/tx_bytes");

			int fd = open(path, O_RDONLY);
			if(fd == -1)
				printf("%s: %s", file->d_name, strerror(errno));

			if(read(fd, path, sizeof(path)) == -1)
				printf("%s: %s", file->d_name, strerror(errno));

			close(fd);

			tx += atoi(path);

			memset(path, 0, sizeof(path));
			strcat(path, INTERFACE_PATH);
			strcat(path, file->d_name);
			strcat(path, "/statistics/rx_bytes");

			fd = open(path, O_RDONLY);
			if(fd == -1)
				printf("Failed to open rx_bytes on %s\n", file->d_name);

			read(fd, path, sizeof(path));
			close(fd);

			rx = atoi(path);
		}
	}

	close(dirfd);
	closedir(dir);

	block *blk = ssb_get_block_by_func(ssb_get_network);

	if(int_count != 0)
	{
		if(stat.tx == 0)
			stat.tx = tx;
		if(stat.rx == 0)
			stat.rx = rx;

		unsigned long dtx = (tx - stat.tx) / 1024;
		unsigned long drx = (rx - stat.rx) / 1024;
		int tx_order = 1;
		int rx_order = 1;

		while(dtx >= 1024)
		{
			dtx = dtx / 1024;
			tx_order++;
		}

		while(drx >= 1024)
		{
			drx = drx / 1024;
			rx_order++;
		}

		sprintf(blk->str, "%lu%cB/s %lu%cB/s", drx, SIPREFIX[rx_order],
				  dtx, SIPREFIX[tx_order]);

		stat.tx = tx;
		stat.rx = rx;

	}
	else
		sprintf(blk->str, "N/a N/a");

	return 0;
}

static int
ssb_mail_get_new(char *dir_name, char *dir_mail)
{
	char path[256] = { 0 };
	strcat(path, dir_mail);
	strcat(path, dir_name);
	strcat(path, "/INBOX/new");

	int dirfd = open(path, O_RDONLY);
	if(dirfd == -1)
		return 0;

	DIR *dir = fdopendir(dirfd);
	if(!dir)
		return 0;

	dirent *f = 0;
	int count = 0;

	while((f = readdir(dir)) != 0)
	{
		count++;
	}

	// Take away 2 for the '.' and '..' directories
	count -= 2;

	closedir(dir);
	close(dirfd);

	return count;
}

static int 
ssb_get_mail()
{
	char *path = getenv("MAIL_DIR");
	if(path == 0x0)
	{
		printf("There is no \"MAIL_DIR\" environment variable!\n");
		return 1;
	}

	int dirfd = open(path, O_RDONLY);
	if(dirfd == -1)
		return 1;

	DIR *dir = fdopendir(dirfd);
	if(!dir)
		return 0;

	dirent *f = 0;
	block *blk = ssb_get_block_by_func(ssb_get_mail);
	char *head = blk->str;
	while((f = readdir(dir)) != 0)
	{
		if(f->d_type != DT_DIR) { continue; }

		int mails = ssb_mail_get_new(f->d_name, path);

		if(mails == 0)
			head[0] = '\0';
		else
		{
			sprintf(head, "%d in %s ", mails, f->d_name);
			head += strlen(head);
		}
	}

	closedir(dir);
	close(dirfd);

	int len = strlen(blk->str);
	if(len != 0)
		blk->str[len-1] = '\0'; // Remove the last space

	return 0;
}

#ifdef SND_PULSEAUDIO
static int
ssb_get_audio()
{
	block *blk = ssb_get_block_by_func(ssb_get_audio);

	sprintf(blk->str, "%d: %d%%", astate.id, astate.volume);

	return 0;
}
#endif

static int
ssb_get_weather(void *arg)
{
	int code = 0;
	struct addrinfo hints = { 0 };
	struct addrinfo *server = 0;

	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	// resolve dns name
	if(getaddrinfo("wttr.in", "80", &hints, &server) != 0)
	{
		printf("Error failed to resolve DNS name\n");
		return 1;
	}

	// get socket
	int sock = socket(server->ai_family,
							server->ai_socktype,
							server->ai_protocol);

	if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)))
		printf("Failed to unbind socket\n");

	// connect to server
	connect(sock, server->ai_addr, server->ai_addrlen);
	freeaddrinfo(server);

	const char *request = "GET /?0nAQT HTTP/1.1\r\nHost: wttr.in\r\nUser-Agent: curl/7.67.0\r\nAccept: */*\r\n\r\n";

#if DEBUG
	printf("Sent %ld bytes!\n", send(sock, request, strlen(request), 0));
#else
	send(sock, request, strlen(request), 0);
#endif

	int msg_len = 0;
	char *msg = calloc(BUFFER_SIZE, sizeof(char));

	if(msg == 0x0)
	{
		printf("Failed to allocate memory\n");
		code = 1;
		goto err;
	}

	while(recv(sock, msg + msg_len, BUFFER_SIZE, 0) == BUFFER_SIZE)
	{
		msg_len += BUFFER_SIZE;
		msg = realloc(msg, msg_len + BUFFER_SIZE);
	}
	if(msg == 0x0)
	{
		printf("Failed to recive the response from server\n");
		code = 1;
		goto err;
	}
	close(sock);

	const char *footer = "\r\n\r\n";
	msg = strstr(msg, footer);
	if(msg == 0x0)
	{
		printf("Failed to find the HTTP footer\n");
		code = 1;
		goto err;
	}

	msg += strlen(footer);

	char *line1 = strtok(msg, "\n");
	if(line1 == 0x0)
	{
		printf("The HTTP respone is garbeled in some way\n");
		code = 1;
		goto err;
	}
	char *line2 = strtok(0, "\n");
	if(line2 == 0x0)
	{
		printf("The HTTP respone is garbeled in some way\n");
		code = 1;
		goto err;
	}
	char *t = strchr(line2, 'C');

	if(t != 0x0)
	{
		t[1] = '\0';
	}
	else
	{
		printf("Failed with the weather\n");
		code = 1;
		goto err;
	}

err:;
	block *blk = ssb_get_block_by_func(ssb_get_weather_wrapper);

	if(code == 0)
	{
		sprintf(blk->str, "%s (%s)", line1 + 15, line2 + 15);
	}
	else
	{
		sprintf(blk->str, "wttr.in error");
	}

	return code;
}

static int
ssb_get_weather_wrapper()
{
	static thrd_t t = { 0 };
	
	if(t != thrd_busy)
		thrd_create(&t, ssb_get_weather, 0);

	return 0;
}

static int
ssb_init()
{
	if((display = XOpenDisplay(0)) == 0)
	{
		printf("Failed to open XDisplay\n");
		return -1;
	}

	ssb_blocks_init();
	
	return 0;
}

static int
ssb_run()
{
	for(;;)
	{
		struct timespec start = { 0 };
		if(clock_gettime(CLOCK_REALTIME, &start) != 0)
		{
			printf("%s", strerror(errno));
			return -1;
		}

		bool changed = false;

		for(unsigned int i = 0; i < ARRAY_LEN(config); i++)
		{
			block *blk = blocks + i;

			if(blk->cfg->freq != 0 && (blk->elapsed >= blk->cfg->freq))
			{
				blk->cfg->func();
				blk->elapsed -= blk->cfg->freq;

				changed = true;
			}

			blk->elapsed += 1000/60;
		}

		if(changed)
			ssb_xset_root();

		struct timespec end = { 0 };
		if(clock_gettime(CLOCK_REALTIME, &end) != 0)
		{
			printf("%s", strerror(errno));
			return -1;
		}

#if DEBUG
		printf("The loop took %fms\n",
				  (float)(end.tv_nsec - start.tv_nsec) / 1000);
#endif

		assert(((1000*1000/60) - (end.tv_nsec - start.tv_nsec) / 1000) > 0);
		usleep((1000*1000/60) - (end.tv_nsec - start.tv_nsec) / 1000);
	}

	return 0;
}

int main()
{
	if(ssb_init() != 0)
	{
		printf("Failed to initialze\n");
		exit(EXIT_FAILURE);
	}

	if(ssb_run() != 0)
		exit(EXIT_FAILURE);

	XCloseDisplay(display);

#ifdef SND_PULSEAUDIO
	pa_context_disconnect(pulse.context);
	pa_context_unref(pulse.context);
	pa_threaded_mainloop_free(pulse.mainloop);
#endif

	return EXIT_SUCCESS;
}
