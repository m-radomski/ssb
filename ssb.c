#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <dirent.h>
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#include <sys/time.h>

#include <X11/Xlib.h>

#include "network.c" // TODO(matt): get rid of it/rewrite

typedef struct dirent dirent;
Display *display = 0;

#define SEP " | "
#define POWER_PATH "/sys/class/power_supply/"
#define BUFFER_SIZE 128

static int
ssb_xset_root(const char *name)
{
	if(!display)
	{
		fprintf(stderr, "Failed open a X window\n");
		return 1;
	}

	int screen_id = XDefaultScreen(display);
	Window root_window = XRootWindow(display, screen_id);
	XStoreName(display, root_window, name);
	XFlush(display);

	return 0;
}

static int
ssb_name_add_date(char *name)
{
	time_t date = time(0);
	struct tm *datef = localtime(&date);
	char date_str[128] = { 0 };

	sprintf(date_str, "%.02d:%.02d:%.02d %.02d-%.02d-%d",
			  datef->tm_hour, datef->tm_min, datef->tm_sec,
			  datef->tm_mday, datef->tm_mon + 1, datef->tm_year + 1900);

	strcat(name, date_str);
	return strlen(date_str);
}

static int
ssb_name_add_memory(char *name)
{
	struct sysinfo systemif = { 0 };
	sysinfo(&systemif);

	float ram_used = (float)(systemif.bufferram + systemif.freeram) / (float)(1 << 20);
	float ram_total = (float)(systemif.totalram) / (float)(1 << 30);
	char ram_str[16] = { 0 };
	if(ram_used >= 1024.0)
		sprintf(ram_str, "%.01fGB/%.01fGB", ram_used / (float)(1 << 10), ram_total);
	else
		sprintf(ram_str, "%.01fMB/%.01fGB", ram_used, ram_total);

	strcat(name, ram_str);
	return strlen(ram_str);
}

static int
ssb_name_add_disk(char *name)
{
	struct statfs statistics = { 0 };
	statfs("/", &statistics);

	float disk_free = (float)(statistics.f_bavail * statistics.f_bsize);
	disk_free = disk_free / (float)(1 << 30);
	char disk_str[16] = { 0 };

	sprintf(disk_str, "%.01fGB", disk_free);

	strcat(name, disk_str);
	return strlen(disk_str);
	return 0;
}

static int
ssb_name_add_battery(char *name)
{
	int dir_fd = open(POWER_PATH, O_RDONLY);
	if(dir_fd == -1)
		return -1;

	DIR *dir = fdopendir(dir_fd);
	if(!dir)
		return -1;

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
				return -1;

			memset(buffer, 0, BUFFER_SIZE);
			read(cap_file, buffer, 8);

			capacity +=  atoi(buffer);

			memset(buffer, 0, BUFFER_SIZE);
			strcpy(buffer, POWER_PATH);

			strcat(buffer, file->d_name);
			strcat(buffer, "/status");

			int state_file = open(buffer, O_RDONLY);
			if(state_file == -1)
				return -1;

			memset(buffer, 0, BUFFER_SIZE);
			read(state_file, buffer, BUFFER_SIZE / 4);

			if(strstr(buffer, "Charging") ||
				strstr(buffer, "Discharging"))
			{
				working_bat = battery_count + 1;
				working_state = buffer[0];
				printf("%s at %s", file->d_name, buffer);
			}
		}
	}

	closedir(dir);
	close(dir_fd);

	char battery_str[32] = { 0 };
	sprintf(battery_str, "%d: %c@%d%%",
			  working_bat, working_state, capacity / battery_count);

	strcat(name, battery_str);
	return strlen(battery_str);
}

static int
ssb_name_add_network(char *name)
{
	int len = network_get(name);

	return len;
}

static int
ssb_name_add_audio(char *name)
{
	FILE *output = popen("pacmd list-sinks | awk '/volume: front/ {vol=$5};/name:/ {count ++} END {print count \": \" vol}'", "r");

	if(!output)
		return -1;

	char audio_str[16] = { 0 };
	char *str_ptr = audio_str;
	char c = 0;
	while((c = fgetc(output)) != EOF)
		*(str_ptr++) = c;

	char *newline = strchr(audio_str, '\n');
	if(newline)
		*newline = '\0';

	strcat(name, audio_str);
	return strlen(audio_str);
}

int main()
{
	display = XOpenDisplay(0);

	while(1)
	{
		struct timespec start = { 0 };
		clock_gettime(CLOCK_REALTIME, &start);

		char current_status[128] = { 0 };
		int status_len = 0;

		/* status_len += ssb_name_add_network(current_status); */
		/* strcat(current_status, SEP); */

		status_len += ssb_name_add_memory(current_status);
		strcat(current_status, SEP);

		status_len += ssb_name_add_disk(current_status);
		strcat(current_status, SEP);

		status_len += ssb_name_add_audio(current_status);
		strcat(current_status, SEP);

		status_len += ssb_name_add_battery(current_status);
		strcat(current_status, SEP);

		status_len += ssb_name_add_date(current_status);

		ssb_xset_root(current_status);

		struct timespec end = { 0 };
		clock_gettime(CLOCK_REALTIME, &end);

		usleep(1000000 - (end.tv_nsec - start.tv_nsec) / 1000);
	}

	return 0;
}
