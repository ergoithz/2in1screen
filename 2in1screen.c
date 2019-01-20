/*
 * Screen rotation daemon (preconfigured for Lenovo Thinkpad Yoga 11e)
 *
 * Compilation: gcc -O2 -o 2in1screen 2in1screen.c -lxcb -lxcb-randr -lxcb-xinput
 *
 * You will probably have to tune this stuff for your case:
 *   - TRANSFORM_PROPERTIES and TRANSFORM_PROPERTIES_SIZE
 *   - TRANSFORM matrixes
 *   - function rotation_changed's conditions
 *
 * TRANSFORM_PROPERTIES_SIZE: number of TRANSFORM_PROPERTIES items
 * TRANSFORM_PROPERTIES: alternating device and property items
 *
 * To get transform devices:
 *   xinput list| grep -i 'touchpad\|pen\|finger'
 *
 * To get transform matrix properties:
 *   xinput list-props "Wacom HID 5073 Pen" | grep "Coordinate Transformation Matrix"
 */

#define TRANSFORM_PROPERTIES_SIZE 0
char
  *TRANSFORM_PROPERTIES[TRANSFORM_PROPERTIES_SIZE] = {};

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/xinput.h>

/*
 * Tests (xyz for scale 0.019122):
 *   - Normal: 482 -20 138
 *   - Right: 26 506 -28
 *   - Left: -2 -500 -88
 *   - Inverted: -505 15 32
 *   - Horizontal (facing up): 89 -13 496
 *   - Horizontal (facing down): -95 -37 -541
 */

double
  accel_threshold = 5.0,
  accel_scale = 1,
  accel_x = 0.0,
  accel_y = 0.0,
  accel_z = 0.0;

unsigned char
  verbosity = 0,
  current_state = 0,
  keep_running = 1;

#define STRESS_CYCLES 20
#define DATA_SIZE 256
char
  basedir[DATA_SIZE],
  *basedir_end = NULL,
  content[DATA_SIZE],
  *ROTATION[4] = {
    "normal",
    "inverted",
    "left",
    "right"
    },
  /*
   * Input transformation matrix:
   *   - c0: touch_area_width / total_width
   *   - c2: touch_area_height / total_height
   *   - c1: touch_area_x_offset / total_width
   *   - c3: touch_area_y_offset / total_height
   *   - matrix: c0 0 c1 0 c2 c3 0 0 1
   */
  *TRANSFORM[4] = {
    " 1  0  0  0  1 0 0 0 1",
    " 1  0  0  0  1 0 0 0 1",
    " 1  0  0  0  1 0 0 0 1",
    " 1  0  0  0  1 0 0 0 1",
    };

#define CRTC_NAME "eDP1"
#define CRTC_NAME_SIZE 4
#define MIN(A, B) ((A) > (B) ? (B) : (A))

int xcb_default_screen;
xcb_randr_crtc_t xcb_randr_crtc;
xcb_input_device_id_t xcb_input_device_id;
xcb_window_t xcb_root;
xcb_connection_t *xcb_connection = NULL;
xcb_randr_screen_size_t *xcb_randr_screen_size = NULL;

static inline char xcb_detect_randr_crtc(const char *name, const int name_size) {
	xcb_randr_get_screen_resources_reply_t *screen_resources = xcb_randr_get_screen_resources_reply(
    xcb_connection,
    xcb_randr_get_screen_resources(xcb_connection, xcb_root),
    NULL
  );
	if (screen_resources == NULL) return 1;

  char success = 0;
	int outputs_size = xcb_randr_get_screen_resources_outputs_length(screen_resources);
	xcb_randr_output_t *outputs = xcb_randr_get_screen_resources_outputs(screen_resources);

  xcb_randr_get_output_info_reply_t *output_info;
  xcb_randr_crtc_t crtc;
  xcb_randr_get_crtc_info_reply_t *crtc_info;
	for (int i = 0; success == 0 && i < outputs_size; i++) {
		output_info = xcb_randr_get_output_info_reply(
      xcb_connection,
      xcb_randr_get_output_info(xcb_connection, outputs[i], XCB_CURRENT_TIME),
      NULL
    );
    if (output_info == NULL) continue;

    crtc = output_info->crtc;
    if(crtc == XCB_NONE) {
      free(output_info);
      continue;
    }

    crtc_info = xcb_randr_get_crtc_info_reply(
      xcb_connection,
      xcb_randr_get_crtc_info(xcb_connection, crtc, XCB_CURRENT_TIME),
      NULL
    );
    if (crtc_info == NULL) {
      free(output_info);
      continue;
    }

    char *info_name = (char *) xcb_randr_get_output_info_name(output_info);
    int info_name_size = xcb_randr_get_output_info_name_length(output_info);

    if (strncmp(info_name, name, MIN(info_name_size, name_size)) == 0) {
      xcb_randr_crtc = crtc;
      success = 1;
    }

    free(output_info);
    free(crtc_info);
    // free(info_name);
	}

  free(screen_resources);
  // free(outputs);
  return success;
}

static inline char xcb_init() {
  // xcb_connection
	xcb_connection = xcb_connect(NULL, &xcb_default_screen);
	if (xcb_connection_has_error(xcb_connection)){
		fprintf(stderr, "unable connect to the X server");
		return EXIT_FAILURE;
	}

  // xcb_root
	xcb_screen_t *xcb_screen = xcb_setup_roots_iterator(
    xcb_get_setup(xcb_connection)
  ).data;
	if (xcb_screen == NULL){
		fprintf(stderr, "unable to retrieve screen informations");
		return EXIT_FAILURE;
	}
  xcb_root = xcb_screen->root;
  // free(xcb_screen);

  // xcb_randr_crtc
  if (!xcb_detect_randr_crtc(CRTC_NAME, CRTC_NAME_SIZE)){
		fprintf(stderr, "Failed to find crtc with name %s", CRTC_NAME);
		return EXIT_FAILURE;
  }

  // xcb_randr_screen_size
  xcb_randr_get_screen_info_reply_t *screen_info = xcb_randr_get_screen_info_reply(
    xcb_connection,
    xcb_randr_get_screen_info(xcb_connection, xcb_root),
    NULL
  );
  if (screen_info == NULL){
		fprintf(stderr, "unable to retrieve screen info");
		return EXIT_FAILURE;
	}
	xcb_randr_screen_size = xcb_randr_get_screen_info_sizes(screen_info);
  free(screen_info);

	return EXIT_SUCCESS;
}

static inline void xcb_close() {
	if (xcb_connection != NULL) xcb_disconnect(xcb_connection);
  else if (xcb_randr_screen_size != NULL) free(xcb_randr_screen_size);
}

static inline int xcb_set_config(xcb_randr_crtc_t crtc, uint16_t rotation){
	xcb_generic_error_t *e = NULL;
	xcb_randr_get_crtc_info_reply_t *crtc_info = xcb_randr_get_crtc_info_reply(
    xcb_connection,
    xcb_randr_get_crtc_info(xcb_connection, crtc, XCB_CURRENT_TIME),
    &e
  );

	if (e != NULL){
		fprintf(stderr, "error get crtc info");
		return EXIT_FAILURE;
	}

  int outputs_size = xcb_randr_get_crtc_info_outputs_length(crtc_info);
	xcb_randr_output_t *outputs = xcb_randr_get_crtc_info_outputs(crtc_info);
	xcb_randr_set_crtc_config_cookie_t cookie = xcb_randr_set_crtc_config(
    xcb_connection,
    crtc,
    XCB_CURRENT_TIME,
    XCB_CURRENT_TIME,
    crtc_info->y,
    crtc_info->x,
    crtc_info->mode,
    rotation,
    outputs_size,
    outputs
  );

	free(xcb_randr_set_crtc_config_reply(xcb_connection, cookie, &e));
	free(crtc_info);

	if (e != NULL){
		fprintf(stderr, "error set crtc config");
		return EXIT_FAILURE;
	}

	xcb_randr_get_screen_info_cookie_t screen_info_cookie =
    xcb_randr_get_screen_info(xcb_connection, xcb_root);
	xcb_randr_get_screen_info_reply_t *screen_info_reply =
    xcb_randr_get_screen_info_reply(xcb_connection, screen_info_cookie, NULL);
	xcb_randr_set_screen_config_cookie_t screen_config = xcb_randr_set_screen_config(
    xcb_connection,
    xcb_root,
    XCB_CURRENT_TIME,
    XCB_CURRENT_TIME,
    screen_info_reply->sizeID,
    rotation,
    screen_info_reply->rate
  );

	free(xcb_randr_set_screen_config_reply(xcb_connection, screen_config, NULL));
	free(screen_info_reply);
	return EXIT_SUCCESS;
}

static inline void xcb_rotate_screen() {
  int rotation;
  int width;
  int height;
  int mwidth;
  int mheight;

  switch (current_state){
		case 1:
      rotation = XCB_RANDR_ROTATION_ROTATE_180;
      width = xcb_randr_screen_size->width;
      height = xcb_randr_screen_size->height;
      mwidth = xcb_randr_screen_size->mwidth;
      mheight = xcb_randr_screen_size->mheight;
			break;
		case 2:
			rotation = XCB_RANDR_ROTATION_ROTATE_90;
      width = xcb_randr_screen_size->height;
      height = xcb_randr_screen_size->width;
      mwidth = xcb_randr_screen_size->mheight;
      mheight = xcb_randr_screen_size->mwidth;
			break;
		case 3:
      rotation = XCB_RANDR_ROTATION_ROTATE_270;
      width = xcb_randr_screen_size->height;
      height = xcb_randr_screen_size->width;
      mwidth = xcb_randr_screen_size->mheight;
      mheight = xcb_randr_screen_size->mwidth;
			break;
    default:
      rotation = XCB_RANDR_ROTATION_ROTATE_0;
      width = xcb_randr_screen_size->width;
      height = xcb_randr_screen_size->height;
      mwidth = xcb_randr_screen_size->mwidth;
      mheight = xcb_randr_screen_size->mheight;
	}

  xcb_set_config(xcb_randr_crtc, rotation);
  xcb_randr_set_screen_size(xcb_connection, xcb_root, width, height, mwidth, mheight);
  xcb_flush(xcb_connection);
}

static inline char rotation_changed() {

  // do not attempt to change orientation when horizontal
  if (accel_z < -accel_threshold || accel_z > accel_threshold) return 0;

  char state = 0;
  if (accel_x > accel_threshold) state = 0;
  else if (accel_x < -accel_threshold) state = 1;
  else if (accel_y > accel_threshold) state = 2;
  else if (accel_y < -accel_threshold) state = 3;

  if (current_state == state) return 0;

  current_state = state;
  return 1;
}

FILE* bdopen (char const *fname, unsigned char leave_open) {
  *basedir_end = '/';
  strcpy(basedir_end + 1, fname);
  FILE *fin = fopen(basedir, "r");
  setvbuf(fin, NULL, _IONBF, 0);
  fgets(content, DATA_SIZE, fin);
  *basedir_end = '\0';
  if (leave_open == 0) {
    fclose(fin);
    return NULL;
  }
  return fin;
}

static inline double read_dev_accel (FILE* fd) {
  fseek(fd, 0, SEEK_SET);
  fgets(content, DATA_SIZE, fd);
  return atof(content) * accel_scale;
}

void help(char const *argv0) {
  fprintf(stderr,
    "Usage: %s [OPTION]...\n"
    "Rotates screen based on accelerometer (daemon)\n"
    "\n"
    "Options:\n"
    "    --verbose    Log debug messages\n"
    "    -h, --help   Show this help\n",
    argv0
  );
}

void handle_signal(int sig) {
  if (sig == SIGTERM || sig == SIGINT || sig == SIGHUP) {
    keep_running = 0;
  }
}

void debug(const char* format, ...) {
  va_list args;
  if (verbosity) {
    time_t now;
    char isotime[21];
    char prefixed[strlen(format) + sizeof isotime + 5];
    time(&now);
    strftime(isotime, sizeof isotime, "%FT%TZ", gmtime(&now));
    sprintf(prefixed, "[ %s ] %s", isotime, format);
    va_start(args, format);
    vprintf(prefixed, args);
    va_end(args);
  }
}

int main(int argc, char const *argv[]) {
  FILE *pf = popen("ls /sys/bus/iio/devices/iio:device*/in_accel*", "r");
  if (!pf) {
    fprintf(stderr, "IO Error.\n");
    return 2;
  }

  char const *arg;
  for (int i = 1; i < argc; i++) {
    arg = argv[i];

    if (strcmp(arg, "--verbose") == 0) {
      verbosity = 1;
    } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "--help") == 0) {
      help(argv[0]);
      return EXIT_SUCCESS;
    } else {
      fprintf(stderr, "Invalid argument %s\n\n", arg);
      help(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (fgets(basedir, DATA_SIZE , pf) != NULL){
    basedir_end = strrchr(basedir, '/');
    if (basedir_end) *basedir_end = '\0';
    fprintf(stderr, "Accelerometer: %s\n", basedir);
  } else {
    fprintf(stderr, "Unable to find any accelerometer.\n");
    return EXIT_FAILURE;
  }
  pclose(pf);

  signal(SIGTERM, handle_signal);
	signal(SIGINT, handle_signal);
  signal(SIGHUP, handle_signal);

  bdopen("in_accel_scale", 0);
  accel_scale = atof(content);
  FILE *dev_accel_x = bdopen("in_accel_x_raw", 1);
  FILE *dev_accel_y = bdopen("in_accel_y_raw", 1);
  FILE *dev_accel_z = bdopen("in_accel_z_raw", 1);

  xcb_init();

  unsigned char stress_cycle = 0;
  while (keep_running) {
    accel_x = read_dev_accel(dev_accel_x);
    accel_y = read_dev_accel(dev_accel_y);
    accel_z = read_dev_accel(dev_accel_z);

    debug(
      "Accelerometer x=%2f y=%2f z=%2f (%s)\n",
      accel_x,
      accel_y,
      accel_z,
      ROTATION[current_state]
    );

    if (rotation_changed()) {
      xcb_rotate_screen();
      stress_cycle = STRESS_CYCLES;
    }

    if (stress_cycle > 0) {
      usleep(5000);
      stress_cycle--;
    } else {
      sleep(1);
    }
  }

  xcb_close();

  return EXIT_SUCCESS;
}
