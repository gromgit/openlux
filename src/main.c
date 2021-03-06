/*
  Copyright (c) 2015 Anonymous Meerkat

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "backend/os/os.h"
#include "backend/video/video.h"
#include "backend/gamma/gamma.h"
#include "backend/time/time.h"
#include "animate.h"
#include "log.h"
#include "color.h"
#include "gamma.h"
#include "kelvin.h"


static void
ol_main_version(void)
{
  puts("openlux v"VERSION_STR);
}

static void
ol_main_help(char* argv0)
{
  ol_main_version();
  puts("");
  puts("Usage:");
  printf(" %s [options]\n", argv0);
  puts("");
  puts("Adjusts screen color temperature");
  puts("");
  puts("Options:");
  puts(" -k, --kelvin        color temperature in kelvin (1000-40000,");
  puts("                         default: 3400)");
  puts(" -r, --red           red color channel (0-255, default: auto)");
  puts(" -g, --green         green color channel (0-255, default: auto)");
  puts(" -b, --blue          blue color channel (0-255, default: auto)");
  puts(" -i, --identity      resets display gamma to identity (no color");
  puts("                         bias)");
  puts("");
  puts(" -R, --reset         resets display gamma to last saved gamma");
  puts(" -s, --save          saves current gamma (automatically done when");
  puts("                         openlux is run for the first time on boot)");
  puts("");
  puts(" -a, --animate       animation time in milliseconds (default: 0)");
  puts(" -d, --delay         animation delay per \"frame\" in milliseconds");
  puts("                         (default: 0)");
  puts("");
  puts(" -h, --help          display this help and exit");
  puts(" -V, --version       display version information and exit");
}

static struct option _ol_main_long_options[] = {
  {"kelvin",   required_argument, 0, 'k'},
  {"red",      required_argument, 0, 'r'},
  {"green",    required_argument, 0, 'g'},
  {"blue",     required_argument, 0, 'b'},
  {"identity", no_argument,       0, 'i'},
  {"reset",    no_argument,       0, 'R'},
  {"save",     no_argument,       0, 's'},
  {"animate",  required_argument, 0, 'a'},
  {"delay",    required_argument, 0, 'd'},
  {"help",     no_argument,       0, 'h'},
  {"version",  no_argument,       0, 'V'},
  {0, 0, 0, 0}
};

int
main(int argc, char** argv)
{
  int ret = 0;

  int opt_kelvin = 3400;
  char opt_red[256] = "auto";
  char opt_green[256] = "auto";
  char opt_blue[256] = "auto";
  bool opt_identity = 0;
  bool opt_save = 0;
  bool opt_reset = 0;
  int opt_anim = 0;
  int opt_delay = 0;

  int c;
  int option_index;

  struct ol_backend_os_s os_backend;
  struct ol_backend_video_s video_backend;
  struct ol_backend_gamma_s gamma_backend;
  struct ol_backend_time_s time_backend;

  struct ol_gamma_s default_gamma_value;
  struct ol_gamma_s current_gamma_value;
  struct ol_gamma_s anim_gamma_value;
  struct ol_gamma_s gamma_value;
  FILE* gamma_file;

  ol_time_t start_time;
  ol_time_t current_time;


  /*** Parse arguments ***/

  while (1)
    {
      c = getopt_long(argc, argv, "hVk:r:g:b:iRsa:d:", _ol_main_long_options,
                      &option_index);
      if (c == -1)
        break;

      switch (c)
        {
        case 0:
          break;

        case 'h':
          ol_main_help(argv[0]);
          return 0;

        case 'V':
          ol_main_version();
          return 0;

        case 'i':
          opt_identity = 1;
          break;

        case 'R':
          opt_reset = 1;
          break;

        case 's':
          opt_save = 1;
          break;

        case 'k':
          opt_kelvin = atoi(optarg);
          break;

        case 'r':
          strcpy(opt_red, optarg);
          break;

        case 'g':
          strcpy(opt_green, optarg);
          break;

        case 'b':
          strcpy(opt_blue, optarg);
          break;

        case 'a':
          opt_anim = atoi(optarg);
          break;

        case 'd':
          opt_delay = atoi(optarg);
          break;

        default:
          ol_main_help(argv[0]);
          return 1;
        }
    }


  /*** Load backends ***/

  if (ol_backend_os_init(&os_backend, -1, NULL))
    {
      OL_LOG_ERR("Unable to load platform backend");
      return 2;
    }

  if (ol_backend_video_init(&video_backend, -1, NULL))
    {
      OL_LOG_ERR("Unable to load video backend");

      ret = 3;
      goto free_os_backend;
    }

  if (ol_backend_gamma_init(&gamma_backend, video_backend.gamma_index,
                            video_backend.gamma_data))
    {
      OL_LOG_ERR("Unable to load gamma backend");

      ret = 4;
      goto free_video_backend;
    }

  if (ol_backend_time_init(&time_backend, os_backend.time_index, NULL))
    {
      OL_LOG_ERR("Unable to load time backend");
      ret = 5;
      goto free_gamma_backend;
    }


  OL_GAMMA_MALLOC(video_backend.gamma_ramp_size, current_gamma_value);
  video_backend.get_gamma(&video_backend, current_gamma_value);

  if (!os_backend.exists(&os_backend, "gamma") || opt_save)
    {
      /* Save gamma */
      gamma_file = os_backend.open(&os_backend, "gamma", "wb");
      fwrite(current_gamma_value.red,
             1, OL_GAMMA_SIZE(video_backend.gamma_ramp_size),
             gamma_file);
      fclose(gamma_file);

      if (opt_save)
        goto free_current_gamma_value;
    }


  /*** Load default gamma ***/

  if (gamma_backend.needs_default_gamma || opt_reset)
    {
      OL_GAMMA_MALLOC(video_backend.gamma_ramp_size, default_gamma_value);

      gamma_file = os_backend.open(&os_backend, "gamma", "rb");
      fread(default_gamma_value.red,
            1, OL_GAMMA_SIZE(video_backend.gamma_ramp_size),
            gamma_file);
      fclose(gamma_file);

      gamma_backend.set_default_gamma(&gamma_backend, default_gamma_value);
    }

  OL_GAMMA_MALLOC(video_backend.gamma_ramp_size, gamma_value);

  /*** Load gamma ***/

  if (opt_reset)
    {
      OL_GAMMA_COPY(gamma_value, default_gamma_value,
                    video_backend.gamma_ramp_size);
    }
  else if (!opt_identity)
    {
      ol_color_t color = ol_kelvin_rgb(opt_kelvin);

      color = OL_COLOR_INIT(
                            ol_color_parse(opt_red,
                                           OL_COLOR_RED(color)),
                            ol_color_parse(opt_green,
                                           OL_COLOR_GREEN(color)),
                            ol_color_parse(opt_blue,
                                           OL_COLOR_BLUE(color))
                            );

      gamma_backend.rgb(&gamma_backend,
                        video_backend.gamma_ramp_size,
                        color, gamma_value);
    }
  else
    {
      gamma_backend.identity(&gamma_backend,
                             video_backend.gamma_ramp_size, gamma_value);
    }


  /*** Set gamma ***/

  if (opt_anim > 0)
    {
      /* Animate it */

      OL_GAMMA_MALLOC(video_backend.gamma_ramp_size, anim_gamma_value);

      start_time = time_backend.get_time(&time_backend);

      for (current_time = time_backend.get_time(&time_backend) - start_time;
           current_time < opt_anim;
           current_time = time_backend.get_time(&time_backend) - start_time)
        {
          ol_animate_lerp(current_gamma_value, gamma_value, anim_gamma_value,
                          video_backend.gamma_ramp_size,
                          current_time, opt_anim);
          video_backend.set_gamma(&video_backend, anim_gamma_value);

          time_backend.sleep(&time_backend, opt_delay);
        }

      OL_GAMMA_FREE(anim_gamma_value);
    }

  /* Set gamma */
  video_backend.set_gamma(&video_backend, gamma_value);


  /*** End ***/

 start_end:
 free_default_gamma_value:

  if (gamma_backend.needs_default_gamma || opt_reset)
    OL_GAMMA_FREE(default_gamma_value);

 free_gamma_value:
  OL_GAMMA_FREE(gamma_value);

 free_current_gamma_value:
  OL_GAMMA_FREE(current_gamma_value);

 free_time_backend:
  time_backend.uninit(&time_backend);

 free_gamma_backend:
  gamma_backend.uninit(&gamma_backend);

 free_video_backend:
  video_backend.uninit(&video_backend);

 free_os_backend:
  os_backend.uninit(&os_backend);

 end:
  return ret;
}
