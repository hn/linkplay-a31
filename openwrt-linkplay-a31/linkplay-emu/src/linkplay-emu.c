/*

   linkplay-emu.c

   Daemon for emulating Linkplay WiFi module sound control.
   Monitors SND_CARD_MIXER for volume changes and
   SND_CARD_STATUS for sound activities and
   adjusts the AXX DAC/AMP accordingly via serial control commands.

   (C) 2024 Hajo Noerenberg

   http://www.noerenberg.de/
   https://github.com/hn/linkplay-a31

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3.0 as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program. If not, see <http://www.gnu.org/licenses/gpl-3.0.txt>.

*/

#define SND_CARD_STATUS	"hw:1"
#define SND_CARD_MIXER	"hw:0"
#define MIX_NAME	"Master"
#define MIX_INDEX	0

#define AXX_VOLMAX	80
#define AXX_VOLOFF	3
#define AXX_VOLPOW	200
#define AXX_IDLETIME	10
#define AXX_PORT	"/dev/ttyS0"

#define DEBUG

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <signal.h>
#include <syslog.h>
#include <termios.h>
#include <alsa/asoundlib.h>

unsigned int hastty;

volatile sig_atomic_t exitplease = 0;

void log_line(int prio, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	if (hastty) {
		vfprintf(stderr, fmt, args);
		fprintf(stderr, "\n");
	} else {
		if (prio < LOG_DEBUG)
			vsyslog(prio, fmt, args);
	}
	va_end(args);
}

void sig_handler(int signum) {
	exitplease = 1;
}

int serial_interface_attribs(int fd, int speed) {
	struct termios tty;

	if (tcgetattr(fd, &tty) < 0)
		return -1;

	cfsetospeed(&tty, (speed_t) speed);
	cfsetispeed(&tty, (speed_t) speed);

	tty.c_cflag |= (CLOCAL | CREAD);	/* ignore modem controls */
	tty.c_cflag &= ~CSIZE;
	tty.c_cflag |= CS8;	/* 8-bit characters */
	tty.c_cflag &= ~PARENB;	/* no parity bit */
	tty.c_cflag &= ~CSTOPB;	/* only need 1 stop bit */
	tty.c_cflag &= ~CRTSCTS;	/* no hardware flowcontrol */

	/* setup for non-canonical mode */
	tty.c_iflag &=
	    ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tty.c_oflag &= ~OPOST;

	/* fetch bytes as they become available */
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 1;

	if (tcsetattr(fd, TCSANOW, &tty) != 0)
		return -1;

	return 0;
}

ssize_t serial_writeln(int fd, char *data) {
	size_t len = strlen(data);
	log_line(LOG_INFO, "UART> %s", data);
	ssize_t n = write(fd, data, len);
	n += write(fd, "\n", 1); /* sum is dirty */
	return n;
}

ssize_t serial_read(int fd) {
	char buf[100];
	ssize_t n = read(fd, buf, sizeof buf - 1);
	if (n > 0) {
		/* strip non-printables except CRLF */
		for (unsigned int i = 0; i < n; i++) {
			if (buf[i] != 10 && buf[i] != 13
			  && (buf[i] < 32 || buf[i] > 127))
			    buf[i] = '.';
		}
		buf[n] = 0;
		log_line(LOG_INFO, "UART< %s", buf);
	}
	return n;
}

int main() {

	static const char *serial_name = AXX_PORT;
	static const char *snd_card_mixer = SND_CARD_MIXER;
	static const char *snd_card_status = SND_CARD_STATUS;
	static const char *mix_name = MIX_NAME;
	static const int mix_index = MIX_INDEX;

	snd_ctl_t *ctl;
	snd_pcm_info_t *pcm_info;
	snd_mixer_t *mix_handle;
	snd_mixer_elem_t *mix_elem;
	snd_mixer_selem_id_t *mix_sid;
	int serial_fd;

	int err;
	int ret = 1;
	unsigned int pcm_ismuted = 1;
	unsigned int pcm_lastvol = 0;
	time_t pcm_lastplaying = 0;
	long vol_min, vol_max;
	unsigned int vol_multi = 0;

	hastty = !!ttyname(0);

	if (!hastty) openlog("linkplay-emu", LOG_PID, LOG_DAEMON);

	signal(SIGTERM, sig_handler);

	serial_fd = open(serial_name, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
	if (serial_fd < 0) {
		log_line(LOG_ERR,
			"Failed to open serial port '%s': %s", serial_name,
			strerror(errno));
		goto fail;
	}
	err = serial_interface_attribs(serial_fd, B57600);
	if (err < 0) {
		log_line(LOG_ERR, "Failed to set serial port attribs");
		goto fail_cs;
	}

	err = snd_ctl_open(&ctl, snd_card_status, SND_CTL_READONLY);
	if (err < 0) {
		log_line(LOG_ERR,
			"Failed to open audio control device '%s': %s",
			snd_card_status, snd_strerror(err));
		goto fail_cs;
	}

	snd_mixer_selem_id_alloca(&mix_sid);
	snd_mixer_selem_id_set_index(mix_sid, mix_index);
	snd_mixer_selem_id_set_name(mix_sid, mix_name);

	err = snd_mixer_open(&mix_handle, 0);
	if (err < 0) {
		log_line(LOG_ERR, "Failed to open mixer: %s",
			snd_strerror(err));
		goto fail_cc;
	}
	err = snd_mixer_attach(mix_handle, snd_card_mixer);
	if (err < 0) {
		log_line(LOG_ERR, "Failed to attach mixer: %s",
			snd_strerror(err));
		goto fail_mc;
	}
	err = snd_mixer_selem_register(mix_handle, NULL, NULL);
	if (err < 0) {
		log_line(LOG_ERR,
			"Failed to register simple element class handle: %s",
			snd_strerror(err));
		goto fail_mc;
	}
	err = snd_mixer_load(mix_handle);
	if (err < 0) {
		log_line(LOG_ERR,
			"Failed to load mixer elements: %s",
			snd_strerror(err));
		goto fail_mc;
	}
	mix_elem = snd_mixer_find_selem(mix_handle, mix_sid);
	if (!mix_elem) {
		log_line(LOG_ERR,
			"Failed to findle mixer simple element: %s",
			snd_strerror(err));
		goto fail_mc;
	}

	err = snd_pcm_info_malloc(&pcm_info);
	if (err < 0) {
		log_line(LOG_ERR,
			"Failed to malloc pcm info: %s", snd_strerror(err));
		goto fail_mc;
	}

	snd_mixer_selem_get_playback_volume_range(mix_elem, &vol_min, &vol_max);

	/* Set MCU ready state */
	serial_writeln(serial_fd, "AXX+MCU+RDY");

	/* Get MCU version */
	serial_writeln(serial_fd, "AXX+MCU+VER");

	/* Initialize DAC (set source) */
	serial_writeln(serial_fd, "AXX+PLM+001");

	log_line(LOG_INFO, "Daemon (compiled on %s) started successfully", __TIMESTAMP__);

	while (!exitplease) {
		long vol_now;
		unsigned int vol_cal;
		unsigned int pcm_isplaying;

		serial_read(serial_fd);

		snd_mixer_wait(mix_handle, 500);
		snd_mixer_handle_events(mix_handle);	/* https://www.raspberrypi.org/forums/viewtopic.php?t=175511 */

		err =
		    snd_mixer_selem_get_playback_volume(mix_elem, 0, &vol_now);
		if (err < 0) {
			log_line(LOG_ERR,
				"Failed to get audio playback volume: %s",
				snd_strerror(err));
			goto fail_pif;
		}
#ifdef AXX_VOLPOW
		vol_cal = AXX_VOLOFF +
		    (AXX_VOLMAX - AXX_VOLOFF) *
		    (pow(AXX_VOLPOW, (float)(vol_now - vol_min) / (vol_max - vol_min)) - 1) / (pow(AXX_VOLPOW, 1) - 1);
#else
		vol_cal = AXX_VOLOFF +
		    (AXX_VOLMAX - AXX_VOLOFF) * (vol_now - vol_min) / (vol_max - vol_min);
#endif /* AXX_VOLPOW */

		err = snd_ctl_pcm_info(ctl, pcm_info);
		if (err < 0) {
			log_line(LOG_ERR,
				"Failed to get audio device info: %s",
				snd_strerror(err));
			goto fail_pif;
		}

		pcm_isplaying =
		    snd_pcm_info_get_subdevices_count(pcm_info) -
		    snd_pcm_info_get_subdevices_avail(pcm_info);

		if (pcm_isplaying) {
			pcm_lastplaying = time(NULL);
		}

		if (!pcm_ismuted
		    && ((time(NULL) - pcm_lastplaying) > AXX_IDLETIME)) {
			/* Mute DAC */
			serial_writeln(serial_fd, "AXX+PLY+000");
			serial_writeln(serial_fd, "AXX+MUT+001");
			pcm_ismuted = 1;
			pcm_lastvol = 0;
		}

		if (pcm_ismuted && pcm_isplaying) {
			/* Unmute DAC */
			serial_writeln(serial_fd, "AXX+MUT+000");
			serial_writeln(serial_fd, "AXX+PLY+001");
			pcm_ismuted = 0;
			vol_multi = 3;	/* set volume multiple times after mute */
		}

		if (vol_multi || (!pcm_ismuted && (pcm_lastvol != vol_cal))) {
			/* Set DAC volume */
			char out[12 + 1];
			sprintf(out, "AXX+VOL+%03d", vol_cal);
			serial_writeln(serial_fd, out);
			pcm_lastvol = vol_cal;
			if (vol_multi) vol_multi--;
		}

		log_line(LOG_DEBUG,
		     "vol_now=%ld (%ld - %ld), vol_cal=%d, isplaying=%d, ismuted=%d",
		     vol_now, vol_min, vol_max, vol_cal, pcm_isplaying,
		     pcm_ismuted);

	}

	/* Mute DAC */
	serial_writeln(serial_fd, "AXX+PLY+000");
	serial_writeln(serial_fd, "AXX+MUT+001");
	ret = 0;

 fail_pif:
	snd_pcm_info_free(pcm_info);
 fail_mc:
	snd_mixer_close(mix_handle);
 fail_cc:
	snd_ctl_close(ctl);
 fail_cs:
	close(serial_fd);
 fail:
	if (!hastty) closelog();
	return ret;

}
