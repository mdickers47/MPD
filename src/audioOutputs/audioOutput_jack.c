/* jack plug in for the Music Player Daemon (MPD)
 * (c)2006 by anarch(anarchsss@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "../audioOutput.h"

#ifdef HAVE_JACK

#include <stdlib.h>
#include <errno.h>

#include "../conf.h"
#include "../log.h"

#include <string.h>

#include <jack/jack.h>
#include <jack/types.h>
#include <jack/ringbuffer.h>

/*#include "dmalloc.h"*/

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static char *name = "mpd";
static char *output_ports[2];
static int ringbuf_sz = 65536;

typedef struct _JackData {
	jack_port_t *ports[2];
	jack_client_t *client;
	jack_ringbuffer_t *ringbuffer[2];
	jack_default_audio_sample_t *samples1;
	jack_default_audio_sample_t *samples2;
	int can_process;
	int bps;
	int shutdown;
	int our_xrun;
} JackData;

/*JackData *jd = NULL;*/

static JackData *newJackData(void)
{
	JackData *ret;
	ret = xcalloc(sizeof(JackData), 1);

	return ret;
}

static void freeJackData(AudioOutput *audioOutput)
{
	JackData *jd = audioOutput->data;
	if (jd) {
		if (jd->ringbuffer[0])
			jack_ringbuffer_free(jd->ringbuffer[0]);
		if (jd->ringbuffer[1])
			jack_ringbuffer_free(jd->ringbuffer[1]);
		if (jd->samples1)
			free(jd->samples1);
		if (jd->samples2)
			free(jd->samples2);
		free(jd);
		audioOutput->data = NULL;
	}

}


static void jack_finishDriver(AudioOutput *audioOutput)
{
	JackData *jd = audioOutput->data;
	int i;

	if ( jd && jd->client ) {
		jack_deactivate(jd->client);
		jack_client_close(jd->client);
	}
	ERROR("disconnect_jack (pid=%d)\n", getpid ());

 	if ( strcmp(name, "mpd") ) {
 		free(name);
 		name = "mpd";
 	}

	for ( i = ARRAY_SIZE(output_ports); --i >= 0; ) {
		if (!output_ports[i])
 			continue;
 		free(output_ports[i]);
 		output_ports[i] = NULL;
 	}

	freeJackData(audioOutput);
}

static int srate(jack_nframes_t rate, void *data)
{
	JackData *jd = (JackData *) ((AudioOutput*) data)->data;
 	AudioFormat *audioFormat = &(((AudioOutput*) data)->outAudioFormat);

 	audioFormat->sampleRate = (int)jack_get_sample_rate(jd->client);

	return 0;
}

static int process(jack_nframes_t nframes, void *arg)
{
	size_t i;
	JackData *jd = (JackData *) arg;
	jack_default_audio_sample_t *out[2];
	size_t avail_data, avail_frames;

	if ( nframes <= 0 )
		return 0;

	out[0] = jack_port_get_buffer(jd->ports[0], nframes);
	out[1] = jack_port_get_buffer(jd->ports[1], nframes);

	/*if ( jd->can_process ) {*/
	while ( nframes ) {
		avail_data = jack_ringbuffer_read_space(jd->ringbuffer[1]);

		if ( avail_data > 0 ) {
		    avail_frames = avail_data / sizeof(jack_default_audio_sample_t);

		    if (avail_frames > nframes) {
			avail_frames = nframes;
			avail_data = nframes*sizeof(jack_default_audio_sample_t);
		    }

		    jack_ringbuffer_read(jd->ringbuffer[0], (char *)out[0],
					 avail_data);
		    jack_ringbuffer_read(jd->ringbuffer[1], (char *)out[1],
					 avail_data);

		    nframes -= avail_frames;
		    out[0] += avail_data;
		    out[1] += avail_data;
		} else {
		    for (i = 0; i < nframes; i++)
  			out[0][i] = out[1][i] = 0.0;
		    nframes = 0;
		}
	}
	/*
	if ( avail_data > 0 ) {

		avail_frames = avail_data / sizeof(jack_default_audio_sample_t);
		if (avail_frames > nframes) {
			avail_frames = nframes;
			avail_data = nframes
				* sizeof(jack_default_audio_sample_t);
		}
		jack_ringbuffer_read(jd->ringbuffer[0], (char *)out[0],
				     avail_data);
		jack_ringbuffer_read(jd->ringbuffer[1], (char *)out[1],
				     avail_data);

		if (avail_frames < nframes) {
			jd->our_xrun = 1;
			for (i = avail_frames; i < nframes; i++) {
				out[0][i] = out[1][i] = 0.0;
			}
		}
	}  else {
 		//ERROR ("avail_data=%d, no play (pid=%d)!\n", avail_data, getpid ());
 		for (i = 0; i < nframes; i++)
  			out[0][i] = out[1][i] = 0.0;
			} */

	/*ERROR("process (pid=%d)\n", getpid());*/
	return 0;
}

static void shutdown_callback(void *arg)
{
	JackData *jd = (JackData *) arg;
	jd->shutdown = 1;
}

static void set_audioformat(AudioOutput *audioOutput)
{
	JackData *jd = audioOutput->data;
	AudioFormat *audioFormat = &audioOutput->outAudioFormat;

	audioFormat->sampleRate = (int) jack_get_sample_rate(jd->client);
	ERROR ("samplerate = %d\n", audioFormat->sampleRate);
	audioFormat->channels = 2;
	audioFormat->bits = 16;
	jd->bps = audioFormat->channels
		* sizeof(jack_default_audio_sample_t)
		* audioFormat->sampleRate;
}

static void error_callback(const char *msg)
{
	ERROR("jack: %s\n", msg);
}

static int jack_initDriver(AudioOutput *audioOutput, ConfigParam *param)
{
	BlockParam *bp;
	char *endptr;
	int val;
 	char *cp = NULL;

	ERROR("jack_initDriver (pid=%d)\n", getpid());
	if ( ! param ) return 0;

	if ( (bp = getBlockParam(param, "ports")) ) {
		DEBUG("output_ports=%s\n", bp->value);

		if (!(cp = strchr(bp->value, ',')))
			FATAL("expected comma and a second value for '%s' "
			      "at line %d: %s\n",
			      bp->name, bp->line, bp->value);

		*cp = '\0';
		output_ports[0] = xstrdup(bp->value);
		*cp++ = ',';

		if (!*cp)
			FATAL("expected a second value for '%s' at line %d: "
			      "%s\n", bp->name, bp->line, bp->value);

		output_ports[1] = xstrdup(cp);

		if (strchr(cp,','))
			FATAL("Only %d values are supported for '%s' "
			      "at line %d\n", (int)ARRAY_SIZE(output_ports),
			      bp->name, bp->line);
	}

	if ( (bp = getBlockParam(param, "ringbuffer_size")) ) {
		errno = 0;
		val = strtol(bp->value, &endptr, 10);

		if ( errno == 0 && endptr != bp->value) {
			ringbuf_sz = val < 65536 ? 65536 : val;
			ERROR("ringbuffer_size=%d\n", ringbuf_sz);
		} else {
			ERROR("%s is not a number; ringbuf_size=%d\n",
			      bp->value, ringbuf_sz);
		}
	}

	if ( (bp = getBlockParam(param, "name"))
	     && (strcmp(bp->value, "mpd") != 0) ) {
		name = xstrdup(bp->value);
		ERROR("name=%s\n", name);
	}

 	return 0;
}

static int jack_testDefault(void)
{
	return 0;
}

static int connect_jack(AudioOutput *audioOutput)
{
	JackData *jd = audioOutput->data;
	char **jports;
	char *port_name;

	if ( (jd->client = jack_client_new(name)) == NULL ) {
		ERROR("jack server not running?\n");
		freeJackData(audioOutput);
		return -1;
	}

	jack_set_error_function(error_callback);
	jack_set_process_callback(jd->client, process, (void *)jd);
	jack_set_sample_rate_callback(jd->client, (JackProcessCallback)srate,
				      (void *)audioOutput);
	jack_on_shutdown(jd->client, shutdown_callback, (void *)jd);

	if ( jack_activate(jd->client) ) {
		ERROR("cannot activate client");
		freeJackData(audioOutput);
		return -1;
	}

	jd->ports[0] = jack_port_register(jd->client, "left",
					  JACK_DEFAULT_AUDIO_TYPE,
					  JackPortIsOutput, 0);
	if ( !jd->ports[0] ) {
		ERROR("Cannot register left output port.\n");
		freeJackData(audioOutput);
		return -1;
	}

	jd->ports[1] = jack_port_register(jd->client, "right",
					  JACK_DEFAULT_AUDIO_TYPE,
					  JackPortIsOutput, 0);
	if ( !jd->ports[1] ) {
		ERROR("Cannot register right output port.\n");
		freeJackData(audioOutput);
		return -1;
	}

	/*  hay que buscar que hay  */
	if ( !output_ports[1]
	     && (jports = (char **)jack_get_ports(jd->client, NULL, NULL,
							JackPortIsPhysical|
							JackPortIsInput)) ) {
		output_ports[0] = jports[0];
		output_ports[1] = jports[1] ? jports[1] : jports[0];
		ERROR("output_ports: %s %s\n", output_ports[0], output_ports[1]);
		free(jports);
	}

	if ( output_ports[1] ) {
		jd->ringbuffer[0] = jack_ringbuffer_create(ringbuf_sz);
		jd->ringbuffer[1] = jack_ringbuffer_create(ringbuf_sz);
		memset(jd->ringbuffer[0]->buf, 0, jd->ringbuffer[0]->size);
		memset(jd->ringbuffer[1]->buf, 0, jd->ringbuffer[1]->size);

		port_name = xmalloc(sizeof(char)*(7+strlen(name)));

		sprintf(port_name, "%s:left", name);
		if ( (jack_connect(jd->client, port_name,
				   output_ports[0])) != 0 ) {
			ERROR("%s is not a valid Jack Client / Port ",
			      output_ports[0]);
			freeJackData(audioOutput);
			free(port_name);
			return -1;
		}
		sprintf(port_name, "%s:right", name);
		if ( (jack_connect(jd->client, port_name,
				   output_ports[1])) != 0 ) {
			ERROR("%s is not a valid Jack Client / Port ",
			      output_ports[1]);
			freeJackData(audioOutput);
			free(port_name);
			return -1;
		}
		free(port_name);
	}

	ERROR("connect_jack (pid=%d)\n", getpid());
	return 1;
}

static int jack_openDevice(AudioOutput *audioOutput)
{
	JackData *jd = audioOutput->data;

	if ( !jd ) {
		ERROR("connect!\n");
		jd = newJackData();
		audioOutput->data = jd;

		if (connect_jack(audioOutput) < 0) {
			freeJackData(audioOutput);
			audioOutput->open = 0;
			return -1;
		}
	}

	set_audioformat(audioOutput);
	audioOutput->open = 1;

	ERROR("jack_openDevice (pid=%d)!\n", getpid ());
	return 0;
}


static void jack_closeDevice(AudioOutput * audioOutput)
{
	/*jack_finishDriver(audioOutput);*/
	audioOutput->open = 0;
	ERROR("jack_closeDevice (pid=%d)\n", getpid());
}

static void jack_dropBufferedAudio (AudioOutput * audioOutput)
{

}

static int jack_playAudio(AudioOutput * audioOutput, char *buff, int size)
{
	JackData *jd = audioOutput->data;
	size_t space;
	int i;
	short *buffer = (short *) buff;
	jack_default_audio_sample_t sample;
	size_t samples = size/4;

	ERROR("jack_playAudio: (pid=%d)!\n", getpid());

	if ( jd->shutdown ) {
		ERROR("Refusing to play, because there is no client thread.\n");
		freeJackData(audioOutput);
		audioOutput->open = 0;
		return 0;
	}

	/*ERROR("jack_playAudio: size=%d\n", size/4);*/
	/*ERROR("jack_playAudio - INICIO\n");*/

	while ( samples && !jd->shutdown ) {
		/*ERROR("\t samples=%d\n", samples);*/
 		if ( (space = jack_ringbuffer_write_space(jd->ringbuffer[0]))
 		     >= samples*sizeof(jack_default_audio_sample_t) ) {
 			/*ERROR("\t samples_b=%d space=%d\n", samples*sizeof(jack_default_audio_sample_t), space);*/

 			space = MIN(space, samples*sizeof(jack_default_audio_sample_t));

			for(i=0; i<space/sizeof(jack_default_audio_sample_t); i++) {
				sample = (jack_default_audio_sample_t) *(buffer++)/32768.0;

				jack_ringbuffer_write(jd->ringbuffer[0], (void*)&sample,
						      sizeof(jack_default_audio_sample_t));

				sample = (jack_default_audio_sample_t) *(buffer++)/32768.0;

				jack_ringbuffer_write(jd->ringbuffer[1], (void*)&sample,
						      sizeof(jack_default_audio_sample_t));

				samples--;
			}

 		} else {
/* 		    ERROR("\t space=%d\n", space); */
/* 		    ERROR("\t size=%d\n", size); */
		    usleep((unsigned long)
 		 	   ((samples*sizeof(jack_default_audio_sample_t)
			     - space)/jd->bps) * 1000000.0);
  	 	}
 	}

	/*ERROR("jack_playAudio - FIN\n");*/

	return 0;

}

AudioOutputPlugin jackPlugin = {
	"jack",
	jack_testDefault,
	jack_initDriver,
	jack_finishDriver,
	jack_openDevice,
	jack_playAudio,
	jack_dropBufferedAudio,
	jack_closeDevice,
	NULL,	/* sendMetadataFunc */
};


#else /* HAVE JACK */

DISABLED_AUDIO_OUTPUT_PLUGIN(jackPlugin)

#endif /* HAVE_JACK */
