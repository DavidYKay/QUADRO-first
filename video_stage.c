/*
video_stage.c - video thread for AR.Drone autopilot agent.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 You should also have received a copy of the Parrot Parrot AR.Drone
 Development License and Parrot AR.Drone copyright notice and disclaimer
 and If not, see
   <https://projects.ardrone.org/attachments/277/ParrotLicense.txt>
 and
   <https://projects.ardrone.org/attachments/278/ParrotCopyrightAndDisclaimer.txt>.

History:

  27-JUL-2007: version 1.0 Created by marc-olivier.dzeukou@parrot.com

  20-NOV-2010 Simon D. Levy: modified to send video and navigation data to autopilot agent
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <VP_Api/vp_api.h>
#include <VP_Api/vp_api_error.h>
#include <VP_Api/vp_api_stage.h>
#include <VP_Api/vp_api_picture.h>
#include <VP_Stages/vp_stages_io_file.h>
#include <VP_Stages/vp_stages_i_camif.h>

#include <config.h>
#include <VP_Os/vp_os_print.h>
#include <VP_Os/vp_os_malloc.h>
#include <VP_Os/vp_os_delay.h>
#include <VP_Stages/vp_stages_yuv2rgb.h>
#include <VP_Stages/vp_stages_buffer_to_picture.h>
#include <VLIB/Stages/vlib_stage_decode.h>

#include <ardrone_tool/ardrone_tool.h>
#include <ardrone_tool/Com/config_com.h>

#ifndef RECORD_VIDEO
#define RECORD_VIDEO
#endif
#ifdef RECORD_VIDEO
#    include <ardrone_tool/Video/video_stage_recorder.h>
#endif

#include <ardrone_tool/Video/video_com_stage.h>

#include "autopilot.h"
#include "agent.h"
#include "agent_comm.h"

#define NB_STAGES 10

static vp_os_mutex_t  video_update_lock = PTHREAD_MUTEX_INITIALIZER;

PIPELINE_HANDLE pipeline_handle;

typedef  struct _vp_stages_gtk_config_ {
	int width;
	int height;
	int rowstride;
}  vp_stages_gtk_config_t;

C_RESULT output_gtk_stage_open( void *cfg, vp_api_io_data_t *in, vp_api_io_data_t *out) {

        printf("video_stage:open()()\n");
	agent_init();

	return (SUCCESS);
}

C_RESULT output_gtk_stage_transform( vp_stages_gtk_config_t *cfg, vp_api_io_data_t *in, vp_api_io_data_t *out) {

        printf("video_stage:transform()\n");

	uint8_t * camera_data = (uint8_t*)in->buffers[0];

	vp_os_mutex_lock(&video_update_lock);

	agent_update(camera_data);

	vp_os_mutex_unlock(&video_update_lock);

	return (SUCCESS);
}

C_RESULT output_gtk_stage_close( void *cfg, vp_api_io_data_t *in, vp_api_io_data_t *out) {
        printf("video_stage:close()()\n");

	agent_close();

	return (SUCCESS);
}


const vp_api_stage_funcs_t vp_stages_output_gtk_funcs =
{
	NULL,
	(vp_api_stage_open_t)output_gtk_stage_open,
	(vp_api_stage_transform_t)output_gtk_stage_transform,
	(vp_api_stage_close_t)output_gtk_stage_close
};

DEFINE_THREAD_ROUTINE(video_stage, data) {

	C_RESULT res;

	vp_api_io_pipeline_t    pipeline;
	vp_api_io_data_t        out;
	vp_api_io_stage_t       stages[NB_STAGES];

	vp_api_picture_t picture;

	video_com_config_t              icc;
	vlib_stage_decoding_config_t    vec;
	vp_stages_yuv2rgb_config_t      yuv2rgbconf;
#ifdef RECORD_VIDEO
	video_stage_recorder_config_t   vrc;
#endif
	/// Picture configuration
	picture.format        = PIX_FMT_YUV420P;

	picture.width         = QVGA_WIDTH;
	picture.height        = QVGA_HEIGHT;
	picture.framerate     = 30;

	picture.y_buf   = vp_os_malloc( QVGA_WIDTH * QVGA_HEIGHT     );
	picture.cr_buf  = vp_os_malloc( QVGA_WIDTH * QVGA_HEIGHT / 4 );
	picture.cb_buf  = vp_os_malloc( QVGA_WIDTH * QVGA_HEIGHT / 4 );

	picture.y_line_size   = QVGA_WIDTH;
	picture.cb_line_size  = QVGA_WIDTH / 2;
	picture.cr_line_size  = QVGA_WIDTH / 2;

	vp_os_memset(&icc,          0, sizeof( icc ));
	vp_os_memset(&vec,          0, sizeof( vec ));
	vp_os_memset(&yuv2rgbconf,  0, sizeof( yuv2rgbconf ));

	icc.com                 = COM_VIDEO();
	icc.buffer_size         = 100000;
	icc.protocol            = VP_COM_UDP;
	COM_CONFIG_SOCKET_VIDEO(&icc.socket, VP_COM_CLIENT, VIDEO_PORT, wifi_ardrone_ip);

	vec.width               = QVGA_WIDTH;
	vec.height              = QVGA_HEIGHT;
	vec.picture             = &picture;
	vec.block_mode_enable   = TRUE;
	vec.luma_only           = FALSE;

	yuv2rgbconf.rgb_format = VP_STAGES_RGB_FORMAT_RGB24;
#ifdef RECORD_VIDEO
	vrc.fp = NULL;
#endif

	pipeline.nb_stages = 0;

	stages[pipeline.nb_stages].type    = VP_API_INPUT_SOCKET;
	stages[pipeline.nb_stages].cfg     = (void *)&icc;
	stages[pipeline.nb_stages].funcs   = video_com_funcs;

	pipeline.nb_stages++;

#ifdef RECORD_VIDEO
	stages[pipeline.nb_stages].type    = VP_API_FILTER_DECODER;
	stages[pipeline.nb_stages].cfg     = (void*)&vrc;
	stages[pipeline.nb_stages].funcs   = video_recorder_funcs;

	pipeline.nb_stages++;
#endif // RECORD_VIDEO
	stages[pipeline.nb_stages].type    = VP_API_FILTER_DECODER;
	stages[pipeline.nb_stages].cfg     = (void*)&vec;
	stages[pipeline.nb_stages].funcs   = vlib_decoding_funcs;

	pipeline.nb_stages++;

	stages[pipeline.nb_stages].type    = VP_API_FILTER_YUV2RGB;
	stages[pipeline.nb_stages].cfg     = (void*)&yuv2rgbconf;
	stages[pipeline.nb_stages].funcs   = vp_stages_yuv2rgb_funcs;

	pipeline.nb_stages++;

	stages[pipeline.nb_stages].type    = VP_API_OUTPUT_SDL;
	stages[pipeline.nb_stages].cfg     = NULL;
	stages[pipeline.nb_stages].funcs   = vp_stages_output_gtk_funcs;

	pipeline.nb_stages++;

	pipeline.stages = &stages[0];

	/* Processing of a pipeline */
	if( !g_exit )
	{
		PRINT("Video stage thread initialisation\n");

		res = vp_api_open(&pipeline, &pipeline_handle);

		if( SUCCEED(res) )
		{
			int loop = SUCCESS;
			out.status = VP_API_STATUS_PROCESSING;

			while( !g_exit && (loop == SUCCESS) )
			{
				if( SUCCEED(vp_api_run(&pipeline, &out)) ) {
					if( (out.status == VP_API_STATUS_PROCESSING || out.status == VP_API_STATUS_STILL_RUNNING) ) {
                                            PRINT("Video stage success\n");
						loop = SUCCESS;
					}
				}
				else loop = -1; // Finish this thread
			}
		}
	}

	PRINT("Video stage thread ended\n\n");

	agent_comm_close();

        exit(0);
}
