/*
 * Copyright (C) 2016 kjnam100@yahoo.co.kr
 *
 * Philips TEA5767 FM radion module for Raspberry pi
 * http://blog.naver.com/kjnam100/220694105789
 *
 * compile with gcc -lm -lwiringPi -o radio_tea5767 radio_tea5767.c
 */
#include <wiringPi.h> 
#include <wiringPiI2C.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>

#define READY_WAIT_TIME 15000
#define MAX_STATION 256
#define HCC_DEFAULT 1	// High Cut Control
#define SNC_DEFAULT 1	// Stereo Noise Cancelling(Signal dependant stereo)
#define FORCE_MONO 0	// 1: force mono, 0: stereo on
#define SEARCH_MODE_DEFAULT 2	// 1: Low, 2: Middle, 3: High
#define RADIO_STATION_INFO "/var/local/radio/radio_station"
#define TUNED_FREQ "/var/local/radio/tuned_freq"
#define MAX_STATION_NAME_LEN 128

int fd;
int dID = 0x60; // i2c Channel the device is on
unsigned char frequencyH = 0;
unsigned char frequencyL = 0;
unsigned int frequencyB;

char *prog_name;

int	station_info_num = 0;
struct _station_info {
	double freq;
	char name[MAX_STATION_NAME_LEN];
} station_info[MAX_STATION];

int wait_ready(void) 
{
	unsigned char radio[5] = {0};
	int loop;

	loop = 0;
	read(fd, radio, 5);
	while((radio[0] & 0x80) == 0 && loop < 2000000/READY_WAIT_TIME) { // max 2 sec
		usleep(READY_WAIT_TIME);
		read(fd, radio, 5);
		loop++;
	}

	if ((radio[0] & 0x80) == 0)	{ // Ready fail
//		fprintf(stderr, "Ready Fail!\n");
		return -1;
	}
	else if (radio[0] & 0x40) { // Band limit reached
//		fprintf(stderr, " Band limit reached!\n");
		return -2;
	}

//	printf("Level ADC=%d\n", radio[4] >> 4);
	return 0;
}

int find_station_info(double freq)
{
	int i;

	for (i = 0; i < station_info_num; i++) 
		if (freq == station_info[i].freq) return i;

	return -1;
}

int save_freq(double freq)
{
	FILE *fd;
	char s_freq[64];

	if (freq < 76.0  || freq > 108.0) return -1;

	if ((fd = fopen(TUNED_FREQ, "w")) == NULL)
		return -1;

	sprintf(s_freq, "%3.1f", freq);
	fputs(s_freq, fd);

	return 0;
}

void get_status(double *freq, unsigned char *mode, unsigned char *level_adc)
{
	unsigned char radio[5] = {0};

	read(fd, radio, 5);
	*freq = ((((radio[0] & 0x3F) << 8) + radio[1]) * 32768 / 4 - 225000) / 10000;
	*freq = round(*freq * 10.0)/1000.0;
	*freq = round(*freq * 10.0)/10.0;

	*mode = radio[2] & 0x80;
	*level_adc = radio[3] >> 4;
}

void print_status(void)
{
	double freq;
	unsigned char stereo, level_adc;
	int	preset;

	get_status(&freq, &stereo, &level_adc);
	printf("%3.1f MHz %s \tSignal Strength:%d/15\n", freq, stereo ? "stereo" : "mono", level_adc);
	preset = find_station_info(freq);
	if (preset >= 0)
		printf("%s [%d/%d]\n", station_info[preset].name, preset + 1, station_info_num);
}

double get_freq()
{
	unsigned char radio[5] = {0};
	double freq;

	read(fd, radio, 5);
	freq = ((((radio[0] & 0x3F) << 8) + radio[1]) * 32768 / 4 - 225000) / 10000;
	freq = round(freq * 10.0)/1000.0;
	freq = round(freq * 10.0)/10.0;

	return freq;
}

void set_freq(double freq, int hcc, int snc, unsigned char forcd_mono, int mute, int standby)
{
	unsigned char radio[5] = {0};

	frequencyB = 4 * (freq * 1000000 + 225000) / 32768; //calculating PLL word
	frequencyH = frequencyB >> 8;
	frequencyL = frequencyB & 0xFF;

	radio[0] = frequencyH; //FREQUENCY H
	if (mute) radio[0] |= 0x80;
	radio[1] = frequencyL; //FREQUENCY L
	radio[2] = 0xB0; //3 byte (0xB0): high side LO injection is on,.
	if (forcd_mono) radio[2] |= 0x08;
	radio[3] = 0x10; // Xtal is 32.768 kHz
	if (freq < 87.5) radio[3] |= 0x20;
	if (hcc) radio[3] |= 0x04;
	if (snc) radio[3] |= 0x02;
	if (standby) radio[3] |= 0x40;
	radio[4] = 0x40; // deemphasis is 75us in Korea and US

	write(fd, radio, 5);

	if (standby) return;

	save_freq(freq);

	if (wait_ready() < 0) {
		fprintf(stderr, "Fail to tune!\n");
		return;
	}
}

int search(int dir, int mode)
{
	unsigned char radio[5] = {0};
	double freq;

	freq = get_freq();

	if (dir) freq += 0.1;
	else freq -= 0.1;

	if (freq >= 108.0 || freq <= 76.0)
		return -1;

	frequencyB = 4 * (freq * 1000000 + 225000) / 32768; //calculating PLL word
	frequencyH = frequencyB >> 8;
	frequencyH = frequencyH | 0x40; // triggers search
	frequencyL = frequencyB & 0xFF;

	//nothing to set in array #2 as up is the normal search direction
	radio[0] = frequencyH; //FREQUENCY H
	radio[0] |= 0x80; // MUTE
	radio[1] = frequencyL; //FREQUENCY L
	radio[2] = 0x10; // high side LO injection is on,.
	radio[2] |= (mode & 0x03) << 5; // high side LO injection is on,.
	if (dir) radio[2] |= 0x80;	// search up/down
	if (FORCE_MONO) radio[2] |= 0x08; 
	radio[3] = 0x10; // Xtal is 32.768 kHz
	//if (freq < 87.5) radio[3] |= 0x20;
	if (HCC_DEFAULT) radio[3] |= 0x04;
	if (SNC_DEFAULT) radio[3] |= 0x02;
	radio[4] = 0x40; // deemphasis is 75us in Korea and US

	write(fd, radio, 5);

	wait_ready();
	set_freq(get_freq(), HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0); // unmute
	
	return 0;
}

void freq_scan(int mode)
{
	double freq = 87.5;
	unsigned char stereo, level_adc;
	int count = 0;
	struct _radio_station {
		double freq;
		unsigned char stereo;
	} radio_station[MAX_STATION];

	set_freq(freq, HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 1, 0);
	wait_ready();
	do {
		if (search(1, mode)) break;
		get_status(&freq, &stereo, &level_adc);
		if (freq >= 108.0) break;
		radio_station[count].freq = freq;
		radio_station[count].stereo = stereo;
		printf("%2d : %3.1f MHz %s \tSignal Strength:%d/15\n", count + 1, freq, stereo ? "stereo" : "mono", level_adc);
		count++;
	} while(freq < 108.0);

	printf("Total %d radio stations\n", count);
	if (count > 0)
		set_freq(radio_station[0].freq, HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
}

int get_station_info()
{
	FILE *fd;
	size_t len;
	int i, si, n;
	double freq;
	char *line = NULL;
	char s_freq[64];

	station_info_num = 0;
	if ((fd = fopen(RADIO_STATION_INFO, "r")) == NULL)
		return 0;

	while((n = getline(&line, &len, fd)) != EOF) {
		if (line[0] == '#') continue;
		i = 0;

		// get station frequency
		while(isspace(line[i])) i++;
		si = i;
		while(!isspace(line[i])) i++;
		strncpy(s_freq, &line[si], i - si);
		s_freq[i] = 0;
		freq = strtod(s_freq, NULL);
		if (freq < 76.0 || freq > 108.0)
			continue;

		station_info[station_info_num].freq = freq;

		// get station name
		while(isspace(line[i])) i++;
		si = i;
		while(line[i] != 0x0d && line[i] != 0x0a && line[i] != 0 && 
				(i - si) < MAX_STATION_NAME_LEN) i++;
		strncpy(station_info[station_info_num].name, &line[si], i - si);
		station_info[station_info_num].name[i - si] = 0;

		station_info_num++;
	}

	if (line) free(line);
	fclose(fd);

	return station_info_num;
}

double get_tuned_freq(void)
{
	FILE *fd;
	char s_freq[16];
	double freq;

	if ((fd = fopen(TUNED_FREQ, "r")) == NULL)
		return 0;

	fscanf(fd, "%s", s_freq);
	freq = strtod(s_freq, NULL);
	fclose(fd);

	if (freq < 76.0 || freq > 108.0)
		return 0;

	return freq;
}

void preset_move(int dir)
{
	int preset;

	if (station_info_num <= 0) return;

	preset = find_station_info(get_freq());
	if (preset >= 0) {
		if (dir) {
			if (++preset >= station_info_num) preset = 0;
		}
		else {
			if (--preset < 0) preset = station_info_num - 1;
		}
	}
	else preset = 1; 

	set_freq(station_info[preset].freq, HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
}

void usage() 
{
	fprintf(stderr, "Usage:\t%s [set frequency [hcc snc]] [frequency]\n", prog_name);
	fprintf(stderr, "\t%s status\n", prog_name);
	fprintf(stderr, "\t%s prev|next\n", prog_name);
	fprintf(stderr, "\t%s scan [1|2|3] [stereo|mono]\n", prog_name);
	fprintf(stderr, "\t%s up|down [1|2|3] [stereo|mono]\n", prog_name);
	fprintf(stderr, "\t%s stepup|stepdown\n", prog_name);
	fprintf(stderr, "\t%s mute|unmute|on|off\n", prog_name);
	exit(1);
}

int main(int argc, char *argv[])
{
	double freq;
	size_t len;
	int hcc, snc, mode = SEARCH_MODE_DEFAULT;

	prog_name = basename(argv[0]);

	//open access to the board, send error msg if fails
	if((fd = wiringPiI2CSetup(dID)) < 0)
		fprintf(stderr, "error opening i2c channel\n");

	get_station_info();

	if (argc < 2) {
		if ((freq = get_tuned_freq())) {
			set_freq(freq, HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
			print_status();
			printf("HCC: %s, SNC: %s\n", HCC_DEFAULT ? "On" : "Off", SNC_DEFAULT ? "On" : "Off");
		}
		else usage();
		exit(0);
	}

	len = strlen(argv[1]);
	if (!strncmp(argv[1], "set", len)) {
		hcc = HCC_DEFAULT; snc = SNC_DEFAULT;
		if (argc <= 2) usage();
		if (atoi(argv[2]) < 76)	usage(); // frequency
		if (argc >= 4) {
			if (atoi(argv[3]) != 0) hcc = 1;
			else hcc = 0;
			if (argc >= 5) {
				if (atoi(argv[4]) != 0) snc = 1;
				else snc = 0;
			}
		}
		set_freq(strtod(argv[2], NULL), hcc, snc, FORCE_MONO, 0, 0);
		print_status();
		printf("HCC: %s, SNC: %s\n", hcc ? "On" : "Off", snc ? "On" : "Off");
	}
	else if (!strncmp(argv[1], "scan", len)) {
		if (argc > 2) {
			mode = atoi(argv[2]);
			if (mode < 1) mode  = 1;
			else if (mode > 3) mode = 3;
		}
		freq_scan(mode);
	}
	else if (!strncmp(argv[1], "status", len)) {
		print_status();
	}
	else if (!strncmp(argv[1], "next", len)) {
		preset_move(1);
		print_status();
	}
	else if (!strncmp(argv[1], "prev", len)) {
		preset_move(0);
		print_status();
	}
	else if (!strncmp(argv[1], "up", len)) {
		if (argc > 2) {
			mode = atoi(argv[2]);
			if (mode < 1) mode  = 1;
			else if (mode > 3) mode = 3;
		}
		search(1, mode);
		print_status();
		save_freq(get_freq());
	}
	else if (!strncmp(argv[1], "down", len)) {
		if (argc > 2) {
			mode = atoi(argv[2]);
			if (mode < 1) mode  = 1;
			else if (mode > 3) mode = 3;
		}
		search(0, mode);
		print_status();
		save_freq(get_freq());
	}
	else if (!strncmp(argv[1], "stepup", len)) {
		freq = get_freq() + 0.1;
		if (freq < 76.0 || freq > 108.0) exit(2);
		set_freq(get_freq()+0.1, HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
		print_status();
		save_freq(get_freq());
	}
	else if (!strncmp(argv[1], "stepdown", len)) {
		freq = get_freq() - 0.1;
		if (freq < 76.0 || freq > 108.0) exit(2);
		set_freq(freq, HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
		print_status();
		save_freq(get_freq());
	}
	else if (!strncmp(argv[1], "mute", len)) {
		set_freq(get_freq(), HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 1, 0);
	}
	else if (!strncmp(argv[1], "unmute", len)) {
		set_freq(get_freq(), HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
	}
	else if (!strncmp(argv[1], "off", len)) {
		set_freq(get_freq(), HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 1);
	}
	else if (!strncmp(argv[1], "on", len)) {
		set_freq(get_freq(), HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
	}
	else {
		int preset;

		freq = strtod(argv[1], NULL);
		preset = (int)freq - 1;

		if (freq >= 76.0 && freq <= 108.0) {
			set_freq(strtod(argv[1], NULL), HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
			print_status();
			printf("HCC: %s, SNC: %s\n", HCC_DEFAULT ? "On" : "Off", SNC_DEFAULT ? "On" : "Off");
		}
		else if (preset >= 0 && preset < station_info_num) {
			set_freq(station_info[preset].freq, HCC_DEFAULT, SNC_DEFAULT, FORCE_MONO, 0, 0);
			print_status();
			printf("HCC: %s, SNC: %s\n", HCC_DEFAULT ? "On" : "Off", SNC_DEFAULT ? "On" : "Off");
		}
		else
			usage();
	}

	close(fd);
	return 0;
}

